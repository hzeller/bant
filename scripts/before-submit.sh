#!/usr/bin/env bash

BAZEL=${BAZEL:-bazel}

# If we're in an intermediate state and can't compile the next
# might fail; but we might still have a valid bant binary from
# last time to fix our own files.
"$BAZEL" build -c opt bant:bant > /dev/null 2>&1
. <(bazel-bin/bant/bant dwyu ...)

set -e

"$BAZEL" test ...

./scripts/run-format.sh
./scripts/make-compilation-db.sh > /dev/null 2>&1
./scripts/run-clang-tidy-cached.cc
