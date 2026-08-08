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

#include "absl/container/flat_hash_set.h"
#include "bant/explore/project-walker.h"
#include "bant/explore/query-utils.h"
#include "bant/explore/source-finder.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/parsed-project.h"
#include "bant/frontend/print-visitor.h"
#include "bant/frontend/source-locator.h"
#include "bant/session.h"
#include "bant/types-bazel.h"
#include "bant/types.h"
#include "bant/util/filesystem.h"
#include "bant/util/grep-highlighter.h"
#include "bant/util/hyperlink-builder.h"
#include "bant/util/text-decorator.h"
#include "bant/workspace.h"

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

namespace {
// Given a target, return location to hyperlink to. Special targets
// __pkg__ and __subpackages__ are linked to the beginning of the BUILD file.
class TargetLocator {
 public:
  explicit TargetLocator(const ParsedProject &project)
      : project_(project),
        supplemental_project_(project_.workspace(), false, false),
        index_(InitialIndex(project)) {}

  std::optional<FileLocation> GetLocationFor(Session &session,
                                             const BazelTarget &target) {
    if (const auto &found = index_.find(target); found != index_.end()) {
      return found->second;
    }
    return FindInSupplemental(session, target);
  }

  const BazelWorkspace &workspace() const { return project_.workspace(); }

 private:
  using TargetToLocation = OneToOne<BazelTarget, FileLocation>;

  static TargetToLocation InitialIndex(const ParsedProject &project) {
    TargetToLocation result;
    absl::flat_hash_set<BazelPackage> all_packages;
    const ProjectWalker walker(project);
    walker.FindTargets(
      {}, [&](const BazelPackage &package, const BazelTarget &target,
              const query::Result &query_target) {
        all_packages.emplace(package);
        result.emplace(target, project.GetLocation(query_target.name));
      });
    for (const BazelPackage &p : all_packages) {
      AddPackageLocationToIndex(p, project, &result);
    }
    return result;
  }

  static void AddPackageLocationToIndex(const BazelPackage &package,
                                        const ParsedProject &project,
                                        TargetToLocation *index) {
    const ParsedBuildFile *file = project.FindParsedOrNull(package);
    if (!file) return;  // should not happen.
    FileLocation loc{.filename = file->name()};
    for (const std::string_view pkg_name : {"__pkg__", "__subpackages__"}) {
      auto package_target = BazelTarget::ParseFrom(pkg_name, package);
      if (!package_target.has_value()) continue;  // should not happen.
      index->emplace(*package_target, loc);
    }
  }

  std::optional<FileLocation> FindInSupplemental(Session &session,
                                                 const BazelTarget &target) {
    const BazelPackage &package = target.package;
    const ParsedBuildFile *file =
      supplemental_project_.GetOrAddPackage(session, package);
    if (!file) return std::nullopt;
    AddPackageLocationToIndex(package, supplemental_project_, &index_);
    query::FindTargets(file->ast, {}, [&](const query::Result &param) {
      auto label = package.QualifiedTarget(param.name);
      if (!label.has_value()) return;
      index_.emplace(*label, supplemental_project_.GetLocation(param.name));
    });
    if (const auto &found = index_.find(target); found != index_.end()) {
      return found->second;
    }
    return std::nullopt;
  }

  const ParsedProject &project_;
  // Since we can't modify the project we're currently iterating through,
  // we keep all the on-demand loaded packages in supplemental_project_
  ParsedProject supplemental_project_;

  TargetToLocation index_;
};
}  // namespace

