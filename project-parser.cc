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

#include "project-parser.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "file-utils.h"
#include "parser.h"

namespace fs = std::filesystem;

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

static void ParseBuildFiles(const std::vector<fs::path> &build_files,
                            const std::string &external_prefix,
                            std::ostream &error_out, ParsedProject *result) {
  const auto start_time = std::chrono::system_clock::now();

  size_t bytes_processed = 0;
  for (const fs::path &build_file : build_files) {
    std::optional<std::string> content = ReadFileToString(build_file);
    if (!content.has_value()) {
      std::cerr << "Could not read " << build_file << "\n";
      ++result->error_count;
      continue;
    }

    const std::string filename = build_file.string();
    auto inserted = result->file_to_ast.emplace(
      filename, ParsedBuildFile(filename, std::move(*content)));
    if (!inserted.second) {
      std::cerr << "Already seen " << filename << "\n";
      continue;
    }

    ParsedBuildFile &parse_result = inserted.first->second;
    ++result->parse_stat.count;
    bytes_processed += parse_result.content.size();

    if (filename.starts_with(external_prefix)) {
      std::string_view project_extract(filename);
      project_extract.remove_prefix(external_prefix.size());
      auto end_of_external_name = project_extract.find_first_of('/');
      auto external_project = project_extract.substr(0, end_of_external_name);
      parse_result.package.project = std::string("@").append(external_project);
      parse_result.package.path =
        TargetPathFromBuildFile(project_extract.substr(end_of_external_name));
    } else {
      parse_result.package.path = TargetPathFromBuildFile(filename);
    }

    Scanner scanner(parse_result.content, &parse_result.line_columns);
    std::stringstream error_collect;
    Parser parser(&scanner, &result->arena, filename.c_str(), error_collect);
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
  const auto end_time = std::chrono::system_clock::now();
  result->parse_stat.duration_usec =
    std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time)
      .count();
}

// Assemble a path that points to the symbolik link bazel generates for
// the external location.
// TODO: properly do this with fs::path
static std::string ExternalProjectDir() {
  const std::string project_dir_name = fs::current_path().filename().string();
  const std::string external_base = absl::StrCat("./bazel-", project_dir_name);
  return absl::StrCat(external_base, "/external");
}
}  // namespace

std::string Stat::ToString(std::string_view thing_name) const {
  if (bytes_processed.has_value()) {
    const float megabyte_per_sec = 1.0f * *bytes_processed / duration_usec;
    return absl::StrFormat("%d %s with %.2f KiB in %.3fms (%.2f MB/sec)", count,
                           thing_name, *bytes_processed / 1024,
                           duration_usec / 1000.0, megabyte_per_sec);
  }
  return absl::StrFormat("%d %s in %.3fms", count, thing_name,
                         duration_usec / 1000.0);
}

std::vector<fs::path> CollectBuildFiles(bool include_external, Stat &stats) {
  std::vector<fs::path> build_files;
  const auto start_time = std::chrono::system_clock::now();

  const auto relevant_build_file_predicate = [](const fs::path &file) {
    const auto &basename = file.filename();
    return basename == "BUILD" || basename == "BUILD.bazel";
  };

  const auto dir_predicate = [](bool allow_symlink, const fs::path &dir) {
    if (dir.filename() == "_tmp") return false;
    if (dir.filename() == ".git") return false;  // lots of irrelevant stuff
    return allow_symlink || !fs::is_symlink(dir);
  };

  // TODO: implement some curry solution
  const auto dir_with_symlink = [&dir_predicate](const fs::path &dir) {
    return dir_predicate(true, dir);
  };
  const auto dir_without_symlink = [&dir_predicate](const fs::path &dir) {
    return dir_predicate(false, dir);
  };

  ParsedProject result;
  // File in the general project
  stats.count =
    CollectFilesRecursive(".", build_files,
                          dir_without_symlink,  // bazel symlink tree: ignore
                          relevant_build_file_predicate);

  const std::string external_name = ExternalProjectDir();
  if (include_external) {
    stats.count +=
      CollectFilesRecursive(external_name, build_files, dir_with_symlink,
                            relevant_build_file_predicate);
  }

  const auto end_time = std::chrono::system_clock::now();
  stats.duration_usec =
    std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time)
      .count();
  return build_files;
}

ParsedProject ParsedProject::FromFilesystem(bool include_external,
                                            std::ostream &error_out) {
  ParsedProject result;
  auto build_files =
    CollectBuildFiles(include_external, result.file_collect_stat);
  const std::string external_name = ExternalProjectDir();
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
