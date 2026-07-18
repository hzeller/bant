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

#include "bant/explore/project-indexing.h"

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
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "bant/explore/aliased-by.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/parsed-project.h"
#include "bant/session.h"
#include "bant/types-bazel.h"
#include "bant/types.h"
#include "bant/util/filesystem.h"
#include "bant/util/grep-highlighter.h"
#include "bant/util/table-printer.h"
#include "bant/workspace.h"

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
//  - cc_grpc_library() : This is the proto library version of grpc.
//    (with a confusing name). It creates another proto header, based on
//    the original name of the *.proto file.
namespace bant {
namespace {

// TODO: is this something we should WeaklyCanonicalizePath() use for ?
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

// special 'abs path' used in the proto buffer rules: they don't have tests
// yet, so don't deal with absl paths yet. TODO: implement.
constexpr std::string_view TODO_IMPL_FOR_PROTO = "(implproto)";

// Strip a "file_path" if it starts with the optional "strip_prefix", otherwise
// return as-is.
static std::string_view StripIfNeeded(std::string_view rel_path,
                                      std::string_view abs_path,
                                      std::string_view strip_prefix) {
  if (strip_prefix.empty()) return rel_path;

  // If the strip prefix starts with slash, we start stripping absolute.
  // Currently special handling if there is an abs path (proto needs to be
  // tested separately)
  const auto file_path =
    (strip_prefix[0] == '/' && abs_path != TODO_IMPL_FOR_PROTO) ? abs_path
                                                                : rel_path;

  while (strip_prefix.starts_with('/')) strip_prefix.remove_prefix(1);
  while (strip_prefix.ends_with('/')) strip_prefix.remove_suffix(1);

  const size_t strip_len = strip_prefix.length();
  if (strip_len > 0 && file_path.length() > strip_len &&
      file_path.starts_with(strip_prefix) && file_path[strip_len] == '/') {
    return file_path.substr(strip_len + 1);
  }
  return file_path;
}

// Indexes needed for lookups during construction of other indexes.
struct HelperIndex {
  OneToN<BazelTarget, BazelTarget> alias_index;
  TargetProvidedFiles filegroups;
};

// Go through cc_library()s and call callback for each header file it exports.
using FindHeaderCallback =
  std::function<void(const BazelTarget &library,      // Lbrary defining
                     std::string_view lib_hdrs_name,  // name in hdrs = []
                     std::string_view reachable_name  // reachable name
                     )>;
static void IterateCCLibraryHeaders(const ParsedBuildFile &build_file,
                                    const TargetProvidedFiles &filegroups,
                                    bool include_sources,
                                    const FindHeaderCallback &callback) {
  static const std::initializer_list<std::string_view> kInterestingLibRules{
    "cc_library",
  };

  const BazelPackage &package = build_file.package;
  query::FindTargets(
    build_file.ast, kInterestingLibRules, [&](const query::Result &cc_lib) {
      auto cc_library = package.QualifiedTarget(cc_lib.name);
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
      bool absl_string_view_skip = (package.path.ends_with("absl/strings"));
      if (absl_string_view_skip) {
        absl_string_view_skip =
          std::find(hdrs.begin(), hdrs.end(), "string_view.h") != hdrs.end() &&
          std::find(textual_hdrs.begin(), textual_hdrs.end(),
                    "string_view.h") != textual_hdrs.end();
      }

      hdrs.insert(hdrs.end(), textual_hdrs.begin(), textual_hdrs.end());

      if (include_sources) {  // for hdrs-canonical we want a global view
        query::AppendStringList(cc_lib.srcs_list, hdrs);
      }

      // If there are references to filegroups, exand these to files first.
      int max_rounds = 2;
      while (ExpandFilegroupsInList(package, filegroups, &hdrs) &&
             --max_rounds > 0) {
      }
      const auto incdirs = query::ExtractStringList(cc_lib.includes_list);
      for (const std::string_view header : hdrs) {
        if (absl_string_view_skip && header == "string_view.h") continue;

        const auto strip_include_prefix = cc_lib.strip_include_prefix;
        const bool is_abs_inc_prefix = strip_include_prefix.starts_with('/');
        const std::string header_abs = package.QualifiedFile(header);
        const std::string_view stripped =
          StripIfNeeded(header, header_abs, strip_include_prefix);
        if (!cc_lib.include_prefix.empty()) {  // cc_library() dictates path.
          callback(*cc_library, header,
                   absl::StrCat(cc_lib.include_prefix, "/", stripped));
          continue;
        }

        // Assemble the header filename as it can be #include'ed in sources.
        const std::string header_fqn = package.QualifiedFile(stripped);
        if (!is_abs_inc_prefix) {
          callback(*cc_library, header, header_fqn);
        } else {
          callback(*cc_library, header, stripped);
        }

        // The same header could also show up with different prefixes, all of
        // them valid. e.g zlib.h and zlib/include/zlib.h. Emit all of these.

        // TODO: double check that the following is what incdirs is supposed to
        // do. Looks like it works for zlib.
        // Could also show up under shorter path with -I
        for (const std::string_view dir : incdirs) {
          const std::string_view incdir = (dir == ".") ? "" : dir;
          std::string prefix(build_file.package.QualifiedFile(incdir));
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

// Innsert (key, target_value) into result, but also insert all alises
// for target_value.
static void InsertLibAndAliasesToTargetSet(
  const std::string &key, const BazelTarget &target_value,
  const OneToN<BazelTarget, BazelTarget> &alias_index,
  ProvidedFromTargetSet &result) {
  result[key].insert(target_value);
  // Also, if there are any aliases in the project for this library,
  // these would also be considered providers of this lib.
  if (const auto aliases_found = alias_index.find(target_value);
      aliases_found != alias_index.end()) {
    result[key].insert(aliases_found->second.begin(),
                       aliases_found->second.end());
  }
}

static void AppendCCLibraryHeaders(const ParsedBuildFile &build_file,
                                   const HelperIndex &idx,
                                   std::ostream &info_out, bool suffix_index,
                                   ProvidedFromTargetSet &result) {
  IterateCCLibraryHeaders(
    build_file, idx.filegroups, false,
    [&](const BazelTarget &cc_library, std::string_view lib_hdrs_name,
        std::string_view reachable_name) {
      const auto canonicalized = LightCanonicalizePath(reachable_name);
      const std::string key = KeyTransform(canonicalized, suffix_index);
      InsertLibAndAliasesToTargetSet(key, cc_library, idx.alias_index, result);
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
                                      const HelperIndex &idx, bool reverse,
                                      ProvidedFromTargetSet &result) {
  const BazelPackage &package = build_file.package;

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
      auto target = package.QualifiedTarget(cc_plib.name);
      if (!target.has_value()) return;

      const bool is_grpc = (cc_plib.rule == "cc_grpc_library");

      // cc_proto_library has deps in deps, cc_grpc_library in srcs.
      auto cc_proto_deps = is_grpc
                             ? query::ExtractStringList(cc_plib.srcs_list)
                             : query::ExtractStringList(cc_plib.deps_list);

      for (const std::string_view dep : cc_proto_deps) {
        auto proto_library = BazelTarget::ParseFrom(dep, package);
        if (!proto_library.has_value()) continue;
        proto_lib2cc_proto_lib[is_grpc].insert({*proto_library, *target});
      }
    });

  // We now know proto cc libraries that can be linked, but we don't know the
  // name of the headers yet. They are derived from the *.proto filename,
  // which are only known to proto_library()s.
  // Looking at the proto_library(), we can derive the header from the *.proto.
  // Putting it all together.
  query::FindTargets(
    build_file.ast, {"proto_library"}, [&](const query::Result &proto_lib) {
      auto target = package.QualifiedTarget(proto_lib.name);
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
        auto proto_srcs = query::ExtractStringList(proto_lib.srcs_list);
        if (proto_srcs.empty()) {
          // Special case: a proto library that has no sources, just acts
          // as a filegroup for all the sources in deps.
          query::AppendStringList(proto_lib.deps_list, proto_srcs);
        }
        int max_rounds = 2;
        while (ExpandFilegroupsInList(package, idx.filegroups, &proto_srcs) &&
               --max_rounds > 0) {
        }

        for (std::string_view proto : proto_srcs) {
          auto dot_pos = proto.find_last_of('.');
          if (dot_pos == std::string_view::npos) continue;

          const std::string_view stem = proto.substr(0, dot_pos);
          const std::string_view suffix = proto.substr(dot_pos + 1);

          if (!absl::StrContains(suffix, "proto")) {
            // possibly file list. Not handling that yet.
            continue;
          }
          if (proto.starts_with(':')) {  // Also a way to name a local
            proto.remove_prefix(1);
          }

          // Create a header file out of it. foo.proto becomes foo.pb.h or, in
          // some environments, foo.proto.h
          for (const std::string_view suffix : {".pb.h", ".proto.h"}) {
            std::string proto_header = absl::StrCat(stem, middle_name, suffix);
            proto_header = package.QualifiedFile(proto_header);
            // What is strip_include_prefix is called strip_import_prefix
            // for proto_library().
            const std::string_view maybe_stripped = StripIfNeeded(
              proto_header, TODO_IMPL_FOR_PROTO, proto_lib.strip_import_prefix);
            const std::string key = KeyTransform(maybe_stripped, reverse);
            InsertLibAndAliasesToTargetSet(key, cc_proto_lib, idx.alias_index,
                                           result);
          }
        }
      }
    });
}
}  // namespace

ProvidedFromTargetSet ExtractExpandedHeaderToLibMapping(
  const ParsedProject &project, std::ostream &info_out, bool suffix_index) {
  ProvidedFromTargetSet result;

  const HelperIndex idx{
    .alias_index = ExtractAliasedBy(project),
    .filegroups = ExtractFilegroupTargets(project),
  };

  for (const auto &[_, build_file] : project.ParsedFiles()) {
    if (!build_file->ast) continue;

    // There are multiple rule types that behave like a cc library and
    // provide header files.
    AppendCCLibraryHeaders(*build_file, idx,  //
                           info_out, suffix_index, result);
    AppendProtoLibraryHeaders(*build_file, idx,  //
                              suffix_index, result);
  }

  return result;
}

HeaderToCanonicalHeader CanonicalHeaderMapping(const ParsedProject &project,
                                               std::ostream &info_out) {
  auto filegroups = ExtractFilegroupTargets(project);
  HeaderToCanonicalHeader result;
  for (const auto &[_, build_file] : project.ParsedFiles()) {
    if (!build_file->ast) continue;

    const BazelPackage &package = build_file->package;
    IterateCCLibraryHeaders(
      *build_file, filegroups, true,
      [&](const BazelTarget &cc_library, std::string_view lib_hdrs_name,
          std::string_view reachable_name) {
        const auto canonicalized = LightCanonicalizePath(reachable_name);
        const std::string header_fqn = package.QualifiedFile(lib_hdrs_name);
        result[canonicalized].insert(header_fqn);
      });
  }

  return result;
}

ProvidedFromTargetSet ExtractProtoToProtoLibMapping(
  const ParsedProject &project, std::ostream &info_out, bool suffix_index) {
  ProvidedFromTargetSet result;

  const auto aliased_by_index = ExtractAliasedBy(project);

  for (const auto &[_, build_file] : project.ParsedFiles()) {
    if (!build_file->ast) continue;

    query::FindTargets(
      build_file->ast, {"proto_library"}, [&](const query::Result &proto_lib) {
        auto target = build_file->package.QualifiedTarget(proto_lib.name);
        if (!target.has_value()) return;

        const auto proto_srcs = query::ExtractStringList(proto_lib.srcs_list);
        for (std::string_view proto : proto_srcs) {
          if (!proto.ends_with(".proto")) continue;
          if (proto.starts_with(':')) proto.remove_prefix(1);

          const std::string proto_fqn =
            build_file->package.QualifiedFile(proto);
          const std::string_view maybe_stripped = StripIfNeeded(
            proto_fqn, TODO_IMPL_FOR_PROTO, proto_lib.strip_import_prefix);
          const std::string key = KeyTransform(maybe_stripped, suffix_index);
          InsertLibAndAliasesToTargetSet(key, *target, aliased_by_index,
                                         result);
        }
      });
  }

  return result;
}

ProvidedFromTargetSet ExtractComponentToTargetMapping(
  const ParsedProject &project, ExtractComponent which,
  bool only_physical_files, std::ostream &info_out) {
  const auto filegroups = ExtractFilegroupTargets(project);
  auto &fs = Filesystem::instance();
  ProvidedFromTargetSet result;

  for (const auto &[_, build_file] : project.ParsedFiles()) {
    if (!build_file->ast) continue;

    query::FindTargets(
      build_file->ast, {"cc_library", "cc_binary"},
      [&](const query::Result &cc_lib) {
        const BazelPackage &package = build_file->package;
        auto cc_library = package.QualifiedTarget(cc_lib.name);
        if (!cc_library.has_value()) return;

        List *search_list = nullptr;
        switch (which) {
        case ExtractComponent::kHdrs: search_list = cc_lib.hdrs_list; break;
        case ExtractComponent::kSrcs: search_list = cc_lib.srcs_list; break;
        case ExtractComponent::kData: search_list = cc_lib.data_list; break;
        }
        auto srcs = query::ExtractStringList(search_list);
        int max_rounds = 2;
        while (ExpandFilegroupsInList(package, filegroups, &srcs) &&
               --max_rounds > 0) {
        }
        for (const std::string_view src : srcs) {
          // TODO: ParsedProject::GetPackageFor() as we might have mixed
          // packages due to filegroups.
          const std::string src_fqn = package.QualifiedFile(src);
          if (!only_physical_files || fs.Exists(src_fqn)) {
            result[src_fqn].emplace(*cc_library);
          }
        }
      });
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

PackageGroups ExtractPackageGroups(const ParsedProject &project) {
  PackageGroups result;
  for (const auto &[_, file_content] : project.ParsedFiles()) {
    if (!file_content->ast) continue;
    query::FindTargets(
      file_content->ast, {"package_group"}, [&](const query::Result &params) {
        auto group_name = file_content->package.QualifiedTarget(params.name);
        if (!group_name.has_value()) return;

        PackageGroup group;
        group.packages = query::ExtractStringList(params.packages);

        // Besides the direct patterns, we can have references to other groups
        // in includes = [].
        for (auto group_ref : query::ExtractStringList(params.includes_list)) {
          auto inc_group = file_content->package.QualifiedTarget(group_ref);
          if (!inc_group.has_value()) continue;
          group.includes.emplace_back(*inc_group);
        }
        result[*group_name] = group;
      });
  }
  return result;
}

// Follow packages and their includes until we have a comprehensive list
// of includede patterns.
static void FollowPackageGroupPatternsTree(
  const PackageGroups &all_groups, const BazelTarget &package_group,
  absl::btree_set<BazelTarget> *seen_already,
  std::vector<std::string_view> *append_to) {
  if (!seen_already->insert(package_group).second) return;
  const auto found = all_groups.find(package_group);
  if (found == all_groups.end()) return;
  const PackageGroup &group = found->second;
  append_to->insert(append_to->end(), group.packages.begin(),
                    group.packages.end());
  for (const BazelTarget &inc_group : group.includes) {
    // TODO: if we leave the package, do the patterns be resolved or are thsee
    // always matched globally ?
    FollowPackageGroupPatternsTree(all_groups, inc_group, seen_already,
                                   append_to);
  }
}

// Public interface of FollowPackageGroupPatternsTree()
std::vector<std::string_view> ResolvePackageGroupPatterns(
  const PackageGroups &all_groups, const BazelTarget &package_group) {
  absl::btree_set<BazelTarget> seen_already;
  std::vector<std::string_view> result;
  FollowPackageGroupPatternsTree(all_groups, package_group, &seen_already,
                                 &result);
  return result;
}

TargetProvidedFiles ExtractFilegroupTargets(const ParsedProject &project) {
  TargetProvidedFiles result;
  for (const auto &[_, file_content] : project.ParsedFiles()) {
    if (!file_content->ast) continue;
    query::FindTargets(
      file_content->ast, {"genrule", "filegroup", "proto_library"},
      [&](const query::Result &params) {
        auto target = file_content->package.QualifiedTarget(params.name);
        if (!target.has_value()) return;

        std::vector<std::string_view> file_list;
        // filegroups and proto_library have sources.
        if (params.rule == "filegroup" || params.rule == "proto_library") {
          query::AppendStringList(params.srcs_list, file_list);
        }

        // Genrule outputs are used in other context as if
        // they were a filegroup.
        if (params.rule == "genrule") {
          query::AppendStringList(params.outs_list, file_list);
        }

        // proto library deps just point to other proto files,
        // but otherwise has no srcs, behaves like a filegroup
        if (params.rule == "proto_library" &&
            (!params.srcs_list || params.srcs_list->empty())) {
          query::AppendStringList(params.deps_list, file_list);
        }

        auto &file_collect = result[*target];
        for (const std::string_view file : file_list) {
          // Note, we don't do fully qualification here, so that we preserve
          // the original string_view that points to the original BUILD file.
          file_collect.insert(file);
        }
      });
  }
  return result;
}

bool ExpandFilegroupsInList(const BazelPackage &context_package,
                            const TargetProvidedFiles &filegropus,
                            std::vector<std::string_view> *list) {
  // TODO: currently users of this function recursively expand filegroups if
  // the result contained filegroups. Put this loop in here.
  // TODO: we only copy the the string-views from the filegroup (thus preserving
  // the SourceLocator-able property), so it can be wrong if interpreted in
  // context_package. Maybe we need a fully qualified version just knowing
  // the project ? (that already has a mapping string-view->build-file(=package)
  bool any_expansion = false;
  absl::btree_set<std::string_view> collected_files;
  for (const std::string_view element : *list) {
    bool expanded_filegroup = false;
    // Let's test if this can be resolved as a filegroup-interpretable target
    const auto potential_target = context_package.QualifiedTarget(element);
    if (potential_target.has_value()) {
      if (const auto found = filegropus.find(*potential_target);
          found != filegropus.end()) {
        for (const std::string_view filename : found->second) {
          collected_files.insert(filename);
        }
        expanded_filegroup = true;
        any_expansion = true;
      }
    }

    if (!expanded_filegroup) {
      // Otherwise, just keep the file as-is
      collected_files.insert(element);
    }
  }

  if (any_expansion) {
    list->clear();
    list->insert(list->end(), collected_files.begin(), collected_files.end());
  }
  return any_expansion;
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
                          const BazelTargetMatcher &filter,
                          const ProvidedFromTarget &provided_from_lib) {
  auto highlighter = CreateGrepHighlighterFromFlags(session);
  if (!highlighter) return;
  auto printer =
    TablePrinter::Create(session.out(), session.flags().output_format,
                         *highlighter, {table_header, "providing-rule"});
  for (const auto &[provided, lib] : provided_from_lib) {
    if (!filter.Match(lib)) continue;
    printer->AddRow({provided, lib.ToString()});
  }
  printer->Finish();
}

void PrintProvidedSources(Session &session, const std::string &table_header,
                          const BazelTargetMatcher &filter,
                          const ProvidedFromTargetSet &provided_from_lib) {
  const auto dup_handling = session.flags().duplicate_handling;
  auto highlighter = CreateGrepHighlighterFromFlags(session);
  if (!highlighter) return;
  auto printer =
    TablePrinter::Create(session.out(), session.flags().output_format,
                         *highlighter, {table_header, "providing-rule"});
  for (const auto &[provided, libs] : provided_from_lib) {
    if (dup_handling == DuplicateHandling::kOutputOnlyDuplicates &&
        libs.size() == 1) {
      continue;
    }
    if (dup_handling == DuplicateHandling::kOutputOnlyUnique &&
        libs.size() != 1) {
      continue;
    }
    std::vector<std::string> list;
    for (const BazelTarget &target : libs) {
      if (filter.Match(target)) list.push_back(target.ToString());
    }
    printer->AddRowWithRepeatedLastColumn({provided}, list);
  }
  printer->Finish();
}

void PrintTargetFileSet(Session &session, const BazelWorkspace &workspace,
                        const BazelTargetMatcher &filter,
                        const TargetProvidedFiles &target_to_files) {
  auto highlighter = CreateGrepHighlighterFromFlags(session);
  if (!highlighter) return;
  auto printer =
    TablePrinter::Create(session.out(), session.flags().output_format,
                         *highlighter, {"label", "files"});
  for (const auto &[target, files] : target_to_files) {
    if (!filter.Match(target)) continue;
    std::vector<std::string> list;
    for (const std::string_view package_relative_file : files) {
      list.emplace_back(
        target.package.FullyQualifiedFile(workspace, package_relative_file));
    }
    printer->AddRowWithRepeatedLastColumn({target.ToString()}, list);
  }
  printer->Finish();
}

void PrintTargetToN(Session &session, const BazelWorkspace &workspace,
                    const BazelTargetMatcher &filter,
                    const PackageGroups &pkg_groups) {
  auto highlighter = CreateGrepHighlighterFromFlags(session);
  if (!highlighter) return;
  auto printer =
    TablePrinter::Create(session.out(), session.flags().output_format,
                         *highlighter, {"label", "pattern"});
  for (const auto &[target, _] : pkg_groups) {
    if (!filter.Match(target)) continue;
    std::vector<std::string> list;
    for (auto p : ResolvePackageGroupPatterns(pkg_groups, target)) {
      list.emplace_back(p);
    }
    printer->AddRowWithRepeatedLastColumn({target.ToString()}, list);
  }
  printer->Finish();
}

void PrintFileToFileSet(Session &session,
                        const HeaderToCanonicalHeader &header_to_headers) {
  const auto dup_handling = session.flags().duplicate_handling;
  const bool suppress_same = session.flags().suppress_same;
  auto highlighter = CreateGrepHighlighterFromFlags(session);
  if (!highlighter) return;
  auto printer =
    TablePrinter::Create(session.out(), session.flags().output_format,
                         *highlighter, {"source", "canonical-source"});
  for (const auto &[header, files] : header_to_headers) {
    std::vector<std::string> list;
    for (const std::string &s : files) {
      if (suppress_same && header == s) continue;
      list.emplace_back(s);
    }
    if (list.empty()) continue;

    if (dup_handling == DuplicateHandling::kOutputOnlyDuplicates &&
        list.size() == 1) {
      continue;
    }
    if (dup_handling == DuplicateHandling::kOutputOnlyUnique &&
        list.size() != 1) {
      continue;
    }

    printer->AddRowWithRepeatedLastColumn({std::string{header}}, list);
  }
  printer->Finish();
}
}  // namespace bant
