// bant - Bazel Navigation Tool
// Copyright (C) 2025 Henner Zeller <h.zeller@acm.org>
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

#include "bant/util/grep-highlighter.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <string_view>
#include <vector>

#include "absl/log/check.h"
#include "bant/session.h"
#include "re2/re2.h"

namespace bant {
GrepHighlighter::GrepHighlighter(bool do_highlight, bool and_semantics)
    : do_highlight_(do_highlight), and_semantics_(and_semantics) {
  const std::initializer_list<std::string_view> colors = {
    "\033[7m",   // Invers
    "\033[41m",  // red background
    // avoid green as that is a typical terminal color
    "\033[44m",  // blue background
    "\033[45m",  // magenta background
    "\033[46m",  // cyan background
  };
  SetHighlightStart(colors);
  SetHighlightEnd("\033[0m");
}

void GrepHighlighter::SetHighlightStart(
  const std::vector<std::string_view> &colors) {
  color_highlight_.clear();
  color_highlight_.insert(color_highlight_.begin(), colors.begin(),
                          colors.end());
  CHECK(!color_highlight_.empty()) << "Must have at least one color";
}

void GrepHighlighter::SetHighlightEnd(std::string_view reset_color) {
  end_highlight_ = reset_color;
}

static RE2 *BuildRegex(std::string_view regex_str, bool case_insensitive,
                       std::ostream &err_out) {
  std::string complete_re(regex_str);
  if (!complete_re.empty()) {
    if (case_insensitive) {
      complete_re.insert(0, "(?i)");
    }
    complete_re.insert(0, "(");
    complete_re.append(")");
  }

  RE2::Options options;
  options.set_log_errors(false);  // We print them ourselves
  auto regex = std::make_unique<RE2>(complete_re, options);
  if (!regex->ok()) {
    err_out << "Grep pattern: " << regex->error() << "\n";
    return nullptr;
  }
  return regex.release();
}

bool GrepHighlighter::AddExpressions(const std::vector<std::string> &regex_list,
                                     bool case_insensitive,
                                     std::ostream &error_out) {
  bool all_good = true;
  for (const std::string_view regex : regex_list) {
    if (RE2 *expr = BuildRegex(regex, case_insensitive, error_out)) {
      matchers_.emplace_back(expr);
    } else {
      all_good = false;
    }
  }
  return all_good;
}

bool GrepHighlighter::EmitMatch(std::string_view content, std::ostream &out,
                                std::string_view prefix,
                                std::string_view suffix) const {
  if (matchers_.empty()) {  // Short path
    out << prefix << content << suffix;
    return true;
  }

  // Preprocess; we first need to determine all the matches so that we can
  // properly highlight overlapping sections. Store them in matching order

  // Remember which regexps matched (for and semnatics)
  std::set<int> matched_regex_index;

  // Matching start point to start of match for a particular color and
  // -1 for 'reset color' (needs to be before regular colors)
  constexpr int kResetCol = -1;
  std::map<const char *, std::vector<int>> pos_to_expression;

  for (size_t i = 0; i < matchers_.size(); ++i) {
    const auto &re = matchers_[i];
    std::string_view run = content;
    std::string_view match;
    while (RE2::FindAndConsume(&run, *re, &match)) {
      matched_regex_index.insert(i);
      if (match.empty()) continue;
      const int color = i % color_highlight_.size();
      pos_to_expression[match.data()].emplace_back(color);
      pos_to_expression[match.data() + match.length()].emplace_back(kResetCol);
    }
  }

  // Requested match conditions met ?
  if (matched_regex_index.empty()) return false;
  if (and_semantics_ && matched_regex_index.size() != matchers_.size()) {
    return false;
  }

  if (!do_highlight_) {
    out << prefix << content << suffix;
    return true;
  }

  // TODO: when we have nested elements inside a colored region, we should
  // reset, add colored insert and re-establish that outer color.

  out << prefix;
  int highlight_depth = 0;  // Only when zero, emit the end match.
  const char *last_end = content.data();
  for (auto &[pos, colors] : pos_to_expression) {
    std::sort(colors.begin(), colors.end());  // Always reset first
    for (const int color : colors) {
      if (color == kResetCol) {
        if (--highlight_depth == 0) {  // Reset only last of overlapping
          out << std::string_view(last_end, pos - last_end);
          out << end_highlight_;
          last_end = pos;
        }
        continue;
      }

      out << std::string_view(last_end, pos - last_end);
      out << color_highlight_[color];
      last_end = pos;
      ++highlight_depth;
    }
  }
  CHECK_EQ(highlight_depth, 0);
  out << std::string_view(last_end, content.end() - last_end);
  out << suffix;

  return true;
}

std::unique_ptr<GrepHighlighter> CreateGrepHighlighterFromFlags(
  Session &session) {
  const auto &flags = session.flags();
  auto result =
    std::make_unique<GrepHighlighter>(flags.do_color, !flags.grep_or_semantics);
  if (!result->AddExpressions(flags.grep_expressions,
                              flags.regex_case_insesitive,
                              session.error())) {
    result.reset();
  }
  return result;
}
}  // namespace bant
