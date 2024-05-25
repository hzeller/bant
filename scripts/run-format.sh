#!/usr/bin/env bash

CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"
"${CLANG_FORMAT}" -i $(find bant/ -name "*.h" -o -name "*.cc")
buildifier -lint=fix MODULE.bazel $(find . -name BUILD)