// TODO: need package.
// TargetToLocation; should look up project, get build file as needed
// and return either location to BUILD file or location to thing.
static bool PrintNodeInternal(Session &session,
                              const GrepHighlighter &highlighter,
                              std::string_view headline, Node *node,
                              const BazelPackage &context,
                              TargetLocator *package_locator) {
  if (!node) return false;

  static constexpr std::string_view kHeadlineColor = "\033[2;37m";
  static constexpr std::string_view kHeadlineReset = "\033[0m";

  const bool make_hyperlinks = session.linkgen() && package_locator;
  const CommandlineFlags &flags = session.flags();
  std::stringstream ast_out;
  PrintVisitor printer(ast_out, flags.do_color);

  // Only if we actually could print a link, use that to close the anchor.
  // Since there are no overlapping links, a simple boolean is sufficient.
  bool link_emitted = false;

  Filesystem &fs = Filesystem::instance();
  TextDecorator text_decorator;
  if (make_hyperlinks) {
    // Whenever the node printer comes accross a string view scalar, it
    // informs us, and we might want to add a decoration later at wherever
    // the current stream position is.
    auto stringview_print_observer = [&](std::string_view s) {
      if (s.empty() || s[0] == '-') return;  // some sort of flag.
      if (s.find_first_of(" \t\n") != std::string_view::npos) return;
      const size_t current_offset = ast_out.tellp();  // where are we at output
      // Note: content of 's' is still valid when decorator is called, as it
      // is backed by AST.
      text_decorator.AddDecoration(
        current_offset, s.length(),
        [&, s](std::ostream &out) {
          // We only go through the effort of
          // attempting to resolve and link these
          // when actually printed.
          auto target = context.QualifiedTarget(s);
          if (target.has_value()) {
            if (auto loc = package_locator->GetLocationFor(session, *target);
                loc.has_value()) {
              link_emitted = session.linkgen()->LinkTo(*loc, out);
              return;
            }
          }

          const auto maybe_file =
            context.FullyQualifiedFile(package_locator->workspace(), s);
          if (!maybe_file) return;
          for (const auto &p : PossibleSourceLocations(*maybe_file)) {
            if (fs.Exists(p.path.path())) {
              link_emitted = session.linkgen()->LinkTo(p.path.path(), out);
              return;
            }
          }
        },
        [&](std::ostream &out) {
          if (link_emitted) out << HyperlinkBuilder::kTerminalEndAnchorText;
        });
    };
    printer.RegisterStringScalarCallback(stringview_print_observer);
  }
  printer.WalkNonNull(node);

  const std::string ast_print = ast_out.str();
  if (!highlighter.Match(ast_print, &text_decorator)) return false;

  if (!headline.empty()) {
    if (flags.do_color) session.out() << kHeadlineColor;
    session.out() << "# " << headline << "\n";
    if (flags.do_color) session.out() << kHeadlineReset;
  }

  text_decorator.Emit(ast_print, session.out());
  session.out() << "\n";
  return true;
}

bool PrintNode(Session &session, const GrepHighlighter &highlighter,
               std::string_view headline, Node *node) {
  return PrintNodeInternal(session, highlighter, headline, node, {}, nullptr);
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
  std::unique_ptr<TargetLocator> xrefs;
  if (session.linkgen()) {
    xrefs = std::make_unique<TargetLocator>(project);
  }
  project.ForEach([&](const BazelPackage &package,
                      ParsedBuildFile &file_content) {
    if (flags.print_only_errors && file_content.errors.empty()) {
      return;
    }
    if (!pattern.Match(package)) {
      return;
    }

    total += file_content.ast->size();

    // Detailed print of package if requested with -a (all)
    if (flags.print_ast) {
      for (Node *item : *file_content.ast) {
        std::stringstream headline;
        auto position_or = FindFirstLocatableString(item);
        if (position_or.has_value()) {
          headline << project.Loc(*position_or);
        }
        if (PrintNodeInternal(session, *highlighter, headline.str(), item,
                              package, xrefs.get())) {
          ++count;
        }
      }
      return;
    }

    // ... otherwise just print matching rules.
    query::FindTargetsAllowEmptyName(
      file_content.ast, {}, [&](const query::Result &result) {
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

        if (PrintNodeInternal(session, *highlighter, headline.str(),
                              result.node, package, xrefs.get())) {
          ++count;
        }
      });
  });
  return {count, total};
}

}  // namespace bant
