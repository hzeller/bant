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
#include <initializer_list>
#include <iterator>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/btree_set.h"
#include "absl/strings/str_cat.h"
#include "bant/explore/aliased-by.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/parsed-project.h"
#include "bant/session.h"
#include "bant/types-bazel.h"
#include "bant/types.h"
#include "bant/util/table-printer.h"

// The header providers maps header filenames to all the libraries that
// provide these, including all the alises pointing to these libraries.
//
// Typically, this should only be exactly one library per header, but
// there are some projects out there that have multiple library targets
// declare to provide the same headers (e.g. due to different visibility
// settings). This is why this mapping is 1:n.
//
// One would expect that we mostly just need to look at cc_library(), but there
// are other targets that implicitly provide headers. We can't look
// at all the rules that bazel implements as we never attempt to understand what
// is going on in *.bzl files as this is solidly outside the scope of bant.
//
// So there are some special handlings of common targets where headers can
// emerge that we support here directly.
//
//  - cc_library(): the typical target that provides header files.
//  - proto_library() and cc_proto_library(). The former gets a name of the
//    proto buffer file, and the latter that depends on it and makes a
//    cc-library out of it.
//    We need to look at both, as we only can derive the name of the header
//    file from the proto buffer file, but need to get the user-chosen name
//    of the libary from cc_proto_iibrary().
//  - grpc_cc_library() : project specific hack for complicated rules in the
//    grpc project. This is like a cc_library(), but also defines
//    headers in a public_hdrs = [] kwarg
//  - cc_grpc_library() : This is the proto library version of grpc.
//    (with a confusing name). It creates another proto header, based on
//    the original name of the *.proto file.
namespace bant {
namespace {

static std::string_view LightCanonicalizePath(std::string_view path) {
  while (path.starts_with("./")) {
    path.remove_prefix(2);
  }
  while (path.starts_with("/")) {
    path.remove_prefix(1);
  }
  return path;
}

// Convert to format needed for suffix matching.
static std::string KeyTransform(std::string_view in, bool suffix_index) {
  return suffix_index ? std::string{in.rbegin(), in.rend()}.append("/")
                      : std::string{in};
}

// Go through cc_library()s and call callback for each header file it exports.
using FindHeaderCallback =
  std::function<void(const BazelTarget &library, std::string_view hdr_loc,
                     const std::string &header_fqn)>;
static void IterateCCLibraryHeaders(const ParsedBuildFile &build_file,
                                    const FindHeaderCallback &callback) {
  // Unfortunately, grpc does not simply have a cc_library(), but its own
  // rule or macro, making it invisible if we just look at cc_library.
  // Hacking it up here to look also for the grpc version.
  static const std::initializer_list<std::string_view> kInterestingLibRules{
    "cc_library",
    "grpc_cc_library",
  };

  query::FindTargets(
    build_file.ast, kInterestingLibRules, [&](const query::Result &cc_lib) {
      auto cc_library = build_file.package.QualifiedTarget(cc_lib.name);
      if (!cc_library.has_value()) return;

      auto hdrs = query::ExtractStringList(cc_lib.hdrs_list);
      const auto textual_hdrs = query::ExtractStringList(cc_lib.textual_hdrs);

      // ABSL HACK...
      // In absl/strings:string_view, there is the string_view.h exported.
      // But it is _also_ exported by absl/strings:strings but with the remark
      // that this is only there for backward compatibility. In fact, it is
      // mentioned twice, in hdrs and in textual_hdrs.
      // We use this fact below to skip headers mentiond in hdrs and _also_
      // mentioned in textual_hdrs to essentially disregard them.
      // This way, we get the desired behavior of bant suggesting to use
      // the :string_view library.
      // Narrow this hack to the very specific library.
      bool absl_string_view_skip =
        (build_file.package.path.ends_with("absl/strings"));
      if (absl_string_view_skip) {
        absl_string_view_skip =
          std::find(hdrs.begin(), hdrs.end(), "string_view.h") != hdrs.end() &&
          std::find(textual_hdrs.begin(), textual_hdrs.end(),
                    "string_view.h") != textual_hdrs.end();
      }

      hdrs.insert(hdrs.end(), textual_hdrs.begin(), textual_hdrs.end());
      query::AppendStringList(cc_lib.public_hdrs, hdrs);  // grpc hack.
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

static void AppendCCLibraryHeaders(
  const ParsedBuildFile &build_file,
  const OneToN<BazelTarget, BazelTarget> &alias_index, std::ostream &info_out,
  bool suffix_index, ProvidedFromTargetSet &result) {
  IterateCCLibraryHeaders(
    build_file, [&](const BazelTarget &cc_library, std::string_view hdr_loc,
                    const std::string &header_fqn) {
      const std::string_view canonicalized = LightCanonicalizePath(header_fqn);
      const std::string key = KeyTransform(canonicalized, suffix_index);
      result[key].insert(cc_library);
      // Also, if there are any aliases in the project for this library,
      // these would also be considered providers of this lib.
      if (const auto aliases_found = alias_index.find(cc_library);
          aliases_found != alias_index.end()) {
        result[key].insert(aliases_found->second.begin(),
                           aliases_found->second.end());
      }
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
                                      bool reverse,
                                      ProvidedFromTargetSet &result) {
  // TODO: once we wire the DependencyGraph through, we can make the look-up
  // in one go. Also we wouldn't be limited to proto_library() and
  // cc_proto_library() having to reside in one package.

  static const std::initializer_list<std::string_view> kInterestingLibRules{
    "cc_proto_library",
    "cc_grpc_library",
  };

  // Find all cc_proto_library()s and remember what proto_library() the dep on.
  // We have the simplified assumption that this is a well-written BUILD and
  // this is only ever a 1:1 relationship.
  // We have two of these: one regular (index:false), one for grpc(index:true)
  OneToOne<BazelTarget, BazelTarget> proto_lib2cc_proto_lib[2];
  query::FindTargets(
    build_file.ast, kInterestingLibRules, [&](const query::Result &cc_plib) {
      auto target = build_file.package.QualifiedTarget(cc_plib.name);
      if (!target.has_value()) return;

      const bool is_grpc = (cc_plib.rule == "cc_grpc_library");

      // cc_proto_library has deps in deps, cc_grpc_library in srcs.
      auto cc_proto_deps = is_grpc
                             ? query::ExtractStringList(cc_plib.srcs_list)
                             : query::ExtractStringList(cc_plib.deps_list);

      for (const std::string_view dep : cc_proto_deps) {
        auto proto_library = BazelTarget::ParseFrom(dep, build_file.package);
        if (!proto_library.has_value()) continue;
        proto_lib2cc_proto_lib[is_grpc].insert({*proto_library, *target});
      }
    });

  // We now know libraries that can be linked, but we don't know the
  // name of the headers yet. They are derived from the *.proto filename,
  // which are only known to proto_library()s.
  // Looking at the proto_library(), we can derive the header from the *.proto.
  // Putting it all together.
  query::FindTargets(
    build_file.ast, {"proto_library"}, [&](const query::Result &proto_lib) {
      auto target = build_file.package.QualifiedTarget(proto_lib.name);
      if (!target.has_value()) return;

      for (const bool is_grpc : {false, true}) {
        const auto &lookup_lib = proto_lib2cc_proto_lib[is_grpc];
        // Is there a cc_{proto,grpc}_library() waiting for our info ?
        auto found_cc_proto_lib = lookup_lib.find(*target);
        if (found_cc_proto_lib == lookup_lib.end()) {
          continue;
        }

        const BazelTarget &cc_proto_lib = found_cc_proto_lib->second;

        // proto buffer headers for grpc have .grpc.pb.h suffix.
        const std::string_view middle_name = is_grpc ? ".grpc" : "";

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
            std::string proto_header = absl::StrCat(stem, middle_name, suffix);
            proto_header = build_file.package.QualifiedFile(proto_header);
            result[KeyTransform(proto_header, reverse)].insert(cc_proto_lib);
          }
        }
      }
    });
}
}  // namespace

ProvidedFromTargetSet ExtractHeaderToLibMapping(const ParsedProject &project,
                                                std::ostream &info_out,
                                                bool suffix_index) {
  ProvidedFromTargetSet result;

  const auto aliased_by_index = ExtractAliasedBy(project);

  for (const auto &[_, build_file] : project.ParsedFiles()) {
    if (!build_file->ast) continue;

    // There are multiple rule types that behave like a cc library and
    // provide header files.
    AppendCCLibraryHeaders(*build_file, aliased_by_index,  //
                           info_out, suffix_index, result);
    AppendProtoLibraryHeaders(*build_file, suffix_index, result);
  }

  return result;
}

ProvidedFromTarget ExtractGeneratedFromGenrule(const ParsedProject &project,
                                               std::ostream &info_out,
                                               bool suffix_index) {
  ProvidedFromTarget result;
  for (const auto &[_, file_content] : project.ParsedFiles()) {
    if (!file_content->ast) continue;
    query::FindTargets(
      file_content->ast, {"genrule"}, [&](const query::Result &params) {
        const auto genfiles = query::ExtractStringList(params.outs_list);

        auto target = file_content->package.QualifiedTarget(params.name);
        if (!target.has_value()) return;

        for (const std::string_view generated : genfiles) {
          const auto gen_fqn = file_content->package.QualifiedFile(generated);
          const auto &inserted =
            result.insert({KeyTransform(gen_fqn, suffix_index), *target});
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

static std::string_view CommonPrefix(std::string_view a, std::string_view b) {
  if (b.length() < a.length()) {
    std::swap(a, b);
  }
  for (size_t i = 0; i < a.length(); ++i) {
    if (a[i] != b[i]) return a.substr(0, i);
  }
  return a;
}

static size_t CommonSlashes(std::string_view a, std::string_view b) {
  const std::string_view common = CommonPrefix(a, b);
  return std::count_if(common.begin(), common.end(),
                       [](char c) { return c == '/'; });
}

std::optional<FindResult> FindBySuffix(const ProvidedFromTargetSet &index,
                                       std::string_view key,
                                       size_t min_fuzzy_paths) {
  if (index.empty()) return std::nullopt;
  const std::string search_key = KeyTransform(key, true);
  const ProvidedFromTargetSet::const_iterator found =
    index.lower_bound(search_key);
  if (found != index.end() && found->first == search_key) {
    return FindResult{.match = std::string(key),  // Exact match.
                      .target_set = &found->second,
                      .fuzzy_score = 0};
  }

  const bool was_at_end = (found == index.end());
  ProvidedFromTargetSet::const_iterator best = found;
  if (was_at_end) {  // Need to look one before.
    --best;
  }

  size_t best_common_path_elements = CommonSlashes(search_key, best->first);

  // A longer match might be hiding before our found position.
  if (!was_at_end &&            // Unless we already looked one before.
      best != index.begin()) {  // Can't go before that
    auto before = std::prev(best);
    const size_t common_path_elements =
      CommonSlashes(search_key, before->first);
    if (common_path_elements > best_common_path_elements) {
      best_common_path_elements = common_path_elements;
      best = before;
    }
  }
  if (best_common_path_elements < min_fuzzy_paths) {
    return std::nullopt;
  }

  return FindResult{
    .match = std::string(
      {best->first.rbegin() + 1 /*skip-slash*/, best->first.rend()}),
    .target_set = &best->second,
    .fuzzy_score = static_cast<int>(best_common_path_elements),
  };
}

void PrintProvidedSources(Session &session, const std::string &table_header,
                          const BazelPattern &pattern,
                          const ProvidedFromTarget &provided_from_lib) {
  auto printer =
    TablePrinter::Create(session.out(), session.flags().output_format,
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
  auto printer =
    TablePrinter::Create(session.out(), session.flags().output_format,
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
