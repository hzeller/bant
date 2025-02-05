#!/usr/bin/env bash

BAZEL=${BAZEL:-bazel}

# Only override compile flags if we're able to build bant.
"${BAZEL}" build -c opt bant:bant > /dev/null 2>&1 \
    && bazel-bin/bant/bant compile-flags > compile_flags.txt
