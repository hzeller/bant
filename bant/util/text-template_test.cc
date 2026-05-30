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

#include "bant/util/text-template.h"

#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ElementsAre;

namespace bant {
namespace {
TEST(TextTemplate, ExtractVariableNames) {
  TextTemplate t;
  EXPECT_THAT(t.Preprocess("abc"), ElementsAre());
  EXPECT_THAT(t.Preprocess("${only}"), ElementsAre("only"));
  EXPECT_THAT(t.Preprocess("${front} something"), ElementsAre("front"));
  EXPECT_THAT(t.Preprocess("hello ${end}"), ElementsAre("end"));
  EXPECT_THAT(t.Preprocess("${front} a ${middle} b ${end}"),
              ElementsAre("front", "middle", "end"));
  EXPECT_THAT(t.Preprocess("a ${early} b ${later} b"),
              ElementsAre("early", "later"));
}

static std::string ExpandToString(std::string_view templ,
                                  const std::vector<std::string> &values) {
  TextTemplate t;
  auto vars = t.Preprocess(templ);
  CHECK_EQ(vars.size(), values.size()) << templ;  // test setup failure
  std::stringstream out;
  t.Write(out, values);
  return out.str();
}

TEST(TextTemplate, VariableExpand) {
  EXPECT_EQ(ExpandToString("foo", {}), "foo");
  EXPECT_EQ(ExpandToString("foo ${var} baz", {"hello"}), "foo hello baz");
  EXPECT_EQ(ExpandToString("${front} foo", {"hello"}), "hello foo");
  EXPECT_EQ(ExpandToString("foo ${end}", {"hello"}), "foo hello");
  EXPECT_EQ(ExpandToString("a=${a}, b=${b}, c=${x}", {"42", "1", "8"}),
            "a=42, b=1, c=8");
}

TEST(TextTemplate, PreparedTemplate) {
  TextTemplate t;
  t.Preprocess("The value of ${text} is ${value}");

  // The data that we want to interface with the prepared template.
  struct SampleData {
    int value;
    std::string_view text;
  };
  std::vector<TextTemplate::ValueAccessor> accessors;
  accessors.emplace_back([](const void *data) -> std::string {
    return std::string(static_cast<const SampleData *>(data)->text);
  });
  accessors.emplace_back([](const void *data) -> std::string {
    return std::to_string(static_cast<const SampleData *>(data)->value);
  });

  SampleData data;
  TextTemplate::Prepared prepared(std::move(t), std::move(accessors));
  std::stringstream out;

  data.value = 42;
  data.text = "answer";
  prepared.Write(out, &data);

  data.value = 1001;
  data.text = "nights";
  prepared.Write(out, &data);

  EXPECT_EQ(out.str(),
            "The value of answer is 42"
            "The value of nights is 1001");
}
}  // namespace
}  // namespace bant
