#!/usr/bin/env bash

clang-format -i $(find . -name "*.h" -o -name "*.cc")
buildifier -lint=fix WORKSPACE.bzlmod MODULE.bazel $(find . -name BUILD)
