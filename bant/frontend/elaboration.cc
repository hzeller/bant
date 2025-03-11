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
#include <cstdint>
#include <cstring>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/strings/str_join.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/macro-substitutor.h"
#include "bant/frontend/parsed-project.h"
#include "bant/frontend/scanner.h"
#include "bant/frontend/source-locator.h"
#include "bant/frontend/substitute-copy.h"
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

// TODO: number of handled operations and functions gets big. Split up.
class SimpleElaborator : public BaseNodeReplacementVisitor {
 public:
  SimpleElaborator(Session &session, ParsedProject *project,
                   const BazelPackage &package,
                   const ElaborationOptions &options)
      : session_(session),
        project_(project),
        options_(options),
        package_(package) {}

  Node *VisitFunCall(FunCall *f) final {
    const NestCounter c(&nest_level_);
    BaseNodeReplacementVisitor::VisitFunCall(f);
    if (options_.builtin_macro_expansion) {
      if (Node *maybe_macro = MacroSubstitute(session_, project_, f);
          maybe_macro != f) {
        maybe_macro->Accept(this);  // expr. eval. TODO: limit features to that.
        return maybe_macro;
      }
    }
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
    case TokenType::kFor: {
      return ProcessFor(b);
    }
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
        Scalar *lhs = bin_op->left()->CastAsScalar();
        Scalar *rhs = bin_op->right()->CastAsScalar();
        if (lhs && rhs && lhs->type() == rhs->type()) {
          const auto &location = project_->GetLocation(bin_op->source_range());
          if (lhs->type() == Scalar::ScalarType::kString) {
            return ConcatStrings(location, lhs->AsString(), rhs->AsString());
          }
          if (lhs->type() == Scalar::ScalarType::kInt) {
            return MakeIntWithStringRep(location, lhs->AsInt() + rhs->AsInt());
          }
        }
      }
      return bin_op;  // Unimplemented op. Return as-is.
    }
    case '-': {
      {
        Scalar *lhs = bin_op->left()->CastAsScalar();
        Scalar *rhs = bin_op->right()->CastAsScalar();
        if (lhs && rhs && lhs->type() == rhs->type()) {
          const auto &location = project_->GetLocation(bin_op->source_range());
          if (lhs->type() == Scalar::ScalarType::kInt) {
            return MakeIntWithStringRep(location, lhs->AsInt() - rhs->AsInt());
          }
        }
      }
      return bin_op;  // Unimplemented op. Return as-is.
    }
    case '.': {
      {
        Scalar *lhs = bin_op->left()->CastAsScalar();
        FunCall *method_call = bin_op->right()->CastAsFunCall();
        if (lhs && method_call && lhs->type() == Scalar::ScalarType::kString) {
          return StringMethodCall(bin_op, lhs->AsString(), method_call);
        }
      }
      return bin_op;
    }
    case '[': {
      List *list = bin_op->left()->CastAsList();
      Scalar *index = bin_op->right()->CastAsScalar();
      if (list && index && index->type() == Scalar::ScalarType::kInt) {
        return ArrayAccess(bin_op, list, index->AsInt());
      }
      return bin_op;
    }

      // Document all the ones not yet implemented
    case kDivide:
    case kFloorDivide:
    case kPercent:
    case kPipeOrBitwiseOr:
    case kAnd:
    case kOr:
    case kLessThan:
    case kLessEqual:
    case kEqualityComparison:
    case kGreaterEqual:
    case kGreaterThan:
    case kNotEqual:
    case kNotIn: {
      // TODO: implement. So far only implemention of ops observed in the
      // field. For that observation and choose priority: turn on this log :)
      // Looks like next candidate might be '%' for formatting...
      if (session_.flags().verbose > 1) {
        project_->Loc(session_.info(), bin_op->source_range())
          << "Op `" << bin_op->op() << "` not implemented yet.\n";
      }
    }
      [[fallthrough]];
    case kIn:  // Strange: this should be handled by list comprehension.
      [[fallthrough]];
    default: return bin_op;
    }
  }

  Node *VisitUnaryExpr(UnaryExpr *unary) final {
    Scalar *scalar = unary->node()->CastAsScalar();
    if (!scalar) return unary;
    if (scalar->type() != Scalar::ScalarType::kInt) return unary;
    switch (unary->op()) {
    case '+': return scalar;
    case '-':
      return MakeIntWithStringRep(project_->GetLocation(scalar->AsString()),
                                  -scalar->AsInt());
    default: return unary;
    }
  }

  Node *VisitListComprehension(ListComprehension *lc) final {
    // TODO: properly implement flat output with multiple `for`.
    // Target v0.2.0+
    return lc->for_node()->Accept(this);
  }

 private:
  Node *ProcessFor(BinOpNode *for_node) {
    Node *const subject = for_node->left();

    BinOpNode *const in_node = for_node->right()->CastAsBinOp();
    if (!in_node) return for_node;

    List *const var_tuple = in_node->left()->Accept(this)->CastAsList();
    List *const iterate_over = in_node->right()->Accept(this)->CastAsList();
    if (!var_tuple || !iterate_over) {
      return for_node;
    }

    for (Node *is_var : *var_tuple) {
      if (!is_var->CastAsIdentifier()) return for_node;  // verify all variables
    }
    List *result = Make<List>(List::Type::kList);
    query::KwMap varmap;
    for (Node *element : *iterate_over) {
      // Values can be a list/tuple or a single value.
      List::iterator name_it = var_tuple->begin();
      // Multi-var case for (a, b) in [(1, 2), (3, 4), (5, 6)]
      if (List *values = element->CastAsList(); values) {
        List::iterator value_it = values->begin();
        while (name_it != var_tuple->end() && value_it != values->end()) {
          Identifier *id = (*name_it)->CastAsIdentifier();
          varmap[id->id()] = *value_it;
          ++name_it;
          ++value_it;
        }
      } else {  // Single value case for (a) in [1, 2, 3]
        Identifier *id = (*name_it)->CastAsIdentifier();
        varmap[id->id()] = element;
      }

      Node *substituted =
        VariableSubstituteCopy(subject, project_->arena(), varmap);
      Node *elabed = substituted->Accept(this);  // Apply elaboration
      result->Append(project_->arena(), elabed);
    }
    return result;
  }

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
    char *new_str = MakeStr(new_length);
    memcpy(new_str, left.data(), left.size());
    memcpy(new_str + left.size(), right.data(), right.size());
    const std::string_view assembled{new_str, new_length};

    // Whenever anyone is asking for where this string is coming from, tell
    // them the original location where the operation is coming from.
    project_->RegisterLocationRange(assembled,
                                    Make<FixedSourceLocator>(op_location));

    return Make<StringScalar>(assembled, false, false);
  }

  IntScalar *MakeIntWithStringRep(const FileLocation &loc, int64_t value) {
    const std::string_view representation =
      CopyToArenaString(std::to_string(value), loc);
    return Make<IntScalar>(representation, value);
  }

  // calls on strings, of the form "foo".method()
  Node *StringMethodCall(Node *orig, std::string_view str, FunCall *method) {
    const std::string_view method_name = method->identifier()->id();
    if (method_name == "format") {
      return HandleStringFormat(orig, str, method->argument());
    }
    if (method_name == "join") {
      return HandleStringJoin(orig, str, method->argument());
    }
    if (method_name == "rsplit") {
      return HandleStringRsplit(orig, str, method->argument());
    }

    return orig;
  }

  Node *HandleStringFormat(Node *orig, std::string_view fmt, List *args) {
    // Very simple string format just looking for {}.
    // TODO: support (Target 0.2.0+)
    //   * {variablename} for kw-args
    //   * {1} numeric
    //   * {} sequene without number.
    std::string assembled;
    size_t last_curly_pos = 0;
    size_t curly_pos;
    List::iterator value_it = args->begin();
    while (value_it != args->end() &&
           (curly_pos = fmt.find("{}", last_curly_pos)) != std::string::npos) {
      assembled.append(fmt.substr(last_curly_pos, curly_pos - last_curly_pos));
      Scalar *scalar = (*value_it)->CastAsScalar();
      if (!scalar) return orig;  // Can only format if all args known.
      assembled.append(scalar->AsString());
      last_curly_pos = curly_pos + 2;
      ++value_it;
    }
    assembled.append(fmt.substr(last_curly_pos));
    return MakeNewStringScalarFrom(assembled, project_->GetLocation(fmt));
  }

  Node *HandleStringJoin(Node *orig, std::string_view separator, List *args) {
    if (args->empty()) return orig;
    List *list_param = (*args->begin())->CastAsList();
    if (!list_param) return orig;
    std::vector<std::string_view> view_list;
    view_list.reserve(list_param->size());
    for (Node *element : *list_param) {
      Scalar *scalar = element->CastAsScalar();
      if (!scalar) return orig;  // Can only join if all values known constants.
      view_list.push_back(scalar->AsString());
    }
    return MakeNewStringScalarFrom(absl::StrJoin(view_list, separator),
                                   project_->GetLocation(separator));
  }

  Node *HandleStringRsplit(Node *orig, std::string_view str, List *args) {
    std::string_view split_string = " ";
    int64_t max_split = std::numeric_limits<int64_t>::max();

    List::iterator arg_it = args->begin();
    if (arg_it != args->end()) {
      Scalar *split_by = (*arg_it)->CastAsScalar();
      if (!split_by || split_by->type() != Scalar::ScalarType::kString) {
        return orig;  // need a constant string here.
      }
      split_string = split_by->AsString();
      ++arg_it;
    }
    if (arg_it != args->end()) {
      Scalar *count = (*arg_it)->CastAsScalar();
      if (!count || count->type() != Scalar::ScalarType::kInt) {
        return orig;
      }
      max_split = count->AsInt();
    }
    if (split_string.empty()) split_string = " ";
    const size_t split_len = split_string.length();
    int pos = str.size() - 1;
    std::vector<StringScalar *> elements;
    for (/**/; pos > 0 && max_split > 0; --max_split) {
      const size_t start_of_split = str.rfind(split_string, pos);
      if (start_of_split == std::string_view::npos) break;
      const size_t after = start_of_split + split_len;
      const std::string_view part = str.substr(after, pos - after + 1);
      // The string_view is from the original file, so it already has location
      elements.push_back(Make<StringScalar>(part, false, false));
      pos = start_of_split - 1;
    }
    if (pos > 0) {
      const std::string_view remaining = str.substr(0, pos + 1);
      elements.push_back(Make<StringScalar>(remaining, false, false));
    }
    List *result = Make<List>(List::Type::kList);
    for (StringScalar *substr : elements | std::views::reverse) {
      result->Append(project_->arena(), substr);
    }
    return result;
  }

  static Node *ArrayAccess(Node *orig, List *list, int64_t index) {
    const int64_t list_size = list->size();
    if (index < 0) index = list_size + index;
    if (index < 0 || index >= list_size) return orig;
    return (*list)[index];
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
    const std::string root_dir_assembled =
      package_.FullyQualifiedFile(project_->workspace(), ".");

    // Remove trailing dot. If current dir, make sure it does end with slash.
    std::string_view root_dir = root_dir_assembled;
    if (root_dir.ends_with("/.")) root_dir.remove_suffix(1);
    if (root_dir == ".") root_dir = "./";

    const std::vector<FilesystemPath> glob_result =
      MultiGlob(root_dir, query::ExtractStringList(include_list),
                query::ExtractStringList(exclude_list));

    // Allocate buffer enough to hold all the strings; we don't need the
    // root_dir prefix, so don't account for that part.
    const size_t skip_offset = root_dir.length();
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
    CHECK(!start_dir.empty());
    bant::Stat &glob_stats =
      session_.GetStatsFor("  - glob() walk ", "files");
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
    // just relative to that. This is the prefix we need to skip when matching,
    // including the slash between the start-dir and matches.
    const size_t skip_prefix =
      start_dir.length() + (start_dir.ends_with("/") ? 0 : 1);

    size_t checked_files = 0;
    auto result = CollectFilesRecursive(
      FilesystemPath(start_dir, match_builder.CommonDirectoryPrefix()),
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

  char *MakeStr(size_t len) {
    return static_cast<char *>(project_->arena()->Alloc(len));
  }

  // Create a new string in arena, copy the value and return the new,
  // locatable string_view
  std::string_view CopyToArenaString(std::string_view in_str,
                                     const FileLocation &loc) {
    // Copy into arena
    const size_t result_size = in_str.size();
    char *new_str = MakeStr(result_size);
    memcpy(new_str, in_str.data(), result_size);  // NOLINT
    const std::string_view arena_str(new_str, result_size);

    // Make sure it can be resolved to a location
    project_->RegisterLocationRange(arena_str, Make<FixedSourceLocator>(loc));
    return arena_str;
  }

  StringScalar *MakeNewStringScalarFrom(std::string_view in_str,
                                        const FileLocation &loc) {
    return Make<StringScalar>(CopyToArenaString(in_str, loc), false, false);
  }

  Session &session_;
  ParsedProject *const project_;
  const ElaborationOptions options_;
  const BazelPackage &package_;
  int nest_level_ = 0;
  absl::flat_hash_map<std::string_view, Node *> global_variables_;
};

}  // namespace

Node *Elaborate(Session &session, ParsedProject *project,
                const BazelPackage &package, const ElaborationOptions &options,
                Node *ast) {
  SimpleElaborator elaborator(session, project, package, options);
  return elaborator.WalkNonNull(ast);
}

void Elaborate(Session &session, ParsedProject *project,
               const ElaborationOptions &options, ParsedBuildFile *build_file) {
  bant::Stat &elab_stats = session.GetStatsFor("Elaborated", "packages");
  const ScopedTimer timer(&elab_stats.duration);
  ++elab_stats.count;

  Node *const result =
    Elaborate(session, project, build_file->package, options, build_file->ast);
  CHECK_EQ(result, build_file->ast) << "Toplevel should never be replaced.";
}

void Elaborate(Session &session, ParsedProject *project,
               const ElaborationOptions &options) {
  for (const auto &[package, build_file] : project->ParsedFiles()) {
    Elaborate(session, project, options, build_file.get());
  }
}
}  // namespace bant
