// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include "bant/tool/preprocess-utils.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/named-content.h"
#include "re2/re2.h"

namespace bant {
struct PreprocessValueResult {
  enum {
    UNKNOWN,  // Not a macro known and also not a constant value.
    IS_KNOWN,
  } how;
  bool is_on;
};

static PreprocessValueResult ParsePreprocessValue(std::string_view value,
                                                  const DefineMap &symbols) {
  if (value == "true" || value == "1") {
    return {PreprocessValueResult::IS_KNOWN, true};
  }
  if (value == "false" || value == "0") {
    return {PreprocessValueResult::IS_KNOWN, false};
  }
  // Maybe another macro ?
  if (auto found = symbols.find(value); found != symbols.end()) {
    return {PreprocessValueResult::IS_KNOWN, found->second};
  }
  return {PreprocessValueResult::UNKNOWN, false};
}

std::vector<TaggedRange> ExtractActiveCCIfdefRanges(std::string_view source,
                                                    DefineMap &define_values) {
  static const LazyRE2 kPreprocessLine{
    R"/((?m)(^[ \t]*#[ \t]*)(define|undef|else|endif|if(?:def|ndef)?)(?:[ \t]+(\w+)(?:[ \t]+([0-9A-Za-z_]+))?)?.*\n)/"};

  std::vector<TaggedRange> result;
  std::string_view run = source;
  const char *last_end = run.data();

  std::string_view start_line;
  std::string_view keyword;
  std::string_view var;
  std::string_view value;
  int nested_skip = 0;
  bool unambiguous_condition = false;  // such as #if 0 or explicitly set -D
  while (RE2::FindAndConsume(&run, *kPreprocessLine,  //
                             &start_line, &keyword, &var, &value)) {
    const size_t len = start_line.data() - last_end;
    std::string_view range{last_end, len};

    // if we skip due to hard constant, we don't even want to include it.
    // But, if it is unknown if this could be included, e.g. due to some changes
    // in the BUILD file, we want to report it for downstream to make decisions
    // on it.
    // If info eve interesting downstream, return some enum of sorts in the Tag.
    if (!(nested_skip && unambiguous_condition)) {
      if (len) result.emplace_back(range, nested_skip == 0);
    }

    if (nested_skip == 0) {
      if (keyword == "define") {
        define_values[var] = ParsePreprocessValue(value, define_values).is_on;
      } else if (keyword == "undef") {
        define_values.erase(var);
      }
    }

    // State machine; After entering skipping, keep track of nested ifdefs.
    if (nested_skip == 0) {
      if (keyword == "else") {
        nested_skip = 1;
      } else if (keyword == "ifdef") {
        const bool contained = define_values.contains(var);
        nested_skip = !contained;
        unambiguous_condition = contained;
      } else if (keyword == "ifndef") {
        const bool contained = define_values.contains(var);
        nested_skip = contained;
        unambiguous_condition = contained;
      } else if (keyword == "if") {
        auto val = ParsePreprocessValue(var, define_values);
        nested_skip = !val.is_on;
        unambiguous_condition = (val.how == PreprocessValueResult::IS_KNOWN);
      }
    } else {  // skip_nest > 0
      if (keyword == "if" || keyword == "ifdef" || keyword == "ifndef") {
        ++nested_skip;
      }
      if (keyword == "endif") {
        --nested_skip;
      }
      if (nested_skip == 1 && keyword == "else") {
        nested_skip = 0;
      }
    }

    last_end = run.data();
  }

  if (!run.empty()) {
    if (!(nested_skip && unambiguous_condition)) {
      result.emplace_back(run, nested_skip == 0);
    }
  }
  return result;
}

DefineMap GetDefinesFromTarget(const query::Result &target) {
  DefineMap result;
  auto insert_define = [&result](std::string_view d) {
    std::vector<std::string_view> elements = absl::StrSplit(d, '=');
    if (elements.size() == 2) {
      result[elements[0]] = ParsePreprocessValue(elements[1], result).is_on;
    } else {
      result[elements[0]] = true;
    }
  };
  for (std::string_view opt : query::ExtractStringList(target.copts)) {
    if (!opt.starts_with("-D")) continue;
    insert_define(opt.substr(2));
  }
  for (std::string_view opt : query::ExtractStringList(target.local_defines)) {
    insert_define(opt);
  }
  // TODO: we should provide a way to walk the deps=[] graph to collect defines,
  // because these are transitive and we need to follow all of them.
  for (std::string_view opt : query::ExtractStringList(target.defines)) {
    insert_define(opt);
  }
  return result;
}

std::vector<TaggedInclude> ExtractCCIncludes(NamedLineIndexedContent *src,
                                             const DefineMap &defines) {
  //          raw str      |comment|include...
  static const LazyRE2 kIncRe{
    R"/((?m)(R"[0-9a-zA-Z_]*\(|\/\/.*$|^\s*#\s*include\s+(["<](\.\./)*[0-9a-zA-Z_/+-]+(\.[a-zA-Z]+)*)[">]))/"};

  std::vector<TaggedInclude> result;
  DefineMap mutable_defines = defines;
  for (auto r : ExtractActiveCCIfdefRanges(src->content(), mutable_defines)) {
    std::string_view run = r.range;
    std::string_view header_path;
    std::string_view outer;
    while (RE2::FindAndConsume(&run, *kIncRe, &outer, &header_path)) {
      if (outer.starts_with("R\"")) {
        // If we start with R"foo( we need to skip forward to )foo"
        const std::string endmatch =
          absl::StrCat(")", outer.substr(2, outer.length() - 3), "\"");
        auto skip = run.find(endmatch);
        if (skip == std::string::npos) continue;  // eof ? ... skip.
        run.remove_prefix(endmatch.size() + skip);
      } else if (outer.starts_with("//")) {
        // ignore comment.
      } else if (!header_path.empty()) {
        result.emplace_back(header_path.substr(1),  // inc
                            header_path[0] == '<',  // is_angled_bracket
                            !r.is_included);        // is_ifdefed_out
      }
    }
  }

  if (!result.empty()) {
    // We only need to fill the location_mapper up to the location the last
    // element was found.
    const std::string_view range(
      src->content().begin(),
      result.back().include.end() - src->content().begin());
    src->mutable_line_index()->InitializeFromStringView(range);
  }
  return result;
}

std::vector<std::string_view> ExtractProtoImports(
  NamedLineIndexedContent *src) {
  //          comment  | import statement
  static const LazyRE2 kImportRe{
    R"/((?m)(\/\/.*$|^\s*import\s+(?:public\s+)?"([0-9a-zA-Z_/\-\.]+\.proto)"))/"};

  std::vector<std::string_view> result;
  std::string_view run = src->content();
  std::string_view import_path;
  std::string_view outer;
  while (RE2::FindAndConsume(&run, *kImportRe, &outer, &import_path)) {
    if (outer.starts_with("//")) {
      // ignore comment.
    } else if (!import_path.empty()) {
      result.push_back(import_path);
    }
  }

  if (!result.empty()) {
    const std::string_view range(src->content().begin(),
                                 result.back().end() - src->content().begin());
    src->mutable_line_index()->InitializeFromStringView(range);
  }
  return result;
}
}  // namespace bant
