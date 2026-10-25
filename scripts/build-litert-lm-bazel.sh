#!/usr/bin/env bash
# Builds libsessio_litert for Windows and macOS with LiteRT-LM's own Bazel
# build, the only build upstream supports on those platforms (Linux uses the
# CMake super-build in scripts/build-litert-lm.sh).
#
# GPU: like the Linux Vulkan runtime, the LiteRT C API is linked dynamically
# against upstream's prebuilt libLiteRt, whose WebGPU (Dawn) accelerator runs on
# Direct3D on Windows and Metal on macOS. All prebuilt libraries (SHA-checked
# against the v0.18.0 LFS pointers) ship next to libsessio_litert; on Windows
# also the DirectX shader compiler Dawn needs. Without a hardware GPU the app
# falls back to the CPU at load time.
#
# Requirements: bazelisk, Java 17, Rust, Python 3, git, curl; on Windows also
# Visual Studio 2022 and Git Bash (this script runs in it).
#
# Usage: scripts/build-litert-lm-bazel.sh [prefix]   (default ~/.cache/sessio-litert-lm-bazel)
# Env:   JOBS (default: Bazel's choice), BAZEL_OUTPUT_USER_ROOT (short path on Windows),
#        BAZEL_DISK_CACHE (optional disk cache directory).
# Result: <prefix>/install with bin/*.dll + lib/sessio_litert.lib (Windows)
#         or lib/*.dylib (macOS); ship every runtime library together.
set -euo pipefail

version=v0.18.0
prefix="${1:-$HOME/.cache/sessio-litert-lm-bazel}"
mkdir -p "$prefix"
prefix="$(cd "$prefix" && pwd)"
hook_dir="$(cd "$(dirname "$0")/../src/call_prompter/litert" && pwd)"

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    platform=windows; target=sessio_litert.dll; prebuilt_os=windows_x86_64
    prebuilt_libs=(libLiteRt.dll libLiteRtWebGpuAccelerator.dll libLiteRtTopKWebGpuSampler.dll libwebgpu_dawn.dll webgpu_dawn.dll) ;;
  Darwin)
    platform=macos; target=libsessio_litert.dylib; prebuilt_os=macos_arm64
    prebuilt_libs=(libLiteRt.dylib libLiteRtWebGpuAccelerator.dylib libLiteRtTopKWebGpuSampler.dylib libwebgpu_dawn.dylib) ;;
  *) echo "Use scripts/build-litert-lm.sh on $(uname -s)" >&2; exit 1 ;;
esac
sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }
# Downloads a file and fails unless it has the expected SHA-256.
fetch() {
  local url=$1 expected=$2 file=$3
  if [[ ! -f "$file" || "$(sha256 "$file")" != "$expected" ]]; then
    curl --fail --location --retry 2 --connect-timeout 20 --max-time 600 "$url" -o "$file"
  fi
  [[ "$(sha256 "$file")" == "$expected" ]] || { echo "SHA-256 mismatch: $file" >&2; exit 1; }
}

if [[ ! -d "$prefix/src/.git" ]]; then
  # Git LFS holds only prebuilt GPU accelerators, which the CPU build skips.
  GIT_LFS_SKIP_SMUDGE=1 git clone --depth 1 --branch "$version" \
    https://github.com/google-ai-edge/LiteRT-LM.git "$prefix/src"
fi
if [[ "$(git -C "$prefix/src" describe --tags --exact-match 2>/dev/null)" != "$version" ]]; then
  echo "$prefix/src is not LiteRT-LM $version" >&2
  exit 1
fi

# Our library is a package inside the LiteRT-LM workspace, linked against the
# same targets as upstream's litert_lm_main.
pkg="$prefix/src/sessio"
mkdir -p "$pkg/prebuilt"
cp -f "$hook_dir/sessio_litert.cpp" "$hook_dir/sessio_litert.h" "$hook_dir/task_gate.h" "$pkg/"

