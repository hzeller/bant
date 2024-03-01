#!/usr/bin/env bash

clang-format -i $(find . -name "*.h" -o -name "*.cc")
buildifier -lint=fix $(find . -name BUILD)
