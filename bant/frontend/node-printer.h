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

#ifndef BANT_NODE_PRINTER_H
#define BANT_NODE_PRINTER_H

#include <optional>
#include <string_view>

#include "bant/frontend/ast.h"
#include "bant/session.h"
#include "bant/util/grep-highlighter.h"

namespace bant {
// Print node to session output and handling filter grep requestes.
// Prints headline if non-empty and node was not filtered out.
// GrepHighlighter is passed in as creating it can be expensive.
// Returns true if this was printed and not filtered out.
bool PrintNode(Session &session, const GrepHighlighter &highlighter,
               std::string_view headline, Node *node);

// Given a node, extract the first identifier or string it encounters. Useful
// to build headlines for PrintNode(). The string_view is from the original
// Project, so it allows to SourceLocator::Loc()-extract the original location.
std::optional<std::string_view> FindFirstLocatableString(Node *ast);
}  // namespace bant
#endif  // BANT_NODE_PRINTER_H
