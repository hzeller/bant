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
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
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

const ParsedProject::VariableBundle &ParsedProject::GetOrAddStarlarkContent(
  Session &session, const BazelTarget &starlark,
  const std::function<void(List *ast, VariableBundle *)> &variable_extractor) {
  if (const auto found = starlark_variables_.find(starlark);
      found != starlark_variables_.end()) {
    return *found->second;
  }
  auto var_inserted =
    starlark_variables_.emplace(starlark, new VariableBundle());
  CHECK(var_inserted.second) << starlark << " inserted twice ?";
  VariableBundle *const bundle = var_inserted.first->second.get();
  auto file_name =
    starlark.package.FullyQualifiedFile(workspace(), starlark.target_name);

  // TODO: these parsing things are very similar to BUILD-file parsing.
  // Unify.
  const FilesystemPath starlark_file(file_name);
  Stat open_and_read_stat;
  std::optional<std::string> content =
    ReadFileToStringUpdateStat(starlark_file, open_and_read_stat);
  if (!content.has_value()) {
    if (session.flags().verbose) {  // starlark reading is best effort.
      session.info() << "Could not read " << starlark << " ("
                     << starlark_file.path() << ")\n";
    }
    ++error_count_;
    return *bundle;
  }

  Stat &parse_stat = session.GetStatsFor("  - parse & elab", "Starlark files");
  const ScopedTimer timer(&parse_stat.duration);

  auto inserted = starlark_to_parsed_.emplace(
    starlark, new ParsedBuildFile(file_name, std::move(*content)));
  CHECK(inserted.second) << "Same starlark twice? " << starlark;

  ParsedBuildFile &parse_result = *inserted.first->second;
  Scanner scanner(parse_result.source_);
  std::stringstream error_collect;
  Parser parser(&scanner, &arena_, error_collect);
  parse_result.ast = parser.parse();
  parse_result.errors = error_collect.str();
  parse_result.package = starlark.package;
  RegisterLocationRange(parse_result.source_.content(), &parse_result.source_);

  if (parser.parse_error()) {
    if (session.flags().verbose) {
      session.error() << error_collect.str();
    }
    ++error_count_;
  } else {
    // Only if we got a clean parse, extract variables.
    variable_extractor(parse_result.ast, bundle);
  }

  ++parse_stat.count;
  const size_t processed = parse_result.source_.size();
  parse_stat.AddBytesProcessed(processed);

  // We're only interested to keep non-private variables that actually resulted
  // in a constant-evaluated concrete value.
  absl::erase_if(*bundle, [](const auto &key_v) -> bool {
    const bool is_concrete_value = key_v.second->CastAsScalar() != nullptr ||
                                   key_v.second->CastAsList() != nullptr;
    return key_v.first.starts_with("_") || !is_concrete_value;
  });

  return *bundle;
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

// -- TODO: maybe printing should move to a different file.

// If we have an arbitrary node, find the fist string to latch on to report
// a file position.
static std::optional<std::string_view> FindFirstLocatableString(Node *ast) {
  class FindFirstString : public BaseVoidVisitor {
   public:
    void VisitFunCall(FunCall *f) override {
      WalkNonNull(f->identifier());
      WalkNonNull(f->right());
    }
    void VisitBinOpNode(BinOpNode *b) final {
      if (result_.has_value()) return;  // Done already, can stop walking.
      BaseVoidVisitor::VisitBinOpNode(b);
    }

    void VisitScalar(Scalar *s) final {
      if (result_.has_value()) return;
      if (!s->AsString().empty()) result_ = s->AsString();
    }
    void VisitIdentifier(Identifier *id) final {
      if (result_.has_value()) return;
      if (id) result_ = id->id();
    }
    std::optional<std::string_view> found() { return result_; }

   private:
    std::optional<std::string_view> result_;
  };

  FindFirstString finder;
  ast->Accept(&finder);
  return finder.found();
}

std::pair<size_t, size_t> PrintProject(Session &session,
                                       const BazelTargetMatcher &pattern,
                                       const ParsedProject &project) {
  size_t count = 0;
  size_t total = 0;
  const CommandlineFlags &flags = session.flags();

  // TODO: we should match all expressions.
  RE2 *regex = nullptr;
  if (!flags.grep_expressions.empty()) {
    regex = flags.grep_expressions.front().get();
  }

  for (const auto &[package, file_content] : project.ParsedFiles()) {
    if (flags.print_only_errors && file_content->errors.empty()) {
      continue;
    }
    if (!pattern.Match(package)) {
      continue;
    }

    total += file_content->ast->size();

    // Detailed print of package if requested...
    if (flags.print_ast) {
      for (Node *item : *file_content->ast) {
        std::stringstream tmp_out;
        auto position_or = FindFirstLocatableString(item);
        if (position_or.has_value()) {
          if (flags.do_color) tmp_out << "\033[2;37m";
          tmp_out << "# " << project.Loc(*position_or);
          if (flags.do_color) tmp_out << "\033[0m";
        }
        tmp_out << "\n";
        PrintVisitor printer(tmp_out, regex, flags.do_color);
        printer.WalkNonNull(item);
        tmp_out << "\n";
        if (!regex || printer.any_highlight()) {  // w/o regex: always print.
          session.out() << tmp_out.str();
          ++count;
        }
      }
      continue;
    }

    // Just print matching rules.
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
        PrintVisitor printer(tmp_out, regex, flags.do_color);
        printer.WalkNonNull(result.node);
        tmp_out << "\n";
        if (!regex || printer.any_highlight()) {  // w/o regex: always print.
          session.out() << tmp_out.str();
          ++count;
        }
      });
  }
  return {count, total};
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
    Identifier *const name = macro_assignment->lhs_maybe_identifier();
    CHECK(name) << "Not an identifier on lhs of " << macro_assignment;
    CHECK(macros_.emplace(name->id(), macro_assignment->value()).second)
      << "Multiple macros of name " << name->id();
  }
  RegisterLocationRange(macro_content_->content(), macro_content_.get());
  return absl::OkStatus();
}
}  // namespace bant
