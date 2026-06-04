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

#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "bant/frontend/named-content.h"
#include "re2/re2.h"

namespace bant {
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
