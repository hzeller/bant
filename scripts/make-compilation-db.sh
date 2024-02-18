#!/usr/bin/env bash
# Note, this also requires python.
set -u
set -e

readonly OUTPUT_BASE="$(bazel info output_base)"

readonly COMPDB_SCRIPT="${OUTPUT_BASE}/external/rules_compdb/generate.py"
[ -r "${COMPDB_SCRIPT}" ] || bazel fetch ...

python3 "${COMPDB_SCRIPT}"

# Remove a flags observed in the wild that clang-tidy doesn't understand.
sed -i -e 's/-fno-canonical-system-headers//g; s/DEBUG_PREFIX_MAP_PWD=.//g' \
       compile_commands.json
