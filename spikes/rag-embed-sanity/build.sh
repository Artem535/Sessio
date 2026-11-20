#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
prefix="${SESSIO_LITERT_DIR:-$HOME/.cache/sessio-litert-lm/install}"
g++ -O2 -std=c++20 "$here/embed_sanity.cpp" \
  -I "$root/src/call_prompter/litert" \
  -L "$prefix/lib" -lsessio_litert -Wl,-rpath,"$prefix/lib" \
  -o "$here/embed_sanity"
echo "Built $here/embed_sanity against $prefix"
