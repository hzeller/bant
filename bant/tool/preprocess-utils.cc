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

#include <string_view>
#include <vector>

#include "absl/strings/str_split.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/named-content.h"
#include "bant/tool/cc-preprocessor.h"
#include "re2/re2.h"

namespace bant {
PreprocessValueResult ParsePreprocessValue(std::string_view value,
                                           const DefineMap &symbols) {
  if (value == "true" || value == "1") {
    return {true, false};
  }
  if (value == "false" || value == "0") {
    return {false, false};
  }
  // Maybe another macro ?
  if (auto found = symbols.find(value); found != symbols.end()) {
    return {found->second, false};
  }
  return {false, true};
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
  DefineMap defines_working_copy(defines);
  auto result = PreprocessInternal(src->content(), defines_working_copy);
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
    R"/((?m)(\/\/.*$|^\s*import\s+(?:public\s+)?"([0-9a-zA-Z_/\-\.]+\.proto[0-9a-zA-Z]*)"))/"};

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
