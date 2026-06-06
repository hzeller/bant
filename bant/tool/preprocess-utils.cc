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

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "bant/frontend/named-content.h"
#include "re2/re2.h"

namespace bant {
static bool ParsePreprocessValue(std::string_view value,
                                 const DefineMap &symbols) {
  if (value == "true") return true;
  if (value == "false") return false;
  // Maybe another macro ?
  if (auto found = symbols.find(value); found != symbols.end()) {
    return found->second;
  }
  // Interpret as number.
  int parsed_val;
  return absl::SimpleAtoi(value, &parsed_val) && parsed_val != 0;
}

std::vector<std::string_view> ExtractActiveCCIfdefRanges(
  std::string_view source, DefineMap &define_values) {
  static const LazyRE2 kPreprocessLine{
    R"/((?m)(^[ \t]*#[ \t]*)(define|undef|else|endif|if(?:def|ndef)?)(?:[ \t]+(\w+)(?:[ \t]+([0-9A-Za-z_]+))?)?.*\n)/"};

  std::vector<std::string_view> result;
  std::string_view run = source;
  const char *last_end = run.data();

  std::string_view start_line;
  std::string_view keyword;
  std::string_view var;
  std::string_view value;
  int nested_skip = 0;
  while (RE2::FindAndConsume(&run, *kPreprocessLine,  //
                             &start_line, &keyword, &var, &value)) {
    // if we're not skipping, emit text and possibly execute define/undef
    if (nested_skip == 0) {
      const size_t len = start_line.data() - last_end;
      if (len) result.emplace_back(last_end, len);

      if (keyword == "define") {
        define_values[var] = ParsePreprocessValue(value, define_values);
      } else if (keyword == "undef") {
        define_values.erase(var);
      }
    }

    // State machine; After entering skipping, keep track of nested ifdefs.
    if (nested_skip == 0) {
      if ((keyword == "ifdef" && !define_values.contains(var)) ||
          (keyword == "ifndef" && define_values.contains(var)) ||
          (keyword == "if" && !ParsePreprocessValue(var, define_values)) ||
          (keyword == "else")) {
        nested_skip = 1;
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
  return result;
}

std::vector<std::string_view> ExtractCCIncludes(NamedLineIndexedContent *src) {
  //          raw str      |comment|include...
  static const LazyRE2 kIncRe{
    R"/((?m)(R"[0-9a-zA-Z_]*\(|\/\/.*$|^\s*#\s*include\s+(["<](\.\./)*[0-9a-zA-Z_/+-]+(\.[a-zA-Z]+)*)[">]))/"};

  std::vector<std::string_view> result;
  std::string_view run = src->content();
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
      result.push_back(header_path);
    }
  }

  if (!result.empty()) {
    // We only need to fill the location_mapper up to the location the last
    // element was found
    const std::string_view range(src->content().begin(),
                                 result.back().end() - src->content().begin());
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
