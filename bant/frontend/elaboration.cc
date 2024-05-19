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

#include "bant/frontend/elaboration.h"

#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/parsed-project.h"
#include "bant/frontend/source-locator.h"
#include "bant/session.h"
#include "bant/util/stat.h"

namespace bant {
namespace {
class NestCounter {
 public:
  explicit NestCounter(int *value) : value_(value) { ++*value_; }
  ~NestCounter() { --*value_; }

 private:
  int *const value_;
};

class SimpleElaborator : public BaseNodeReplacementVisitor {
 public:
  explicit SimpleElaborator(ParsedProject *project) : project_(project) {}

  Node *VisitFunCall(FunCall *f) final {
    const NestCounter c(&nest_level_);
    return BaseNodeReplacementVisitor::VisitFunCall(f);
  }

  Node *VisitList(List *l) final {
    // TODO: maybe increase nest level here, but need to make sure
    // toplevel project would be at level 0 (as file-ast is a list)
    return BaseNodeReplacementVisitor::VisitList(l);
  }

  Node *VisitAssignment(Assignment *a) final {
    Node *result = BaseNodeReplacementVisitor::VisitAssignment(a);
    if (nest_level_ == 0) {
      global_variables_[a->identifier()->id()] = a->value();
    }
    return result;
  }

  // Very narrow of operations actually supported. Only what we typically need.
  Node *VisitBinOpNode(BinOpNode *b) final {
    Node *post_visit = BaseNodeReplacementVisitor::VisitBinOpNode(b);
    BinOpNode *bin_op = post_visit->CastAsBinOp();  // still binop ?
    if (!bin_op) return post_visit;
    switch (bin_op->op()) {
    case '+': {
      {
        List *left = bin_op->left()->CastAsList();
        List *right = bin_op->right()->CastAsList();
        if (left && right && left->type() == right->type()) {
          return ConcatLists(left, right);
        }
      }
      {
        Scalar *left = bin_op->left()->CastAsScalar();
        Scalar *right = bin_op->right()->CastAsScalar();
        if (left && right && left->type() == right->type() &&
            left->type() == Scalar::ScalarType::kString) {
          return ConcatStrings(project_->GetLocation(bin_op->source_range()),
                               left->AsString(), right->AsString());
        }
      }
      return bin_op;  // Unimplemented op. Return as-is.
    }
    default: return bin_op;
    }
  }

  Node *VisitIdentifier(Identifier *i) final {
    auto found = global_variables_.find(i->id());
    return found != global_variables_.end() ? found->second : i;
  }

 private:
  List *ConcatLists(List *left, List *right) {
    List *result = Make<List>(left->type());
    for (Node *n : *left) {
      result->Append(project_->arena(), n);
    }
    for (Node *n : *right) {
      result->Append(project_->arena(), n);
    }
    return result;
  }

  StringScalar *ConcatStrings(const FileLocation &op_location,
                              std::string_view left, std::string_view right) {
    const size_t new_length = left.size() + right.size();
    char *new_str = static_cast<char *>(project_->arena()->Alloc(new_length));
    memcpy(new_str, left.data(), left.size());
    memcpy(new_str + left.size(), right.data(), right.size());
    std::string_view assembled{new_str, new_length};
    StringScalar *result = Make<StringScalar>(assembled, false, false);

    // Whenever anyone is asking for where this string is coming from, tell
    // them the original location where the operation is coming from.
    project_->RegisterLocationRange(assembled,
                                    Make<FixedSourceLocator>(op_location));
    return result;
  }

  template <typename T, class... U>
  T *Make(U &&...args) {
    return project_->arena()->New<T>(std::forward<U>(args)...);
  }

  ParsedProject *const project_;
  int nest_level_ = 0;
  absl::flat_hash_map<std::string_view, Node *> global_variables_;
};
}  // namespace

Node *Elaborate(ParsedProject *project, Node *ast) {
  SimpleElaborator elaborator(project);
  return elaborator.WalkNonNull(ast);
}

void Elaborate(Session &session, ParsedProject *project) {
  bant::Stat &elab_stats = session.GetStatsFor("Elaborated", "files");
  const ScopedTimer timer(&elab_stats.duration);

  for (const auto &[_, build_file] : project->ParsedFiles()) {
    Node *const result = Elaborate(project, build_file->ast);
    CHECK_EQ(result, build_file->ast) << "Toplevel should never be replaced";
    ++elab_stats.count;
  }
}
}  // namespace bant
