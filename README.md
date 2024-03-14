bant - Build Analysis and Navigation Tool
=========================================

Quick-and-dirty hack for my personal projects that use [bazel]. Extracting a
list of targets; finding which headers belong to them, DWYU ...
Outputs are for easy scripting with `grep`, `awk`, `buildozer` etc.

Probably not too useful for anyone else.

 * Goal: Reading bazel-like BUILD files and doing useful things with content.
 * Non-Goal: parse full starlark (e.g. *.bzl files with `def`-initions)

Early Stages. WIP.

### Current status

#### Parsing
 * Parses most of simple BUILD/BUILD.bazel files and builds an AST of the
   Lists/Tuples/Function-calls (=build rules) involved for inspection and
   writing tools for. Should parse most common BUILD files.
 * No expression evaluation yet (e.g. glob() calls do not see files yet)
 * Given a directory with a bazel project, parses all BUILD files including
   the external ones that bazel had extracted in `bazel-${project}/external/`,
   report parse errors.
 * This currently only works somewhat with non-bzlmod builds. The external
   project symlinks seem to have moved around a bit in bzlmod, so can't be
   found properly anymore.

#### Commands
Commands are given on the command line of `bant`. They can be shortened as
long as they are unique, e.g. `lib` is sufficient to invoke `lib-headers`.
Some have extra command line options (e.g. for `parse`, `-p` prints AST).

 * `list` List all the BUILD files it would consider for the other commands.
    Use `-x` to limit scope to not include the external rules.
 * `parse` Parse build files and emit syntax errors if any.
    * `-p` print AST (should look similar to the input :) ).
    * `-e` print AST, but only for files that had syntax errors.
 * `lib-headers` for each header exported with `hdrs = [...]` in libraries,
    report which library that is (two columns, easy to `grep` or `awk` over).
 * `genrule-outputs` like `lib-headers`, but shows all the generated files
    and which genrule created it.
 * `dwyu` Depend on What You Use (DWYU): Determine which dependencies are
   needed in `cc_library()`, `cc_binary()`, and `cc_test()` targets.
   Greps through their declared sources to find which headers they include.
   Uses the information from `lib-headers` to determine which libraries
   these sources thus need to depend on.
   Emit [buildozer] commands to 'add' or 'remove' dependencies.
   Note, `bant` can't currently see all sources of header included
   (e.g. glob() is not implemented yet). If unclear if a library can be
   removed, it is conservatively _not_ suggested for removal.
   You can use this to clean up existing builds, or, while in the development
   and you added/removed headers from your code, to update your BUILD files
   using the ouptut of `bant dwyu`.
   Note: does not take visibiliity into acccount yet.
   You could call this a simple `build_cleaner` ...
 * `canonicalize` emits edits to canonicalize targets, e.g.
    * `//foo/bar:baz` when already in `//foo/bar` becomes `:baz`
    * `//foo:foo` becomes `//foo`
    * `@foo//:foo` becomes `@foo`
    * `foo` without `:` prefix becomes `:foo`

Also, see `bant -h` or [Synopsis](#synopsis) below.

### Nice-to-have/next steps/TODO

  * variable-expand and some const-evaluate expressions to flatten and
    see more relevant info (e.g. outputs from `glob()`).
  * Maybe a path query language to extract in a way that the output
    then can be used for scripting.
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
`bazel-${yourproject}/external`.

Bant never does any fetching, it just observes the existing workspace. Given
that `bazel` adapts the visible external projecgts depending on what targets
are used, consider a bazel command that needs all of them, e.g.
`bazel test ...` before running `bant`.

### Synopsis

```
$ bazel-bin/bant/bant -h
Copyright (c) 2024 Henner Zeller. This program is free software; license GPL 2.0.
Usage: bazel-bin/bant/bant [options] <command>
Options
    -C <directory> : Change to project directory (default = '.')
    -x             : Do not read BUILD files of eXternal projects (e.g. @foo)
                     (i.e. only read the files in the direct project)
    -q             : Quiet: don't print info messages to stderr.
    -o <filename>  : Instead of stdout, emit command primary output to file.
    -v             : Verbose; print some stats.
    -h             : This help.

Commands (unique prefix sufficient):
    parse          : Just parse BUILD files of project, emit parse errors
                     (which might well be due to bant not handling that yet).
                     -p : also print abstract syntax tree (AST) for all files.
                     -e : Only for files with parse errors: print partial AST.
    list           : List all the build files found in project
    lib-headers    : Print table header files -> libraries that define them.
    genrule-outputs: Print table generated files -> genrules creating them.
    dwyu           : DWYU: Depend on What You Use (emit buildozer edit script)
    canonicalize   : Emit rename edits to canonicalize targets.
```

### Usage examples

```bash
 bant parse -p -v  # Read bazel project in current dir, print AST, and stats.
 bant parse -e     # Read project, print AST of files that couldn't be parsed.
 bant parse -C ~/src/verible -v  # Read project in given directory.
 bant list         # List all the build files including the referenced external
 bant list -x      # List BUILD files only in this project, no external.
 bant lib-headers  # For each header found in project, print exporting target.
 bant dwyu         # Look which headers are used and suggest add/remove deps
 . <(bant dwyu foo/...)  # fix dependencies in package foo/... in one line.
```

### Development

Compiled using `bazel` >= 6.
Relevant dependencies are already in the `shell.nix` so you can set up
your environment [with that automatically][nix-devel-env].

To get a useful compilation database for `clangd` to be happy, run first

```
scripts/make-compilation-db.sh
```

[bazel]: https://bazel.build/
[buildozer]: https://github.com/bazelbuild/buildtools/blob/master/buildozer/README.md
[nix-devel-env]: https://nixos.wiki/wiki/Development_environment_with_nix-shell
