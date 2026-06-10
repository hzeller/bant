// -*- c++ -*-
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

// Until we have re2c in BCR, we manually do this and check in.
// re2c -T bant/tool/cc-preprocessor.re > bant/tool/cc-preprocessor.cc

#include "bant/tool/cc-preprocessor.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "bant/tool/preprocess-utils.h"

namespace bant {
std::vector<TaggedInclude> PreprocessInternal(std::string_view source,
                                              DefineMap &defines) {
  struct BranchState {
    bool is_active;
    bool is_ambiguous;
  };
  std::vector<BranchState> condition_stack;
  bool current_emitting = true;

  auto update_emitting_state = [&]() {
    current_emitting =
      condition_stack.empty() ? true : condition_stack.back().is_active;
  };

  auto is_ambiguous = [&]() {
    return condition_stack.empty() ? false
                                   : condition_stack.back().is_ambiguous;
  };

  // re2c internal variables to keep track of lexing state.
  const char *YYCURSOR = source.data();
  const char *const YYLIMIT = source.data() + source.size();
  const char *YYMARKER;
  const char *yyt1, *yyt2, *yyt3;

  // Variables remembering 'capture group' ranges.
  const char *b_start;
  const char *d_start, *d_end;
  const char *i_start, *i_end;
  const char *k_start, *k_end;
  const char *v_start, *v_end;

  std::vector<TaggedInclude> result;
  while (YYCURSOR < YYLIMIT) {
    /*!re2c
    re2c:define:YYCTYPE = char;
    re2c:yyfill:enable  = 0;
    re2c:define:YYLESSTHAN = "YYLIMIT - YYCURSOR < @@";

    POUND = "#" [ \t]*;
    IDNUM = [a-zA-Z0-9_]+;

    // Match preprocessor directives
    POUND "if" [ \t]+ (@v_start IDNUM @v_end)
    {
      const std::string_view value(v_start, v_end - v_start);
      const auto parsed = ParsePreprocessValue(value, defines);
      const bool is_active = current_emitting && parsed.is_on;
      condition_stack.emplace_back(is_active, parsed.is_ambiguous);
      update_emitting_state();

      continue;
    }

    POUND (@k_start("ifdef"|"ifndef")) [ \t]+ (@v_start IDNUM @v_end)
    {
      const bool is_ifndef = (k_start[2] == 'n');
      const std::string_view macro(v_start, v_end - v_start);

      const bool macro_known = defines.contains(macro);
      const bool is_active = current_emitting && (macro_known ^ is_ifndef);
      condition_stack.emplace_back(is_active, !macro_known);
      update_emitting_state();

      continue;
    }

    POUND "else"
    {
      if (!condition_stack.empty()) {
        condition_stack.back().is_active ^= true;
        update_emitting_state();
      }
      continue;
    }

    POUND "endif"
    {
      if (!condition_stack.empty()) {
        condition_stack.pop_back();
        update_emitting_state();
      }
      continue;
    }

    POUND "define" [ \t]+ (@k_start IDNUM @k_end) [ \t]+ (@v_start IDNUM @v_end)
    {
      const std::string_view macro(k_start, k_end - k_start);
      const std::string_view value(v_start, v_end - v_start);
      if (current_emitting) {
        defines[macro] = ParsePreprocessValue(value, defines).is_on;
      }
      continue;
    }

    POUND "include" [ \t]* (@b_start[<"]) (@i_start [^>"]* @i_end) [">]
    {
      const std::string_view inc(i_start, i_end - i_start);
      if (current_emitting || is_ambiguous()) {
         result.emplace_back(inc, b_start[0] == '<', !current_emitting);
      }
      continue;
    }

    // Skip EOL comments.
    "//"
    {
      while (YYCURSOR < YYLIMIT && *YYCURSOR != '\n') { YYCURSOR++; }
      continue;
    }

    // skip block comments
    "/*"
    {
      const std::string_view remainder(YYCURSOR, YYLIMIT - YYCURSOR);
      const size_t close_pos = remainder.find("*/");
      if (close_pos != std::string_view::npos) {
        YYCURSOR += close_pos + 2;
      } else {
        YYCURSOR = YYLIMIT;
      }
      continue;
    }

    // skip raw string literals
    "R\"" (@d_start [a-zA-Z0-9_\-.?*+^$()[\]{}|]* @d_end) "("
    {
      const std::string_view delim(d_start, d_end - d_start);
      const std::string close_seq = absl::StrCat(")", delim, "\"");

      const std::string_view remainder(YYCURSOR, YYLIMIT - YYCURSOR);
      size_t close_pos = remainder.find(close_seq);
      if (close_pos != std::string_view::npos) {
        YYCURSOR += close_pos + close_seq.size();
      } else {
        YYCURSOR = YYLIMIT;
      }
      continue;
    }

    // fallback any other
    . | '\n'
    {
      continue;
    }
    */
  }

  return result;
}
}  // namespace bant
