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

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/log/check.h"
#include "re2/re2.h"

namespace bant {
std::vector<std::string_view> TextTemplate::Preprocess(std::string_view text) {
  static const LazyRE2 kVariableExpandRe{"(\\${[A-Za-z0-9_-]+})"};

  std::vector<std::string_view> variables_found;
  parts_.clear();

  std::string_view run = text;
  const char *last_end = text.data();
  std::string_view var_expand;
  while (RE2::FindAndConsume(&run, *kVariableExpandRe, &var_expand)) {
    parts_.emplace_back(last_end,
                        static_cast<size_t>(var_expand.data() - last_end));
    var_expand.remove_prefix(2);  // remove ${
    var_expand.remove_suffix(1);  // remove }
    variables_found.emplace_back(var_expand);
    last_end = run.data();
  }
  parts_.emplace_back(run);
  return variables_found;
}

void TextTemplate::Write(std::ostream &out,
                         const std::vector<std::string> &substitutions) const {
  CHECK_EQ(substitutions.size() + 1, parts_.size()) << "wrong #substitutions";
  auto parts_it = parts_.begin();
  auto subst_it = substitutions.begin();
  while (subst_it < substitutions.end()) {
    out << *parts_it++;
    out << *subst_it++;
  }
  out << *parts_it;
}
}  // namespace bant
