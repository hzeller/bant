// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include "project-parser.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "parser.h"

namespace fs = std::filesystem;

namespace bant {
namespace {
std::optional<std::string> ReadFileToString(const fs::path &filename) {
  std::ifstream is(filename, std::ifstream::binary);
  if (!is.good()) return std::nullopt;
  std::string result;
  char buffer[4096];
  for (;;) {
    is.read(buffer, sizeof(buffer));
    result.append(buffer, is.gcount());
    if (!is.good()) break;
  }
  return result;
}

// Collect files found recursively and store in "paths".
// Uses predicate "include_dir_p" to check if directory should be walked,
// and "include_file_p" if file should be included.
// Returns number of files looked at.
using PathAccept = std::function<bool(const fs::path &)>;
size_t CollectFilesRecursive(const fs::path &dir, std::vector<fs::path> *paths,
                             const PathAccept &want_dir_p,
                             const PathAccept &want_file_p) {
  std::error_code err;
  if (!fs::is_directory(dir, err) || err.value() != 0) return 0;
  if (!want_dir_p(dir)) return 0;

  size_t count = 0;
  for (const fs::directory_entry &e : fs::directory_iterator(dir)) {
    ++count;
    if (e.is_directory()) {
      count += CollectFilesRecursive(e.path(), paths, want_dir_p, want_file_p);
    } else if (want_file_p(e.path())) {
      paths->emplace_back(e.path());
    }
  }
  return count;
}

// Given a BUILD, BUILD.bazel filename, return the bare project path with
// no prefix or suffix.
// ./foo/bar/baz/BUILD.bazel turns into foo/bar/baz
std::string_view TargetPathFromBuileFile(std::string_view file) {
  file = file.substr(0, file.find_last_of('/'));  // Remove BUILD-file
  while (!file.empty() && (file[0] == '.' || file[0] == '/')) {
    file.remove_prefix(1);
  }
  return file;
}

static void ParseBuildFiles(const std::vector<fs::path> &build_files,
                            const std::string &external_prefix,
                            ParsedProject *result) {
  const auto start_time = std::chrono::system_clock::now();

  for (const fs::path &build_file : build_files) {
    std::optional<std::string> content = ReadFileToString(build_file);
    if (!content.has_value()) {
      std::cerr << "Could not read " << build_file << "\n";
      ++result->error_count;
      continue;
    }

    const std::string filename = build_file.string();
    auto inserted =
      result->file_to_ast.emplace(filename, FileContent(std::move(*content)));
    if (!inserted.second) {
      std::cerr << "Already seen " << filename << "\n";
      continue;
    }

    FileContent &parse_result = inserted.first->second;
    ++result->build_file_count;
    result->total_content_size += parse_result.content.size();

    if (filename.starts_with(external_prefix)) {
      std::string_view project_extract(filename);
      project_extract.remove_prefix(external_prefix.size());
      auto end_of_external_name = project_extract.find_first_of('/');
      auto external_project = project_extract.substr(0, end_of_external_name);
      parse_result.project =
        std::string("@").append(external_project).append("//");
      parse_result.rel_path =
        TargetPathFromBuileFile(project_extract.substr(end_of_external_name));
    } else {
      parse_result.project = "//";
      parse_result.rel_path = TargetPathFromBuileFile(filename);
    }

    Scanner scanner(parse_result.content);
    std::stringstream error_collect;
    Parser parser(&scanner, &result->arena, filename.c_str(), error_collect);
    parse_result.ast = parser.parse();
    parse_result.errors = error_collect.str();
    if (parser.parse_error()) {
      std::cerr << error_collect.str();
      ++result->error_count;
    }
  }

  // fill FYI field.
  const auto end_time = std::chrono::system_clock::now();
  result->parse_duration_usec =
    std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time)
      .count();
}
}  // namespace

ParsedProject ParsedProject::FromFilesystem(bool include_external) {
  std::vector<fs::path> build_files;
  const auto start_time = std::chrono::system_clock::now();

  const PathAccept relevant_build_file_predicate = [](const fs::path &file) {
    const auto &basename = file.filename();
    return basename == "BUILD" || basename == "BUILD.bazel";
  };

  const auto dir_predicate = [](bool allow_symlink, const fs::path &dir) {
    if (dir.filename() == "_tmp") return false;
    if (dir.filename() == ".git") return false;  // lots of irrelevant stuff
    return allow_symlink || !fs::is_symlink(dir);
  };

  // TODO: implement some curry solution
  const PathAccept dir_with_symlink = [&dir_predicate](const fs::path &dir) {
    return dir_predicate(true, dir);
  };
  const PathAccept dir_without_symlink = [&dir_predicate](const fs::path &dir) {
    return dir_predicate(false, dir);
  };

  ParsedProject result;
  // File in the general project
  result.files_searched =
    CollectFilesRecursive(".", &build_files,
                          dir_without_symlink,  // bazel symlink tree: ignore
                          relevant_build_file_predicate);

  // All the external files (TODO: properly do this with fs::path)
  const std::string project_dir_name = fs::current_path().filename().string();
  const std::string external_base = "./bazel-" + project_dir_name;
  const std::string external_name = external_base + "/external";
  if (include_external) {
    result.files_searched +=
      CollectFilesRecursive(external_name, &build_files, dir_with_symlink,
                            relevant_build_file_predicate);
  }

  const auto end_time = std::chrono::system_clock::now();
  result.file_walk_duration_usec =
    std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time)
      .count();

  ParseBuildFiles(build_files, external_name + "/", &result);
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
    out << file_content.project << file_content.rel_path << " = ";
    PrintVisitor(out).WalkNonNull(file_content.ast);
    out << "\n";
  }
}
}  // namespace bant
