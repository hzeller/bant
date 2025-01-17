// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>
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

#include "bant/frontend/elaboration.h"

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/parsed-project.h"
#include "bant/frontend/source-locator.h"
#include "bant/session.h"
#include "bant/types-bazel.h"
#include "bant/util/file-utils.h"
#include "bant/util/glob-match-builder.h"
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
  SimpleElaborator(Session &session, ParsedProject *project,
                   const BazelPackage &package)
      : session_(session), project_(project), package_(package) {}

  Node *VisitFunCall(FunCall *f) final {
    const NestCounter c(&nest_level_);
    BaseNodeReplacementVisitor::VisitFunCall(f);
    const std::string_view fun_name = f->identifier()->id();
    if (fun_name == "glob") {
      return HandleGlob(f);
    }
    if (fun_name == "select") {
      return HandleSelect(f);
    }
    return f;
  }

  Node *VisitList(List *l) final {
    // TODO: maybe increase nest level here, but need to make sure
    // toplevel project would be at level 0 (as file-ast is a list)
    return BaseNodeReplacementVisitor::VisitList(l);
  }

  Node *VisitAssignment(Assignment *a) final {
    Node *result = BaseNodeReplacementVisitor::VisitAssignment(a);
    if (nest_level_ == 0 && a->maybe_identifier()) {
      global_variables_[a->maybe_identifier()->id()] = a->value();
    }
    return result;
  }

  // Variable substituion with value if known.
  Node *VisitIdentifier(Identifier *i) final {
    auto found = global_variables_.find(i->id());
    return found != global_variables_.end() ? found->second : i;
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
        // If there are undefined values on one side of the expression (e.g.
        // unknown variable), just return the part that is a list - it will
        // be better and more useful downstream.
        if (left && !right) return left;
        if (!left && right) return right;

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
    const std::string_view assembled{new_str, new_length};

    // Whenever anyone is asking for where this string is coming from, tell
    // them the original location where the operation is coming from.
    project_->RegisterLocationRange(assembled,
                                    Make<FixedSourceLocator>(op_location));

    return Make<StringScalar>(assembled, false, false);
  }

  Node *HandleSelect(FunCall *fun) {
    Node *default_node = fun;  // If we won't find a default, we'll return call
    for (Node *arg : *fun->argument()) {
      List *const select_map = arg->CastAsList();
      if (!select_map || select_map->type() != List::Type::kMap) continue;
      for (Node *item : *select_map) {
        BinOpNode *map_item = item->CastAsBinOp();
        if (!map_item || map_item->op() != ':') continue;
        Scalar *key = map_item->left()->CastAsScalar();
        if (!key) continue;
        if (session_.flags().custom_flags.contains(key->AsString())) {
          return map_item->right();
        }
        if (key->AsString() == "//conditions:default") {
          default_node = map_item->right();
        }
      }
    }
    return default_node;
  }

  Node *HandleGlob(FunCall *fun) {
    // Extract arguments. include_list is allowed to be a positional parameter.
    List *include_list = nullptr;
    List *exclude_list = nullptr;
    for (Node *arg : *fun->argument()) {
      if (List *as_list = arg->CastAsList()) {
        include_list = as_list;  // include_list is positional parameter.
        continue;
      }
      if (Assignment *kwarg = arg->CastAsAssignment()) {
        if (!kwarg->maybe_identifier()) continue;
        const std::string_view kw = kwarg->maybe_identifier()->id();
        if (kw == "include") {
          include_list = kwarg->value()->CastAsList();
        } else if (kw == "exclude") {
          exclude_list = kwarg->value()->CastAsList();
        }
      }
    }

    // Find directory to start the glob()-ing.
    const std::string root_dir =
      package_.FullyQualifiedFile(project_->workspace(), ".");
    const std::vector<FilesystemPath> glob_result =
      MultiGlob(root_dir, query::ExtractStringList(include_list),
                query::ExtractStringList(exclude_list));

    // Allocate buffer enough to hold all the strings; we don't need the
    // root_dir prefix, so don't account for that part.
    const size_t skip_offset = root_dir.length() + 1;
    size_t glob_strings_size = 0;
    for (const auto &f : glob_result) {
      CHECK_LT(skip_offset, f.path().length());
      glob_strings_size += f.path().length() - skip_offset;
    }
    char *const glob_strings_blob =
      static_cast<char *>(project_->arena()->Alloc(glob_strings_size));

    // Assemble result list, copying the filesystem paths to arena block and
    // collect in a list.
    List *glob_result_list = Make<List>(List::Type::kList);
    char *element_begin = glob_strings_blob;
    for (const auto &f : glob_result) {
      const size_t copy_len = f.path().length() - skip_offset;
      memcpy(element_begin, f.path().data() + skip_offset, copy_len);  // NOLINT
      const std::string_view permanent_string{element_begin, copy_len};
      auto *string_scalar = Make<StringScalar>(permanent_string, false, false);
      glob_result_list->Append(project_->arena(), string_scalar);
      element_begin += permanent_string.length();
    }

    // Any string within our allocated blob can be located back to come from
    // the original glob() function call.
    const FileLocation fun_location =
      project_->GetLocation(fun->identifier()->id());
    const std::string_view glob_location_range{glob_strings_blob,
                                               glob_strings_size};
    project_->RegisterLocationRange(glob_location_range,
                                    Make<FixedSourceLocator>(fun_location));

    return glob_result_list;
  }

  // Globbing that allows for include and exclude lists, as well as ** glob
  // characters. Combining GlobMatchBuilder and CollectFilesRecursive().
  std::vector<FilesystemPath> MultiGlob(
    std::string_view start_dir,  //
    const std::vector<std::string_view> &include,
    const std::vector<std::string_view> &exclude) {
    bant::Stat &glob_stats =
      session_.GetStatsFor("  - of which glob() walking", "files");
    const ScopedTimer timer(&glob_stats.duration);

    GlobMatchBuilder match_builder;
    for (const std::string_view i : include) {
      match_builder.AddIncludePattern(i);
    }
    for (const std::string_view e : exclude) {
      match_builder.AddExcludePattern(e);
    }
    auto dir_matcher = match_builder.BuildDirectoryMatchPredicate();
    auto file_matcher = match_builder.BuildFileMatchPredicate();

    // The glob pattern does not know about the full path up to this point,
    // just relative to that. This is the prefix we need to skip when matching.
    const size_t skip_prefix = start_dir.length() + 1;  // w/ slash.

    size_t checked_files = 0;
    auto result = CollectFilesRecursive(
      FilesystemPath(start_dir),
      [&](const FilesystemPath &dir) {
        return dir_matcher(std::string_view(dir.path()).substr(skip_prefix));
      },
      [&](const FilesystemPath &file) {
        ++checked_files;
        return file_matcher(std::string_view(file.path()).substr(skip_prefix));
      });
    glob_stats.count += checked_files;
    return result;
  }

  // Convenience method to allocate some object in our Arena.
  template <typename T, class... U>
  T *Make(U &&...args) {
    return project_->arena()->New<T>(std::forward<U>(args)...);
  }

  Session &session_;
  ParsedProject *const project_;
  const BazelPackage &package_;
  int nest_level_ = 0;
  absl::flat_hash_map<std::string_view, Node *> global_variables_;
};

}  // namespace

Node *Elaborate(Session &session, ParsedProject *project,
                const BazelPackage &package, Node *ast) {
  SimpleElaborator elaborator(session, project, package);
  return elaborator.WalkNonNull(ast);
}

void Elaborate(Session &session, ParsedProject *project,
               ParsedBuildFile *build_file) {
  bant::Stat &elab_stats = session.GetStatsFor("Elaborated", "packages");
  const ScopedTimer timer(&elab_stats.duration);
  ++elab_stats.count;

  Node *const result =
    Elaborate(session, project, build_file->package, build_file->ast);
  CHECK_EQ(result, build_file->ast) << "Toplevel should never be replaced.";
}

void Elaborate(Session &session, ParsedProject *project) {
  for (const auto &[package, build_file] : project->ParsedFiles()) {
    Elaborate(session, project, build_file.get());
  }
}
}  // namespace bant
