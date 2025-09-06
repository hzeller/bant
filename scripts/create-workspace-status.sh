#!/bin/sh

version_from_git() {
    git describe --match=v* 2>/dev/null \
        | sed 's/v\([^-]*\)-\([0-9]*\).*/\1-\2/' \
	| sed 's/^v//'
}

# Get version from git including count since last tag.
BUILD_GIT_VERSION="$(version_from_git)"

if [ -n "${BUILD_GIT_VERSION}" ]; then
    echo "BUILD_GIT_VERSION \"${BUILD_GIT_VERSION}\""
fi
