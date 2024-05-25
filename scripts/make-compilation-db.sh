#!/usr/bin/env bash

BAZEL=${BAZEL:-bazel}
"${BAZEL}" run @hedron_compile_commands//:refresh_all

# Fix up the paths to not be specific to optimized build
sed -i 's/bazel-out\/[^/]\+\/bin/bazel-bin/g' compile_commands.json
