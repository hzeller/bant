#!/usr/bin/env bash

version_from_git() {
    set -o pipefail
    git describe --match=v* 2>/dev/null \
    | sed 's/v\([^-]*\)-\([0-9]*\).*/\1-\2/' 2>/dev/null
}

version_from_module_bazel() {
    awk '/module/  { in_module=1; }
         /version/ { if (in_module) print $0; }
         /)/       { in_module=0; }' $(dirname $0)/../MODULE.bazel \
    | sed 's/.*version[ ]*=[ ]*"\([0-9.]*\)".*/\1/p;d'
}

# Get version from git including everything since last tag, but if that is
# not available, just fall back to the version we get from module bazel.
echo "BUILD_VERSION \"$(version_from_git || version_from_module_bazel)\""
