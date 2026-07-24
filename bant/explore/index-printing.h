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

#ifndef BANT_PROJECT_INDEXING_PRINTING_
#define BANT_PROJECT_INDEXING_PRINTING_

#include <string>

#include "bant/explore/project-indexing.h"
#include "bant/session.h"
#include "bant/types-bazel.h"
#include "bant/workspace.h"

namespace bant {
// Pretty-print provided files and targets they are coming from in two columns.
void PrintProvidedSources(Session &session, const std::string &table_header,
                          const BazelTargetMatcher &filter,
                          const ProvidedFromTarget &provided_from_lib);

void PrintProvidedSources(Session &session, const std::string &table_header,
                          const BazelTargetMatcher &filter,
                          const ProvidedFromTargetSet &provided_from_lib);

// Print filegroup-like set, mapping BazelTarget -> filestheyprovide*
// Makes files fully qualified
void PrintTargetFileSet(Session &session, const BazelWorkspace &workspace,
                        const BazelTargetMatcher &filter,
                        const TargetProvidedFiles &target_to_files);

// Print OneToN package group to list of patterns (cave: currently the table
// header writes 'pattern')
void PrintTargetToN(Session &session, const BazelWorkspace &workspace,
                    const BazelTargetMatcher &filter,
                    const PackageGroups &pkg_groups);

void PrintFileToFileSet(Session &session,
                        const HeaderToCanonicalHeader &header_to_headers);
}  // namespace bant

#endif  // BANT_PROJECT_INDEXING_PRINTING_
