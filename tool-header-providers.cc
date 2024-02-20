// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
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

#include "tool-header-providers.h"

#include <functional>
#include <string_view>

#include "ast.h"
#include "project-parser.h"
#include "query-utils.h"
#include "types-bazel.h"

namespace bant {
namespace {
// Find cc_library and call callback for each header file it exports.
using FindHeaderCallback =
  std::function<void(std::string_view library_name, std::string_view header)>;
static void FindCCLibraryHeaders(Node *ast, const FindHeaderCallback &cb) {
  query::FindTargets(ast, {"cc_library"},
                     [&cb](const query::TargetParameters &params) {
                       if (!params.hdrs_list) return;
                       for (Node *header : *params.hdrs_list) {
                         Scalar *scalar = header->CastAsScalar();
                         if (!scalar) continue;
                         std::string_view header_file = scalar->AsString();
                         if (header_file.empty()) continue;
                         cb(params.name, header_file);
                       }
                     });
}
}  // namespace

HeaderToTargetMap ExtractHeaderToLibMapping(const ParsedProject &project) {
  HeaderToTargetMap result;
  for (const auto &[filename, file_content] : project.file_to_ast) {
    if (!file_content.ast) continue;
    FindCCLibraryHeaders(
      file_content.ast, [&result, &file_content](std::string_view lib_name,
                                                 std::string_view header) {
        std::string header_fqn = file_content.package.path;
        if (!header_fqn.empty()) header_fqn.append("/");
        header_fqn.append(header);

        BazelTarget target(file_content.package, lib_name);
        const auto &inserted = result.insert({header_fqn, target});
        if (!inserted.second && target != inserted.first->second) {
          // TODO: differentiate between info-log (external projects) and
          // error-log (current project, as these are actionable).
          // For now: just report errors.
          const bool is_error = file_content.package.project.empty();
          if (is_error) {
            // TODO: Get file-position from other target which might be
            // in a different file.
            std::cerr << file_content.filename << ":"
                      << file_content.line_columns.GetRange(header)
                      << " Header '" << header_fqn << "' in "
                      << target.ToString() << " already provided by "
                      << inserted.first->second.ToString() << "\n";
          }
        }
      });
  }
  return result;
}

void PrintLibraryHeaders(FILE *out, const ParsedProject &project) {
  const auto header_to_lib = ExtractHeaderToLibMapping(project);
  int longest = 0;
  for (const auto &[header, _] : header_to_lib) {
    longest = std::max(longest, (int)header.length());
  }
  for (const auto &[header, lib] : header_to_lib) {
    fprintf(out, "%*s\t%s\n", -longest, header.c_str(), lib.ToString().c_str());
  }
}
}  // namespace bant
