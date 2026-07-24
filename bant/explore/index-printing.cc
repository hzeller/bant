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

#include "bant/explore/index-printing.h"

#include <string>
#include <string_view>
#include <vector>

#include "bant/explore/project-indexing.h"
#include "bant/session.h"
#include "bant/types-bazel.h"
#include "bant/util/grep-highlighter.h"
#include "bant/util/table-printer.h"
#include "bant/workspace.h"

namespace bant {

void PrintProvidedSources(Session &session, const std::string &table_header,
                          const BazelTargetMatcher &filter,
                          const ProvidedFromTarget &provided_from_lib) {
  auto highlighter = CreateGrepHighlighterFromFlags(session);
  if (!highlighter) return;
  auto printer =
    TablePrinter::Create(session.out(), session.flags().output_format,
                         *highlighter, {table_header, "providing-rule"});
  for (const auto &[provided, lib] : provided_from_lib) {
    if (!filter.Match(lib)) continue;
    printer->AddRow({provided, lib.ToString()});
  }
  printer->Finish(session.flags().column_select);
}

void PrintProvidedSources(Session &session, const std::string &table_header,
                          const BazelTargetMatcher &filter,
                          const ProvidedFromTargetSet &provided_from_lib) {
  const auto dup_handling = session.flags().duplicate_handling;
  auto highlighter = CreateGrepHighlighterFromFlags(session);
  if (!highlighter) return;
  auto printer =
    TablePrinter::Create(session.out(), session.flags().output_format,
                         *highlighter, {table_header, "providing-rule"});
  for (const auto &[provided, libs] : provided_from_lib) {
    if (dup_handling == DuplicateHandling::kOutputOnlyDuplicates &&
        libs.size() == 1) {
      continue;
    }
    if (dup_handling == DuplicateHandling::kOutputOnlyUnique &&
        libs.size() != 1) {
      continue;
    }
    std::vector<std::string> list;
    for (const BazelTarget &target : libs) {
      if (filter.Match(target)) list.push_back(target.ToString());
    }
    printer->AddRowWithRepeatedLastColumn({provided}, list);
  }
  printer->Finish(session.flags().column_select);
}

void PrintTargetFileSet(Session &session, const BazelWorkspace &workspace,
                        const BazelTargetMatcher &filter,
                        const TargetProvidedFiles &target_to_files) {
  auto highlighter = CreateGrepHighlighterFromFlags(session);
  if (!highlighter) return;
  auto printer =
    TablePrinter::Create(session.out(), session.flags().output_format,
                         *highlighter, {"label", "files"});
  for (const auto &[target, files] : target_to_files) {
    if (!filter.Match(target)) continue;
    std::vector<std::string> list;
    for (const std::string_view package_relative_file : files) {
      list.emplace_back(
        target.package.FullyQualifiedFile(workspace, package_relative_file));
    }
    printer->AddRowWithRepeatedLastColumn({target.ToString()}, list);
  }
  printer->Finish(session.flags().column_select);
}

void PrintTargetToN(Session &session, const BazelWorkspace &workspace,
                    const BazelTargetMatcher &filter,
                    const PackageGroups &pkg_groups) {
  auto highlighter = CreateGrepHighlighterFromFlags(session);
  if (!highlighter) return;
  auto printer =
    TablePrinter::Create(session.out(), session.flags().output_format,
                         *highlighter, {"label", "pattern"});
  for (const auto &[target, _] : pkg_groups) {
    if (!filter.Match(target)) continue;
    std::vector<std::string> list;
    for (auto p : ResolvePackageGroupPatterns(pkg_groups, target)) {
      list.emplace_back(p);
    }
    printer->AddRowWithRepeatedLastColumn({target.ToString()}, list);
  }
  printer->Finish(session.flags().column_select);
}

void PrintFileToFileSet(Session &session,
                        const HeaderToCanonicalHeader &header_to_headers) {
  const auto dup_handling = session.flags().duplicate_handling;
  const bool suppress_same = session.flags().suppress_same;
  auto highlighter = CreateGrepHighlighterFromFlags(session);
  if (!highlighter) return;
  auto printer =
    TablePrinter::Create(session.out(), session.flags().output_format,
                         *highlighter, {"source", "canonical-source"});
  for (const auto &[header, files] : header_to_headers) {
    std::vector<std::string> list;
    for (const std::string &s : files) {
      if (suppress_same && header == s) continue;
      list.emplace_back(s);
    }
    if (list.empty()) continue;

    if (dup_handling == DuplicateHandling::kOutputOnlyDuplicates &&
        list.size() == 1) {
      continue;
    }
    if (dup_handling == DuplicateHandling::kOutputOnlyUnique &&
        list.size() != 1) {
      continue;
    }

    printer->AddRowWithRepeatedLastColumn({std::string{header}}, list);
  }
  printer->Finish(session.flags().column_select);
}
}  // namespace bant
