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

#ifndef BANT_OUTPUT_FORMAT_H
#define BANT_OUTPUT_FORMAT_H
namespace bant {

// Output format for commands (chosen with -f option) influencing the
// table printer.
// kNative though might also interpreted differently depending on command.
enum class OutputFormat {
  kNative,  // Default printing for that command. Table printer: aligned Table
  kSExpr,   // lisp s-expression
  kPList,   // lisp p-list
  kJSON,
  kCSV,
  kGraphviz,
};
}  // namespace bant
#endif
