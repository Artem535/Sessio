#!/usr/bin/env bash
# Builds libsessio_litert: Sessio's prompter runtime on the LiteRT-LM C++ API.
#
# LiteRT-LM is built from source with its own CMake super-build (Abseil,
# Protobuf, TFLite, LiteRT, tokenizers). That takes a long time and roughly
# 8 GB of RAM per job, so it runs once, outside the normal Sessio build, and
# Sessio is configured against the result with -DSESSIO_LITERT_DIR=<prefix>/install.
#
# Requirements (upstream, verified on Ubuntu with GCC 13+): cmake 3.25+, make,
# Java 17 (ANTLR), Rust/cargo, Python 3.12+ with headers (build scripts only),
# zlib, OpenSSL and libcurl development packages.
#
# Usage: scripts/build-litert-lm.sh [prefix]   (default ~/.cache/sessio-litert-lm)
# Env:   JOBS (default 2), CC/CXX (default gcc/g++).
set -euo pipefail

version=v0.18.0
prefix="$(realpath -m "${1:-$HOME/.cache/sessio-litert-lm}")"
jobs="${JOBS:-2}"
cc="$(command -v "${CC:-gcc}")"
cxx="$(command -v "${CXX:-g++}")"
hook_dir="$(cd "$(dirname "$0")/../src/call_prompter/litert" && pwd)"

mkdir -p "$prefix"
if [[ ! -d "$prefix/src/.git" ]]; then
  git clone --depth 1 --branch "$version" https://github.com/google-ai-edge/LiteRT-LM.git "$prefix/src"
fi
if [[ "$(git -C "$prefix/src" describe --tags --exact-match 2>/dev/null)" != "$version" ]]; then
  echo "$prefix/src is not LiteRT-LM $version" >&2
  exit 1
fi

# The orchestrator project has no languages, so compilers must be passed to
# both phases explicitly.
cmake -S "$prefix/src" -B "$prefix/build" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
  "-DLITERTLM_CC=$cc" "-DLITERTLM_CXX=$cxx" \
  "-DLITERTLM_HOST_CC_COMPILER=$cc" "-DLITERTLM_HOST_CXX_COMPILER=$cxx" \
  "-DLITERTLM_TARGET_CC_COMPILER=$cc" "-DLITERTLM_TARGET_CXX_COMPILER=$cxx" \
  "-DLITERTLM_TOOLCHAIN_ARGS=-DCMAKE_PROJECT_LiteRT-LM_INCLUDE=$hook_dir/sessio_litert_hook.cmake;-DSESSIO_LITERT_SOURCE_DIR=$hook_dir"
mkdir -p "$prefix/build/litert_lm/build"
CMAKE_BUILD_PARALLEL_LEVEL="$jobs" cmake --build "$prefix/build"

lib="$(find "$prefix/build/litert_lm/build" -maxdepth 1 -name 'libsessio_litert.so' -o -maxdepth 1 -name 'libsessio_litert.dylib' -o -maxdepth 1 -name 'sessio_litert.dll' | head -n 1)"
if [[ -z "$lib" ]]; then
  echo "libsessio_litert was not produced" >&2
  exit 1
fi
mkdir -p "$prefix/install/lib"
cp -f "$lib" "$prefix/install/lib/"
cp -f "$prefix/src/LICENSE" "$prefix/install/LICENSE.litert-lm"
echo "Built $(basename "$lib"). Configure Sessio with -DSESSIO_LITERT_DIR=$prefix/install"
