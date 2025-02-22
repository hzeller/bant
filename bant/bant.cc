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

// The following is to work around clang-tidy being confused and not
// understanding that unistd.h indeed provides getopt(). So let's include
// unistd.h for correctness, and then soothe clang-tidy with decls.
// TODO: how make it just work with including unistd.h ?
#include <unistd.h>                            // NOLINT
extern "C" {                                   //
extern char *optarg;                           // NOLINT
extern int optind;                             // NOLINT
int getopt(int, char *const *, const char *);  // NOLINT
}

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "bant/cli-commands.h"
#include "bant/output-format.h"
#include "bant/session.h"

// Generated from at compile time from git tag or MODULE.bazel version
#include "bant/generated-build-version.h"
#include "bant/util/filesystem-prewarm-cache.h"

#define BOLD  "\033[1m"
#define RED   "\033[1;31m"
#define RESET "\033[0m"

// Print version coming from BANT_BUILD_GIT_VERSION that is generated by
// by workspace status extracted from git.
// If that didn't happen (e.g. we're built as part of being a MODULE.bazel
// dependency), use BANT_MODULE_VERSION as secondary source.
static int print_version() {
#ifdef BANT_BUILD_GIT_VERSION
#define RELEVANT_VERSION BANT_BUILD_GIT_VERSION
#elif defined(BANT_MODULE_VERSION)
#define RELEVANT_VERSION BANT_MODULE_VERSION
#else
#define RELEVANT_VERSION "(unknown)"
#endif
  fprintf(stderr,
          "bant v%s <http://bant.build/>\n"
          "Copyright (c) 2024-2025 Henner Zeller. "
          "This program is free software; GPL 3.0.\n",
          RELEVANT_VERSION);
  return EXIT_SUCCESS;
#undef RELEVANT_VERSION
}

static int usage(const char *prog, const char *message, int exit_code) {
  print_version();
  fprintf(stderr, "Usage: %s [options] <command> [bazel-target-pattern...]\n",
          prog);
  fprintf(stderr, R"(Options
    -C <directory> : Change to this project directory first (default = '.')
    -q             : Quiet: don't print info messages to stderr.
    -o <filename>  : Instead of stdout, emit command primary output to file.
    -f <format>    : Output format, support depends on command. One of
                   : native (default), s-expr, plist, json, csv
                     Unique prefix ok, so -fs , -fp, -fj or -fc is sufficient.
    -r             : Follow dependencies recursively starting from pattern.
                     Without parameter, follows dependencies to the end.
                     An optional parameter allows to limit the nesting depth,
                     e.g. -r2 just follows two levels after the toplevel
                     pattern. -r0 is equivalent to not providing -r.
    -v             : Verbose; print some stats. Multiple times: more verbose.
    -h             : This help.
    --//<option>   : configurable flag attribute to be used in select() and
                     picked up by elaboration (-e) (experimental; does not yet
                     read config_setting(), but flag value is used directly).

Commands (unique prefix sufficient):
    %s== Parsing ==%s
    print          : Print AST matching pattern. -E : only files w/ parse errors
                      -e : elaborate; light eval: expand variables, concat etc.
                      -g <regex> : 'grep' - only print targets where any string
                                    matches regex.
                      -i If '-g' is given: case insensitive
    parse          : Parse all BUILD files from pattern. Follow deps with -r
                     Emit parse errors. Silent otherwise: No news are good news.
                      -v : some stats.

    %s== Extract facts ==%s (Use -f to choose output format) ==
    workspace      : Print external projects found in WORKSPACE/MODULE.bazel
                     Without pattern: All external projects.
                     With pattern   : Subset referenced by matching targets.
                     → 3 column table: (project, version, path)

    -- Given '-r', the following also follow dependencies recursively --
    list-packages  : List all BUILD files and the package they define
                     → 2 column table: (buildfile, package)
    list-targets   : List BUILD file locations of rules with matching targets
                     → 3 column table: (buildfile:location, ruletype, target)
    list-leafs     : Show only targets not referenced anywhere.
                     → 3 column table: (buildfile:location, ruletype, target)
    aliased-by     : List targets and the various aliases pointing to it.
                     → 2 column table: (actual, alias*)
    depends-on     : List cc library targets and the libraries they depend on
                     → 2 column table: (target, dependency*)
    has-dependent  : List cc library targets and the libraries that depend on it
                     → 2 column table: (target, dependent*)
    lib-headers    : Print headers provided by cc_library()s matching pattern.
                     → 2 column table: (header-filename, cc-library-target)
    genrule-outputs: Print generated files by genrule()s matching pattern.
                     → 2 column table: (filename, genrule-target)

    %s== Tools ==%s
    dwyu           : DWYU: Depend on What You Use (emit buildozer edit script)
                      -k strict: emit remove even if # keep comment in line.
    canonicalize   : Emit rename edits to canonicalize targets.
    compile-flags  : (experimental) Emit compile flags. Redirect or output with
                     -o compile_flags.txt
    compilation-db : (experimental) Emit compilation db. Redirect or output with
                     -o compile_commands.json
)",
          BOLD, RESET, BOLD, RESET, BOLD, RESET);

  if (message) {
    fprintf(stderr, "\n%s%s%s\n", RED, message, RESET);
  }
  return exit_code;
}

