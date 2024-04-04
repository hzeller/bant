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

#ifndef PRINT_UTIL_H
#define PRINT_UTIL_H

#include <algorithm>
#include <ostream>

#include "absl/log/check.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"

class TablePrinter {
 public:
  TablePrinter(int columns) : widths_(columns) {}

  void AddRow(const std::vector<std::string> &row) {
    CHECK_EQ(row.size(), widths_.size());
    for (size_t i = 0; i < widths_.size(); ++i) {
      widths_[i] = std::max(widths_[i], (int)row[i].length());
    }
    buffer_.push_back(row);
  }

  // Print as whitespace-separated table or sexpr
  void Print(std::ostream &out, bool as_sexpr) {
    if (as_sexpr) out << "(";
    for (size_t r = 0; r < buffer_.size(); ++r) {
      const auto &row = buffer_[r];
      if (as_sexpr) {
        out << (r == 0 ? "(" : "\n (");
        for (size_t c = 0; c < row.size(); ++c) {
          if (c != 0) out << " ";
          out << "\"" << absl::CEscape(row[c]) << "\"";
        }
        out << ")";
      } else {
        for (size_t i = 0; i < widths_.size(); ++i) {
          out << absl::StrFormat("%*s", -widths_[i] - 1, row[i]);
        }
        out << "\n";
      }
    }
    if (as_sexpr) out << ")\n";
  }

 private:
  // Buffer to keep while determining the print width;
  std::vector<int> widths_;
  std::vector<std::vector<std::string>> buffer_;
};
#endif  // PRINT_UTIL_H
