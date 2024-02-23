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

#include "types-bazel.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace bant {
std::string BazelPackage::ToString() const {
  return absl::StrCat(project, "//", path);
}

std::string BazelPackage::QualifiedFile(std::string_view relative_file) const {
  if (path.empty()) return std::string(relative_file);
  return absl::StrCat(path, "/", relative_file);
}

std::string_view BazelPackage::LastElement() const {
  auto pos = path.find_last_of('/');
  if (pos == std::string::npos) return "";
  return std::string_view(path).substr(pos + 1);
}

/*static*/ std::optional<BazelPackage> BazelPackage::ParseFrom(
  std::string_view str) {
  auto maybe_colon = str.find_last_of(':');
  if (maybe_colon != std::string_view::npos) {
    str = str.substr(0, maybe_colon);
  }
  using absl::StrSplit;
  std::vector<std::string_view> parts = StrSplit(str, absl::ByString("//"));
  if (parts.size() > 2) return std::nullopt;
  if (parts.size() == 1 && parts[0][0] != '@') return std::nullopt;
  return BazelPackage(parts[0], parts.size() == 2 ? parts[1] : "");
}

/*static*/ std::optional<BazelTarget> BazelTarget::ParseFrom(
  std::string_view str, const BazelPackage &context) {
  std::vector<std::string_view> parts = absl::StrSplit(str, ':');
  std::string_view package;
  std::string_view target;
  switch (parts.size()) {
  case 1: {
    package = parts[0];
    auto last_slash = package.find_last_of('/');
    if (last_slash != std::string_view::npos) {
      // //absl/strings to be interpreted as //absl/strings:strings
      target = package.substr(last_slash + 1);
    } else if (!package.empty() && package[0] == '@') {
      // we just have a toplevel, e.g. @jsonhpp
      target = package.substr(1);
    } else {
      package = "";  // Package without delimiter or package.
      target = str;
    }
    break;
  }
  case 2: {
    package = parts[0];
    target = parts[1];
    break;
  }
  default:  //
    return std::nullopt;
  }
  if (package.empty()) {
    return BazelTarget(context, target);
  }
  auto package_part = BazelPackage::ParseFrom(package);
  if (!package_part.has_value()) {
    return std::nullopt;
  }
  return BazelTarget(package_part.value(), target);
}

/*static*/ bool BazelTarget::LooksWellformed(std::string_view str) {
  return str.starts_with(":") || str.starts_with("//") || str.starts_with("@");
}

std::string BazelTarget::ToString() const {
  if (package.LastElement() == target_name) {
    return package.ToString();  // target==package -> compact representation.
  }
  return absl::StrCat(package.ToString(), ":", target_name);
}

std::string BazelTarget::ToStringRelativeTo(
  const BazelPackage &other_package) const {
  if (other_package != package) return ToString();  // regular handling.
  return absl::StrCat(":", target_name);
}

}  // namespace bant