// Bazel custom flag extraction, such as --//foo:bar to be used in selectors.
static void ExtractCustomFlags(int *argc, char *argv[],
                               absl::flat_hash_set<std::string> *result) {
  int out_arg = 1;
  for (int i = 1; i < *argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg.starts_with("--//")) {
      result->emplace(arg.substr(2));
      continue;
    }
    argv[out_arg++] = argv[i];
  }
  *argc = out_arg;
}

int main(int argc, char *argv[]) {
  // non-nullptr streams if chosen by user.
  std::unique_ptr<std::ostream> user_primary_out;
  std::unique_ptr<std::ostream> user_info_out;

  // Default primary and info outputs.
  std::ostream *primary_out = &std::cout;
  std::ostream *info_out = &std::cerr;

  bant::CommandlineFlags flags;
  flags.do_color = isatty(STDOUT_FILENO);

  bool regex_case_insesitive = false;

  // Since we're using basic getopt() currently, we've to fish out all the
  // double-dash bazel configs, otherwise getopt() gets confused about unknown
  // options.
  // TODO: replace with better flag handling.
  ExtractCustomFlags(&argc, argv, &flags.custom_flags);

  using bant::OutputFormat;
  static const std::map<std::string_view, OutputFormat> kFormatOutNames = {
    {"native", OutputFormat::kNative}, {"s-expr", OutputFormat::kSExpr},
    {"plist", OutputFormat::kPList},   {"csv", OutputFormat::kCSV},
    {"json", OutputFormat::kJSON},     {"graphviz", OutputFormat::kGraphviz},
  };
  int opt;
  while ((opt = getopt(argc, argv, "C:qo:vhpecbf:r::Vkg:i")) != -1) {
    switch (opt) {
    case 'C': {
      std::error_code err;
      std::filesystem::current_path(optarg, err);
      if (err) {
        std::cerr << "Can't change into directory " << optarg << "\n";
        return 1;
      }
      break;
    }

    case 'q':  //
      user_info_out.reset(new std::ostream(nullptr));
      info_out = user_info_out.get();
      break;

    case 'o':  //
      if (std::string_view(optarg) == "-") {
        primary_out = &std::cout;
        break;
      }
      user_primary_out.reset(new std::fstream(
        optarg, std::ios::out | std::ios::binary | std::ios::trunc));
      if (!user_primary_out->good()) {
        std::cerr << "Could not open '" << optarg << "'\n";
        return 1;
      }
      primary_out = user_primary_out.get();
      break;

    case 'r':
      flags.recurse_dependency_depth = optarg  //
                                         ? atoi(optarg)
                                         : std::numeric_limits<int>::max();
      break;

    case 'k': flags.ignore_keep_comment = true; break;

    case 'g': flags.grep_regex = optarg; break;

    case 'i':
      regex_case_insesitive = true;
      break;
      // "print" options
    case 'p': flags.print_ast = true; break;
    case 'E': flags.print_only_errors = true; break;
    case 'b':  // old option.
      fprintf(stderr, "Note, the name of -b option changed to -e\n");
      [[fallthrough]];
    case 'e': flags.elaborate = true; break;
    case 'f': {
      auto found = kFormatOutNames.lower_bound(optarg);
      if (found == kFormatOutNames.end() || !found->first.starts_with(optarg)) {
        return usage(argv[0], "invalid -f format", EXIT_FAILURE);
      }
      flags.output_format = found->second;
    } break;
    case 'v': flags.verbose++; break;  // More -v, more detail.
    case 'V': return print_version();
    default: return usage(argv[0], nullptr, EXIT_SUCCESS);
    }
  }

  if (!flags.grep_regex.empty()) {
    if (regex_case_insesitive) {
      flags.grep_regex.insert(0, "(?i)");
    }
    flags.grep_regex.insert(0, "(");
    flags.grep_regex.append(")");
  }

  bant::FilesystemPrewarmCacheInit(argc, argv);

  bant::Session session(primary_out, info_out, flags);
  std::vector<std::string_view> positional_args;
  for (int i = optind; i < argc; ++i) {
    positional_args.emplace_back(argv[i]);
  }

  using bant::CliStatus;
  const CliStatus result = RunCliCommand(session, positional_args);
  if (result == CliStatus::kExitCommandlineClarification) {
    session.error() << "\n\n";  // A bit more space to let message stand out.
    return usage(argv[0], nullptr, static_cast<int>(result));
  }

  if (flags.verbose) {
    // If verbose explicitly chosen, we want to print this even if -q.
    // So not to info_out, but std::cerr
    for (const std::string_view subsystem : session.stat_keys()) {
      std::cerr << subsystem << " " << *session.stat(subsystem) << "\n";
    }
  }
  return static_cast<int>(result);
}
