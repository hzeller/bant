#!/usr/bin/env bash

clang-format -i *.h *.cc
buildifier -lint=fix BUILD
