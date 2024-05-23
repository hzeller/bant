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

#include "bant/explore/header-providers.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/btree_set.h"
#include "absl/strings/str_cat.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/parsed-project.h"
#include "bant/session.h"
#include "bant/types-bazel.h"
#include "bant/types.h"
#include "bant/util/table-printer.h"

namespace bant {
namespace {
// Go through cc_library()s and call callback for each header file it exports.
using FindHeaderCallback =
  std::function<void(const BazelTarget &library, std::string_view hdr_loc,
                     const std::string &header_fqn)>;
static void IterateCCLibraryHeaders(const ParsedBuildFile &build_file,
                                    const FindHeaderCallback &callback) {
  query::FindTargets(
    build_file.ast, {"cc_library"}, [&](const query::Result &cc_lib) {
      auto cc_library = BazelTarget::ParseFrom(cc_lib.name, build_file.package);
      if (!cc_library.has_value()) return;

      auto hdrs = query::ExtractStringList(cc_lib.hdrs_list);
      const auto textual_hdrs = query::ExtractStringList(cc_lib.textual_hdrs);

      // HACK...
      // In absl/strings:string_view, there is the string_view.h exported.
      // But it is _also_ exported by absl/strings:strings but with the remark
      // that this is only there for backward compatibility. In fact, it is
      // mentioned twice, in hdrs and in textual_hdrs.
      // We use this fact below to skip headers mentiond in hdrs and _also_
      // mentioned in textual_hdrs to essentially disregard them.
      // This way, we get the desired behavior of bant suggesting to use
      // the :string_view library.
      // Narrow this hack to the very specific library.
      bool absl_string_view_skip = (build_file.package.path == "absl/strings");
      if (absl_string_view_skip) {
        absl_string_view_skip =
          std::find(hdrs.begin(), hdrs.end(), "string_view.h") != hdrs.end() &&
          std::find(textual_hdrs.begin(), textual_hdrs.end(),
                    "string_view.h") != textual_hdrs.end();
      }

      hdrs.insert(hdrs.end(), textual_hdrs.begin(), textual_hdrs.end());
      for (const std::string_view header : hdrs) {
        if (absl_string_view_skip && header == "string_view.h") continue;

        if (!cc_lib.include_prefix.empty()) {  // cc_library() dictates path.
          callback(*cc_library, header,
                   absl::StrCat(cc_lib.include_prefix, "/", header));
          continue;
        }

        // Assemble the header filename as it can be #include'ed in sources.
        const std::string header_fqn = build_file.package.QualifiedFile(header);

        // There can be an include prefix to be removed (typically: "").
        std::string_view strip_prefix = cc_lib.strip_include_prefix;
        // In protobuf, strip_include_prefix starts with '/' ???
        while (strip_prefix.starts_with('/')) strip_prefix.remove_prefix(1);
        while (strip_prefix.ends_with('/')) strip_prefix.remove_suffix(1);

        const size_t strip_len = strip_prefix.length();
        if (strip_len > 0 && header_fqn.length() > strip_len &&
            header_fqn.starts_with(strip_prefix) &&
            header_fqn[strip_len] == '/') {
          callback(*cc_library, header, header_fqn.substr(strip_len + 1));
        } else {
          callback(*cc_library, header, header_fqn);
        }

        // The same header could also show up with different prefixes, all of
        // them valid. e.g zlib.h and zlib/include/zlib.h. Emit all of these.

        // TODO: double check that the following is what incdirs is supposed to
        // do. Looks like it works for zlib.
        // Could also show up under shorter path with -I
        const auto incdirs = query::ExtractStringList(cc_lib.includes_list);
        for (const std::string_view dir : incdirs) {
          std::string prefix(dir);
          if (!prefix.ends_with('/')) {
            prefix.append("/");
          }
          if (header_fqn.starts_with(prefix)) {
            callback(*cc_library, header, header_fqn.substr(prefix.length()));
          }
        }
      }
    });
}

static void AppendCCLibraryHeaders(const ParsedBuildFile &build_file,
                                   std::ostream &info_out,
                                   ProvidedFromTargetSet &result) {
  IterateCCLibraryHeaders(
    build_file, [&](const BazelTarget &cc_library, std::string_view hdr_loc,
                    const std::string &header_fqn) {
      // Sometimes there can be multiple libraries exporting the same header.
      result[header_fqn].insert(cc_library);
    });
}

// proto_library(), cc_proto_library().
// Since we don't look into the *.bzl rule, we need to assemble the
// expected generated files here ourselves.
//
// The cc_proto_library() provides the cc_library(), but the header-file
// is derived from the *.proto file that in itself is given to proto_library().
// So we need to look at both.
// To find cc library for proto header foo.pb.h, we need two parts:
//  1. find all the cc_proto_library()s and see what proto_library() they use.
//  2. find all used proto_library()s that are mentioned in cc_proto_library()s,
//     derive the header file from the *.proto file and store the mapping
//     header->cc_library that we're after.
static void AppendProtoLibraryHeaders(const ParsedBuildFile &build_file,
                                      ProvidedFromTargetSet &result) {
  // TODO: once we wire the DependencyGraph through, we can make the look-up
  // in one go. Also we wouldn't be limited to proto_library() and
  // cc_proto_library() having to reside in one package.

  // Find all cc_proto_library()s and remember what proto_library() the dep on.
  OneToOne<BazelTarget, BazelTarget> proto_lib2cc_proto_lib;
  query::FindTargets(
    build_file.ast, {"cc_proto_library"}, [&](const query::Result &cc_plib) {
      auto target = BazelTarget::ParseFrom(cc_plib.name, build_file.package);
      if (!target.has_value()) return;
      const auto cc_proto_deps = query::ExtractStringList(cc_plib.deps_list);
      for (const std::string_view dep : cc_proto_deps) {
        auto proto_library = BazelTarget::ParseFrom(dep, build_file.package);
        if (!proto_library.has_value()) continue;
        proto_lib2cc_proto_lib.insert({*proto_library, *target});
      }
    });

  // Looking at the proto_library(), we can derive the header from the *.proto.
  // Putting it all together.
  query::FindTargets(
    build_file.ast, {"proto_library"}, [&](const query::Result &proto_lib) {
      auto target = BazelTarget::ParseFrom(proto_lib.name, build_file.package);
      if (!target.has_value()) return;

      // Is there a cc_proto_library() waiting for our info ?
      auto found_cc_proto_lib = proto_lib2cc_proto_lib.find(*target);
      if (found_cc_proto_lib == proto_lib2cc_proto_lib.end()) {
        return;  // This proto lib is probably used for some other language.
      }
      const BazelTarget &cc_proto_lib = found_cc_proto_lib->second;

      // Now, look through all *.proto files this proto_library() gets,
      // assemble the header filename from it and record in our result.
      const auto proto_srcs = query::ExtractStringList(proto_lib.srcs_list);
      for (std::string_view proto : proto_srcs) {
        if (!proto.ends_with(".proto")) {
          // possibly file list. Not handling that yet.
          continue;
        }
        if (proto.starts_with(':')) {  // Also a way to name a local
          proto.remove_prefix(1);
        }

        // Create a header file out of it. foo.proto becomes foo.pb.h or, in
        // some environments, foo.proto.h
        auto dot_pos = proto.find_last_of('.');
        const std::string_view stem = proto.substr(0, dot_pos);
        for (const std::string_view suffix : {".pb.h", ".proto.h"}) {
          std::string proto_header = absl::StrCat(stem, suffix);
          proto_header = build_file.package.QualifiedFile(proto_header);
          result[proto_header].insert(cc_proto_lib);
        }
      }
    });
}
}  // namespace

ProvidedFromTargetSet ExtractHeaderToLibMapping(const ParsedProject &project,
                                                std::ostream &info_out) {
  ProvidedFromTargetSet result;

  for (const auto &[_, build_file] : project.ParsedFiles()) {
    if (!build_file->ast) continue;

    // There are multiple rule types that behave like a cc library and
    // provide header files.
    AppendCCLibraryHeaders(*build_file, info_out, result);
    AppendProtoLibraryHeaders(*build_file, result);
  }

  return result;
}

ProvidedFromTarget ExtractGeneratedFromGenrule(const ParsedProject &project,
                                               std::ostream &info_out) {
  ProvidedFromTarget result;
  for (const auto &[_, file_content] : project.ParsedFiles()) {
    if (!file_content->ast) continue;
    query::FindTargets(
      file_content->ast, {"genrule"}, [&](const query::Result &params) {
        const auto genfiles = query::ExtractStringList(params.outs_list);

        auto target =
          BazelTarget::ParseFrom(params.name, file_content->package);
        if (!target.has_value()) return;

        for (const std::string_view generated : genfiles) {
          const auto gen_fqn = file_content->package.QualifiedFile(generated);
          const auto &inserted = result.insert({gen_fqn, *target});
          if (!inserted.second && target != inserted.first->second) {
            // TODO: differentiate between info-log (external projects) and
            // error-log (current project, as these are actionable).
            // For now: just report errors.
            const bool is_error = file_content->package.project.empty();
            if (is_error) {
              // TODO: Get file-position from other target which might be
              // in a different file.
              project.Loc(info_out, generated)
                << " '" << gen_fqn << "' in " << target->ToString()
                << " also created by " << inserted.first->second.ToString()
                << "\n";
            }
          }
        }
      });
  }
  return result;
}

void PrintProvidedSources(Session &session, const std::string &table_header,
                          const BazelPattern &pattern,
                          const ProvidedFromTarget &provided_from_lib) {
  auto printer = TablePrinter::Create(session.out(), session.output_format(),
                                      {table_header, "providing-rule"});
  for (const auto &[provided, lib] : provided_from_lib) {
    if (!pattern.Match(lib)) continue;
    printer->AddRow({provided, lib.ToString()});
  }
  printer->Finish();
}

void PrintProvidedSources(Session &session, const std::string &table_header,
                          const BazelPattern &pattern,
                          const ProvidedFromTargetSet &provided_from_lib) {
  auto printer = TablePrinter::Create(session.out(), session.output_format(),
                                      {table_header, "providing-rule"});
  for (const auto &[provided, libs] : provided_from_lib) {
    std::vector<std::string> list;
    for (const BazelTarget &target : libs) {
      if (pattern.Match(target)) list.push_back(target.ToString());
    }
    printer->AddRowWithRepeatedLastColumn({provided}, list);
  }
  printer->Finish();
}
}  // namespace bant
