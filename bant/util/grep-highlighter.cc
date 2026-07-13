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

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "absl/log/check.h"
#include "bant/session.h"
#include "bant/util/text-decorator.h"
#include "re2/re2.h"

namespace bant {
static constexpr std::initializer_list<std::string_view> kDefaultColors = {
  "\033[7m",   // Invers
  "\033[41m",  // red background
  // avoid green as that is a typical terminal color
  "\033[44m",  // blue background
  "\033[45m",  // magenta background
  "\033[46m",  // cyan background
};

GrepHighlighter::GrepHighlighter(bool do_highlight, bool and_semantics)
    : do_highlight_(do_highlight), and_semantics_(and_semantics) {
  SetHighlightStart(kDefaultColors);
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

bool GrepHighlighter::AppendExpressions(
  const std::vector<std::string> &regex_list, bool case_insensitive,
  std::ostream &error_out, RegexList *list) {
  bool all_good = true;
  for (const std::string_view regex : regex_list) {
    if (RE2 *const expr = BuildRegex(regex, case_insensitive, error_out)) {
      list->emplace_back(expr);
    } else {
      all_good = false;
    }
  }
  return all_good;
}

bool GrepHighlighter::AddExpressions(const std::vector<std::string> &regex_list,
                                     bool case_insensitive,
                                     std::ostream &error_out) {
  return AppendExpressions(regex_list, case_insensitive, error_out, &matchers_);
}

bool GrepHighlighter::AddExcludeExpressions(
  const std::vector<std::string> &regex_list, bool case_insensitive,
  std::ostream &error_out) {
  return AppendExpressions(regex_list, case_insensitive, error_out,
                           &exclude_matchers_);
}

bool GrepHighlighter::Match(std::string_view content,
                            TextDecorator *decorator) const {
  for (const auto &exclude_re : exclude_matchers_) {
    if (RE2::PartialMatch(content, *exclude_re)) return false;
  }

  if (matchers_.empty()) return true;  // Short path

  // Remember which regexps matched (for and semnatics)
  std::set<int> matched_regex_index;

  for (size_t i = 0; i < matchers_.size(); ++i) {
    const auto &re = matchers_[i];
    std::string_view run = content;
    std::string_view match;
    while (RE2::FindAndConsume(&run, *re, &match)) {
      matched_regex_index.insert(i);
      if (match.empty()) continue;
      if (decorator && do_highlight_) {
        const size_t offset = match.data() - content.data();
        const int color_index = i % color_highlight_.size();
        const std::string_view color = color_highlight_[color_index];
        const std::string_view reset = end_highlight_;
        decorator->AddDecoration(
          offset, match.length(), [color](std::ostream &o) { o << color; },
          [reset](std::ostream &o) { o << reset; });
      }
    }
  }

  // Requested match conditions met ?
  if (matched_regex_index.empty()) return false;
  if (and_semantics_ && matched_regex_index.size() != matchers_.size()) {
    return false;
  }
  return true;
}

bool GrepHighlight(const GrepHighlighter &highligher, std::string_view content,
                   std::ostream &out, std::string_view prefix,
                   std::string_view suffix) {
  TextDecorator decorator;
  if (!highligher.Match(content, &decorator)) return false;
  out << prefix;
  decorator.Emit(content, out);
  out << suffix;
  return true;
}

std::unique_ptr<GrepHighlighter> CreateGrepHighlighterFromFlags(
  Session &session) {
  const auto &flags = session.flags();
  const bool do_highlight = flags.do_color;
  auto result =
    std::make_unique<GrepHighlighter>(do_highlight, !flags.grep_or_semantics);
  if (!result->AddExpressions(flags.grep_include_expressions,
                              flags.regex_case_insesitive, session.error())) {
    result.reset();
  }
  if (!result->AddExcludeExpressions(flags.grep_exclude_expressions,
                                     flags.regex_case_insesitive,
                                     session.error())) {
    result.reset();
  }

  return result;
}
}  // namespace bant
