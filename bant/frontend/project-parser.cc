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

#include "bant/frontend/project-parser.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "bant/frontend/parser.h"
#include "bant/types-bazel.h"
#include "bant/util/file-utils.h"

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

// First foo/bar/baz/BUILD -> @foo//bar/baz
std::optional<BazelPackage> PackageFromExternal(std::string_view path) {
  path = TargetPathFromBuildFile(path);  // remove BUILD
  return BazelPackage::ParseFrom(absl::StrCat("@", path));
}

static void ParseBuildFiles(const std::vector<FilesystemPath> &build_files,
                            const std::string &external_prefix,
                            std::ostream &error_out, ParsedProject *result) {
  const absl::Time start_time = absl::Now();

  size_t bytes_processed = 0;
  for (const FilesystemPath &build_file : build_files) {
    std::optional<std::string> content = ReadFileToString(build_file);
    if (!content.has_value()) {
      std::cerr << "Could not read " << build_file.path() << "\n";
      ++result->error_count;
      continue;
    }

    const std::string &filename = build_file.path();
    auto inserted = result->file_to_ast.emplace(
      filename, ParsedBuildFile(filename, std::move(*content)));
    if (!inserted.second) {
      std::cerr << "Already seen " << filename << "\n";
      continue;
    }

    ParsedBuildFile &parse_result = inserted.first->second;
    ++result->parse_stat.count;
    bytes_processed += parse_result.source.size();

    if (filename.starts_with(external_prefix)) {
      std::string_view project_extract(filename);
      project_extract.remove_prefix(external_prefix.size());
      auto opt_package = PackageFromExternal(project_extract);
      if (!opt_package.has_value()) {
        std::cerr << filename << ": Can't parse as package\n";
        continue;
      }
      parse_result.package = *opt_package;
    } else {
      parse_result.package.path = TargetPathFromBuildFile(filename);
    }

    Scanner scanner(parse_result.source);
    std::stringstream error_collect;
    Parser parser(&scanner, &result->arena, error_collect);
    parse_result.ast = parser.parse();
    parse_result.errors = error_collect.str();
    if (parser.parse_error()) {
      error_out << error_collect.str();
      ++result->error_count;
    }
  }

  if (bytes_processed > 0) {
    result->parse_stat.bytes_processed = bytes_processed;
  }

  // fill FYI field.
  const absl::Time end_time = absl::Now();
  result->parse_stat.duration = end_time - start_time;
}

// Assemble a path that points to the symbolik link bazel generates for
// the external location.
static FilesystemPath ExternalProjectDir() {
  const std::string project_dir_name =
    std::filesystem::current_path().filename().string();
  const std::string external_base = absl::StrCat("./bazel-", project_dir_name);
  return FilesystemPath(absl::StrCat(external_base, "/external"));
}
}  // namespace

std::string Stat::ToString(std::string_view thing_name) const {
  const int64_t duration_usec = absl::ToInt64Microseconds(duration);
  if (bytes_processed.has_value()) {
    const float megabyte_per_sec = 1.0f * *bytes_processed / duration_usec;
    return absl::StrFormat("%d %s with %.2f KiB in %.3fms (%.2f MB/sec)", count,
                           thing_name, *bytes_processed / 1024,
                           duration_usec / 1000.0, megabyte_per_sec);
  }
  return absl::StrFormat("%d %s in %.3fms", count, thing_name,
                         duration_usec / 1000.0);
}

std::vector<FilesystemPath> CollectBuildFiles(bool include_external,
                                              Stat &stats) {
  std::vector<FilesystemPath> build_files;
  const absl::Time start_time = absl::Now();

  const auto relevant_build_file_predicate = [](const FilesystemPath &file) {
    const std::string_view basename = file.filename();
    return basename == "BUILD" || basename == "BUILD.bazel";
  };

  const auto dir_predicate = [](bool allow_symlink, const FilesystemPath &dir) {
    const std::string_view filename = dir.filename();
    if (filename == "_tmp") return false;
    if (filename == ".git") return false;  // lots of irrelevant stuff
    return allow_symlink || !dir.is_symlink();
  };

  // TODO: implement some curry solution
  const auto dir_with_symlink = [&dir_predicate](const FilesystemPath &dir) {
    return dir_predicate(true, dir);
  };
  const auto dir_without_symlink = [&dir_predicate](const FilesystemPath &dir) {
    return dir_predicate(false, dir);
  };

  // File in the general project
  stats.count =
    CollectFilesRecursive(FilesystemPath("."), build_files,
                          dir_without_symlink,  // bazel symlink tree: ignore
                          relevant_build_file_predicate);

  const FilesystemPath external_name = ExternalProjectDir();
  if (include_external) {
    stats.count +=
      CollectFilesRecursive(external_name, build_files, dir_with_symlink,
                            relevant_build_file_predicate);
  }

  const absl::Time end_time = absl::Now();
  stats.duration = end_time - start_time;

  return build_files;
}

ParsedProject ParsedProject::FromFilesystem(bool include_external,
                                            std::ostream &error_out) {
  ParsedProject result;
  auto build_files =
    CollectBuildFiles(include_external, result.file_collect_stat);
  const std::string external_name = ExternalProjectDir().path();
  ParseBuildFiles(build_files, absl::StrCat(external_name, "/"), error_out,
                  &result);
  return result;
}

void PrintProject(std::ostream &out, std::ostream &info_out,
                  const ParsedProject &project, bool only_files_with_errors) {
  for (const auto &[filename, file_content] : project.file_to_ast) {
    if (only_files_with_errors && file_content.errors.empty()) {
      continue;
    }
    info_out << "------- file " << filename << "\n";
    info_out << file_content.errors;
    if (!file_content.ast) continue;
    out << file_content.package.ToString() << " = ";
    PrintVisitor(out).WalkNonNull(file_content.ast);
    out << "\n";
  }
}
}  // namespace bant
