#!/usr/bin/env bash
# Proves the zoneinfo configure-time policy of token-backend/schedule:
#   1. default source dir            -> configures
#   2. explicit missing runtime dir  -> configures with a warning
#   3. missing source dir            -> configure fails
# Usage: CMAKE_ARGS="-DCMAKE_PREFIX_PATH=... -DVCPKG_TARGET_TRIPLET=x64-linux ..." \
#        token-backend/scripts/check_zoneinfo_configure.sh
set -u
here="$(cd "$(dirname "$0")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
run() { cmake -S "$here" -B "$tmp/$1" -G Ninja ${CMAKE_ARGS:-} "${@:2}" 2>&1; }
fail=0
out=$(run ok) || { echo "FAIL default"; fail=1; }
out=$(run warn -DPCM_SCHEDULE_ZONEINFO_DIR=/usr/share/pcm-schedule/zoneinfo) \
  && tr -s " \n" "  " <<<"$out" | grep -q "has no timezone data" || { echo "FAIL runtime-dir warning"; fail=1; }
out=$(run bad -DPCM_SCHEDULE_ZONEINFO_SOURCE_DIR=/nonexistent) \
  && { echo "FAIL missing source dir configured"; fail=1; }
[ $fail -eq 0 ] && echo "zoneinfo configure checks passed"
exit $fail
