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
#include <optional>
#include <ranges>
#include <stack>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
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
#include "re2/re2.h"

namespace bant {
namespace {
// Given a list, extract all the posargs from it (or, if this is a kwargs
// list, extract the values from the rhs), iff all of these values are
// scalars
std::optional<std::vector<std::string_view>> ExtractScalarPosArgs(List *list) {
  std::vector<std::string_view> result;
  if (list == nullptr) return std::nullopt;
  for (Node *n : *list) {
    if (!n) continue;  // Parse error of sorts.
    if (BinOpNode *binop = n->CastAsBinOp(); binop && binop->op() == '=') {
      n = binop->right();
    }
    Scalar *scalar = n->CastAsScalar();
    if (!scalar) return std::nullopt;
    result.emplace_back(scalar->AsString());
  }
  return result;
}

class NestCounter {
 public:
  explicit NestCounter(int *value) : value_(value) { ++*value_; }
  ~NestCounter() { --*value_; }

 private:
  int *const value_;
};

using VariableBundle = ParsedProject::VariableBundle;

// TODO: number of handled operations and functions gets big. Split up.
class SimpleElaborator : public BaseNodeReplacementVisitor {
 public:
  SimpleElaborator(Session &session, ParsedProject *project,
                   const BazelPackage &package,
                   const ElaborationOptions &options,
                   VariableBundle *variable_storage)
      : session_(session),
        project_(project),
        options_(options),
        package_(package),
        variables_(*variable_storage) {}

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

    // Implementing a few common functions. Getting out of hand to do this
    // inline here. Think of breaking them out.
    const std::string_view fun_name = f->identifier()->id();
    if (fun_name == "glob") {
      return HandleGlob(f);
    }
    if (fun_name == "select") {
      return HandleSelect(f);
    }
    if (fun_name == "len") {
      return HandleLen(f);
    }
    if (fun_name == "range") {
      return HandleRange(f);
    }
    if (options_.expand_load_functions && fun_name == "load") {
      HandleLoad(f);
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
    if (nest_level_ != 0) return result;  // only toplevel assignments.
    if (Identifier *identifier = a->lhs_maybe_identifier()) {
      variables_[identifier->id()] = a->value();
    } else if (List *tuple_assign = a->lhs_maybe_tuple()) {
      List *const rhs = a->value()->CastAsList();
      if (rhs && tuple_assign->size() == rhs->size()) {
        using It = List::iterator;
        It left = tuple_assign->begin();
        It right = rhs->begin();
        for (/**/; left != tuple_assign->end(); ++left, ++right) {
          if (Identifier *identifier = (*left)->CastAsIdentifier()) {
            variables_[identifier->id()] = *right;
          }
        }
      }
    }
    return result;
  }

  // Variable substituion with value if known.
  Node *VisitIdentifier(Identifier *identifier) final {
    const std::string_view id = identifier->id();
    auto found = variables_.find(id);
    return found == variables_.end() ? identifier : found->second;
  }

