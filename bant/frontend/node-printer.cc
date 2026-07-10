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

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "bant/explore/cross-reference.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/parsed-project.h"
#include "bant/frontend/print-visitor.h"
#include "bant/session.h"
#include "bant/types-bazel.h"
#include "bant/util/grep-highlighter.h"
#include "bant/util/hyperlink-builder.h"

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
               std::string_view headline, Node *node,
               const CrossReferenceMap *xrefs) {
  if (!node) return false;

  static constexpr std::string_view kHeadlineColor = "\033[2;37m";
  static constexpr std::string_view kHeadlineReset = "\033[0m";

  const bool make_hyperlinks = session.linkgen() && xrefs;
  const CommandlineFlags &flags = session.flags();
  std::stringstream ast_out;
  PrintVisitor printer(ast_out, flags.do_color);

  // somewhat hacky: remmeber if the link anntation start actually was
  // succesful, so when we get to the close-link annoation we know to emit the
  // corresponding end anchor text.
  bool last_print_link_success = false;

  // need also cross reference map.
  struct OffsetAnnotation {
    size_t offset;
    std::function<void(std::ostream &)> annotation_printer;
  };
  std::vector<OffsetAnnotation> annotations;  // strictly ordered by startpos.
  if (make_hyperlinks) {
    auto annotation_adder = [&](std::string_view s) {
      auto found = xrefs->FindBySubrange(s);
      if (found == xrefs->end()) return;
      std::visit(
        [&](const auto &linkable) {
          const size_t current_offset = ast_out.tellp();
          // Add start- and end-annotation
          annotations.push_back(
            OffsetAnnotation{current_offset, [&](std::ostream &out) {
                               last_print_link_success =
                                 session.linkgen()->LinkTo(linkable, out);
                             }});
          annotations.push_back(OffsetAnnotation{
            current_offset + s.length(), [&](std::ostream &out) {
              if (last_print_link_success) {
                out << HyperlinkBuilder::kTerminalEndAnchorText;
              }
            }});
        },
        *found);
    };
    printer.RegisterStringScalarCallback(annotation_adder);
  }
  printer.WalkNonNull(node);

  std::stringstream headline_out;
  if (!headline.empty()) {
    if (flags.do_color) headline_out << kHeadlineColor;
    headline_out << "# " << headline << "\n";
    if (flags.do_color) headline_out << kHeadlineReset;
  }

  // TODO: the highlighter should of course also collect annotations, then
  // we sort everything and apply all of them as annoations.
  std::string print_out = ast_out.str();
  const std::string_view print_out_view = print_out;
  std::stringstream annotation_out;
  if (make_hyperlinks) {
    size_t last_offset = 0;
    for (const OffsetAnnotation &annotation : annotations) {
      const size_t new_offset = annotation.offset;
      annotation_out << print_out_view.substr(last_offset,
                                              new_offset - last_offset);
      annotation.annotation_printer(annotation_out);
      last_offset = new_offset;
    }
    annotation_out << print_out_view.substr(last_offset) << "\n";
    print_out = annotation_out.str();
  }
  return highlighter.EmitMatch(print_out, session.out(), headline_out.str(),
                               "\n");
}

// Print visibility, but not regular print walk, but put in one line.
static void MaybePrintVisibility(List *visibility, std::ostream &out) {
  if (!visibility) return;
  out << " (visibility:";
  for (Node *v : *visibility) {
    const Scalar *const s = v->CastAsScalar();
    if (!s) continue;
    out << " " << s->AsString();
  }
  out << ")";
}

std::pair<size_t, size_t> PrintProject(Session &session,
                                       const BazelTargetMatcher &pattern,
                                       const ParsedProject &project) {
  size_t count = 0;
  size_t total = 0;
  const CommandlineFlags &flags = session.flags();

  auto highlighter = CreateGrepHighlighterFromFlags(session);
  if (!highlighter) {
    return {count, total};  // Issue building the highligher.
  }
  std::unique_ptr<CrossReferenceMap> xrefs;
  if (session.linkgen()) {
    xrefs = BuildCrossReferences(project);
  }
  for (const auto &[package, file_content] : project.ParsedFiles()) {
    if (flags.print_only_errors && file_content->errors.empty()) {
      continue;
    }
    if (!pattern.Match(package)) {
      continue;
    }

    total += file_content->ast->size();

    // Detailed print of package if requested with -a (all)
    if (flags.print_ast) {
      for (Node *item : *file_content->ast) {
        std::stringstream headline;
        auto position_or = FindFirstLocatableString(item);
        if (position_or.has_value()) {
          headline << project.Loc(*position_or);
        }
        if (PrintNode(session, *highlighter, headline.str(), item,
                      xrefs.get())) {
          ++count;
        }
      }
      continue;
    }

    // ... otherwise just print matching rules.
    query::FindTargetsAllowEmptyName(
      file_content->ast, {}, [&](const query::Result &result) {
        std::optional<BazelTarget> maybe_target;
        if (!result.name.empty()) {
          maybe_target = package.QualifiedTarget(result.name);
        }
        // If pattern requires some match, need to check now.
        if (!maybe_target.has_value() || !pattern.Match(*maybe_target)) {
          return;
        }

        // TODO: instead of just marking the range of the function name,
        // show the range the whole function covers until closed parenthesis.
        std::stringstream headline;
        headline << project.Loc(result.node->identifier()->id());
        if (maybe_target.has_value()) {  // only has value if target with name.
          headline << " " << *maybe_target;
        }
        MaybePrintVisibility(result.visibility, headline);

        if (PrintNode(session, *highlighter, headline.str(), result.node,
                      xrefs.get())) {
          ++count;
        }
      });
  }
  return {count, total};
}

}  // namespace bant
