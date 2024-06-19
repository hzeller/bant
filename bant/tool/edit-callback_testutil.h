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

#ifndef BANT_TOOL_EDIT_CALLBACK_TESTUTIL_
#define BANT_TOOL_EDIT_CALLBACK_TESTUTIL_

#include <string>
#include <string_view>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "bant/tool/edit-callback.h"
#include "bant/types-bazel.h"
#include "gtest/gtest.h"

namespace bant {
class EditExpector {
 public:
  EditExpector() = default;
  EditExpector(EditExpector &&) = delete;
  EditExpector(const EditExpector &) = delete;

  ~EditExpector() {
    if (!expected_edits_.empty()) {
      for (const std::string &e : expected_edits_) {
        ADD_FAILURE() << "'" << e << "' expected, but never seen.";
      }
    }
  }
  EditExpector &ExpectAdd(std::string_view target) {
    expected_edits_.insert(Encode(EditRequest::kAdd, "", target));
    return *this;
  }
  EditExpector &ExpectRemove(std::string_view target) {
    expected_edits_.insert(Encode(EditRequest::kRemove, target, ""));
    return *this;
  }
  EditExpector &ExpectRename(std::string_view before, std::string_view after) {
    expected_edits_.insert(Encode(EditRequest::kRename, before, after));
    return *this;
  }

  EditCallback checker() {
    return [this](EditRequest op, const BazelTarget &target,
                  std::string_view before, std::string_view after) {
      const std::string actual = Encode(op, before, after);
      auto erase_count = expected_edits_.erase(actual);
      EXPECT_TRUE(erase_count == 1) << "'" << actual << "' not in expectations";
    };
  }

 private:
  static std::string Encode(EditRequest op, std::string_view before,
                            std::string_view after) {
    switch (op) {
    case EditRequest::kAdd: return absl::StrCat("Add(", after, ")");
    case EditRequest::kRemove: return absl::StrCat("Remove(", before, ")");
    case EditRequest::kRename:
      return absl::StrCat("Rename(", before, " -> ", after, ")");
    default: return "?";
    }
  }
  absl::flat_hash_set<std::string> expected_edits_;
};
}  // namespace bant

#endif  // BANT_TOOL_EDIT_CALLBACK_TESTUTIL_
