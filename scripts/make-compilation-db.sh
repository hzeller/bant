#!/usr/bin/env bash

BAZEL=${BAZEL:-bazel}

# Make sure all external projects are visible.
# Note: bazel fetch or sync don't make them visible,
# so we have to explicitly tickle a target there.
"${BAZEL}" build -c opt @googletest//:gtest

# Only override compile flags if we're able to build bant.
"${BAZEL}" build -c opt bant:bant > /dev/null 2>&1 \
    && bazel-bin/bant/bant compile-flags -o compile_flags.txt
