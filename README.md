[![License: GPL v2](https://img.shields.io/badge/License-GPL_v2-blue.svg)](https://www.gnu.org/licenses/gpl-2.0.html)
[![Continuous Integration](https://github.com/hzeller/bant/workflows/ci/badge.svg)](https://github.com/hzeller/bant/actions/workflows/ci.yml)

bant - Build Analysis and Navigation Tool
=========================================

Utility to support projects using the [bazel] build system.
Clean up BUILD files with DWYU (build_cleaner), extracting list of targets,
finding which headers belong to them...
Outputs are for easy scripting with `grep`, `awk`, `buildozer` etc.

Useful hack for my personal projects, probably not too useful for anyone else.

 * Goal: Reading bazel-like BUILD files and doing useful things with content.
 * Non-Goal: parse full starlark (e.g. *.bzl files with `def`-initions)

Early Stages. WIP.

### Current status

#### Parsing
 * Parses most of simple BUILD/BUILD.bazel files and builds an AST of the
   Lists/Tuples/Function-calls (=build rules).
   Should parse most common BUILD files.
 * No expression evaluation yet (e.g. glob() or list comprehensions)

#### Commands
Commands are given on the command line of `bant`. They can be shortened as
long as they are unique, e.g. `lib` is sufficient to invoke `lib-headers`.
Some have extra command line options (e.g. for `parse`, `-p` prints AST).

See `bant -h` for general [Synopsis](#synopsis). Play around with the
various `-f` output formats of `workspace`, `list-packages` etc. for your
preferred post-processing tools (default is just a plain table easy to process
with e.g. `awk` and `grep`, but s-expressions and json are structured
outputs interesting in other contexts).

Useful everyday commands
 * `print` is useful to just print a particular rule or rules matching a pattern
   `bant print //foo/bar:baz` (it is re-created from the AST, so you see
   exactly what `bant` sees). In general this is much faster than manually
   finding and opening the file, in particular when it is in some external
   project (consider `bant print @abseil-cpp//absl/strings`).
   If you _want_ to open the file, `bant list-target //foo/bar:baz`
   will output the filename and exact line/column range where the target
   resides.

 * Use `lib-headers` if you want to know the library to depend on
   for a particular header file. Or, do that automatically with `dwyu`...

 * `dwyu` Depend on What You Use (DWYU): Determine which dependencies are
   needed in `cc_library()`, `cc_binary()`, and `cc_test()` targets.
   Greps through their declared sources to find which headers they include.
   Uses the information from `lib-headers` to determine which libraries
   these sources thus need to depend on.
   Emit [buildozer] commands to 'add' or 'remove' dependencies.
   Note, `bant` can't currently see all sources of header included
   (e.g. glob() is not implemented yet). If unclear if a library can be
   removed, it is conservatively _not_ suggested for removal.
   You can use this to clean up existing builds, or keep your BUILD files
   up-to-date in development.
   I usually just source the output of `bant` directly: `. <(bant dwyu ...)`

   Caveats:
     * Does not understand package groups in visibility yet; these will be
       considered 'public' and might result in adding dependencies that bazel
       would not allow.
     * Always adds the direct dependency of a header, even if another dependency
       exists that provides that header with an indirection. This is by design,
       but maybe there should be an option to allow indirect dependencies up
       to N-levels apart ?

   You could call this a simple CC-target [`build_cleaner`][build_cleaner]...

 * `canonicalize` emits edits to canonicalize targets, e.g.
    * `//foo/bar:baz` when already in package `//foo/bar` becomes `:baz`
    * `//foo:foo` becomes `//foo`
    * `@foo//:foo` becomes `@foo`
    * `foo` without `:` prefix becomes `:foo`

### Nice-to-have/next steps/TODO

  * variable-expand and some const-evaluate expressions to flatten and
    see more relevant info (e.g. outputs from `glob()`).
  * expand list-comprehensions
  * Maybe a path query language to extract in a way that the output
    then can be used for scripting (though keeping it simple. Main goal is still
    to just dump things in a useful format for standard tools to post-process).
  * language server for BUILD files.
  * ...

### Compile/Installation

To compile

```
bazel build -c opt //bant
```
Resulting binary will be `bazel-bin/bant/bant`

To install, use the installer; the `-s` option will ask for `sudo` access:

```bash
# To some writable directory that does not require root access
bazel run -c opt //bant:install -- ~/bin

# For a system directory that requires root-access, call with -s option.
# (Do _not_ run bazel with sudo.)
bazel run -c opt //bant:install -- -s /usr/local/bin
```

### Use

Note, `bant` can only find external projects if `bazel` has set up the
workspace, and fetched, unpacked and made visible these into
`bazel-out/../../../external`

Bant never does any fetching, it just observes the existing workspace. Given
that `bazel` adapts the visible external projecgts depending on what targets
are used, consider a bazel command that needs all of them, e.g.
`bazel test ...` before running `bant`.

### Synopsis

```
$ bazel-bin/bant/bant -h
Copyright (c) 2024 Henner Zeller. This program is free software; license GPL 2.0.
Usage: bazel-bin/bant/bant [options] <command> [bazel-target-pattern]
Options
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
    -v             : Verbose; print some stats.
    -h             : This help.

Commands (unique prefix sufficient):
    == Parsing ==
    print          : Print AST matching pattern. -e : only files w/ parse errors
    parse          : Parse all BUILD files from pattern. Follow deps with -r
                     Emit parse errors. Silent otherwise: No news are good news.

    == Extract facts == (Use -f to choose output format) ==
    workspace      : Print external projects found in WORKSPACE.
                     → 3 column table: (project, version, path)

    -- Given '-r', the following also follow dependencies recursively --
    list-packages  : List all BUILD files and the package they define
                     → 2 column table: (buildfile, package)
    list-targets   : List BUILD file locations of rules with matching targets
                     → 3 column table: (buildfile:location, ruletype, target)
    depends-on     : List cc library targets and the libraries they depend on
                     → 2 column table: (target, dependency*)
    has-dependent  : List cc library targets and the libraries that depend on it
                     → 2 column table: (target, dependent*)
    lib-headers    : Print headers provided by cc_library()s matching pattern.
                     → 2 column table: (header-filename, cc-library-target)
    genrule-outputs: Print generated files by genrule()s matching pattern.
                     → 2 column table: (filename, genrule-target)

    == Tools ==
    dwyu           : DWYU: Depend on What You Use (emit buildozer edit script)
    canonicalize   : Emit rename edits to canonicalize targets.
```

### Usage examples

```bash
 bant parse -e -v  # Read bazel project, print AST for files with parse errors.
 bant parse -C ~/src/verible -v  # Read project in given directory.
 bant print //foo:bar   # Print specific target AST matching pattern
 bant print //foo/...   # Print all build files matching recursive pattern.
 bant workspace         # List all the external projects listed in workspace.
 bant list-packages -r  # List all the build files, follow dependencies
 bant list-targets ...  # List all targets in this project
 bant lib-headers       # For each header found in project, print exporting lib
 bant dwyu ...         # Look which headers are used and suggest add/remove deps
 . <(bant dwyu foo/...)  # YOLO oneliner: build_clean deps in package foo/...
                         # by sourcing the emitted buildozer edit script.
```

### Development

Compiled using `bazel` >= 6.
Relevant dependencies are already in the `shell.nix` so you can set up
your environment [with that automatically][nix-devel-env].

To get a useful compilation database for `clangd` to be happy, run first

```bash
scripts/make-compilation-db.sh
```

Before submit, run `scripts/before-submit.sh` ... and fix potential
`clang-tidy` issues (or update `.clang-tidy` if it is not useful).

[bazel]: https://bazel.build/
[buildozer]: https://github.com/bazelbuild/buildtools/blob/master/buildozer/README.md
[nix-devel-env]: https://nixos.wiki/wiki/Development_environment_with_nix-shell
[build_cleaner]: https://github.com/bazelbuild/bazel/issues/6871
