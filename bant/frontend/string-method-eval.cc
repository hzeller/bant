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

#include "bant/frontend/string-method-eval.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_join.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/elaboration-factories.h"
#include "re2/re2.h"

namespace bant {
// Given a list, extract all the posargs from it (or, if this is a kwargs
// list, extract the values from the rhs), iff all of these values are
// scalars
static std::optional<std::vector<std::string_view>> ExtractScalarPosArgs(
  List *list) {
  std::vector<std::string_view> result;
  if (list == nullptr) return std::nullopt;
  for (Node *n : *list) {
    if (!n) continue;  // Parse error of sorts.
    if (BinOpNode *binop = n->CastAsBinOp(); binop && binop->op() == '=') {
      n = binop->right();
    }
    const Scalar *const scalar = n->CastAsScalar();
    if (!scalar) return std::nullopt;
    result.emplace_back(scalar->AsString());
  }
  return result;
}

Node *StringMethodEval::StringMethodCall(Node *orig, StringScalar *object,
                                         FunCall *method) {
  const std::string_view method_name = method->identifier()->id();
  if (method_name == "format") {
    return Format(orig, object->AsString(), method);
  }
  if (method_name == "join") {
    return Join(orig, object->AsString(), method->argument());
  }
  if (method_name == "split") {
    return Split(orig, object->AsString(), method->argument());
  }
  if (method_name == "rsplit") {
    return Rsplit(orig, object->AsString(), method->argument());
  }
  if (method_name == "replace") {
    return Replace(orig, object->AsString(), method->argument());
  }
  if (method_name == "startswith") {
    return StartsWith(orig, object->AsString(), method->argument());
  }
  if (method_name == "endswith") {
    return EndsWith(orig, object->AsString(), method->argument());
  }
  if (method_name == "title") {
    return Title(orig, object->AsString());
  }
  if (method_name == "removeprefix") {
    return RemovePrefix(orig, object, method->argument());
  }
  if (method_name == "removesuffix") {
    return RemoveSuffix(orig, object, method->argument());
  }
  if (method_name == "strip") {
    return Strip(orig, object, method->argument());
  }
  if (method_name == "upper") {
    return Upper(orig, object);
  }
  if (method_name == "lower") {
    return Lower(orig, object);
  }
  if (method_name == "partition") {
    return Partition(orig, object, method->argument());
  }
  if (method_name == "rpartition") {
    return RPartition(orig, object, method->argument());
  }
  if (method_name == "find") {
    return Find(orig, object->AsString(), method->argument());
  }
  if (method_name == "rfind") {
    return RFind(orig, object->AsString(), method->argument());
  }
  return orig;  // Not handled.
}

Node *StringMethodEval::HandlePercentFormat(Node *orig, std::string_view fmt,
                                            Node *what) {
  if (List *list = what->CastAsList(); list) {
    return HandlePercentFormatList(orig, fmt, {list->begin(), list->end()});
  }
  if (Scalar *value = what->CastAsScalar(); value) {
    return HandlePercentFormatList(orig, fmt, {value});
  }
  return orig;
}

Node *StringMethodEval::HandlePercentFormatList(
  Node *orig, std::string_view fmt, const std::vector<Node *> &args) {
  std::string assembled;
  size_t last_fmt_pos = 0;
  size_t fmt_pos;
  auto value_it = args.begin();
  while (value_it != args.end() &&
         (fmt_pos = fmt.find('%', last_fmt_pos)) != std::string::npos) {
    if (fmt_pos > fmt.size() - 2) break;
    assembled.append(fmt.substr(last_fmt_pos, fmt_pos - last_fmt_pos));

    const char format_char = fmt[fmt_pos + 1];
    if (format_char != 's' && format_char != 'd') {
      assembled.append("%");
      last_fmt_pos = fmt_pos + 1;
      continue;
    }
    const Scalar *const value = (*value_it)->CastAsScalar();
    if (!value) return orig;  // Can only format if all args known.
    switch (format_char) {
    case 's': assembled.append(value->AsString()); break;
    case 'd': assembled.append(std::to_string(value->AsInt())); break;
    default: break;
    }
    last_fmt_pos = fmt_pos + 2;
    ++value_it;
  }
  if (value_it != args.end()) return orig;  // not enough args
  assembled.append(fmt.substr(last_fmt_pos));
  return f_.MakeNewStringScalarFrom(assembled, f_.project()->GetLocation(fmt));
}

Node *StringMethodEval::Format(Node *orig, std::string_view fmt,
                               FunCall *method) {
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
        if (const Scalar *scalar = found->second->CastAsScalar(); scalar) {
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
  return f_.MakeNewStringScalarFrom(assembled, f_.project()->GetLocation(fmt));
}

Node *StringMethodEval::Join(Node *orig, std::string_view separator,
                             List *args) {
  if (args->empty()) return orig;
  List *list_param = (*args->begin())->CastAsList();
  if (!list_param) return orig;
  std::vector<std::string_view> view_list;
  view_list.reserve(list_param->size());
  for (Node *element : *list_param) {
    const Scalar *const scalar = element->CastAsScalar();
    if (!scalar) return orig;  // Can only join if all values known constants.
    view_list.push_back(scalar->AsString());
  }
  return f_.MakeNewStringScalarFrom(absl::StrJoin(view_list, separator),
                                    f_.project()->GetLocation(separator));
}

Node *StringMethodEval::Replace(Node *orig, std::string_view str, List *args) {
  auto replace_args = ExtractScalarPosArgs(args);
  if (!replace_args.has_value() || replace_args->size() != 2) return orig;
  const std::string_view from = replace_args->at(0);
  const std::string_view to = replace_args->at(1);
  std::string subject(str);
  size_t start_pos = 0;
  while ((start_pos = subject.find(from, start_pos)) != std::string::npos) {
    subject.replace(start_pos, from.length(), to);
    start_pos += to.length();
  }
  return f_.MakeNewStringScalarFrom(subject, f_.project()->GetLocation(str));
}

struct StringRangeParam {
  static std::optional<StringRangeParam> FromArgs(List *args) {
    StringRangeParam result;
    List::iterator arg_it = args->begin();

    if (arg_it == args->end()) return std::nullopt;
    const Scalar *const maybe_str = (*arg_it)->CastAsScalar();
    if (!maybe_str || maybe_str->type() != Scalar::ScalarType::kString) {
      return std::nullopt;  // need a constant string here.
    }
    result.str = maybe_str->AsString();
    ++arg_it;

    if (arg_it != args->end()) {
      const Scalar *const from = (*arg_it)->CastAsScalar();
      if (!from || from->type() != Scalar::ScalarType::kInt) {
        return std::nullopt;
      }
      result.start_pos = from->AsInt();
      ++arg_it;
    }

    if (arg_it != args->end()) {
      const Scalar *const to = (*arg_it)->CastAsScalar();
      if (!to || to->type() != Scalar::ScalarType::kInt) {
        return std::nullopt;
      }
      result.end_pos = to->AsInt();
    }
    if (result.end_pos < result.start_pos) return std::nullopt;
    result.len = result.end_pos - result.start_pos;
    return result;
  }

  std::string_view str;
  size_t start_pos = 0;
  size_t end_pos = std::numeric_limits<size_t>::max();
  size_t len;
};

Node *StringMethodEval::StartsWith(Node *orig, std::string_view str,
                                   List *args) {
  auto param = StringRangeParam::FromArgs(args);
  if (!param.has_value()) return orig;
  if (param->start_pos > str.size()) return orig;
  std::string_view active_str = str.substr(param->start_pos, param->len);
  const auto &location = f_.project()->GetLocation(str);
  return f_.MakeBoolWithStringRep(location, active_str.starts_with(param->str));
}

Node *StringMethodEval::EndsWith(Node *orig, std::string_view str, List *args) {
  auto param = StringRangeParam::FromArgs(args);
  if (!param.has_value()) return orig;
  if (param->start_pos > str.size()) return orig;
  std::string_view active_str = str.substr(param->start_pos, param->len);
  const auto &location = f_.project()->GetLocation(str);
  return f_.MakeBoolWithStringRep(location, active_str.ends_with(param->str));
}

Node *StringMethodEval::Find(Node *orig, std::string_view str, List *args) {
  auto param = StringRangeParam::FromArgs(args);
  if (!param.has_value()) return orig;
  const auto &location = f_.project()->GetLocation(str);
  int64_t result;
  if (param->start_pos > str.size()) {
    result = -1;
  } else {
    const size_t pos =
      str.substr(0, param->end_pos).find(param->str, param->start_pos);
    result = pos == std::string_view::npos ? -1 : pos;
  }
  return f_.MakeIntWithStringRep(location, result);
}

Node *StringMethodEval::RFind(Node *orig, std::string_view str, List *args) {
  auto param = StringRangeParam::FromArgs(args);
  if (!param.has_value()) return orig;
  const auto &location = f_.project()->GetLocation(str);
  int64_t result;
  if (param->str.length() > param->end_pos || param->start_pos > str.size()) {
    result = -1;
  } else {
    const size_t pos =
      str.rfind(param->str, param->end_pos - param->str.length());
    result =
      pos == std::string_view::npos || pos <= param->start_pos ? -1 : pos;
  }
  return f_.MakeIntWithStringRep(location, result);
}

Node *StringMethodEval::Title(Node *orig, std::string_view str) {
  std::string result(str);
  bool new_word = true;
  for (char &c : result) {
    if (new_word && std::isalpha(c)) c = toupper(c);
    new_word = std::isspace(c);
  }
  return f_.MakeNewStringScalarFrom(result, f_.project()->GetLocation(str));
}

namespace {
struct SplitParams {
  static std::optional<SplitParams> FromArgs(List *args) {
    SplitParams result;
    List::iterator arg_it = args->begin();
    if (arg_it != args->end()) {
      const Scalar *const split_by = (*arg_it)->CastAsScalar();
      if (!split_by || split_by->type() != Scalar::ScalarType::kString) {
        return std::nullopt;  // need a constant string here.
      }
      result.split_by = split_by->AsString();
      ++arg_it;
    }
    if (arg_it != args->end()) {
      const Scalar *const count = (*arg_it)->CastAsScalar();
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
}  // namespace

Node *StringMethodEval::Rsplit(Node *orig, std::string_view str, List *args) {
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
    elements.push_back(f_.Make<StringScalar>(part, false, false));
    pos = start_of_split - 1;
  }
  if (pos > 0) {
    const std::string_view remaining = str.substr(0, pos + 1);
    elements.push_back(f_.Make<StringScalar>(remaining, false, false));
  }
  List *result = f_.Make<List>(List::Type::kList);
  for (StringScalar *substr : elements | std::views::reverse) {
    result->Append(f_.arena(), substr);
  }
  return result;
}

Node *StringMethodEval::Split(Node *orig, std::string_view str, List *args) {
  auto split_args = SplitParams::FromArgs(args);
  if (!split_args.has_value()) return orig;
  const std::string_view split_by = split_args->split_by;
  const size_t split_len = split_by.length();
  size_t pos = 0;
  List *result = f_.Make<List>(List::Type::kList);
  for (int64_t count = split_args->max_split; count; --count) {
    const size_t start_of_split = str.find(split_by, pos);
    if (start_of_split == std::string_view::npos) break;
    const std::string_view part = str.substr(pos, start_of_split - pos);
    // The string_view is from the original file, so it already has location
    result->Append(f_.arena(), f_.Make<StringScalar>(part, false, false));
    pos = start_of_split + split_len;
  }
  if (pos != std::string::npos) {
    result->Append(f_.arena(),
                   f_.Make<StringScalar>(str.substr(pos), false, false));
  }
  return result;
}

Node *StringMethodEval::RemovePrefix(Node *orig, StringScalar *object,
                                     List *args) {
  auto start_args = ExtractScalarPosArgs(args);
  if (!start_args.has_value() || start_args->size() != 1) return orig;
  const std::string_view prefix = start_args->at(0);

  std::string_view str = object->AsString();
  if (!str.starts_with(prefix)) return object;
  return f_.Make<StringScalar>(str.substr(prefix.length()),
                               object->is_triple_quoted(), object->is_raw());
}

Node *StringMethodEval::RemoveSuffix(Node *orig, StringScalar *object,
                                     List *args) {
  auto start_args = ExtractScalarPosArgs(args);
  if (!start_args.has_value() || start_args->size() != 1) return orig;
  const std::string_view suffix = start_args->at(0);

  std::string_view str = object->AsString();
  if (!str.ends_with(suffix)) return object;
  return f_.Make<StringScalar>(str.substr(0, str.length() - suffix.length()),
                               object->is_triple_quoted(), object->is_raw());
}

static std::string_view strip_view(std::string_view str,
                                   std::string_view strip_chars) {
  const size_t start = str.find_first_not_of(strip_chars);
  if (start == std::string_view::npos) return str.substr(str.length());
  const size_t end = str.find_last_not_of(strip_chars);
  return str.substr(start, end - start + 1);
}

Node *StringMethodEval::Strip(Node *orig, StringScalar *object, List *args) {
  std::string_view strip_chars = " \n\r\t";
  auto start_args = ExtractScalarPosArgs(args);
  if (start_args.has_value() && !start_args->empty()) {
    strip_chars = start_args->at(0);
  }
  const std::string_view str = object->AsString();
  const std::string_view result = strip_view(str, strip_chars);
  if (result.length() == str.length()) return object;
  return f_.Make<StringScalar>(result, object->is_triple_quoted(),
                               object->is_raw());
}

Node *StringMethodEval::Upper(Node *orig, StringScalar *object) {
  const std::string_view str = object->AsString();
  std::string result(str);
  std::transform(result.begin(), result.end(), result.begin(), ::toupper);
  if (result == str) return object;
  return f_.MakeNewStringScalarFrom(result, f_.project()->GetLocation(str));
}

Node *StringMethodEval::Lower(Node *orig, StringScalar *object) {
  const std::string_view str = object->AsString();
  std::string result(str);
  std::transform(result.begin(), result.end(), result.begin(), ::tolower);
  if (result == str) return object;
  return f_.MakeNewStringScalarFrom(result, f_.project()->GetLocation(str));
}

Node *StringMethodEval::MakePartitionTuple(std::string_view str,
                                           StringScalar *object, size_t offset,
                                           size_t sep_len, bool empty_front) {
  List *result = f_.Make<List>(List::Type::kTuple);
  if (offset == std::string_view::npos) {
    StringScalar *empty = f_.Make<StringScalar>(str.substr(0, 0), false, false);
    result->Append(f_.arena(), empty_front ? empty : object);
    result->Append(f_.arena(), empty);
    result->Append(f_.arena(), empty_front ? object : empty);
  } else {
    std::string_view first = str.substr(0, offset);
    std::string_view middle = str.substr(offset, sep_len);
    std::string_view last = str.substr(offset + sep_len);
    const bool triple = object->is_triple_quoted();
    const bool raw = object->is_raw();
    result->Append(f_.arena(), f_.Make<StringScalar>(first, triple, raw));
    result->Append(f_.arena(), f_.Make<StringScalar>(middle, triple, raw));
    result->Append(f_.arena(), f_.Make<StringScalar>(last, triple, raw));
  }
  return result;
}

Node *StringMethodEval::Partition(Node *orig, StringScalar *object,
                                  List *args) {
  auto p_args = ExtractScalarPosArgs(args);
  if (!p_args.has_value() || p_args->size() != 1) return orig;
  const std::string_view sep = p_args->at(0);
  const std::string_view str = object->AsString();
  return MakePartitionTuple(str, object, str.find(sep), sep.length(), false);
}

Node *StringMethodEval::RPartition(Node *orig, StringScalar *object,
                                   List *args) {
  auto p_args = ExtractScalarPosArgs(args);
  if (!p_args.has_value() || p_args->size() != 1) return orig;
  const std::string_view sep = p_args->at(0);
  const std::string_view str = object->AsString();
  return MakePartitionTuple(str, object, str.rfind(sep), sep.length(), true);
}

}  // namespace bant
