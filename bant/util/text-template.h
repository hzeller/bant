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

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace bant {

// Text template that allows simple variable ${foo}-style substitutions.
// Split into a preprocessing and expansion part to minimize the runtime of
// the latter. Preprocess() only needs to be called once.
class TextTemplate {
 public:
  // Parse and prepare template from given text to be expanded in Write().
  // Elements of `${VARIABLE}` are extracted. All these `VARIABLE`s are
  // returned in the same sequence they appear in the text.
  //
  // The returned string-views are pointing to the original text, so "text"
  // must outlive the Preprocess() call if the variable names are to be used.
  // Relevant static parts of the template are copied, so "text" does _not_
  // need to be alive anymore during the later Write() calls.
  //
  // Each call replaces the previous template.
  std::vector<std::string_view> Preprocess(std::string_view text);

  // Write text to "out" stream, replace variable occurences with the strings
  // in the "substitutions" vector. The vector must contain exactly the number
  // of elements the Preprocess() returned - each value is emitted at
  // its corresponding position.
  void Write(std::ostream &out,
             const std::vector<std::string> &substitutions) const;

 private:
  std::vector<std::string> parts_;
};
}  // namespace bant
