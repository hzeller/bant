bant - Build Analysis and Navigation Tool
=========================================

Quick-and-dirty hack for my personal projects that use [bazel]. Extracting a
list of targets; finding which headers belong to them etc. for easy scripting
with `grep`, `awk`, `buildozer` etc.

Probably not useful for anyone else.

 * Goal: Reading bazel-like BUILD files and doing useful things with content.
 * Non-Goal: parse full starlark.

Early Stages. WIP.

### Current status

 * Parses most of simple BUILD/BUILD.bazel files and builds an AST of the
   Lists/Tuples/Function-calls (=build rules) involved for inspection and
   writing tools for (Use `-P` or `-Pe` for inspection).
   Can't parse more Python-specific functionality yet, missing mostly right
   now: array/string slice operators and more intricate list-comprehensions.
   Goal is to be best effort and fast, no full Starlark parsing intented.
 * Given a directory with a bazel project, parses all BUILD files including
   the external ones that bazel had extracted in `bazel-${project}/external/`,
   report parse errors. Note, build your project first fully with `bazel`,
   otherwise it will not have created the symbolic-link tree in the exernal/
   directory.
 * `-L` command: Simply list all the BUILD files it would consider for the
   other commands. Use `-x` to limit scope to not include the external rules.
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

Synopsis:

```
$ bazel-bin/bant/bant -h
Copyright (c) 2024 Henner Zeller. This program is free software; license GPL 2.0.
Usage: bazel-bin/bant/bant [options]
Options
        -C<directory>  : Project base directory (default: current dir = '.')
        -x             : Do not read BUILD files of eXternal projects.
                         (i.e. only read the files in the direct project)
        -q             : Quiet: don't print info messages to stderr.
        -v             : Verbose; print some stats.
        -h             : This help.

Commands:
        (no-flag)      : Just parse BUILD files of project, emit parse errors.
                         Parse is primary objective, errors go to stdout.
                         Other commands below with different main output
                         emit errors to info stream (stderr or none if -q)
        -L             : List all the build files found in project
        -P             : Print parse tree (-e : only files with parse errors)
        -H             : Print table header files -> targets that define them.
        -D             : DWYU: Depend on What You Use (emit buildozer edits)
```

Usage examples

```bash
 bant -pv      # reads bazel project in current dir, print AST and some stats
 bant -E       # read project, print AST of files that couldn't be parsed
 bant -C ~/src/verible -v  # read bazel project in given directory; print stats
 bant -x      # read bazel project, but don't parse referenced external projects
 bant -L      # List all the build files including the referenced external
 bant -Lx     # Only list build files in this project.
 bant -H      # for each header, print defining lib
 bant -D      # Look which headers are used and suggest add/remove dependencies
```

### Development

To get a useful compilation database for `clangd` to be happy, run first

```
scripts/make-compilation-db.sh
```

[bazel]: https://bazel.build/
[buildozer]: https://github.com/bazelbuild/buildtools/blob/master/buildozer/README.md
