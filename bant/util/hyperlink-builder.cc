// bant - Bazel Navigation Tool
// Copyright (C) 2026 Henner Zeller <h.zeller@acm.org>
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

#include "bant/util/hyperlink-builder.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "bant/frontend/source-locator.h"
#include "bant/types-bazel.h"
#include "bant/util/text-template.h"

namespace bant {
/*
Constants extracted from project
  ${project_root}    // Absolute root of dir of the project on local filesystem
  ${external_root}   // Absolute root dir where all the external projets are
  ${repo_url}        // if available: this is the base-url for ${project_file}s

Variables used within links
  ${project_file}    // filename relative to ${project_root}
  ${external_file}   // filename relative to ${external_root}
  ${generated_file}  // filename that is generated; relative to ${project_root}

  // range from-to as line and column information, as zero and one based.
  ${line_start_0} ${col_start_0} and ${line_start_1} ${col_start_1}
  ${line_end_0}   ${col_end_0}   and ${line_end_1}   ${col_end_1}
*/

// Our starting templates. TODO: read templates from file if user has a better
// way to view files locally. Then, these are the fallback templates.
// The first pattern that has all the variables needed in a particular context
// is chosen. So, go from specific (e.g. with line numbers) to unspecific to
// fallback.
// clang-format off
static constexpr std::string_view kUrlTemplates[] = {
  // Use repo_urls (if available) to assemble a link directly to the repo.
  // This can only be done on project paths, as external and generated are
  // only available locally.
  "${repo_url}/${project_file}#L${line_start_1}-L${line_end_1}",
  "${repo_url}/${project_file}",

  // Fallbacks: just plain file URLs to local filesystem. Unfortunately, in
  // that case we never can give location, as browsers don't support jumping
  // to these typically.
  // However, some editors can deal with line/column links, so we also provide
  // specific versions. If anything it shows the users that there might be
  // a chance to configure their terminal+editor combination to do the jump.
  "file://${project_root}/${project_file}?line=${line_start_1}&column=${col_start_1}",

  "file://${project_root}/${project_file}",

  "file://${project_root}/${generated_file}?line=${line_start_1}&column=${col_start_1}",
  "file://${project_root}/${generated_file}",

  "file://${external_root}/${external_file}?line=${line_start_1}&column=${col_start_1}",
  "file://${external_root}/${external_file}",
};
// clang-format on

static bool AllElementsInSet(const std::vector<std::string_view> &of_interest,
                             const std::set<std::string_view> &available) {
  for (const std::string_view s : of_interest) {
    if (!available.contains(s)) return false;
  }
  return true;
}

// Replace every variable in template that are constants, leave the rests as-is.
// Returns a new template that only contains hitherto unbound ${variables}.
// The returned new template has the prefix and suffix applied.
static std::string ReplaceKnownValues(std::string_view tpl,
                                      const HyperlinkBuilder::VarKV &constants,
                                      std::string_view prefix,
                                      std::string_view suffix) {
  TextTemplate t;
  auto vars = t.Preprocess(tpl);
  std::vector<std::string> replacements;
  for (auto v : vars) {
    if (const auto found = constants.find(v); found != constants.end()) {
      replacements.emplace_back(found->second);
    } else {
      replacements.emplace_back(absl::StrCat("${", v, "}"));
    }
  }
  std::stringstream out;
  t.Write(out, replacements);
  return absl::StrCat(prefix, out.str(), suffix);
}

// Build an accessor given the variable name assuming FileLocation as data.
static TextTemplate::ValueAccessor AccessorForVariable(std::string_view v) {
  if (v == "project_file" || v == "generated_file" || v == "external_file") {
    return [](const void *data) -> std::string {
      const FileLocation &floc = *static_cast<const FileLocation *>(data);
      return std::string(floc.filename);
    };
  }
  if (v == "line_start_0") {
    return [](const void *data) -> std::string {
      const FileLocation &floc = *static_cast<const FileLocation *>(data);
      return std::to_string(floc.line_column_range.start.line);
    };
  }
  if (v == "line_start_1") {
    return [](const void *data) -> std::string {
      const FileLocation &floc = *static_cast<const FileLocation *>(data);
      return std::to_string(floc.line_column_range.start.line + 1);
    };
  }
  if (v == "line_end_0") {
    return [](const void *data) -> std::string {
      const FileLocation &floc = *static_cast<const FileLocation *>(data);
      return std::to_string(floc.line_column_range.end.line);
    };
  }
  if (v == "line_end_1") {
    return [](const void *data) -> std::string {
      const FileLocation &floc = *static_cast<const FileLocation *>(data);
      return std::to_string(floc.line_column_range.end.line + 1);
    };
  }

  if (v == "col_start_0") {
    return [](const void *data) -> std::string {
      const FileLocation &floc = *static_cast<const FileLocation *>(data);
      return std::to_string(floc.line_column_range.start.col);
    };
  }
  if (v == "col_start_1") {
    return [](const void *data) -> std::string {
      const FileLocation &floc = *static_cast<const FileLocation *>(data);
      return std::to_string(floc.line_column_range.start.col + 1);
    };
  }
  if (v == "col_end_0") {
    return [](const void *data) -> std::string {
      const FileLocation &floc = *static_cast<const FileLocation *>(data);
      return std::to_string(floc.line_column_range.end.col);
    };
  }
  if (v == "col_end_1") {
    return [](const void *data) -> std::string {
      const FileLocation &floc = *static_cast<const FileLocation *>(data);
      return std::to_string(floc.line_column_range.end.col + 1);
    };
  }
  LOG(FATAL) << "Invalid variable - should've been caught before: " << v;
}

static TextTemplate::Prepared Prepare(std::string_view tpl,
                                      const HyperlinkBuilder::VarKV &constants,
                                      std::string_view prefix,
                                      std::string_view suffix) {
  const auto flattened = ReplaceKnownValues(tpl, constants, prefix, suffix);
  TextTemplate t;
  auto vars = t.Preprocess(flattened);
  std::vector<TextTemplate::ValueAccessor> accessors;
  accessors.reserve(vars.size());
  for (const std::string_view v : vars) {
    accessors.emplace_back(AccessorForVariable(v));
  }
  return {std::move(t), std::move(accessors)};
}

bool HyperlinkBuilder::Build(const VarKV &constants, std::string_view prefix,
                             std::string_view suffix) {
  using StringSet = std::set<std::string_view>;
  static const StringSet kAllowedBaseVariables{
    "project_root",  "external_root", "repo_url",        // constants
    "external_file", "project_file",  "generated_file",  // runtime
  };
  static const StringSet kLocVars{
    "line_start_0", "col_start_0", "line_start_1", "col_start_1",  // start..
    "line_end_0",   "col_end_0",   "line_end_1",   "col_end_1",    // ...end
  };

  auto test_all_variables_are_supported =
    [&](std::string_view tpl, const std::vector<std::string_view> &vars) {
      for (const std::string_view var : vars) {
        if (!kAllowedBaseVariables.contains(var) &&
            !kLocVars.contains(var)) {  // prevent typos.
          std::cerr << "Error: Unsupported variable ${" << var << "} in " << tpl
                    << "\n";
          return false;
        }
      }
      return true;
    };

  const auto const_vars = constants | std::views::keys;

  // The different kinds of URLs have different variables available.

  // Project relative
  StringSet available_for_in_project_no_loc{const_vars.begin(),
                                            const_vars.end()};
  available_for_in_project_no_loc.insert("project_file");

  StringSet available_for_in_project_with_loc{available_for_in_project_no_loc};
  available_for_in_project_with_loc.insert(kLocVars.begin(), kLocVars.end());

  // External projects
  StringSet available_for_external_no_loc{const_vars.begin(), const_vars.end()};
  available_for_external_no_loc.insert("external_file");

  StringSet available_for_external_with_loc{available_for_external_no_loc};
  available_for_external_with_loc.insert(kLocVars.begin(), kLocVars.end());

  // Generated files
  StringSet available_for_generated_no_loc{const_vars.begin(),
                                           const_vars.end()};
  available_for_generated_no_loc.insert("generated_file");

  StringSet available_for_generated_with_loc{available_for_generated_no_loc};
  available_for_generated_with_loc.insert(kLocVars.begin(), kLocVars.end());

  // Go through all templates. The _first_ that matches all the criteria for a
  // particular type (i.e. only uses variables available for the type) is used.
  bool success = true;
  TextTemplate var_tester;
  for (const std::string_view tpl : kUrlTemplates) {
    const auto vars = var_tester.Preprocess(tpl);
    if (!test_all_variables_are_supported(tpl, vars)) {
      success = false;
      continue;
    }
    if (!project_file_.has_value() &&
        AllElementsInSet(vars, available_for_in_project_no_loc)) {
      project_file_ = Prepare(tpl, constants, prefix, suffix);
    }
    if (!project_file_with_loc_.has_value() &&
        AllElementsInSet(vars, available_for_in_project_with_loc)) {
      project_file_with_loc_ = Prepare(tpl, constants, prefix, suffix);
    }

    if (!external_file_.has_value() &&
        AllElementsInSet(vars, available_for_external_no_loc)) {
      external_file_ = Prepare(tpl, constants, prefix, suffix);
    }
    if (!external_file_with_loc_.has_value() &&
        AllElementsInSet(vars, available_for_external_with_loc)) {
      external_file_with_loc_ = Prepare(tpl, constants, prefix, suffix);
    }

    if (!generated_file_.has_value() &&
        AllElementsInSet(vars, available_for_generated_no_loc)) {
      generated_file_ = Prepare(tpl, constants, prefix, suffix);
    }
    if (!generated_file_with_loc_.has_value() &&
        AllElementsInSet(vars, available_for_generated_with_loc)) {
      generated_file_with_loc_ = Prepare(tpl, constants, prefix, suffix);
    }
  }

  return success;
}

static bool LooksLikeGeneratedPath(std::string_view path) {
  // TODO: use the same criteria as kSourceLocations in dwyu
  return path.starts_with("bazel-");
}

bool HyperlinkBuilder::LinkTo(const FileLocation &location,
                              std::ostream &out) const {
  FileLocation print_location = location;
  const std::optional<TextTemplate::Prepared> *template_to_use = nullptr;
  if (location.filename.starts_with(workspace_.external_dir)) {
    template_to_use = &external_file_with_loc_;
    print_location.filename.remove_prefix(workspace_.external_dir.length());
  } else if (LooksLikeGeneratedPath(location.filename)) {
    template_to_use = &generated_file_with_loc_;
  } else {
    template_to_use = &project_file_with_loc_;
  }
  if (!template_to_use || !template_to_use->has_value()) {
    return false;
  }

  template_to_use->value().Write(out, &print_location);
  return true;
}

bool HyperlinkBuilder::LinkTo(const BazelPackage &pkg,
                              std::string_view filename,
                              std::ostream &out) const {
  const std::optional<TextTemplate::Prepared> *template_to_use = nullptr;
  if (pkg.project.empty()) {
    template_to_use = &project_file_;
  } else {
    template_to_use = &external_file_;
  }

  if (!template_to_use || !template_to_use->has_value()) {
    return false;
  }

  FileLocation floc;
  const std::string fqn = pkg.FullyQualifiedFile(workspace_, filename);
  floc.filename = fqn;
  template_to_use->value().Write(out, &floc);
  return true;
}

bool HyperlinkBuilder::LinkTo(std::string_view filename,
                              std::ostream &out) const {
  // TODO: this is not fully correct: we should check if filename is actually
  // external.
  return LinkTo({}, filename, out);
}

static std::string MakeAbsolute(std::string_view path) {
  std::error_code ec;
  auto cpath = std::filesystem::canonical(path, ec);
  if (!ec) return cpath.string();
  return std::string{path};  // ¯\_(ツ)_/¯
}

std::unique_ptr<HyperlinkBuilder> MakeLinkBuilder(
  const BazelWorkspace &workspace, bool enable_links) {
  auto link_builder = std::make_unique<HyperlinkBuilder>(workspace);
  if (enable_links) {
    HyperlinkBuilder::VarKV constants;
    const std::string project_root = std::filesystem::current_path().string();
    constants["project_root"] = project_root;
    std::string external_abs_dir;
    if (absl::StrContains(workspace.external_dir, "../")) {
      external_abs_dir = MakeAbsolute(workspace.external_dir);
    } else {
      external_abs_dir =
        absl::StrCat(project_root, "/", workspace.external_dir);
    }
    constants["external_root"] = external_abs_dir;
    link_builder->Build(constants);
  }
  return link_builder;
}

}  // namespace bant
