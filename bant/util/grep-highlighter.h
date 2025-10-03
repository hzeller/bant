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

#ifndef BANT_GREP_HIGHLIGHTER_H
#define BANT_GREP_HIGHLIGHTER_H

#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "bant/session.h"
#include "re2/re2.h"

namespace bant {
// Given a bunch of regular expressions, check if any of them matches and
// print to output.
// Highlight matches on terminal if requested with "highlight" (typically
// callers will set this depending on isatty())
// If "regexp" list is empty, just prints output plain.
class GrepHighlighter {
 public:
  // "do_highlight" : emit matches with color highlights.
  // "and_semantics": require all distinct expressions match the content
  //                  to emit ('AND' semantics). Set to false for 'OR'.
  GrepHighlighter(bool do_highlight, bool and_semantics);

  // Set regular expressions. If there are issues, emit error to given stream
  // and return false; Should be called once.
  bool AddExpressions(const std::vector<std::string> &regex_list,
                      bool case_insensitive, std::ostream &error_out);

  bool HasExpressions() const { return !matchers_.empty(); }

  // Set different hightlight start strings for each expressionn. If there are
  // more expressions than colors, they cycle through.
  void SetHighlightStart(const std::vector<std::string_view> &colors);

  // The string used at the end of a highlight.
  // By default terminal reset escape.
  void SetHighlightEnd(std::string_view reset_color);

  // Work-horse: given content, check for matches and emit to output stream if
  // and only if there are matches (if regexp list was empty, then by definition
  // this is a match).
  //
  // If "do_highlight" was selected in the constructor, emits terminal escape
  // sequences around the matches to color the output.
  //
  // Returns if "content" matched and something was printed to the output.
  // value == regexp.size(), then all of them matched ('and' expression').
  //
  // Iff content is written also emit prefix and suffix (but prefix and suffix
  // are not subject to match checking).
  bool EmitMatch(std::string_view content, std::ostream &out,
                 std::string_view prefix = "",
                 std::string_view suffix = "") const;

 private:
  const bool do_highlight_;
  const bool and_semantics_;
  std::vector<std::string> color_highlight_;
  std::string end_highlight_;
  std::vector<std::unique_ptr<RE2>> matchers_;
};

// Convenience factory: create a GrepHighlighter from the flags in the
// session. Returns a fully constructed GrepHighlighter or nullptr if there
// was an issue with the regular expressions.
std::unique_ptr<GrepHighlighter> CreateGrepHighlighterFromFlags(
  Session &session);
}  // namespace bant

#endif  // BANT_GREP_HIGHLIGHTER_H
