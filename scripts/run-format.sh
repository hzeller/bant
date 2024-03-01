#!/usr/bin/env bash

clang-format -i $(find . -name "*.h" -o -name "*.cc")
buildifier -lint=fix WORKSPACE $(find . -name BUILD)
