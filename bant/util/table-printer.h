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

#ifndef TABLE_PRINTER_H
#define TABLE_PRINTER_H

#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "bant/output-format.h"
#include "bant/util/grep-highlighter.h"

namespace bant {
class TablePrinter {
 public:
  // Table with the folllowing header names. Number of headers determines
  // number of columns.
  static std::unique_ptr<TablePrinter> Create(
    std::ostream &out, OutputFormat format, const GrepHighlighter &highlighter,
    const std::vector<std::string> &headers);
  virtual ~TablePrinter() = default;

  // A simple row with column number of strings to be printed.
  virtual void AddRow(const std::vector<std::string> &row) = 0;

  // Print a row with the first column-1 elements fixed text and the last
  // element a repeated value.
  // Depending on the output format, this will be rendered differently:
  // Plain formats such as table and CSV print multiple full rows, repeating
  //  the first part.
  // Structured outputs such as sexpr, json, and plists will print a repeated
  //  element.
  virtual void AddRowWithRepeatedLastColumn(
    const std::vector<std::string> &row_prefix,
    const std::vector<std::string> &repeat_col) {}

  virtual void Finish() = 0;
};
}  // namespace bant

#endif  // TABLE_PRINTER_H
