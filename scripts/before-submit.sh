#!/usr/bin/env bash

BAZEL=${BAZEL:-bazel}

# If we're in an intermediate state and can't compile the next
# might fail; but we might still have a valid bant binary from
# last time to fix our own files.
"$BAZEL" build -c opt bant:bant > /dev/null 2>&1
. <(bazel-bin/bant/bant dwyu ...)

set -e

# Making sure things are still compatible with c++20
"$BAZEL" test --cxxopt=-std=c++20 --host_cxxopt=-std=c++20 ...

# Regular c++23 compile.
"$BAZEL" test ...

./scripts/run-format.sh
./scripts/make-compilation-db.sh > /dev/null 2>&1
./scripts/run-clang-tidy-cached.cc
