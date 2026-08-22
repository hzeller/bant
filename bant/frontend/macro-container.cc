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

#include "bant/frontend/macro-container.h"

#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/named-content.h"
#include "bant/frontend/parsed-project.h"
#include "bant/frontend/parser.h"
#include "bant/frontend/scanner.h"
#include "bant/types-bazel.h"
#include "bant/util/file-utils.h"
#include "bant/util/filesystem.h"

namespace bant {
// Builtin macros are always at toplevel.
// The keys are assembled that it is possible to go backwards in slashes and
// thus find according to our desired scoping rules:
//  - if in package: go backwards, in that package, then check for project
//    specific macros, falling back to builtin
//  - if in project: go backwards in that project, but don't look at any
//    package macros. Once hitting project base, fall back to builtin.
static constexpr std::string_view kBuiltinKey = "B";
static constexpr std::string_view kFromFilePrefix = "B/[from-file]";

/*static*/ std::string MacroContainer::KeyForPackage(
  const BazelPackage &package) {
  // Slash-separated elements, so that we can look backwards.
  std::string result;

  result.reserve(kFromFilePrefix.length() + package.project.length() +
                 package.path.length() + 2);
  result.append(kFromFilePrefix);
  if (!package.project.empty()) {
    result.append("/").append(package.project);
  }
  if (!package.path.empty()) {
    result.append("/").append(package.path);
  }
  return result;
}

// TODO: measure how expensive this is. This is called for everything that
// looks like a macro-call in the elaboration, so the backwards search
// for each of them (typically just ending up at builtin anyway) can be
// expensive. For now: KISS, but might need to revisit and build index later.
Node *MacroContainer::FindMacro(std::string_view macro_name,
                                const BazelPackage &package) const {
  const std::string full_key = KeyForPackage(package);
  const auto next_key = [](std::string_view key) {
    const std::string_view::size_type last_slash = key.find_last_of('/');
    if (last_slash == std::string_view::npos) return key.substr(0, 0);
    return key.substr(0, last_slash);
  };
  for (std::string_view key = full_key; !key.empty(); key = next_key(key)) {
    auto found_package = macros_.find(key);
    if (found_package == macros_.end()) continue;

    const MacroByName &by_name = found_package->second;
    auto found_macro = by_name.find(macro_name);
    if (found_macro != by_name.end()) return found_macro->second;
  }

  return nullptr;
}

absl::Status MacroContainer::AddMacroContent(std::string_view source_name,
                                             std::string_view content,
                                             std::string_view package_key,
                                             std::ostream &errors) {
  auto named_content =
    std::make_unique<NamedLineIndexedContent>(source_name, content);
  Scanner scanner(*named_content);
  Parser parser(&scanner, arena_, errors);
  List *const macro_list = parser.parse();
  if (parser.parse_error()) {
    return absl::InvalidArgumentError(
      absl::StrCat("Parse error in macro file ", source_name));
  }

  MacroByName &by_name = macros_[package_key];
  for (Node *n : *macro_list) {
    Assignment *const macro_assignment = n->CastAsAssignment();
    if (!macro_assignment) {
      return absl::InvalidArgumentError(
        absl::StrCat(source_name, ": Expected assignment, got ", ToString(n)));
    }
    const Identifier *const name = macro_assignment->lhs_maybe_identifier();
    if (!name) {
      return absl::InvalidArgumentError(
        absl::StrCat(source_name, ": Expected identifier on lhs of ",
                     ToString(macro_assignment)));
    }
    by_name.emplace(name->id(), macro_assignment->value());
  }
  project_->RegisterLocationRange(named_content->content(),
                                  named_content.get());
  macro_contents_.push_back(std::move(named_content));
  return absl::OkStatus();
}

absl::Status MacroContainer::SetBuiltinMacroContent(std::string_view content) {
  return AddMacroContent("(bant-builtin)", content, kBuiltinKey, std::cerr);
}

absl::Status MacroContainer::LoadPackageMacros(const BazelPackage &package) {
  auto load_file =
    package.FullyQualifiedFile(project_->workspace(), ".bant-macros");
  if (!load_file) return absl::NotFoundError(package.ToString());
  return LoadMacrosFromFile(package, FilesystemPath(*load_file));
}

absl::Status MacroContainer::LoadMacrosFromFile(
  const BazelPackage &package, const FilesystemPath &macro_file) {
  const std::string lookup_key = KeyForPackage(package);
  if (macros_.contains(lookup_key)) {
    return absl::AlreadyExistsError(
      absl::StrCat("already loaded macros for ", package.ToString()));
  }

  Filesystem &fs = Filesystem::instance();
  if (!fs.Exists(macro_file.path())) {
    return absl::NotFoundError(
      absl::StrCat("Macro file not found: ", macro_file.path()));
  }

  std::optional<std::string> content = fs.ReadFileToString(macro_file.path());
  if (!content.has_value()) {
    return absl::NotFoundError(
      absl::StrCat("Macro file not readable: ", macro_file.path()));
  }
  auto owned = std::make_unique<std::string>(std::move(*content));
  const std::string_view view = *owned;
  macro_owned_content_.push_back(std::move(owned));
  std::stringstream error_collect;
  absl::Status status =
    AddMacroContent(macro_file.path(), view, lookup_key, error_collect);
  if (!status.ok()) {
    std::cerr << error_collect.str();
  }
  return status;
}
}  // namespace bant
