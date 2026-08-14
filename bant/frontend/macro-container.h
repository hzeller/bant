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

#ifndef BANT_MACRO_CONTAINER_
#define BANT_MACRO_CONTAINER_

#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/named-content.h"
#include "bant/types-bazel.h"
#include "bant/util/arena.h"
#include "bant/util/file-utils.h"

namespace bant {
class ParsedProject;  // TODO: cyclic dependency (for register range)
class MacroContainer {
 public:
  MacroContainer(Arena *arena, ParsedProject *project)
      : arena_(arena), project_(project) {}

  Node *FindMacro(std::string_view name, const BazelPackage &) const;

  // Attempt to load macros for given
  absl::Status LoadPackageMacros(const BazelPackage &package);

  // Set content of bant file defining the macros to be found in FindMacro().
  // Can be called multiple times (e.g. built-in + project-local).
  // Passed string view must outlive ParsedProject lifetime.
  absl::Status SetBuiltinMacroContent(std::string_view content);

 private:
  // Load project-local macro definitions from a .bant-macros file.
  // Returns NotFoundError if the file doesn't exist (caller can ignore).
  absl::Status LoadMacrosFromFile(const FilesystemPath &macro_file);

  // Core macro-parsing logic: parse assignments from content and add to
  // macros_ map. On name collision, later definitions win.
  absl::Status AddMacroContent(std::string_view source_name,
                               std::string_view content, std::ostream &errors);

  Arena *const arena_;
  ParsedProject *const project_;

  std::vector<std::unique_ptr<NamedLineIndexedContent>> macro_contents_;
  std::vector<std::unique_ptr<std::string>> macro_owned_content_;
  absl::flat_hash_map<std::string_view, Node *> macros_;
};
}  // namespace bant
#endif  // BANT_MACRO_CONTAINER_
