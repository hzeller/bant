#!/usr/bin/env bash

set -e

BAZEL=${BAZEL:-bazel}

"$BAZEL" test ...

"$BAZEL" build -c opt bant:bant > /dev/null 2>&1
. <(bazel-bin/bant/bant dwyu ...)

./scripts/run-format.sh
./scripts/make-compilation-db.sh > /dev/null 2>&1
./scripts/run-clang-tidy-cached.cc
