#!/usr/bin/env bash

BAZEL=${BAZEL:-bazel}
"${BAZEL}" fetch ...
"${BAZEL}" build -c opt bant:bant > /dev/null 2>&1

bazel-bin/bant/bant compile-flags > compile_flags.txt
