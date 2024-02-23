bant - Build Analysis and Navigation Tool
=========================================

Quick-and-dirty hack for my personal projects that use bazel. Extracting a list
of targets; finding which headers belong to them etc. for easy scripting with
`awk` etc.

Probably not useful for anyone else.

 * Goal: Reading bazel-like BUILD files and doing useful things with content.
 * Non-Goal: parse full starlark.

WIP.

### Current status

 * Parses most of simple BUILD.bazel files and builds an AST (but can't parse
   more Python-specific functionality is used). Lists/Tuples/Function calls
   mostly.
 * Given a directory with a bazel project, parses all BUILD files including
   the external ones that bazel had extracted in `bazel-${project}/external/`,
   report parse errors (typically because)
 * `-H` command: for each header exported with `hdrs = [...]` in libraries,
   report which library that is (two columns, easy to `grep` or `awk` over).
 * `-D` grep all include files used in sources and libraries, determine which
   libraries defined them and emit [buildozer] commands to 'add' or 'remove'
   dependencies. If unclear if a library can be removed, it is conservatively
   _not_ suggested for removal. 'Depend on What You Use' DWYU.

### Nice-to-have/next steps/TODO

  * variable-expand and const-evaluate expressions to flatten the info.
  * Maybe a path query language to extract in a way that the output
    then can be used for scripting.

Synopsis:

```
$ bazel-bin/bant -h
Usage: bazel-bin/bant [options]
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
        -D             : Declare What You Use suggestions (not useful yet)
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
