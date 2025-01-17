#!/usr/bin/env bash

version_from_git() {
    set -o pipefail
    git describe --match=v* 2>/dev/null \
        | sed 's/v\([^-]*\)-\([0-9]*\).*/\1-\2/' \
	| sed 's/^v//'
}

# Get version from git including count since last tag.
BUILD_GIT_VERSION="$(version_from_git)"

if [ ! -z "${BUILD_GIT_VERSION}" ]; then
    echo "BUILD_GIT_VERSION \"${BUILD_GIT_VERSION}\""
fi
