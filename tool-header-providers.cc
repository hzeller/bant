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

// Given a BUILD, BUILD.bazel filename, return the bare project path with
// no prefix or suffix.
// ./foo/bar/baz/BUILD.bazel turns into foo/bar/baz
static std::string_view TargetPathFromBuileFile(std::string_view file) {
  file = file.substr(0, file.find_last_of('/'));  // Remove BUILD-file
  while (!file.empty() && (file[0] == '.' || file[0] == '/')) {
    file.remove_prefix(1);
  }
  return file;
}

static void FindCCLibraryHeaders(Node *ast, const FindCallback &cb) {
  LibraryHeaderFinder(cb).WalkNonNull(ast);
}
}  // namespace

using HeaderToTargetMap = std::map<std::string, std::string>;
HeaderToTargetMap ExtractHeaderToLibMapping(const ParsedProject &project) {
  HeaderToTargetMap result;
  for (const auto &[filename, parse_result] : project.file_to_ast) {
    const std::string_view target_basename(TargetPathFromBuileFile(filename));
    if (!parse_result.ast) continue;
    FindCCLibraryHeaders(parse_result.ast,
                         [target_basename, &result](std::string_view name,
                                                    std::string_view header) {
                           std::string header_fqn(target_basename);
                           std::string lib_fqn(target_basename);
                           if (!target_basename.empty()) header_fqn.append("/");
                           header_fqn.append(header);
                           lib_fqn.append(":").append(name);
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
    fprintf(out, "%*s\t%s\n", -longest - 1, header.c_str(), lib.c_str());
  }
}
}  // namespace bant
