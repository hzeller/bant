#!/usr/bin/env bash

CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"
"${CLANG_FORMAT}" -i $(find bant/ -name "*.h" -o -name "*.cc")
# for now, no -lint=fix as that is version dependent
buildifier MODULE.bazel $(find . -name BUILD)
