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
#ifndef BANT_ELABORATION_FACTORIES_H
#define BANT_ELABORATION_FACTORIES_H

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "bant/frontend/ast.h"
#include "bant/frontend/parsed-project.h"
#include "bant/frontend/source-locator.h"
#include "bant/util/arena.h"

namespace bant {
// Factories for various objects generated in the elaboration. The underlying
// allocations are performed by the Arena.
class ElaborationFactories {
 public:
  explicit ElaborationFactories(ParsedProject *project)
      : project_(project), arena_(project->arena()) {}

  // Create a StringScalar node from string_view
  StringScalar *MakeNewStringScalarFrom(std::string_view in_str,
                                        const FileLocation &loc) {
    return Make<StringScalar>(CopyToArenaString(in_str, loc), false, false);
  }

  // Create a IntScalar from an int with string representation.
  IntScalar *MakeIntWithStringRep(const FileLocation &loc, int64_t value) {
    const std::string_view representation =
      CopyToArenaString(std::to_string(value), loc);
    return Make<IntScalar>(representation, value);
  }

  // Create a IntScalar representing a boolean with True/False string rep.
  IntScalar *MakeBoolWithStringRep(const FileLocation &loc, bool value) {
    const std::string_view representation =
      CopyToArenaString(value ? "True" : "False", loc);
    return Make<IntScalar>(representation, value ? 1 : 0);
  }

  char *MakeStr(size_t len) { return static_cast<char *>(arena_->Alloc(len)); }

  // Create object T with U-parameters in arena.
  template <typename T, class... U>
  T *Make(U &&...args) {
    return arena_->New<T>(std::forward<U>(args)...);
  }

  Arena *arena() { return arena_; }
  ParsedProject *project() { return project_; }

 private:
  // Create a new string in arena, copy the value and return the new,
  // locatable string_view
  std::string_view CopyToArenaString(std::string_view in_str,
                                     const FileLocation &loc) {
    // Copy into arena
    const size_t result_size = in_str.size();
    char *new_str = MakeStr(result_size);
    memcpy(new_str, in_str.data(), result_size);  // NOLINT
    const std::string_view arena_str(new_str, result_size);

    // Make sure it can be resolved to a location
    project_->RegisterLocationRange(arena_str, Make<FixedSourceLocator>(loc));
    return arena_str;
  }

  ParsedProject *project_;
  Arena *arena_;
};
}  // namespace bant

#endif  // BANT_ELABORATION_FACTORIES_H
