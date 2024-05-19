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

#include "bant/frontend/source-locator.h"

#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

namespace bant {
std::ostream &SourceLocator::Loc(std::ostream &out, std::string_view s) const {
  out << source_name() << ":" << GetLocation(s);
  return out;
}

std::string SourceLocator::Loc(std::string_view s) const {
  std::stringstream out;
  Loc(out, s);
  return out.str();
}
}  // namespace bant
