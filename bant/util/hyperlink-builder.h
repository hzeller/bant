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

#ifndef BANT_HYPERLINK_BUILDER_H
#define BANT_HYPERLINK_BUILDER_H

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "bant/frontend/source-locator.h"
#include "bant/types-bazel.h"
#include "bant/util/text-template.h"
#include "bant/workspace.h"

namespace bant {
class HyperlinkBuilder {
 public:
  // Prefix and suffix needed for terminal links.
  // https://gist.github.com/egmontkob/eb114294efbcd5adb1944c9f3cb5feda
  static constexpr std::string_view kTerminalStartAnchor = "\033]8;;";
  static constexpr std::string_view kTerminalEndAnchor = "\033\\";
  static constexpr std::string_view kTerminalEndAnchorText = "\033]8;;\033\\";

  using VarKV = absl::flat_hash_map<std::string, std::string>;

  explicit HyperlinkBuilder(const BazelWorkspace &workspace)
      : workspace_(workspace) {}

  // Prepare HyperlinkBuilder using the given project constants that can
  // be accessed as part of the variables.
  //
  // Build internal state; needs to be called before any LinkTo() methods
  // will return links (if Build() is not called, LinkTo() will print an empty
  // empty string and return false; so simply don't call build if
  // --hyperlinks=false)
  //
  // Prefix and suffix are prepended and appended to the generated links
  // (e.g. for <a href=... or terminal escape codes)
  bool Build(const VarKV &constants,
             std::string_view prefix = kTerminalStartAnchor,
             std::string_view suffix = kTerminalEndAnchor);

  // Create a link to file location and return if it was possible to write
  // a string..
  bool LinkTo(const FileLocation &location, std::ostream &out) const;

  // Create a link to the file relative to the bazel package.
  bool LinkTo(const BazelPackage &pkg, std::string_view filename,
              std::ostream &out) const;

 private:
  const BazelWorkspace &workspace_;

  std::optional<TextTemplate::Prepared> project_path_;
  std::optional<TextTemplate::Prepared> project_path_with_loc_;

  std::optional<TextTemplate::Prepared> external_path_;
  std::optional<TextTemplate::Prepared> external_path_with_loc_;

  std::optional<TextTemplate::Prepared> generated_path_;
  std::optional<TextTemplate::Prepared> generated_path_with_loc_;
};

// Utility wrapper for a location to be printed as link on the terminal
struct HyperLinked {
  const HyperlinkBuilder *link_builder = nullptr;
  const FileLocation &location;
  std::string_view anchor_text;  // if empty, location is printed.
  std::string_view anchor_close = HyperlinkBuilder::kTerminalEndAnchorText;
};
inline std::ostream &operator<<(std::ostream &out, const HyperLinked &h) {
  const bool printed_link =
    h.link_builder && h.link_builder->LinkTo(h.location, out);
  if (h.anchor_text.empty()) {
    out << h.location;
  } else {
    out << h.anchor_text;
  }
  if (printed_link) {
    out << h.anchor_close;
  }
  return out;
}

// Convenience factory
std::unique_ptr<HyperlinkBuilder> MakeLinkBuilder(
  const BazelWorkspace &workspace, bool enable_links);

}  // namespace bant

#endif  // BANT_HYPERLINK_BUILDER_H