# Official prebuilt runtime and GPU accelerator of this release.
for lib in "${prebuilt_libs[@]}"; do
  pointer="$(git -C "$prefix/src" show "$version:prebuilt/$prebuilt_os/$lib")"
  expected="$(printf '%s\n' "$pointer" | sed -n 's/^oid sha256://p')"
  [[ "$expected" =~ ^[0-9a-f]{64}$ ]] || { echo "Invalid LFS pointer: $lib" >&2; exit 1; }
  fetch "https://media.githubusercontent.com/media/google-ai-edge/LiteRT-LM/$version/prebuilt/$prebuilt_os/$lib" \
    "$expected" "$pkg/prebuilt/$lib"
done
if [[ "$platform" == windows ]]; then
  # Linking libLiteRt.dll needs an import library, which upstream does not ship.
  vswhere="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
  libexe="$("$vswhere" -latest -products '*' -find 'VC/Tools/MSVC/**/bin/Hostx64/x64/lib.exe' | head -n 1 | tr -d '\r')"
  tools="$(dirname "$(cygpath -u "$libexe")")"
  {
    echo "LIBRARY libLiteRt.dll"
    echo "EXPORTS"
    "$tools/dumpbin.exe" -nologo -exports "$(cygpath -w "$pkg/prebuilt/libLiteRt.dll")" | tr -d '\r' |
      awk '/ordinal +hint +RVA +name/ { table = 1; next } table && NF >= 4 && $1 ~ /^[0-9]+$/ { print "  " $4 }'
  } > "$pkg/prebuilt/libLiteRt.def"
  [[ "$(grep -c LiteRt "$pkg/prebuilt/libLiteRt.def")" -gt 100 ]] || { echo "libLiteRt.dll exports not found" >&2; exit 1; }
  "$tools/lib.exe" -nologo -machine:x64 "-def:$(cygpath -w "$pkg/prebuilt/libLiteRt.def")" \
    "-out:$(cygpath -w "$pkg/prebuilt/libLiteRt.lib")"
  # Dawn's Direct3D 12 backend compiles shaders with DXC (same release as upstream's WORKSPACE).
  fetch https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2602/dxc_2026_02_20.zip \
    a1e89031421cf3c1fca6627766ab3020ca4f962ac7e2caa7fab2b33a8436151e "$prefix/dxc.zip"
  rm -rf "$prefix/dxc"
  py="$(command -v python3 || command -v python)"
  "$py" -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' \
    "$(cygpath -w "$prefix/dxc.zip")" "$(cygpath -w "$prefix/dxc")"
fi
cat > "$pkg/exported_symbols.txt" <<'EOF'
__ZN6sessio6litert*
__ZNK6sessio6litert*
__ZTIN6sessio6litert*
__ZTSN6sessio6litert*
__ZTVN6sessio6litert*
EOF
cat > "$pkg/BUILD" <<'EOF'
# Generated by Sessio's scripts/build-litert-lm-bazel.sh.
SRCS = ["sessio_litert.cpp", "sessio_litert.h", "task_gate.h"]

# Import libraries for the prebuilt runtime's dynamic LiteRT C API.
cc_import(
    name = "litert_runtime_windows",
    interface_library = "prebuilt/libLiteRt.lib",
    # The install step supplies the prebuilt DLL. Upstream also stages a
    # libLiteRt.dll on Windows, so do not ask Bazel to copy a second one.
    system_provided = True,
)

cc_import(
    name = "litert_runtime_macos",
    shared_library = "prebuilt/libLiteRt.dylib",
)

DEPS = [
    "//runtime/engine:engine_factory",
    "//runtime/engine:engine_impl_selected",
    "//runtime/engine:engine_interface",
    "//runtime/engine:engine_settings",
    "//runtime/engine:io_types",
    "//runtime/executor:executor_settings_base",
    "//runtime/executor:llm_executor_settings",
    "//runtime/proto:sampler_params_cc_proto",
    "//runtime/util:logging",
    "@com_google_absl//absl/time",
]

