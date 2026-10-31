#!/usr/bin/env bash
set -euo pipefail

# Instrument the actual capture reader and source lifecycle, keeping SDK/Qt
# binaries external. Usage: script <configured-build> <dependency-prefix> <curl>
repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=$(realpath "${1:?configured build directory required}")
dependency_prefix=$(realpath "${2:?dependency prefix required}")
curl_library=$(realpath "${3:?compatible libcurl required}")
sdk_dir=$(find "$build_dir/_deps/livekit-sdk" -mindepth 1 -maxdepth 1 -type d \
    -name 'livekit-sdk-*' -print -quit)
test_dir="$build_dir/microphone-sanitized"
mkdir -p "$test_dir"
read -r -a qt_cflags <<< "$(pkg-config --cflags Qt6Core Qt6Multimedia)"
read -r -a qt_libs <<< "$(pkg-config --libs Qt6Core Qt6Multimedia)"
moc_path="$(pkg-config --variable=libexecdir Qt6Core)/moc"
"$moc_path" "$repo_dir/src/video/audio_capture_adapter.h" -o "$test_dir/moc.cpp"
"${SESSIO_SANITIZER_CXX:-clang++}" -std=c++20 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer "${qt_cflags[@]}" \
    -I"$repo_dir/src/video" -I"$sdk_dir/include" -I"$dependency_prefix/include" \
    "$repo_dir/test/audio_capture_adapter_lifecycle_tests.cpp" \
    "$repo_dir/src/video/audio_capture_adapter.cpp" \
    "$repo_dir/src/video/audio_chunker.cpp" "$test_dir/moc.cpp" \
    -o "$test_dir/lifecycle" "${qt_libs[@]}" "$dependency_prefix/lib/libgtest.a" \
    -L"$sdk_dir/lib" -Wl,-rpath,"$sdk_dir/lib" -llivekit "$curl_library"
ASAN_OPTIONS=detect_leaks=0 \
LD_LIBRARY_PATH="$(dirname "$curl_library")${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$test_dir/lifecycle"
