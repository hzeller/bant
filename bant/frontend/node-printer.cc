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

#include "bant/frontend/node-printer.h"

#include <optional>
#include <sstream>
#include <string_view>

#include "bant/frontend/ast.h"
#include "bant/frontend/print-visitor.h"
#include "bant/session.h"
#include "bant/util/grep-highlighter.h"

namespace bant {
// If we have an arbitrary node, find the fist string or identifier to latch
// on to report a file position.
std::optional<std::string_view> FindFirstLocatableString(Node *ast) {
  class FindFirstString : public BaseVoidVisitor {
   public:
    void VisitFunCall(FunCall *f) override {
      WalkNonNull(f->identifier());
      WalkNonNull(f->right());
    }
    void VisitBinOpNode(BinOpNode *b) final {
      if (result_.has_value()) return;  // Done already, can stop walking.
      BaseVoidVisitor::VisitBinOpNode(b);
    }

    void VisitScalar(Scalar *s) final {
      if (result_.has_value()) return;
      if (!s->AsString().empty()) result_ = s->AsString();
    }
    void VisitIdentifier(Identifier *id) final {
      if (result_.has_value()) return;
      if (id) result_ = id->id();
    }
    std::optional<std::string_view> found() { return result_; }

   private:
    std::optional<std::string_view> result_;
  };

  FindFirstString finder;
  ast->Accept(&finder);
  return finder.found();
}

bool PrintNode(Session &session, const GrepHighlighter &highlighter,
               std::string_view headline, Node *node) {
  if (!node) return false;

  static constexpr std::string_view kHeadlineColor = "\033[2;37m";
  static constexpr std::string_view kHeadlineReset = "\033[0m";

  const CommandlineFlags &flags = session.flags();
  std::stringstream ast_out;
  PrintVisitor printer(ast_out, flags.do_color);
  printer.WalkNonNull(node);

  std::stringstream headline_out;
  if (!headline.empty()) {
    if (flags.do_color) headline_out << kHeadlineColor;
    headline_out << "# " << headline << "\n";
    if (flags.do_color) headline_out << kHeadlineReset;
  }

  return highlighter.EmitMatch(ast_out.str(), session.out(), headline_out.str(),
                               "\n");
}
}  // namespace bant
