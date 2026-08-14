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
#include "bant/session.h"
#include "bant/types-bazel.h"
#include "bant/util/file-utils.h"
#include "bant/util/filesystem.h"

namespace bant {
Node *MacroContainer::FindMacro(std::string_view name,
                                const BazelPackage &package) const {
  auto found = macros_.find(name);
  if (found != macros_.end()) return found->second;
  return nullptr;
}

absl::Status MacroContainer::AddMacroContent(std::string_view source_name,
                                             std::string_view content,
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
    macros_.insert_or_assign(name->id(), macro_assignment->value());
  }
  project_->RegisterLocationRange(named_content->content(),
                                  named_content.get());
  macro_contents_.push_back(std::move(named_content));
  return absl::OkStatus();
}

absl::Status MacroContainer::SetBuiltinMacroContent(std::string_view content) {
  return AddMacroContent("(bant-builtin)", content, std::cerr);
}

absl::Status MacroContainer::LoadMacrosFromFile(
  Session &session, const FilesystemPath &macro_file) {
  std::optional<std::string> content =
    Filesystem::instance().ReadFileToString(macro_file.path());
  if (!content.has_value()) {
    return absl::NotFoundError(
      absl::StrCat("Macro file not found: ", macro_file.path()));
  }
  auto owned = std::make_unique<std::string>(std::move(*content));
  const std::string_view view = *owned;
  macro_owned_content_.push_back(std::move(owned));
  std::stringstream error_collect;
  absl::Status status = AddMacroContent(macro_file.path(), view, error_collect);
  if (!status.ok()) {
    session.info() << error_collect.str();
  }
  return status;
}
}  // namespace bant
