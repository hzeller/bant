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

// TODO:
//   - some want find includes yet because they are the result of a glob()
//     operation, e.g. gtest/gtest.h.
//   - Don't add things that are not visible (e.g. absl vlog_is_on)
//   - generated sources: add heuristic. Check out = "..." fields. Or
//     proto buffers.
//

#include "bant/tool/dwyu.h"

#include <array>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "bant/frontend/named-content.h"
#include "bant/frontend/project-parser.h"
#include "bant/tool/edit-callback.h"
#include "bant/tool/header-providers.h"
#include "bant/types-bazel.h"
#include "bant/util/file-utils.h"
#include "bant/util/query-utils.h"
#include "re2/re2.h"

// Looking for source files directly in the source tree, but if not found
// in the various locations generated files could be.
static constexpr std::string_view kSourceLocations[] = {
  "", "bazel-out/host/bin/", "bazel-bin/"};

namespace bant {
namespace {

// Given a header file, check if it is in the list. Take possible prefix
// into account.
bool IsHeaderInList(std::string_view header,
                    const std::vector<std::string_view> &list,
                    std::string_view prefix_path) {
  // The list items are provided without the full path in the cc_library(),
  // so we always need to prepend that prefix_path.
  for (std::string_view list_item : list) {
    if (header.ends_with(list_item) &&  // cheap first test before strcat
        absl::StrCat(prefix_path, "/", list_item) == header) {
      return true;
    }
  }
  return false;
}

struct FileProviderLookups {
  ProvidedFromTargetMap headers_from_libs;
  ProvidedFromTargetMap files_from_genrules;
};

// Given the sources, grep for headers it uses and resolve their defining
// dependency targets.
// Report in "all_headers_accounted_for", that we found
// a library for each of the headers we have seen.
// This is important as only then we can confidently suggest removals in that
// target.
std::set<BazelTarget> DependenciesForIncludes(
  const BazelTarget &target_self, const ParsedBuildFile &context,
  const std::vector<std::string_view> &sources,
  const FileProviderLookups &file_index, bool *all_headers_accounted_for,
  Stat &stats, std::ostream &info_out, bool verbose) {
  size_t total_size = 0;
  std::set<BazelTarget> result;
  for (std::string_view src_name : sources) {
    const std::string source_file = context.package.QualifiedFile(src_name);

    // File could be in multiple locations, primary or generated. Use first.
    std::optional<std::string> src_content;
    bool generated_source = false;
    std::string src_concrete_filename;
    for (std::string_view search_path : kSourceLocations) {
      src_concrete_filename = absl::StrCat(search_path, source_file);
      src_content = ReadFileToString(FilesystemPath(src_concrete_filename));
      if (src_content.has_value()) break;
      generated_source = true;  // Sources searched after first are generated.
    }
    if (!src_content.has_value()) {
      // Nothing we can do about this for now. These are probably
      // coming from some generated sources or we don't see all packages.
      context.source.Loc(info_out, src_name)
        << " Can not read '" << source_file << "' referenced in "
        << target_self.ToString() << " Missing ? Generated ?\n";
      *all_headers_accounted_for = false;
      continue;
    }

    ++stats.count;
    total_size += src_content->size();
    NamedLineIndexedContent scanned_source(src_concrete_filename, *src_content);
    const auto pound_includes = ExtractCCIncludes(&scanned_source);

    // Now for all includes, we need to make sure we can account for it.
    for (const std::string_view inc_file : pound_includes) {
      if (IsHeaderInList(inc_file, sources, target_self.package.path)) {
        continue;  // Cool, our own list srcs=[...], hdrs=[...]
      }

      // mmh, maybe we included it without the proper prefix ?
      if (IsHeaderInList(inc_file, sources, "")) {
        if (!generated_source) {  // Only complain if actionable
          scanned_source.Loc(info_out, inc_file)
            << inc_file << " header relative to this file. "
            << "Consider FQN relative to project root.\n";
        }
        continue;  // But, anyway, found it in our own sources; accounted for.
      }

      if (const auto found = file_index.headers_from_libs.find(inc_file);
          found != file_index.headers_from_libs.end()) {
        if (found->second != target_self) {
          result.insert(found->second);  // A target we know is exporting it.
        }
        continue;
      }

      // Maybe include is not provided with path relative to project root ?
      const std::string abs_header = context.package.QualifiedFile(inc_file);
      if (const auto found = file_index.headers_from_libs.find(abs_header);
          found != file_index.headers_from_libs.end()) {
        if (!generated_source) {  // Only complain if actionable
          scanned_source.Loc(info_out, inc_file)
            << " " << inc_file << " header relative to this file. "
            << "Consider FQN relative to project root.\n";
        }
        if (found->second != target_self) {
          result.insert(found->second);
        }
        continue;
      }

      // Everything beyond here, we don't really know where a header is
      // coming from, so need to be careful suggesting removal of any dep.
      *all_headers_accounted_for = false;

      // Not found anywhere, but maybe we can at least report possible source.
      if (const auto found = file_index.files_from_genrules.find(inc_file);
          found != file_index.files_from_genrules.end()) {
        scanned_source.Loc(info_out, inc_file)
          << " " << inc_file << " not accounted for; generated by genrule "
          << found->second.ToString() << ", but not "
          << "in hdrs=[...] of any cc_library() we depend on.\n";
        context.source.Loc(info_out, src_name)
          << " ... in source of rule " << target_self.ToString() << "\n";
        continue;
      }

      // More possible checks
      //  - is this part of any library, but only in the srcs=[], but not
      //    in hdrs ? Then suggest to export it in that library.
      //  - is it not mentioned anywhere, but it shows up in the filesystem ?
      //    maybe forgot to add to any library.

      // No luck. Source includes it, but we don't know where it is.
      // Be careful with remove suggestion, so consider 'not accounted for'.
      if (verbose) {
        // Until we have a glob() implementation, this is pretty noisy at this
        // point. So wrap only show it if verbose enabled.
        scanned_source.Loc(info_out, inc_file)
          << " " << inc_file
          << " unaccounted for; lib missing ? bazel build needed ?\n";
        context.source.Loc(info_out, src_name)
          << " ... in source of rule " << target_self.ToString() << "\n";
      }
    }
  }

  if (stats.bytes_processed.has_value()) {
    stats.bytes_processed = *stats.bytes_processed + total_size;
  } else {
    stats.bytes_processed = total_size;
  }
  return result;
}

// We can only confidently remove a target if we actually know about its
// existence in the project. If not, be cautious.
std::set<BazelTarget> ExtractKnownLibraries(const ParsedProject &project) {
  std::set<BazelTarget> result;
  using query::TargetParameters;
  for (const auto &[_, parsed_package] : project.file_to_ast) {
    const BazelPackage &current_package = parsed_package->package;
    query::FindTargets(parsed_package->ast, {"cc_library"},  //
                       [&](const TargetParameters &target) {
                         if (target.alwayslink) {
                           // Don't include always-link targets: this makes
                           // sure they are not accidentally removed.
                           return;
                         }
                         auto self = BazelTarget::ParseFrom(
                           absl::StrCat(":", target.name), current_package);
                         if (!self.has_value()) {
                           return;
                         }
                         result.insert(*self);
                       });
  }
  return result;
}
}  // namespace

std::vector<std::string_view> ExtractCCIncludes(NamedLineIndexedContent *src) {
  static const LazyRE2 kIncRe{
    R"/((?m)^\s*#include\s+"([0-9a-zA-Z_/-]+(\.[a-zA-Z]+)*)")/"};

  std::vector<std::string_view> result;
  std::string_view run = src->content();
  std::string_view header_path;
  while (RE2::FindAndConsume(&run, *kIncRe, &header_path)) {
    result.push_back(header_path);
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

void CreateDependencyEdits(const ParsedProject &project, Stat &stats,
                           std::ostream &info_out, bool verbose_messages,
                           const EditCallback &emit_deps_edit) {
  FileProviderLookups hdr_idx;
  hdr_idx.headers_from_libs = ExtractHeaderToLibMapping(project, info_out);
  hdr_idx.files_from_genrules = ExtractGeneratedFromGenrule(project, info_out);
  const std::set<BazelTarget> known_libs = ExtractKnownLibraries(project);

  const absl::Time start_time = absl::Now();
  using query::TargetParameters;
  for (const auto &[_, parsed_package] : project.file_to_ast) {
    if (!parsed_package->package.project.empty()) {
      continue;  // Only interested in our project, not the externals
    }
    const BazelPackage &current_package = parsed_package->package;
    query::FindTargets(
      parsed_package->ast, {"cc_library", "cc_binary", "cc_test"},
      [&](const TargetParameters &target) {
        auto self = BazelTarget::ParseFrom(absl::StrCat(":", target.name),
                                           current_package);
        if (!self.has_value()) {
          return;
        }

        // Looking at the include files the sources reference, map these back
        // to the dependencies that provide them: these are the deps we needed.
        bool all_header_deps_known = true;
        std::vector<std::string_view> sources;
        query::ExtractStringList(target.srcs_list, sources);
        query::ExtractStringList(target.hdrs_list, sources);
        auto deps_needed =
          DependenciesForIncludes(*self, *parsed_package,  //
                                  sources, hdr_idx,        //
                                  &all_header_deps_known,  //
                                  stats, info_out, verbose_messages);

        // Check all the dependencies that the build target requested and
        // verify we actually need them. If not: remove.
        std::vector<std::string_view> deps;
        query::ExtractStringList(target.deps_list, deps);
        for (std::string_view dependency_target : deps) {
          auto requested_target = BazelTarget::ParseFrom(dependency_target,  //
                                                         current_package);
          if (!requested_target.has_value()) {
            parsed_package->source.Loc(info_out, dependency_target)
              << " Invalid target name '" << dependency_target << "'\n";
            continue;
          }

          // Strike off the dependency requested in the build file from the
          // dependendencies we independently determined from the #includes.
          // If it is not on that list, it is a canidate for removal.
          bool requested_was_needed = deps_needed.erase(*requested_target);

          const bool potential_remove_suggestion_safe =
            known_libs.contains(*requested_target) && all_header_deps_known;

          // Emit the edits.
          if (!requested_was_needed && potential_remove_suggestion_safe) {
            emit_deps_edit(EditRequest::kRemove, *self, dependency_target, "");
          }
        }

        // Now, if there is still something we need, add them.
        for (const BazelTarget &need_add : deps_needed) {
          emit_deps_edit(EditRequest::kAdd, *self, "",
                         need_add.ToStringRelativeTo(current_package));
        }
      });
  }
  const absl::Time end_time = absl::Now();
  stats.duration = end_time - start_time;
}
}  // namespace bant
