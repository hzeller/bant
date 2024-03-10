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

#include "bant/tool/header-providers.h"

#include <functional>
#include <string_view>

#include "absl/strings/str_format.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/project-parser.h"
#include "bant/types-bazel.h"
#include "bant/util/query-utils.h"

// Inject dependency to gtest, as we don't glob() the files yet.
#define BANT_GTEST_HACK

namespace bant {
namespace {
// Find cc_library and call callback for each header file it exports.
using FindHeaderCallback =
  std::function<void(std::string_view library_name, std::string_view header)>;
static void FindCCLibraryHeaders(Node *ast, const FindHeaderCallback &cb) {
  query::FindTargets(
    ast, {"cc_library"}, [&cb](const query::TargetParameters &params) {
      std::vector<std::string_view> incdirs;
      query::ExtractStringList(params.includes_list, incdirs);
      std::vector<std::string_view> headers;
      query::ExtractStringList(params.hdrs_list, headers);

      for (std::string_view header_file : headers) {
        if (!params.include_prefix.empty()) {  // cc_library() dictates path.
          cb(params.name,
             absl::StrCat(params.include_prefix, "/", header_file));
          continue;
        }
        cb(params.name, header_file);
        // Could also show up under shorter path with -I
        for (std::string_view dir : incdirs) {
          std::string prefix(dir);
          if (!prefix.ends_with('/')) prefix.append("/");
          if (header_file.starts_with(prefix)) {
            cb(params.name, header_file.substr(prefix.length()));
          }
        }
      }
    });
}
}  // namespace

ProvidedFromTargetMap ExtractHeaderToLibMapping(const ParsedProject &project,
                                                std::ostream &info_out) {
  ProvidedFromTargetMap result;

#ifdef BANT_GTEST_HACK
  // gtest hack (can't glob() the headers yet, so manually add these to
  // the first project that looks like it is googletest...
  for (const auto &[_, file_content] : project.file_to_ast) {
    if (file_content.package.project.find("googletest") == std::string::npos) {
      continue;
    }
    BazelTarget test_target;
    test_target.package.project = file_content.package.project;
    test_target.target_name = "gtest";
    result["gtest/gtest.h"] = test_target;
    result["gmock/gmock.h"] = test_target;
    break;
  }
#endif

  // cc_library()
  for (const auto &[_, file_content] : project.file_to_ast) {
    if (!file_content.ast) continue;
    FindCCLibraryHeaders(file_content.ast, [&](std::string_view lib_name,
                                               std::string_view header) {
      std::string header_fqn(header);
      if (header.find_first_of('/') == std::string::npos) {
        header_fqn = file_content.package.QualifiedFile(header);
      }

      auto target = BazelTarget::ParseFrom(lib_name, file_content.package);
      if (!target.has_value()) return;

      const auto &inserted = result.insert({header_fqn, *target});
      if (!inserted.second && target != inserted.first->second) {
        // TODO: differentiate between info-log (external projects) and
        // error-log (current project, as these are actionable).
        // For now: just report errors.
        const bool is_error = file_content.package.project.empty();
        if (is_error) {
          // TODO: Get file-position from other target which might be
          // in a different file.
          info_out << file_content.filename << ":"
                   << file_content.line_columns.GetRange(header) << " Header '"
                   << header_fqn << "' in " << target->ToString()
                   << " already provided by "
                   << inserted.first->second.ToString() << "\n";
        }
      }
    });
  }

  // proto_library(), cc_proto_library().
  // To find cc library for proto header foo.pb.h, we need two parts:
  //  1. find proto_library() and look at the srcs. x.proto -> x.pb
  //  2. find the cc_proto_library() that depends on (1). that is the library
  //     that will export the header generated in 1.
  //  Execution: gather both infos, then push in result.
  ProvidedFromTargetMap header2proto_library;  // header created by protolib
  std::map<BazelTarget, BazelTarget> proto_lib_inputTo_cc_proto;
  for (const auto &[_, file_content] : project.file_to_ast) {
    if (!file_content.ast) continue;
    query::FindTargets(
      file_content.ast, {"proto_library", "cc_proto_library"},
      [&](const query::TargetParameters &params) {
        auto target = BazelTarget::ParseFrom(params.name, file_content.package);
        if (params.rule == "proto_library") {
          std::vector<std::string_view> proto_srcs;
          query::ExtractStringList(params.srcs_list, proto_srcs);
          for (const std::string_view proto : proto_srcs) {
            if (!proto.ends_with(".proto")) {
              // possibly file list. Not handling that yet.
              continue;
            }
            std::string proto_header;
            auto dot_pos = proto.find_last_of('.');
            proto_header.append(proto.substr(0, dot_pos)).append(".pb.h");
            proto_header = file_content.package.QualifiedFile(proto_header);
            header2proto_library.insert({proto_header, *target});
          }
        }

        else {
          // Look for all the dependencies that cc_proto_library() uses.
          std::vector<std::string_view> cc_proto_deps;
          query::ExtractStringList(params.deps_list, cc_proto_deps);
          for (const std::string_view dep : cc_proto_deps) {
            auto proto_library_target =
              BazelTarget::ParseFrom(dep, file_content.package);
            if (!proto_library_target.has_value()) continue;
            proto_lib_inputTo_cc_proto.insert({*proto_library_target, *target});
          }
        }
      });
  }

  for (const auto &[proto_header, proto_lib] : header2proto_library) {
    auto found = proto_lib_inputTo_cc_proto.find(proto_lib);
    if (found != proto_lib_inputTo_cc_proto.end()) {
      result.insert({proto_header, found->second});
    } else {
      //info_out << "Don't know how to associate " << proto_header << "\n";
    }
  }

  return result;
}

ProvidedFromTargetMap ExtractGeneratedFromGenrule(const ParsedProject &project,
                                                  std::ostream &info_out) {
  ProvidedFromTargetMap result;
  for (const auto &[_, file_content] : project.file_to_ast) {
    if (!file_content.ast) continue;
    query::FindTargets(
      file_content.ast, {"genrule"},
      [&](const query::TargetParameters &params) {
        std::vector<std::string_view> genfiles;
        query::ExtractStringList(params.outs_list, genfiles);

        auto target = BazelTarget::ParseFrom(params.name, file_content.package);
        if (!target.has_value()) return;

        for (std::string_view generated : genfiles) {
          std::string gen_fqn = file_content.package.QualifiedFile(generated);
          const auto &inserted = result.insert({gen_fqn, *target});
          if (!inserted.second && target != inserted.first->second) {
            // TODO: differentiate between info-log (external projects) and
            // error-log (current project, as these are actionable).
            // For now: just report errors.
            const bool is_error = file_content.package.project.empty();
            if (is_error) {
              // TODO: Get file-position from other target which might be
              // in a different file.
              info_out << file_content.filename << ":"
                       << file_content.line_columns.GetRange(generated) << " '"
                       << gen_fqn << "' in " << target->ToString()
                       << " also created by "
                       << inserted.first->second.ToString() << "\n";
            }
          }
        }
      });
  }
  return result;
}

void PrintProvidedSources(const ProvidedFromTargetMap &provided_from_lib,
                          std::ostream &out) {
  int longest = 0;
  for (const auto &[provided, _] : provided_from_lib) {
    longest = std::max(longest, (int)provided.length());
  }
  for (const auto &[provided, lib] : provided_from_lib) {
    out << absl::StrFormat("%*s\t%s\n", -longest, provided, lib.ToString());
  }
}
}  // namespace bant
