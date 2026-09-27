#!/usr/bin/env bash
# ClientModeWindow must never gain a dependency on Database/QClientModel — the
# spec's privacy boundary is structural. This grep-based check runs in CI
# (wired into test/CMakeLists.txt as a CTest case in Step 3) rather than as a
# GoogleTest, since what it verifies is an absence of an #include, not runtime
# behavior.
set -euo pipefail
FILE="$1"
if grep -qE '#include\s*"(database|qclient_model)\.h"' "$FILE"; then
  echo "FORBIDDEN: $FILE must not include database.h or qclient_model.h" >&2
  exit 1
fi
echo "OK: $FILE has no forbidden includes"