cc_binary(
    name = "sessio_litert.dll",
    srcs = SRCS,
    local_defines = ["SESSIO_LITERT_BUILD", "SESSIO_LITERT_DXGI"],
    linkopts = ["dxgi.lib"],
    linkshared = True,
    deps = DEPS + [":litert_runtime_windows"],
)

cc_binary(
    name = "libsessio_litert.dylib",
    srcs = SRCS,
    copts = ["-fvisibility=hidden"],
    local_defines = ["SESSIO_LITERT_BUILD", "SESSIO_LITERT_APPLE_GPU"],
    additional_linker_inputs = ["exported_symbols.txt"],
    linkopts = [
        "-Wl,-exported_symbols_list,$(location exported_symbols.txt)",
        "-Wl,-install_name,@rpath/libsessio_litert.dylib",
        # The prebuilt runtime libraries ship next to this one.
        "-Wl,-rpath,@loader_path",
    ],
    linkshared = True,
    deps = DEPS + [":litert_runtime_macos"],
)
EOF

startup=()
[[ -n "${BAZEL_OUTPUT_USER_ROOT:-}" ]] && startup+=("--output_user_root=$BAZEL_OUTPUT_USER_ROOT")
args=(-c opt --deleted_packages=swift --define=litert_runtime_link_mode=dynamic)
[[ "$platform" == macos ]] && args+=(--config=macos_arm64 --copt=-DLITERT_USE_WEBGPU_ACCELERATOR)
[[ "$platform" == windows ]] && args+=(--output_groups=+interface_library --copt=/DLITERT_USE_WEBGPU_ACCELERATOR)
[[ -n "${JOBS:-}" ]] && args+=("--jobs=$JOBS")
[[ -n "${BAZEL_DISK_CACHE:-}" ]] && args+=("--disk_cache=$BAZEL_DISK_CACHE" "--repository_cache=$BAZEL_DISK_CACHE/repo")

cd "$prefix/src"
# Git Bash would rewrite //labels and /flags as Windows paths.
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'
bazelisk ${startup[@]+"${startup[@]}"} build "${args[@]}" "//sessio:$target"
bin="$(bazelisk ${startup[@]+"${startup[@]}"} info "${args[@]}" bazel-bin)/sessio"

rm -rf "$prefix/install"
mkdir -p "$prefix/install/lib"
if [[ "$platform" == windows ]]; then
  mkdir -p "$prefix/install/bin"
  cp -f "$bin/sessio_litert.dll" "$pkg"/prebuilt/*.dll "$prefix/install/bin/"
  cp -f "$prefix/dxc/bin/x64/dxcompiler.dll" "$prefix/dxc/bin/x64/dxil.dll" "$prefix/install/bin/"
  implib="$(ls "$bin"/sessio_litert*.lib | head -n 1)"
  cp -f "$implib" "$prefix/install/lib/sessio_litert.lib"
  cp -f "$prefix/dxc/LICENSE-MIT.txt" "$prefix/install/LICENSE.dxc" 2>/dev/null || true
else
  lib="$prefix/install/lib"
  cp -f "$bin/libsessio_litert.dylib" "$pkg"/prebuilt/*.dylib "$lib/"
  chmod u+w "$lib"/*.dylib
  # Resolve the runtime through @rpath, whatever install name upstream gave it.
  runtime_id="$(otool -D "$lib/libLiteRt.dylib" | sed -n 2p)"
  if [[ "$runtime_id" != "@rpath/libLiteRt.dylib" ]]; then
    install_name_tool -id @rpath/libLiteRt.dylib "$lib/libLiteRt.dylib"
    install_name_tool -change "$runtime_id" @rpath/libLiteRt.dylib "$lib/libsessio_litert.dylib"
  fi
  for dylib in "$lib"/*.dylib; do
    install_name_tool -add_rpath @loader_path "$dylib" 2>/dev/null || true
    codesign --force --sign - "$dylib"
  done
fi
cp -f "$prefix/src/LICENSE" "$prefix/install/LICENSE.litert-lm"
echo "Built $target. Configure Sessio with -DSESSIO_LITERT_DIR=$prefix/install"
