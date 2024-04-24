// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
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

#ifndef BANT_NAMED_CONTENT_
#define BANT_NAMED_CONTENT_

#include <ostream>
#include <string>
#include <string_view>
#include <utility>

#include "bant/frontend/linecolumn-map.h"

namespace bant {
// A NamedLineIndexedContent is represeenting some immutable content that
// has a natural name (e.g. a filename) and whose content is a blob of text
// that is processed.
//
// It is meant to be passed to some sort of scanning process that looks at the
// content will update the line index.
//
// Users of this class then have a convenient way to extract location
// information from any string-view that is a substring of the content.
// These can be displayed as something like "my/filename.txt:17:22-27".
//
// This is a wrapper around a content that needs to be owned somehwere else.
class NamedLineIndexedContent {
 public:
  // Create NamedLineIndexedContent with filename and content.
  // Does _not_ initialize line index yet, that will happend during whatever
  // scanning operation is processing this text.
  NamedLineIndexedContent(std::string_view filename, std::string_view content)
      : name_(filename), content_(content) {}

  NamedLineIndexedContent(const NamedLineIndexedContent &) = delete;
  NamedLineIndexedContent(NamedLineIndexedContent &&) = default;

  // The immutable view of the content.
  std::string_view content() const { return content_; }
  size_t size() const { return content_.size(); }

  // Name of this content, typically the filename.
  std::string_view name() const { return name_; }

  // The index to be filled by the scanning process.
  LineColumnMap *mutable_line_index() { return &line_index_; }

  // Given "text", that must be a substring of content(), return range.
  LineColumnRange GetRange(std::string_view text) const;

  // Given the string_view, that must be a substring of content(), format
  // the location of that string view to stream;
  std::ostream &Loc(std::ostream &out, std::string_view s) const;

  // Same, but returning a string.
  std::string Loc(std::string_view s) const;

 private:
  std::string name_;
  std::string_view content_;
  LineColumnMap line_index_;
};
}  // namespace bant
#endif  // BANT_NAMED_CONTENT_
