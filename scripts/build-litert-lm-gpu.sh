#!/usr/bin/env bash
# Incremental Vulkan runtime assembly from the pinned v0.18.0 CPU archive tree.
# Compiles the CURRENT Sessio shim and WebGPU options object, never an app build
# or a multi-hour upstream superbuild. Run inside Ubuntu distrobox.
# Usage: bash scripts/build-litert-lm-gpu.sh [GPU root] [existing CPU root]
# Optional PREBUILT_DIR supplies unmodified official .so files (SHA checked).
set -euo pipefail
task_root=$(realpath -m "${1:-$HOME/.cache/sessio-litert-lm-gpu}")
task_cpu=$(realpath -m "${2:-$HOME/.cache/sessio-litert-lm}")
task_repo=$(cd -- "$(dirname -- "$0")/.." && pwd)
test "$task_root" != "$task_cpu" || { echo 'GPU and CPU roots must differ' >&2; exit 1; }
for task_tool in cmake curl sha256sum patchelf; do command -v "$task_tool" >/dev/null; done
test "$(uname -sm)" = 'Linux x86_64' || { echo 'This prebuilt set is Linux x86_64 only' >&2; exit 1; }
test "$(git -C "$task_cpu/src" describe --tags --exact-match)" = v0.18.0
task_build=$task_cpu/build/litert_lm/build
test -f "$task_build/CMakeFiles/sessio_litert.dir/link.txt" || {
  echo 'A completed CPU archive tree is required; this script never starts a superbuild' >&2; exit 1;
}
mkdir -p "$task_root/prebuilt" "$task_root/build" "$task_root/install/lib" "$task_root/install/include"
sha256sum "$task_cpu/install/lib/libsessio_litert.so" > "$task_root/build/cpu-install-before.sha256"
: > "$task_root/prebuilt/SHA256SUMS"
for task_lib in libLiteRt.so libLiteRtWebGpuAccelerator.so libLiteRtTopKWebGpuSampler.so libwebgpu_dawn.so libGemmaModelConstraintProvider.so; do
  task_pointer=$(git -C "$task_cpu/src" show "v0.18.0:prebuilt/linux_x86_64/$task_lib")
  task_expected=$(printf '%s\n' "$task_pointer" | sed -n 's/oid sha256://p')
  [[ "$task_expected" =~ ^[0-9a-f]{64}$ ]] || { echo "Invalid release pointer: $task_lib" >&2; exit 1; }
  task_file=$task_root/prebuilt/$task_lib
  if ! test -f "$task_file" || ! printf '%s  %s\n' "$task_expected" "$task_file" | sha256sum -c --status; then
    if test -n "${PREBUILT_DIR:-}"; then
      cp -- "$PREBUILT_DIR/$task_lib" "$task_file"
    else
      curl --fail --location --proto '=https' --proto-redir '=https' --retry 1 --connect-timeout 20 --max-time 180 \
        "https://media.githubusercontent.com/media/google-ai-edge/LiteRT-LM/v0.18.0/prebuilt/linux_x86_64/$task_lib" -o "$task_file"
    fi
  fi
  printf '%s  %s\n' "$task_expected" "$task_file" | sha256sum --check
  printf '%s  %s\n' "$task_expected" "$task_lib" >> "$task_root/prebuilt/SHA256SUMS"
  # Verify original bytes BEFORE patching ELF metadata; retain both manifests.
  cp -- "$task_file" "$task_root/install/lib/$task_lib"
  patchelf --set-rpath '$ORIGIN' "$task_root/install/lib/$task_lib"
done
cmake "-DSESSIO_GPU_ROOT=$task_root" "-DSESSIO_CPU_BUILD=$task_build" \
  "-DSESSIO_REPO=$task_repo" -P "$task_repo/src/call_prompter/litert/sessio_litert_gpu.cmake"
cp -a "$task_cpu/install/lib/"libkissfft*.so* "$task_root/install/lib/"
cp "$task_repo/src/call_prompter/litert/sessio_litert.h" "$task_root/install/include/"
cp "$task_cpu/src/LICENSE" "$task_root/install/LICENSE.litert-lm"
cp "$task_cpu/install/LICENSE.kissfft" "$task_root/install/LICENSE.kissfft"
patchelf --set-rpath '$ORIGIN' "$task_root/install/lib/libsessio_litert.so"
(cd "$task_root/install/lib" && sha256sum *.so* > ../SHA256SUMS.installed)
sha256sum --check "$task_root/build/cpu-install-before.sha256"
echo "GPU runtime: $task_root/install"
echo "Configure a NEW app build with -DSESSIO_LITERT_DIR=$task_root/install (rebuild consumers of sessio_litert.h)."
echo 'Ship every install/lib/*.so* together; all runtime/plugin/Dawn RPATHs use $ORIGIN.'
