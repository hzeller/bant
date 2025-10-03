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

#include "bant/util/table-printer.h"

#include <map>
#include <sstream>
#include <string_view>

#include "bant/output-format.h"
#include "bant/util/grep-highlighter.h"
#include "gtest/gtest.h"

namespace bant {
TEST(TablePrinter, PlainTable) {
  const std::map<OutputFormat, std::string_view> kTests{
    {OutputFormat::kNative,
     "short            somevalue\n"
     "somewhatlongtext xyz\n"},
    {OutputFormat::kSExpr,
     "((\"short\" \"somevalue\")\n"
     " (\"somewhatlongtext\" \"xyz\"))\n"},
    {OutputFormat::kPList,
     "((:foo \"short\" :bar \"somevalue\")\n"
     " (:foo \"somewhatlongtext\" :bar \"xyz\"))\n"},
    {OutputFormat::kJSON,
     "{\"foo\": \"short\", \"bar\": \"somevalue\"}\n"
     "{\"foo\": \"somewhatlongtext\", \"bar\": \"xyz\"}\n"},
    {OutputFormat::kCSV,
     "\"foo\",\"bar\"\n"
     "\"short\",\"somevalue\"\n"
     "\"somewhatlongtext\",\"xyz\"\n"},
  };

  const GrepHighlighter highlighter(false, true);
  for (const auto &[fmt, expected] : kTests) {
    std::stringstream out;
    auto printer = TablePrinter::Create(out, fmt, highlighter, {"foo", "bar"});
    printer->AddRow({"short", "somevalue"});
    printer->AddRow({"somewhatlongtext", "xyz"});
    printer->Finish();
    EXPECT_EQ(expected, out.str()) << (int)fmt;
  }
}

TEST(TablePrinter, TableWithRepeatedLastCol) {
  const std::map<OutputFormat, std::string_view> kTests{
    {OutputFormat::kNative,
     // noval never emitted
     "oneval   somevalue\n"
     "threeval abc\n"
     "threeval def\n"
     "threeval xyz\n"},
    {OutputFormat::kSExpr,
     "((\"noval\" ())\n"
     " (\"oneval\" (\"somevalue\"))\n"
     " (\"threeval\" (\"abc\"\n"
     "              \"def\"\n"
     "              \"xyz\")))\n"},
    {OutputFormat::kPList,
     "((:foo \"noval\" :bar ())\n"
     " (:foo \"oneval\" :bar (\"somevalue\"))\n"
     " (:foo \"threeval\" :bar (\"abc\"\n"
     "                        \"def\"\n"
     "                        \"xyz\")))\n"},
    {OutputFormat::kJSON,
     "{\"foo\": \"noval\", \"bar\": []}\n"
     "{\"foo\": \"oneval\", \"bar\": [\"somevalue\"]}\n"
     "{\"foo\": \"threeval\", \"bar\": [\"abc\", \"def\", \"xyz\"]}\n"},
    {OutputFormat::kCSV,
     // noval never emitted
     "\"foo\",\"bar\"\n"
     "\"oneval\",\"somevalue\"\n"
     "\"threeval\",\"abc\"\n"
     "\"threeval\",\"def\"\n"
     "\"threeval\",\"xyz\"\n"},
  };

  const GrepHighlighter highlighter(false, true);
  for (const auto &[fmt, expected] : kTests) {
    std::stringstream out;
    auto printer = TablePrinter::Create(out, fmt, highlighter, {"foo", "bar"});
    printer->AddRowWithRepeatedLastColumn({"noval"}, {});
    printer->AddRowWithRepeatedLastColumn({"oneval"}, {"somevalue"});
    printer->AddRowWithRepeatedLastColumn({"threeval"}, {"abc", "def", "xyz"});
    printer->Finish();
    EXPECT_EQ(expected, out.str()) << (int)fmt;
  }
}

}  // namespace bant
