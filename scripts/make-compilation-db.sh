#!/usr/bin/env bash

BAZEL=${BAZEL:-bazel}
"${BAZEL}" build -c opt bant:bant > /dev/null 2>&1

bazel-bin/bant/bant compilation-db > compile_commands.json
