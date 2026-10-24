#!/usr/bin/env bash
# Builds libsessio_litert: Sessio's prompter runtime on the LiteRT-LM C++ API.
#
# LiteRT-LM is built from source with its own CMake super-build (Abseil,
# Protobuf, TFLite, LiteRT, tokenizers). That takes a long time and roughly
# substantial RAM, so it runs once, outside the normal Sessio build, and
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
# v0.18.0 tracks TensorFlow master and LiteRT/RE2 main. Pin them: TensorFlow
# f83e881 (2026-09-28) removed the TFLite proto CMakeLists the v0.18.0 shims
# still add, so TFLite stays on its parent; LiteRT and RE2 stay at the
# v0.18.0 release date (2026-10-06).
tflite_tag=b4449a41869c3dbac7b4db333d019a941584e7a5
litert_tag=65fd9cca2192d3595552777e740ee618b48504fe
re2_tag=2da0056814cf180480a19f5cf811e7e1c054bf6d
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
  "-DCMAKE_BUILD_PARALLEL_LEVEL=$jobs" \
  "-DLITERTLM_CC=$cc" "-DLITERTLM_CXX=$cxx" \
  "-DLITERTLM_HOST_CC_COMPILER=$cc" "-DLITERTLM_HOST_CXX_COMPILER=$cxx" \
  "-DLITERTLM_TARGET_CC_COMPILER=$cc" "-DLITERTLM_TARGET_CXX_COMPILER=$cxx" \
  "-DLITERTLM_TOOLCHAIN_ARGS=-DCMAKE_POSITION_INDEPENDENT_CODE=ON;-DCMAKE_PROJECT_LiteRT-LM_INCLUDE=$hook_dir/sessio_litert_hook.cmake;-DCMAKE_PROJECT_LiteRT_INCLUDE=$hook_dir/sessio_litert_cpu.cmake;-DSESSIO_LITERT_SOURCE_DIR=$hook_dir;-DLITERTLM_TFLITE_TAG=$tflite_tag;-DLITERTLM_LITERT_TAG=$litert_tag;-DLITERTLM_RE2_TAG=$re2_tag"
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
if [[ "$(uname -s)" == Linux ]]; then
  # Upstream builds KissFFT shared. Ship it beside our library and avoid a
  # runtime dependency on the temporary LiteRT-LM build directory.
  command -v patchelf >/dev/null
  cp -a "$prefix/build/litert_lm/build/_deps/kissfft_lib-build/"libkissfft*.so* "$prefix/install/lib/"
  cp -f "$prefix/build/litert_lm/build/third_party/kissfft/LICENSES/BSD-3-Clause" "$prefix/install/LICENSE.kissfft"
  patchelf --set-rpath '$ORIGIN' "$prefix/install/lib/$(basename "$lib")"
fi
echo "Built $(basename "$lib"). Configure Sessio with -DSESSIO_LITERT_DIR=$prefix/install"
