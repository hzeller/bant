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

#include <string>
#include <string_view>

#include "bant/frontend/linecolumn-map.h"
#include "bant/frontend/source-locator.h"

namespace bant {

// A NamedLineIndexedContent is a view of some immutable content that
// has a natural name (e.g. a filename) and whose content is a blob of text
// that is processed.
//
// It is meant to be passed to some sort of scanning process that looks at the
// content and will update the line index (and can use the source_name() for
// error reporting).
//
// Users of this class then have a convenient way to extract location
// using the SourceLocator capabilities. Location information can be queried
// with any string-view that is a substring of the content (up to what has
// already been scan-processed).
// These can be displayed as something like "my/filename.txt:17:22-27".
//
// Note, this is a view for content that needs to be owned somehwere else.
class NamedLineIndexedContent : public SourceLocator {
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

  // The index to be filled by the scanning process.
  LineColumnMap *mutable_line_index() { return &line_index_; }

  // -- SourceLocator interface

  // Name of this content, typically the filename.
  std::string_view source_name() const final { return name_; }

  // Given "text", that must be a substring of content(), return range.
  LineColumnRange GetLocation(std::string_view text) const final;

 private:
  const std::string name_;
  const std::string_view content_;
  LineColumnMap line_index_;
};
}  // namespace bant
#endif  // BANT_NAMED_CONTENT_
