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

#include "bant/tool/header-providers.h"

#include <functional>
#include <string_view>

#include "absl/strings/str_format.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/project-parser.h"
#include "bant/types-bazel.h"
#include "bant/util/query-utils.h"

#define BANT_GTEST_HACK 1

namespace bant {
namespace {
// Find cc_library and call callback for each header file it exports.
using FindHeaderCallback =
  std::function<void(std::string_view library_name, std::string_view header)>;
static void FindCCLibraryHeaders(Node *ast, const FindHeaderCallback &cb) {
  query::FindTargets(
    ast, {"cc_library"}, [&cb](const query::TargetParameters &params) {
      std::vector<std::string_view> incdirs;
      query::ExtractStringList(params.includes_list, incdirs);
      std::vector<std::string_view> headers;
      query::ExtractStringList(params.hdrs_list, headers);

      for (std::string_view header_file : headers) {
        cb(params.name, header_file);
        // Could also show up under shorter path with -I
        for (std::string_view dir : incdirs) {
          std::string prefix(dir);
          if (!prefix.ends_with('/')) prefix.append("/");
          if (header_file.starts_with(prefix)) {
            cb(params.name, header_file.substr(prefix.length()));
          }
        }
      }
    });
}
}  // namespace

ProvidedFromTargetMap ExtractHeaderToLibMapping(const ParsedProject &project,
                                                std::ostream &info_out) {
  ProvidedFromTargetMap result;

#ifdef BANT_GTEST_HACK
  // gtest hack (can't glob() the headers yet, so manually add these to
  // the first project that looks like it is googletest...
  for (const auto &[_, file_content] : project.file_to_ast) {
    if (file_content.package.project.find("googletest") == std::string::npos) {
      continue;
    }
    BazelTarget test_target;
    test_target.package.project = file_content.package.project;
    test_target.target_name = "gtest";
    result["gtest/gtest.h"] = test_target;
    result["gmock/gmock.h"] = test_target;
    break;
  }
#endif

  for (const auto &[_, file_content] : project.file_to_ast) {
    if (!file_content.ast) continue;
    FindCCLibraryHeaders(file_content.ast, [&](std::string_view lib_name,
                                               std::string_view header) {
      std::string header_fqn = file_content.package.QualifiedFile(header);

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
          info_out << file_content.filename << ":"
                   << file_content.line_columns.GetRange(header) << " Header '"
                   << header_fqn << "' in " << target.ToString()
                   << " already provided by "
                   << inserted.first->second.ToString() << "\n";
        }
      }
    });
  }

  return result;
}

ProvidedFromTargetMap ExtractGeneratedFromGenrule(const ParsedProject &project,
                                                  std::ostream &info_out) {
  ProvidedFromTargetMap result;
  for (const auto &[_, file_content] : project.file_to_ast) {
    if (!file_content.ast) continue;
    query::FindTargets(
      file_content.ast, {"genrule"},
      [&](const query::TargetParameters &params) {
        std::vector<std::string_view> genfiles;
        query::ExtractStringList(params.outs_list, genfiles);

        BazelTarget target(file_content.package, params.name);
        for (std::string_view generated : genfiles) {
          std::string gen_fqn = file_content.package.QualifiedFile(generated);
          const auto &inserted = result.insert({gen_fqn, target});
          if (!inserted.second && target != inserted.first->second) {
            // TODO: differentiate between info-log (external projects) and
            // error-log (current project, as these are actionable).
            // For now: just report errors.
            const bool is_error = file_content.package.project.empty();
            if (is_error) {
              // TODO: Get file-position from other target which might be
              // in a different file.
              info_out << file_content.filename << ":"
                       << file_content.line_columns.GetRange(generated) << " '"
                       << gen_fqn << "' in " << target.ToString()
                       << " also created by "
                       << inserted.first->second.ToString() << "\n";
            }
          }
        }
      });
  }
  return result;
}

void PrintProvidedSources(const ProvidedFromTargetMap &provided_from_lib,
                          std::ostream &out) {
  int longest = 0;
  for (const auto &[provided, _] : provided_from_lib) {
    longest = std::max(longest, (int)provided.length());
  }
  for (const auto &[provided, lib] : provided_from_lib) {
    out << absl::StrFormat("%*s\t%s\n", -longest, provided, lib.ToString());
  }
}
}  // namespace bant
