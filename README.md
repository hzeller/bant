bant - Build Analysis and Navigation Tool
=========================================

Quick-and-dirty hack for my personal projects that use [bazel]. Extracting a
list of targets; finding which headers belong to them etc. for easy scripting
with `grep`, `awk`, `buildozer` etc.

Probably not useful for anyone else.

 * Goal: Reading bazel-like BUILD files and doing useful things with content.
 * Non-Goal: parse full starlark (e.g. *.bzl files with `def`-initions)

Early Stages. WIP.

### Current status

#### Parsing
 * Parses most of simple BUILD/BUILD.bazel files and builds an AST of the
   Lists/Tuples/Function-calls (=build rules) involved for inspection and
   writing tools for (Use `-P` or `-Pe` for parse-tree inspection).
   Mostly ok; might still stumble upon rare Python-specific syntax elements.
 * No expression evaluation yet (e.g. glob() calls do not see files yet)
 * Given a directory with a bazel project, parses all BUILD files including
   the external ones that bazel had extracted in `bazel-${project}/external/`,
   report parse errors.

#### Commands
 * `-L` command: Simply list all the BUILD files it would consider for the
   other commands. Use `-x` to limit scope to not include the external rules.
 * `-P` command: Print parse for project (should look similar to the input :) ).
 * `-H` command: for each header exported with `hdrs = [...]` in libraries,
   report which library that is (two columns, easy to `grep` or `awk` over).
 * `-D` grep all include files used in sources and libraries, determine which
   libraries defined them and emit [buildozer] commands to 'add' or 'remove'
   dependencies. If unclear if a library can be removed, it is conservatively
   _not_ suggested for removal. 'Depend on What You Use' DWYU.

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
`bazel test ...` before bant.

### Synopsis

```
$ bazel-bin/bant/bant -h
Copyright (c) 2024 Henner Zeller. This program is free software; license GPL 2.0.
Usage: bazel-bin/bant/bant [options]
Options
        -C<directory>  : Change to project directory (default = '.')
        -x             : Do not read BUILD files of eXternal projects.
                         (i.e. only read the files in the direct project)
        -q             : Quiet: don't print info messages to stderr.
        -v             : Verbose; print some stats.
        -h             : This help.

Commands:
        (no-flag)      : Just parse BUILD files of project, emit parse errors.
                         Parse is primary objective, errors go to stdout.
                         Other commands below with different main output
                         emit errors to info stream (stderr or muted with -q)
        -L             : List all the build files found in project
        -P             : Print parse tree (-e : only files with parse errors)
        -H             : Print table header files -> targets that define them.
        -D             : DWYU: Depend on What You Use (emit buildozer edits)
```

### Usage examples

```bash
 bant -P -v   # reads bazel project in current dir, print AST and some stats
 bant -Pe     # read project, print AST of files that couldn't be parsed
 bant -C ~/src/verible -v  # read bazel project in given directory; print stats
 bant -x      # read bazel project, but don't parse referenced external projects
 bant -L      # List all the build files including the referenced external
 bant -Lx     # Only list build files in this project.
 bant -H      # for each header, print libray exporting it
 bant -D      # Look which headers are used and suggest add/remove dependencies
```

### Development

Relevant dependencies are already in the `shell.nix` so you can set up
your environment [with that automatically][nix-devel-env].

To get a useful compilation database for `clangd` to be happy, run first

```
scripts/make-compilation-db.sh
```

[bazel]: https://bazel.build/
[buildozer]: https://github.com/bazelbuild/buildtools/blob/master/buildozer/README.md
[nix-devel-env]: https://nixos.wiki/wiki/Development_environment_with_nix-shell
