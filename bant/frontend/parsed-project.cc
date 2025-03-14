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

#include "bant/frontend/parsed-project.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "bant/builtin-macros.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/named-content.h"
#include "bant/frontend/parser.h"
#include "bant/frontend/print-visitor.h"
#include "bant/frontend/scanner.h"
#include "bant/frontend/source-locator.h"
#include "bant/session.h"
#include "bant/types-bazel.h"
#include "bant/util/file-utils.h"
#include "bant/util/stat.h"
#include "bant/workspace.h"
#include "re2/re2.h"

namespace bant {
namespace {

// Given a BUILD, BUILD.bazel filename, return the bare project path with
// no prefix or suffix.
// ./foo/bar/baz/BUILD.bazel turns into foo/bar/baz
std::string_view TargetPathFromBuildFile(std::string_view file) {
  file = file.substr(0, file.find_last_of('/'));  // Remove BUILD-file
  while (!file.empty() && (file[0] == '.' || file[0] == '/')) {
    file.remove_prefix(1);
  }
  return file;
}

// Given a bazel pattern, find the start directory to recursively walk the
// filesytem from.
static std::optional<FilesystemPath> DetermineSearchDirFromPattern(
  const BazelWorkspace &workspace, const BazelPattern &pattern) {
  std::string start_dir;
  if (!pattern.project().empty()) {
    auto dir_or = workspace.FindPathByProject(pattern.project());
    if (!dir_or.has_value()) {
      std::cerr << "Unknown project " << pattern.project() << ".\n";
      return std::nullopt;
    }
    start_dir = dir_or->path();
    start_dir.append("/");
  }
  start_dir.append(pattern.path());
  if (start_dir.empty()) start_dir = ".";
  return FilesystemPath(start_dir);
}

// Convenience function to just collect all the BUILD files. Update "stats"
// with total files searched and total time.
// If pattern contains a project name, the path is resolved from "workspace".
std::vector<FilesystemPath> CollectBuildFiles(Session &session,
                                              const BazelWorkspace &workspace,
                                              const BazelPattern &pattern) {
  bant::Stat &walk_stats =
    session.GetStatsFor("BUILD file glob walk", "files/directories");
  const ScopedTimer timer(&walk_stats.duration);

  // Predicates to decide if files should be included.
  const bool allow_recursive_walking = pattern.is_recursive();
  const auto is_build_file_predicate = [&](const FilesystemPath &file) {
    const std::string_view basename = file.filename();
    walk_stats.count++;
    return basename == "BUILD" || basename == "BUILD.bazel";
  };

  const auto dir_predicate = [&](const FilesystemPath &dir) {
    walk_stats.count++;
    if (!allow_recursive_walking) return false;  // Only looking at one level.
    if (dir.is_symlink()) return false;
    const std::string_view filename = dir.filename();
    // Skip irrelevant stuff
    if (filename == "_tmp") return false;
    if (filename == ".cache") return false;
    if (filename == ".git") return false;
    return true;
  };

  auto dir_or = DetermineSearchDirFromPattern(workspace, pattern);
  if (!dir_or) return {};
  return CollectFilesRecursive(*dir_or, dir_predicate, is_build_file_predicate);
}
}  // namespace

ParsedProject::ParsedProject(BazelWorkspace workspace, bool verbose,
                             bool with_builtin_macros)
    : workspace_(std::move(workspace)) {
  arena_.SetVerbose(verbose);
  if (with_builtin_macros) {
    CHECK_OK(SetBuiltinMacroContent(kBuiltinMacros));
  }
}

int ParsedProject::FillFromPattern(Session &session,
                                   const BazelPatternBundle &bundle) {
  int count = 0;
  std::set<FilesystemPath> unique_files;  // bundle might match multiple same
  for (const BazelPattern &pattern : bundle.patterns()) {
    const auto build_files = CollectBuildFiles(session, workspace(), pattern);
    for (const FilesystemPath &build_file : build_files) {
      if (unique_files.insert(build_file).second) {
        ++count;
        AddBuildFile(session, build_file, pattern.project());
      }
    }
  }
  return count;
}

ParsedBuildFile *ParsedProject::AddBuildFile(Session &session,
                                             const FilesystemPath &build_file,
                                             std::string_view project) {
  std::string_view package_path = build_file.path();
  if (!project.empty()) {
    // Somewhat silly to reconstruct the path by asking the worksapce again,
    // we have the information upstream, but it decays to a simple path.
    // Should be fixed, but good enough for now.
    auto prefix_or = workspace().FindPathByProject(project);
    if (!prefix_or.has_value()) {
      std::cerr << build_file.path() << ": Can't determine package.\n";
      return nullptr;  // should not happen.
    }
    // Path to project is prefix, everything afterwards is the pack path
    package_path = package_path.substr(prefix_or->path().length());
  }

  const BazelPackage package(project, TargetPathFromBuildFile(package_path));
  return AddBuildFile(session, build_file, package);
}

ParsedBuildFile *ParsedProject::AddBuildFile(Session &session,
                                             const FilesystemPath &build_file,
                                             const BazelPackage &package) {
  Stat open_and_read_stat;
  std::optional<std::string> content =
    ReadFileToStringUpdateStat(build_file, open_and_read_stat);
  if (!content.has_value()) {
    session.info() << "Could not read " << build_file.path() << "\n";
    ++error_count_;
    return nullptr;
  }

  return AddBuildFileContent(session, package,
                             build_file,  //
                             std::move(*content), open_and_read_stat);
}

ParsedBuildFile *ParsedProject::AddBuildFileContent(Session &session,
                                                    const BazelPackage &package,
                                                    const FilesystemPath &file,
                                                    std::string content,
                                                    const Stat &read_stat) {
  session.GetStatsFor("read(BUILD)      ", "BUILD files").Add(read_stat);

  Stat &parse_stat = session.GetStatsFor("Parse & build AST", "BUILD files");
  const ScopedTimer timer(&parse_stat.duration);

  auto inserted = package_to_parsed_.emplace(
    package, new ParsedBuildFile(file.path(), std::move(content)));

  if (!inserted.second) {
    ParsedBuildFile *existing = inserted.first->second.get();
    // Should typically not happen, but maybe both BUILD and BUILD.bazel are
    // there ? Report for the user to figure out.
    session.info() << file.path() << ": Package " << package
                   << " already seen before in "
                   << existing->source_.source_name() << "\n";
    return existing;
  }

  ParsedBuildFile &parse_result = *inserted.first->second;
  Scanner scanner(parse_result.source_);
  std::stringstream error_collect;
  Parser parser(&scanner, &arena_, error_collect);
  parse_result.ast = parser.parse();
  parse_result.errors = error_collect.str();
  if (parser.parse_error()) {
    session.error() << error_collect.str();
    ++error_count_;
  }
  parse_result.package = package;

  RegisterLocationRange(parse_result.source_.content(), &parse_result.source_);

  ++parse_stat.count;
  const size_t processed = parse_result.source_.size();
  parse_stat.AddBytesProcessed(processed);

  return inserted.first->second.get();
}

void ParsedProject::RegisterLocationRange(std::string_view range,
                                          const SourceLocator *source_locator) {
  location_maps_.Insert(range, source_locator);
}

FileLocation ParsedProject::GetLocation(std::string_view text) const {
  auto found = location_maps_.FindBySubrange(text);
  CHECK(found.has_value())
    << "Not in any of the files managed by ParsedProject '" << text << "'";
  return found.value()->GetLocation(text);
}

std::string_view ParsedProject::GetSurroundingLine(
  std::string_view text) const {
  auto found = location_maps_.FindBySubrange(text);
  CHECK(found.has_value())
    << "Not in any of the files managed by ParsedProject '" << text << "'";
  return found.value()->GetSurroundingLine(text);
}

const ParsedBuildFile *ParsedProject::FindParsedOrNull(
  const BazelPackage &package) const {
  auto found = package_to_parsed_.find(package);
  if (found == package_to_parsed_.end()) return nullptr;
  return found->second.get();
}

// Print visibility, but not regular print walk, but put in one line.
static void MaybePrintVisibility(List *visibility, std::ostream &out) {
  if (!visibility) return;
  out << " (visibility:";
  for (Node *v : *visibility) {
    Scalar *s = v->CastAsScalar();
    if (!s) continue;
    out << " " << s->AsString();
  }
  out << ")";
}

size_t PrintProject(Session &session, const BazelTargetMatcher &pattern,
                    const ParsedProject &project) {
  size_t count = 0;
  const CommandlineFlags &flags = session.flags();

  std::unique_ptr<RE2> regex;
  if (!flags.grep_regex.empty()) {
    RE2::Options options;
    options.set_log_errors(false);  // We print them ourselves
    regex = std::make_unique<RE2>(flags.grep_regex, options);
    if (!regex->ok()) {
      // This really needs the session passed in so that we can reach the
      // correct error stream.
      std::cerr << "Grep pattern: " << regex->error() << "\n";
      return count;
    }
  }

  for (const auto &[package, file_content] : project.ParsedFiles()) {
    if (flags.print_only_errors && file_content->errors.empty()) {
      continue;
    }
    if (!pattern.Match(package)) {
      continue;
    }

    query::FindTargetsAllowEmptyName(
      file_content->ast, {}, [&](const query::Result &result) {
        std::optional<BazelTarget> maybe_target;
        if (!result.name.empty()) {
          maybe_target = package.QualifiedTarget(result.name);
        }
        // If pattern requires some match, need to check now.
        if (!maybe_target.has_value() || !pattern.Match(*maybe_target)) {
          return;
        }

        // TODO: instead of just marking the range of the function name,
        // show the range the whole function covers until closed parenthesis.
        std::stringstream tmp_out;
        if (flags.do_color) tmp_out << "\033[2;37m";
        tmp_out << "# " << project.Loc(result.node->identifier()->id());
        if (maybe_target.has_value()) {  // only has value if target with name.
          tmp_out << " " << *maybe_target;
        }
        MaybePrintVisibility(result.visibility, tmp_out);
        if (flags.do_color) tmp_out << "\033[0m";
        tmp_out << "\n";
        PrintVisitor printer(tmp_out, regex.get(), flags.do_color);
        printer.WalkNonNull(result.node);
        tmp_out << "\n";
        if (!regex || printer.any_highlight()) {  // w/o regex: always print.
          session.out() << tmp_out.str();
          ++count;
        }
      });
  }
  return count;
}

Node *ParsedProject::FindMacro(std::string_view name) const {
  auto found = macros_.find(name);
  if (found != macros_.end()) return found->second;
  return nullptr;
}

absl::Status ParsedProject::SetBuiltinMacroContent(std::string_view content) {
  if (macro_content_) {
    // In tests, call ParsedProject without builtin-macros
    return absl::InternalError("Attempt to register multiple built-ins");
  }
  macro_content_ =
    std::make_unique<NamedLineIndexedContent>("(bant-builtin)", content);
  Scanner scanner(*macro_content_);  // directly parsing compiled-in string-view
  Parser parser(&scanner, &arena_, std::cerr);
  List *const builtin_list = parser.parse();
  if (parser.parse_error()) {
    return absl::InternalError("Issue in bant/builtin-macros.bnt");
  }
  for (Node *n : *builtin_list) {
    Assignment *const macro_assignment = n->CastAsAssignment();
    CHECK(macro_assignment) << "Expected assignment, got " << n;
    Identifier *const name = macro_assignment->maybe_identifier();
    CHECK(name) << "Not an identifier on lhs of " << macro_assignment;
    CHECK(macros_.emplace(name->id(), macro_assignment->value()).second)
      << "Multiple macros of name " << name->id();
  }
  RegisterLocationRange(macro_content_->content(), macro_content_.get());
  return absl::OkStatus();
}
}  // namespace bant