  // Very narrow of operations actually supported. Only what we typically need.
  Node *VisitBinOpNode(BinOpNode *b) final {
    Node *post_visit = BaseNodeReplacementVisitor::VisitBinOpNode(b);
    BinOpNode *bin_op = post_visit->CastAsBinOp();  // still binop ?
    if (!bin_op) return post_visit;

    // Some of the following ops are only partially implemented. If they
    // are, we return from there, otherwise we end up at the bottom to
    // report a worthwhile operation to implement.
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
            return ConcatStrings(location, lhs, rhs);
          }
          if (lhs->type() == Scalar::ScalarType::kInt) {
            return MakeIntWithStringRep(location, lhs->AsInt() + rhs->AsInt());
          }
        }
      }
      break;
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
      break;
    }
    case '*': {
      {
        Scalar *lhs = bin_op->left()->CastAsScalar();
        Scalar *rhs = bin_op->right()->CastAsScalar();
        if (lhs && rhs && lhs->type() == rhs->type()) {
          const auto &location = project_->GetLocation(bin_op->source_range());
          if (lhs->type() == Scalar::ScalarType::kInt) {
            return MakeIntWithStringRep(location, lhs->AsInt() * rhs->AsInt());
          }
        }
      }
      break;
    }
    case '.': {
      {
        Scalar *lhs = bin_op->left()->CastAsScalar();
        FunCall *method_call = bin_op->right()->CastAsFunCall();
        if (lhs && method_call && lhs->type() == Scalar::ScalarType::kString) {
          return StringMethodCall(bin_op, lhs->AsString(), method_call);
        }
      }
      {
        List *lhs = bin_op->left()->CastAsList();
        FunCall *method_call = bin_op->right()->CastAsFunCall();
        if (lhs && method_call) {
          return ListMethodCall(bin_op, lhs, method_call);
        }
      }
      break;
    }
    case '%': {
      Scalar *lhs = bin_op->left()->CastAsScalar();
      if (lhs && lhs->type() == Scalar::ScalarType::kString) {
        return HandlePercentFormat(bin_op, lhs->AsString(), bin_op->right());
      }
      break;
    }
    case '|': {
      List *lhs = bin_op->left()->CastAsList();
      List *rhs = bin_op->right()->CastAsList();
      if (lhs && lhs->type() == List::Type::kMap &&  //
          rhs && rhs->type() == List::Type::kMap) {
        return MergeMaps(bin_op, lhs, rhs);
      }
      break;
    }
    case '[': {
      return HandleArrayOrSliceAccess(bin_op);
    }

      // Operations that we don't worry about reporting as non-implmeneted.
    case ':':  // map element 'operator'. Not doing anything with it.
      return bin_op;
    case kIn:
    case kNotIn: {
      Scalar *scalar = bin_op->left()->CastAsScalar();
      if (!scalar) return bin_op;  // LHS needs to be a valid scalar.
      if (List *in_list = bin_op->right()->CastAsList(); in_list) {
        return InListExpression(bin_op, scalar, in_list);
      }
      Scalar *rhs_string = bin_op->right()->CastAsScalar();
      if (rhs_string && rhs_string->type() == Scalar::ScalarType::kString) {
        return InStringExpression(bin_op, scalar, rhs_string->AsString());
      }
      break;
    }

    // Document all the ones not yet implemented
    case kDivide:
    case kFloorDivide:
    case kAnd:
    case kOr:
    case kLessThan:
    case kLessEqual:
    case kEqualityComparison:
    case kGreaterEqual:
    case kGreaterThan:
    case kNotEqual:
      // binops, known to not be handled (yet)
      break;

    default:
      // These are tokens that should not be a bin-operator.
      LOG(DFATAL) << "Unexpected bin-op " << bin_op->op();
    }

    // TODO: implement. So far only implemention of ops observed in the
    // field. For that observation and choose priority: turn on this log :)
    // Looks like next candidate might be '%' for formatting...
    if (session_.flags().verbose > 1) {
      auto &log = project_->Loc(session_.info(), bin_op->source_range());
      log << "Op `" << bin_op->op() << "` for operands not implemented yet;";
      if (session_.flags().verbose > 2) {
        log << " (" << bin_op << ")";  // Noisy: whole operation around.
      }
      log << "\n";
    }
    // Return unimplemented nodes as-is.
    return bin_op;
  }

  Node *VisitTernary(Ternary *ternary) final {
    BaseNodeReplacementVisitor::VisitTernary(ternary);
    Scalar *value = ternary->condition()->CastAsScalar();
    if (!value) return ternary;
    return value->AsInt() ? ternary->positive() : ternary->negative();
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
    current_lh_type_.push(lc->type());
    return WalkNonNull(lc->for_node());
    current_lh_type_.pop();
  }

 private:
  Node *ProcessFor(BinOpNode *for_node) {
    Node *const subject = for_node->left();

    BinOpNode *const in_node = for_node->right()->CastAsBinOp();
    if (!in_node || !in_node->left() || !in_node->right()) return for_node;

    List *const var_tuple = WalkNonNull(in_node->left())->CastAsList();
    List *const iterate_over = WalkNonNull(in_node->right())->CastAsList();
    if (!var_tuple || !iterate_over) {
      return for_node;
    }

    for (Node *is_var : *var_tuple) {
      if (!is_var->CastAsIdentifier()) return for_node;  // verify all variables
    }
    List *result = Make<List>(current_lh_type_.top());
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
      Node *elabed = WalkNonNull(substituted);  // Apply elaboration
      if (elabed) result->Append(project_->arena(), elabed);
    }
    return result;
  }

  List *ConcatLists(List *left, List *right) {
    if (right->empty()) return left;
    if (left->empty()) return right;
    List *result = Make<List>(left->type());
    for (List *list : {left, right}) {
      for (Node *n : *list) {
        result->Append(project_->arena(), n);
      }
    }
    return result;
  }

  Scalar *ConcatStrings(const FileLocation &op_location,  //
                        Scalar *left, Scalar *right) {
    const std::string_view left_str = left->AsString();
    if (left_str.empty()) return right;
    const std::string_view right_str = right->AsString();
    if (right_str.empty()) return left;

    const size_t new_length = left_str.size() + right_str.size();
    char *new_str = MakeStr(new_length);
    memcpy(new_str, left_str.data(), left_str.size());
    memcpy(new_str + left_str.size(), right_str.data(), right_str.size());
    const std::string_view assembled{new_str, new_length};

    // Whenever anyone is asking for where this string is coming from, tell
    // them the original location of the concat + operation.
    project_->RegisterLocationRange(assembled,
                                    Make<FixedSourceLocator>(op_location));

    return Make<StringScalar>(assembled, false, false);
  }

  Node *MergeMaps(Node *fallback, List *lhs, List *rhs) {
    // First pass: Find effective node mapping and ensure all keys are scalars
    absl::flat_hash_map<std::string_view, Node *> collect;
    for (List *list : {lhs, rhs}) {
      for (Node *element : *list) {
        BinOpNode *kv = element->CastAsBinOp();
        if (!kv || kv->op() != ':') return fallback;
        Scalar *key = kv->left()->CastAsScalar();
        if (!key) return fallback;
        collect[key->AsString()] = element;
      }
    }

    List *result = Make<List>(List::Type::kMap);
    // We need to keep the original order, so walk through the og lists.
    for (List *list : {lhs, rhs}) {
      for (Node *element : *list) {
        // We already know that the following pointer chasins is safe.
        auto key = element->CastAsBinOp()->left()->CastAsScalar()->AsString();
        auto found = collect.find(key);
        if (found == collect.end()) continue;
        result->Append(project_->arena(), found->second);
        collect.erase(found);
      }
    }

    return result;
  }

  Node *InListExpression(BinOpNode *binop, Scalar *scalar, List *in_list) {
    bool found_in_list = false;
    bool any_unknown = false;
    const std::string_view value = scalar->AsString();
    for (Node *element : *in_list) {
      Scalar *s_item = element->CastAsScalar();
      if (!s_item) {
        any_unknown = true;
        continue;
      }
      if (s_item->AsString() == value) {
        found_in_list = true;
        break;
      }
    }
    if (!found_in_list && any_unknown) return binop;  // can't decide.
    const bool flip_result = (binop->op() == TokenType::kNotIn);
    const auto &location = project_->GetLocation(binop->source_range());
    return MakeBoolWithStringRep(location, found_in_list ^ flip_result);
  }

  Node *InStringExpression(BinOpNode *binop, Scalar *needle_scalar,
                           std::string_view haystack) {
    const std::string_view needle = needle_scalar->AsString();
    const bool flip_result = (binop->op() == TokenType::kNotIn);
    return MakeBoolWithStringRep(
      project_->GetLocation(binop->source_range()),
      absl::StrContains(haystack, needle) ^ flip_result);
  }

  IntScalar *MakeBoolWithStringRep(const FileLocation &loc, bool value) {
    const std::string_view representation =
      CopyToArenaString(value ? "True" : "False", loc);
    return Make<IntScalar>(representation, value ? 1 : 0);
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
      return HandleStringFormat(orig, str, method);
    }
    if (method_name == "join") {
      return HandleStringJoin(orig, str, method->argument());
    }
    if (method_name == "split") {
      return HandleStringSplit(orig, str, method->argument());
    }
    if (method_name == "rsplit") {
      return HandleStringRsplit(orig, str, method->argument());
    }

    return orig;  // Not handled.
  }

  Node *HandleStringFormat(Node *orig, std::string_view fmt, FunCall *method) {
    static const LazyRE2 kFmtExtract{
      R"REGEX(({(?:([0-9]+)?|([a-zA-Z_][a-zA-Z0-9_]*))}))REGEX"};

    std::string assembled;
    const auto kwargs = query::ExtractKwArgs(method);
    const auto posargs_or = ExtractScalarPosArgs(method->argument());
    if (!posargs_or) {
      return orig;
    }
    const auto &posargs = *posargs_or;

    std::string_view match;
    std::optional<int> num;
    std::optional<std::string_view> str;

    size_t specifier_count = 0;
    std::string_view run = fmt;
    size_t last_pos = 0;
    while (RE2::FindAndConsume(&run, *kFmtExtract, &match, &num, &str)) {
      const size_t new_pos = match.data() - fmt.data();
      assembled.append(fmt.substr(last_pos, new_pos - last_pos));
      last_pos = new_pos + match.size();
      if (num.has_value()) {
        if (*num >= 0 && std::cmp_less(*num, posargs.size())) {
          assembled.append(posargs[*num]);
        }
      } else if (str.has_value()) {
        if (const auto found = kwargs.find(*str); found != kwargs.end()) {
          if (Scalar *scalar = found->second->CastAsScalar(); scalar) {
            assembled.append(scalar->AsString());
          } else {
            return orig;  // parameter that can not be converted to scalar.
          }
        }
      } else if (specifier_count < posargs.size()) {
        assembled.append(posargs[specifier_count]);
      } else {
        assembled.append(match);  // Nothing matches ? Keep specifier.
      }
      ++specifier_count;
    }
    assembled.append(fmt.substr(last_pos));
    return MakeNewStringScalarFrom(assembled, project_->GetLocation(fmt));
  }

  // Very simplistic right now: only understands %s
  Node *HandlePercentFormat(Node *orig, std::string_view fmt, Node *what) {
    if (List *list = what->CastAsList(); list) {
      return HandlePercentFormatList(orig, fmt, list);
    }
    if (Scalar *value = what->CastAsScalar(); value) {
      return HandlePercentFormatValue(orig, fmt, value);
    }
    return orig;
  }

  // TODO: the following two methods are somewhat duplicated; combine.
  Node *HandlePercentFormatList(Node *orig, std::string_view fmt, List *args) {
    std::string assembled;
    size_t last_fmt_pos = 0;
    size_t fmt_pos;
    List::iterator value_it = args->begin();
    while (value_it != args->end() &&
           (fmt_pos = fmt.find("%s", last_fmt_pos)) != std::string::npos) {
      assembled.append(fmt.substr(last_fmt_pos, fmt_pos - last_fmt_pos));
      Scalar *value = (*value_it)->CastAsScalar();
      if (!value) return orig;  // Can only format if all args known.
      assembled.append(value->AsString());
      last_fmt_pos = fmt_pos + 2;
      ++value_it;
    }
    assembled.append(fmt.substr(last_fmt_pos));
    return MakeNewStringScalarFrom(assembled, project_->GetLocation(fmt));
  }

  Node *HandlePercentFormatValue(Node *orig, std::string_view fmt,
                                 Scalar *value) {
    std::string assembled;
    size_t last_fmt_pos = 0;
    if (const size_t fmt_pos = fmt.find("%s", last_fmt_pos);
        fmt_pos != std::string::npos) {
      assembled.append(fmt.substr(last_fmt_pos, fmt_pos - last_fmt_pos));
      assembled.append(value->AsString());
      last_fmt_pos = fmt_pos + 2;
    } else {
      return orig;
    }
    assembled.append(fmt.substr(last_fmt_pos));
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

  struct SplitParams {
    static std::optional<SplitParams> FromArgs(List *args) {
      SplitParams result;
      List::iterator arg_it = args->begin();
      if (arg_it != args->end()) {
        Scalar *split_by = (*arg_it)->CastAsScalar();
        if (!split_by || split_by->type() != Scalar::ScalarType::kString) {
          return std::nullopt;  // need a constant string here.
        }
        result.split_by = split_by->AsString();
        ++arg_it;
      }
      if (arg_it != args->end()) {
        Scalar *count = (*arg_it)->CastAsScalar();
        if (!count || count->type() != Scalar::ScalarType::kInt) {
          return std::nullopt;
        }
        result.max_split = count->AsInt();
      }
      if (result.split_by.empty()) result.split_by = " ";
      return result;
    }

    std::string_view split_by = " ";
    int64_t max_split = std::numeric_limits<int64_t>::max();
  };

  Node *HandleStringRsplit(Node *orig, std::string_view str, List *args) {
    auto split_args = SplitParams::FromArgs(args);
    if (!split_args.has_value()) return orig;
    const std::string_view split_by = split_args->split_by;
    const size_t split_len = split_by.length();
    int pos = str.size() - 1;
    std::vector<StringScalar *> elements;
    for (int64_t count = split_args->max_split; pos > 0 && count; --count) {
      const size_t start_of_split = str.rfind(split_by, pos);
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

  Node *HandleStringSplit(Node *orig, std::string_view str, List *args) {
    auto split_args = SplitParams::FromArgs(args);
    if (!split_args.has_value()) return orig;
    const std::string_view split_by = split_args->split_by;
    const size_t split_len = split_by.length();
    size_t pos = 0;
    List *result = Make<List>(List::Type::kList);
    for (int64_t count = split_args->max_split; count; --count) {
      const size_t start_of_split = str.find(split_by, pos);
      if (start_of_split == std::string_view::npos) break;
      const std::string_view part = str.substr(pos, start_of_split - pos);
      // The string_view is from the original file, so it already has location
      result->Append(project_->arena(), Make<StringScalar>(part, false, false));
      pos = start_of_split + split_len;
    }
    if (pos != std::string::npos) {
      result->Append(project_->arena(),
                     Make<StringScalar>(str.substr(pos), false, false));
    }
    return result;
  }

  enum class MapExtract { kKeys, kValues, kItems };
  List *ExtractMapItems(List *map_list, MapExtract what) {
    List *result = Make<List>(List::Type::kList);
    for (Node *element : *map_list) {
      BinOpNode *const kv = element->CastAsBinOp();
      if (!kv || kv->op() != ':') continue;  // ¯\_(ツ)_/¯
      Node *n = nullptr;
      switch (what) {
      case MapExtract::kKeys: n = kv->left(); break;
      case MapExtract::kValues: n = kv->right(); break;
      case MapExtract::kItems: {
        List *tuple = Make<List>(List::Type::kTuple);
        tuple->Append(project_->arena(), kv->left());
        tuple->Append(project_->arena(), kv->right());
        n = tuple;
        break;
      }
      }
      if (n) result->Append(project_->arena(), n);
    }
    return result;
  }

  Node *ListMethodCall(Node *orig, List *list, FunCall *method) {
    const std::string_view method_name = method->identifier()->id();
    if (list->type() == List::Type::kMap) {
      if (method_name == "keys") {
        return ExtractMapItems(list, MapExtract::kKeys);
      }
      if (method_name == "values") {
        return ExtractMapItems(list, MapExtract::kValues);
      }
      if (method_name == "items") {
        return ExtractMapItems(list, MapExtract::kItems);
      }
      if (method_name == "get") {
        return MapGetAccess(orig, list, method->argument());
      }
    }
    return orig;  // Not handled.
  }

  static Node *MapAccess(Node *orig, List *list, std::string_view key,
                         Node *fallback_value = nullptr) {
    for (Node *element : *list) {
      BinOpNode *const kv = element->CastAsBinOp();
      if (!kv || kv->op() != ':') return orig;
      Scalar *const key_scalar = kv->left()->CastAsScalar();
      if (!key_scalar) return orig;
      if (key_scalar->AsString() == key) return kv->right();
    }
    return fallback_value ? fallback_value : orig;
  }

  static Node *MapGetAccess(Node *orig, List *map_list, List *args) {
    if (args->size() != 2) return orig;
    Scalar *const key = args->at(0)->CastAsScalar();
    if (!key) return orig;
    return MapAccess(orig, map_list, key->AsString(), args->at(1));
  }

  static std::optional<int> GetOptionalIntScalar(Node *n) {
    if (!n) return std::nullopt;
    Scalar *scalar = n->CastAsScalar();
    if (!scalar) return std::nullopt;
    if (scalar->type() != Scalar::ScalarType::kInt) return std::nullopt;
    return scalar->AsInt();
  }

  Node *HandleArrayOrSliceAccess(BinOpNode *bin_op) {
    List *const list = bin_op->left()->CastAsList();
    if (list) {
      Scalar *const index = bin_op->right()->CastAsScalar();
      if (!index) {
        BinOpNode *const slice_access = bin_op->right()->CastAsBinOp();
        if (!slice_access || slice_access->op() != ':') return bin_op;
        const auto start = GetOptionalIntScalar(slice_access->left());
        const auto end = GetOptionalIntScalar(slice_access->right());
        return list->AsSlice(project_->arena(), start, end);
      }
      if (list->type() == List::Type::kMap) {
        return MapAccess(bin_op, list, index->AsString());
      }
      if (index->type() == Scalar::ScalarType::kInt) {
        return list->AtIndex(index->AsInt(), bin_op);
      }
    }
    // Still here ? Maybe this is a string.
    Scalar *const scalar = bin_op->left()->CastAsScalar();
    if (scalar && scalar->type() == Scalar::ScalarType::kString) {
      Scalar *const index = bin_op->right()->CastAsScalar();
      if (index) {
        if (index->type() != Scalar::ScalarType::kInt) return bin_op;
        return scalar->AtIndex(project_->arena(), index->AsInt());
      }
      BinOpNode *const slice_access = bin_op->right()->CastAsBinOp();
      if (!slice_access || slice_access->op() != ':') return bin_op;
      const auto start = GetOptionalIntScalar(slice_access->left());
      const auto end = GetOptionalIntScalar(slice_access->right());
      return scalar->AsSlice(project_->arena(), start, end);
    }
    return bin_op;
  }

  Node *HandleLen(FunCall *fun) {
    if (fun->argument()->size() != 1) return fun;
    const auto &location = project_->GetLocation(fun->identifier()->id());
    if (Scalar *scalar = fun->argument()->at(0)->CastAsScalar(); scalar) {
      return MakeIntWithStringRep(location, scalar->AsString().length());
    }
    if (List *list = fun->argument()->at(0)->CastAsList(); list) {
      return MakeIntWithStringRep(location, list->size());
    }
    return fun;
  }

  static std::optional<int64_t> GetIntAt(List *list, size_t pos) {
    if (pos >= list->size()) return std::nullopt;
    Scalar *const scalar = list->at(pos)->CastAsScalar();
    if (!scalar) return std::nullopt;
    if (scalar->type() != Scalar::ScalarType::kInt) return std::nullopt;
    return scalar->AsInt();
  }

  Node *HandleRange(FunCall *fun) {
    if (fun->argument()->size() < 2) return fun;
    auto start = GetIntAt(fun->argument(), 0);
    auto end = GetIntAt(fun->argument(), 1);
    auto step = GetIntAt(fun->argument(), 2);
    if (!start || !end) return fun;
    if (!step.has_value()) {
      step = 1;
    }

    List *result = Make<List>(List::Type::kList);
    if (step.value() == 0) {
      return result;  // next best thing to an infinite range.
    }
    // Note, the following makes sure to work with negative and abs(step) > 1
    const int64_t range = *end - *start;
    const int64_t step_fillup_add = *step + (*step < 0 ? 1 : -1);
    int64_t elements = (range + step_fillup_add) / *step;
    // We don't co-routine yield that, just creating a concrete list.
    const auto &location = project_->GetLocation(fun->identifier()->id());
    if (elements <= 0 || elements > 20'000) return result;  // prevent DoS
    for (int64_t i = *start; elements > 0; i += *step, elements--) {
      result->Append(project_->arena(), MakeIntWithStringRep(location, i));
    }
    return result;
  }

  // Load potential starlark files and extract requested variables.
  void HandleLoad(FunCall *load_fun) {
    const auto args = query::ExtractStringList(load_fun->argument());
    if (args.size() < 2) return;
    auto bazel_ref = BazelTarget::ParseFrom(args[0], package_);
    if (!bazel_ref.has_value()) return;

    // If not already cached, trigger parsing Starlark file which then
    // is elaborated.
    const VariableBundle &bzl_variables = project_->GetOrAddStarlarkContent(
      session_, *bazel_ref, [&](List *ast, VariableBundle *bundle) {
        ElaborationOptions starlark_options;
        starlark_options.expand_load_functions = false;  // Don't chase further
        starlark_options.builtin_macro_expansion = false;
        // Elaborate that file and extract variables.
        SimpleElaborator starlark_elab(session_, project_, bazel_ref->package,
                                       starlark_options, bundle);
        starlark_elab.WalkNonNull(ast);
      });

    // Remaining load()-args are the variables we should use locally.
    for (size_t i = 1; i < args.size(); ++i) {
      auto found = bzl_variables.find(args[i]);
      if (found == bzl_variables.end()) continue;
      // Alternatively, instead of args[i] could also use found->first here
      // to point to original location. Might be useful in some cases.
      variables_[args[i]] = found->second;
    }
  }

  Node *HandleSelect(FunCall *fun) {
    Node *default_node = fun;  // If we won't find a default, we'll return call
    for (Node *arg : *fun->argument()) {
      List *const select_map = arg->CastAsList();
      if (!select_map || select_map->type() != List::Type::kMap) continue;
      for (Node *item : *select_map) {
        if (!item) continue;
        BinOpNode *map_item = item->CastAsBinOp();
        if (!map_item || map_item->op() != ':' || !map_item->left()) continue;
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
        if (!kwarg->lhs_maybe_identifier()) continue;
        const std::string_view kw = kwarg->lhs_maybe_identifier()->id();
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
    bant::Stat &glob_stats = session_.GetStatsFor("  - glob() walk ", "files");
    const ScopedTimer timer(&glob_stats.duration);

    GlobMatchBuilder match_builder;
    for (const std::string_view i : include) {
      match_builder.AddIncludePattern(i);
    }
    for (const std::string_view e : exclude) {
      match_builder.AddExcludePattern(e);
    }
    auto dir_matcher = match_builder.BuildRecurseDirMatchPredicate();
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
        if (file.is_directory()) return false;  // only interested in files.
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
  // Stack is a meh-abstraction here; should be addressed when list
  // comprehension is fixed.
  std::stack<List::Type> current_lh_type_;
  ParsedProject::VariableBundle &variables_;
};

}  // namespace

Node *Elaborate(Session &session, ParsedProject *project,
                const BazelPackage &package, const ElaborationOptions &options,
                Node *ast) {
  VariableBundle variable_storage;
  SimpleElaborator elaborator(session, project, package, options,
                              &variable_storage);
  return elaborator.WalkNonNull(ast);
}

void Elaborate(Session &session, ParsedProject *project,
               const ElaborationOptions &options, ParsedBuildFile *build_file) {
  // If the build file has errors, AST is not well defined and might contain
  // nullptr nodes and is not worth error check. Bail out early.
  if (!build_file->errors.empty()) return;

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
