// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include "tool-header-providers.h"

#include <functional>
#include <string_view>

#include "ast.h"
#include "project-parser.h"

namespace bant {
namespace {
// TODO: these of course need to be configurable. Ideally with a simple
// path query language.
using FindCallback =
  std::function<void(std::string_view library_name, std::string_view header)>;
class LibraryHeaderFinder : public BaseVisitor {
 public:
  explicit LibraryHeaderFinder(const FindCallback &cb) : found_cb_(cb) {}

  void VisitFunCall(FunCall *f) final {
    current_.Reset(f->identifier()->id() == "cc_library");
    if (!current_.is_relevant) return;  // Nothing interesting beyond here.
    for (Node *element : *f->argument()) {
      WalkNonNull(element);
    }
    if (!current_.name.empty() && current_.header_list) {
      for (Node *header : *current_.header_list) {
        Scalar *scalar = header->CastAsScalar();
        if (!scalar) continue;
        std::string_view header_file = scalar->AsString();
        if (header_file.empty()) continue;
        found_cb_(current_.name, header_file);
      }
    }
  }

  void VisitAssignment(Assignment *a) {
    if (!current_.is_relevant) return;  // can prune walk here
    if (!a->identifier() || !a->value()) return;
    const std::string_view lhs = a->identifier()->id();
    if (Scalar *scalar = a->value()->CastAsScalar(); scalar && lhs == "name") {
      current_.name = scalar->AsString();
    } else if (List *list = a->value()->CastAsList(); list && lhs == "hdrs") {
      current_.header_list = list;
    }
  }

 private:
  struct CollectResult {
    void Reset(bool relevant) {
      is_relevant = relevant;
      name = "";
      header_list = nullptr;
    }

    bool is_relevant = false;
    std::string_view name;
    List *header_list;
  };

  // TODO: this assumes library call being a toplevel function; might need
  // stack here for more sophisticated searches.
  CollectResult current_;

  const FindCallback &found_cb_;
};

static void FindCCLibraryHeaders(Node *ast, const FindCallback &cb) {
  LibraryHeaderFinder(cb).WalkNonNull(ast);
}
}  // namespace

using HeaderToLibMap = std::map<std::string, std::string>;
HeaderToLibMap ExtractHeaderToLibMapping(const ParsedProject &project) {
  HeaderToLibMap result;
  for (const auto &[filename, file_content] : project.file_to_ast) {
    if (!file_content.ast) continue;
    FindCCLibraryHeaders(file_content.ast, [&result, &file_content](
                                             std::string_view lib_name,
                                             std::string_view header) {
      std::string header_fqn = file_content.rel_path;
      if (!header_fqn.empty()) header_fqn.append("/");
      header_fqn.append(header);
      std::string lib_fqn = file_content.project + file_content.rel_path;
      // If the library name is the same as the directory name, this is just
      // the default name.
      if (!file_content.rel_path.ends_with(std::string("/").append(lib_name))) {
        lib_fqn.append(":").append(lib_name);
      }
      result[header_fqn] = lib_fqn;
    });
  }
  return result;
}

void PrintLibraryHeaders(FILE *out, const ParsedProject &project) {
  const auto header_to_lib = ExtractHeaderToLibMapping(project);
  int longest = 0;
  for (const auto &[header, _] : header_to_lib) {
    longest = std::max(longest, (int)header.length());
  }
  for (const auto &[header, lib] : header_to_lib) {
    fprintf(out, "%*s\t%s\n", -longest, header.c_str(), lib.c_str());
  }
}
}  // namespace bant
