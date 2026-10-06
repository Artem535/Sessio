# Call Transcription Engine (phases 0–1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `src/transcription/` — a Qt-free module that turns call audio (48 kHz mono int16, several tracks) into timed Russian phrases using GigaAM v3 + Silero VAD through sherpa-onnx, with the library and the model built, fetched and installed through CMake on Linux, Windows and macOS (builds in CI on all three; behaviour on Windows and macOS is tested by hand by the user).

**Architecture:** Pure units behind interfaces (`Resampler`, `PhraseSegmenter` over `IVoiceActivityDetector`, `ISpeechRecognizer`) feed a `TranscriptionEngine` with one segmenter thread and one decode worker, so ten tracks cost ten VADs and one recogniser. The sherpa-onnx implementations live in a separate static library so the pure library and its tests never link the ONNX runtime. sherpa-onnx is built from source with `FetchContent` (the prebuilt Linux tarballs use the old `std::string` ABI); model files are downloaded at build time, hash-pinned, and installed as plain data files.

**Tech Stack:** C++20, CMake 3.28, GoogleTest, sherpa-onnx v1.13.8 (C++ API), GigaAM v3 RNN-T (int8), Silero VAD.

Spec: `docs/superpowers/specs/2026-10-06-call-transcription-design.md`. Measurements: `docs/asciidoc/15-realtime-asr-spike.adoc`.

## Global Constraints

- Issue #118, branch `feat/118-call-transcription`, worktree `.worktrees/transcription`. Never develop on `main`.
- C++20, two-space indentation, `PascalCase` classes, `camelCase` methods, `snake_case` data fields (as the existing code). Namespace `pcm::transcription`.
- The module has no dependency on Qt widgets. These two phases add no Qt dependency at all (callbacks are `std::function`; the Qt signal wrapper arrives in phase 3).
- Engine: GigaAM v3 RNN-T, `model_type = "nemo_transducer"`, `sample_rate 16000`, `feature_dim 80`, `num_threads 4`; Silero VAD threshold 0.5, minimum silence 0.4 s, minimum speech 0.25 s, maximum phrase 20 s (the VAD enforces the split), window 512 samples, VAD buffer 120 s.
- Threads: `pushAudio` only copies into a per-track queue under a short lock; one segmenter thread; one decode worker (a shared recogniser is used from that worker only).
- Decode queue older than 10 s ⇒ "delayed" notice; phrases are never discarded silently. `stop()` drains for at most 5 s.
- Logs contain counts, durations and state only — no text, names or audio.
- sherpa-onnx: `FetchContent`, tag `v1.13.8`, `GIT_SHALLOW`, shared libs; Python, tests, check, PortAudio, WebSocket, binaries, TTS, speaker diarization, GPU OFF; C API ON; C API examples OFF. Added with `EXCLUDE_FROM_ALL` so its own install rules stay out of ours.
- `SESSIO_ENABLE_TRANSCRIPTION`: default ON on every platform (user decision: the build must work on Linux, Windows and macOS; the user tests runtime behaviour on Windows and macOS by hand). OFF is an escape hatch: no sherpa, no model download, no module. Windows and macOS CI jobs must build with it ON; a failure there is a blocker for this branch, not something to switch off.
- Model files are plain data files, never Qt resources. Install locations: Linux `${CMAKE_INSTALL_DATADIR}/sessio/models` (lowercase, as the existing `sessio/zoneinfo`; the spec's `Sessio/` capitalisation is corrected in Task 1), macOS `Sessio.app/Contents/Resources/models`, Windows `models` beside the executable (installed by CMake into the install tree, and picked up by the installer step in Task 9). Shared libraries: Linux `${CMAKE_INSTALL_LIBDIR}/sessio`; Windows DLLs beside `Sessio.exe`; macOS `Sessio.app/Contents/Frameworks`. Layout: `<root>/gigaam-v3-rnnt/{encoder.int8.onnx,decoder.onnx,joiner.onnx,tokens.txt}` and `<root>/silero_vad.onnx`.
- Pinned SHA-256 (files, not archives):
  - `encoder.int8.onnx` `0ec54e5f130a91c0f228d21795ede10b31c992a1f73e45dcd827e843a71a9b50`
  - `decoder.onnx` `5d1cfef155d8e07f213bd2f3229b5f6c5c29355c7fa5c1b9eb62cb1925725254`
  - `joiner.onnx` `fd1d02f45c2ad3d6b67cc149811ad794ab4b020ed49a0a9e2790a8619d1cddd8`
  - `tokens.txt` `17cc514451bcceac9c280068c71502f8448f99e9fb1456b8d0761651fd0392f2`
  - `silero_vad.onnx` `9e2449e1087496d8d4caba907f23e0bd3f78d91fa552479bb9c23ac09cbb1fd6`
  - `test_wavs/example.wav` `d8aaaa18a5098d7c6de0595ae7ac1e64cacd0d4022af3595213bdaf23be77e69`
- Downloads: `https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-nemo-transducer-giga-am-v3-russian-2025-12-16.tar.bz2` and `.../asr-models/silero_vad.onnx`.
- Build environment: the container cannot build; run builds on the host with `distrobox-host-exec`. Reuse the prebuilt vcpkg tree (`~/Soft/vcpkg`, installed tree from `.claude/worktrees/videoprovider-domain-layer/build-release/vcpkg_installed`). Link with `--parallel 3` (more overflows tmpfs). Run `ctest` with `-j1` if `SingleInstanceGuard` tests fail (socket race, unrelated).
- Before each commit: `git diff --check`. Commit messages: short imperative, end with the Co-Authored-By line from the session reminder.
- Windows and macOS: only the build and packaging are automated here. Local commands in this plan are Linux; the Windows/macOS equivalents run in CI. Do not claim they work at runtime — the user verifies.

## File Structure

```
cmake/Transcription.cmake                 option, FetchContent, model target, install helper
cmake/FetchTranscriptionModels.cmake      cmake -P script: download, verify, lay out the model files
src/transcription/CMakeLists.txt          Sessio_transcription (pure) + Sessio_transcription_sherpa
src/transcription/transcription_types.h   TrackId, TrackRole, TrackInfo, TranscribedPhrase, constants
src/transcription/resampler.{h,cpp}       N×16 kHz int16 -> 16 kHz float, stateful FIR decimator
src/transcription/voice_activity.h        IVoiceActivityDetector, SpeechSegment
src/transcription/phrase_segmenter.{h,cpp}  window the stream, drain closed segments
src/transcription/speech_recognizer.h     ISpeechRecognizer
src/transcription/model_locator.{h,cpp}   find model files per platform
src/transcription/transcription_engine.{h,cpp}  tracks, threads, queues, stats
src/transcription/sherpa_backend.{h,cpp}  sherpa VAD + recogniser (links sherpa-onnx-cxx-api)
test/transcription_resampler_tests.cpp
test/transcription_segmenter_tests.cpp
test/transcription_locator_tests.cpp
test/transcription_engine_tests.cpp       fakes, no model
test/transcription_model_tests.cpp        real model; skipped without model; ctest label `models`
test/transcription_test_support.h         FakeVad, FakeRecognizer, audio builders, waitFor
```

Modify: root `CMakeLists.txt` (include `cmake/Transcription.cmake`, `add_subdirectory(src/transcription)`, install rules), `test/CMakeLists.txt`, `.github/workflows/cmake-multi-platform.yml`, `packaging/Sessio.iss`, the spec (path, platform scope).

---

### Task 1: CMake option, sherpa-onnx FetchContent and an empty module (phase 0, part 1)

**Files:**
- Create: `cmake/Transcription.cmake`, `src/transcription/CMakeLists.txt`, `src/transcription/transcription_types.h`
- Modify: `CMakeLists.txt` (after `option(PCM_BUILD_TESTS ...)` line 56 and in the `add_subdirectory` list after `src/video`), spec model path

**Interfaces:**
- Produces: CMake option `SESSIO_ENABLE_TRANSCRIPTION`; variable `SESSIO_TRANSCRIPTION_ENABLED` (bool); target `Sessio_transcription` (static, include dir `src/transcription` PUBLIC); targets `sherpa-onnx-cxx-api`, `sherpa-onnx-c-api`; variable `SESSIO_SHERPA_SOURCE_DIR`; `transcription_types.h` types below.

- [ ] **Step 1: Write the option and FetchContent module**

`cmake/Transcription.cmake`:

```cmake
# Live call transcription: sherpa-onnx (built from source) plus the GigaAM and
# Silero VAD model files. ON on every platform.
option(SESSIO_ENABLE_TRANSCRIPTION "Build live call transcription (sherpa-onnx + models)" ON)
set(SESSIO_TRANSCRIPTION_ENABLED ${SESSIO_ENABLE_TRANSCRIPTION})

if(NOT SESSIO_ENABLE_TRANSCRIPTION)
  return()
endif()

include(FetchContent)

# The prebuilt Linux sherpa-onnx tarballs use the old std::string ABI and cannot
# be used with cxx-api.h, so build from source on every platform.
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
foreach(_opt PYTHON TESTS CHECK PORTAUDIO WEBSOCKET BINARY TTS SPEAKER_DIARIZATION GPU)
  set(SHERPA_ONNX_ENABLE_${_opt} OFF CACHE BOOL "" FORCE)
endforeach()
set(SHERPA_ONNX_ENABLE_C_API ON CACHE BOOL "" FORCE)
set(SHERPA_ONNX_BUILD_C_API_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(sherpa_onnx
  GIT_REPOSITORY https://github.com/k2-fsa/sherpa-onnx.git
  GIT_TAG v1.13.8
  GIT_SHALLOW TRUE
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(sherpa_onnx)
set(SESSIO_SHERPA_SOURCE_DIR "${sherpa_onnx_SOURCE_DIR}")
```

Pitfall: `BUILD_SHARED_LIBS ON` as a cache variable would turn every later library in the project shared. Wrap the sherpa block so it is restored:

```cmake
set(_sessio_saved_shared "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS ON)            # normal variable, not cache: scoped to this block
FetchContent_MakeAvailable(sherpa_onnx)
set(BUILD_SHARED_LIBS "${_sessio_saved_shared}")
```

Use this form (a normal variable, no `CACHE`) instead of the cache line above.

- [ ] **Step 2: Types and the module CMake**

`src/transcription/transcription_types.h`:

```cpp
#pragma once

#include <cstdint>
#include <string>

namespace pcm::transcription {

using TrackId = std::string;

enum class TrackRole { Practitioner, Participant };

struct TrackInfo {
  TrackId id;
  TrackRole role = TrackRole::Participant;
  std::string display_name;
};

struct TranscribedPhrase {
  TrackId track_id;
  TrackRole role = TrackRole::Participant;
  std::string speaker_name;
  int64_t start_ms = 0;  // from the start of the call
  int64_t end_ms = 0;
  std::string text;
};

constexpr int kRecognizerSampleRate = 16000;

}  // namespace pcm::transcription
```

`src/transcription/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME ${PROJECT_NAME}_transcription)

find_package(Threads REQUIRED)

add_library(${TARGET_NAME} STATIC
  transcription_types.h
)
set_target_properties(${TARGET_NAME} PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${TARGET_NAME} PUBLIC Threads::Threads)
```

(Later tasks append sources to this file; the empty-library `LINKER_LANGUAGE` line is removed in Task 2.)

- [ ] **Step 3: Wire into the root CMake**

In `CMakeLists.txt` directly after `option(PCM_BUILD_TESTS ...)` add `include(cmake/Transcription.cmake)`. After `add_subdirectory(src/video)` add:

```cmake
if(SESSIO_TRANSCRIPTION_ENABLED)
  add_subdirectory(src/transcription)
endif()
```

- [ ] **Step 4: Correct the spec path and configure**

In the spec replace ``Linux `share/Sessio/models/` `` with ``Linux `share/sessio/models/` (lowercase, as `sessio/zoneinfo`)``.

Run on the host:

```bash
distrobox-host-exec cmake --preset vcpkg-release
```

Expected: configure succeeds; `build-release/_deps/sherpa_onnx-src` exists; log shows ONNX Runtime downloaded. If ONNX Runtime download needs hash pinning, see Task 3.

- [ ] **Step 5: Build the two sherpa targets**

```bash
distrobox-host-exec cmake --build build-release --target sherpa-onnx-cxx-api --parallel 3
```

Expected: `build-release/lib/libsherpa-onnx-c-api.so` and `libsherpa-onnx-cxx-api.so` exist, and `build-release/_deps/onnxruntime-src/lib/libonnxruntime.so` exists (these are the locations seen in the spike).

- [ ] **Step 6: Commit**

```bash
git add cmake/Transcription.cmake src/transcription CMakeLists.txt docs/superpowers/specs
git commit -m "Add SESSIO_ENABLE_TRANSCRIPTION option and sherpa-onnx FetchContent (#118)"
```

---

### Task 2: Resampler

**Files:**
- Create: `src/transcription/resampler.h`, `src/transcription/resampler.cpp`, `test/transcription_resampler_tests.cpp`
- Modify: `src/transcription/CMakeLists.txt`, `test/CMakeLists.txt`

**Interfaces:**
- Produces: `class Resampler { explicit Resampler(int input_rate); std::vector<float> process(std::span<const int16_t> in); }` — throws `std::invalid_argument` unless `input_rate > 0 && input_rate % 16000 == 0`; output is 16 kHz float in [-1, 1); stateful across calls; rate 16000 is a pass-through that only scales by `1/32768`.

- [ ] **Step 1: Write the failing tests**

`test/transcription_resampler_tests.cpp`:

```cpp
#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <vector>

#include "resampler.h"

using pcm::transcription::Resampler;

namespace {

std::vector<int16_t> sine(double hz, int rate, int samples, double amplitude) {
  std::vector<int16_t> out(samples);
  for (int i = 0; i < samples; ++i) {
    out[i] = static_cast<int16_t>(amplitude * std::sin(2.0 * std::numbers::pi * hz * i / rate));
  }
  return out;
}

double rms(const std::vector<float>& v, size_t from) {
  double sum = 0;
  for (size_t i = from; i < v.size(); ++i) sum += static_cast<double>(v[i]) * v[i];
  return std::sqrt(sum / static_cast<double>(v.size() - from));
}

}  // namespace

TEST(ResamplerTest, OutputLengthIsInputOverRatio) {
  Resampler r(48000);
  EXPECT_EQ(r.process(std::vector<int16_t>(480, 0)).size(), 160u);
  EXPECT_EQ(r.process(std::vector<int16_t>(4800, 0)).size(), 1600u);
}

TEST(ResamplerTest, PreservesConstantSignal) {
  Resampler r(48000);
  const auto out = r.process(std::vector<int16_t>(4800, 10000));
  EXPECT_NEAR(out.back(), 10000.0f / 32768.0f, 1e-3);
}

TEST(ResamplerTest, PassesOneKilohertzAndRejectsTwelveKilohertz) {
  Resampler low(48000);
  const auto kept = low.process(sine(1000, 48000, 9600, 10000));
  EXPECT_NEAR(rms(kept, 800), 10000.0 / 32768.0 / std::sqrt(2.0), 0.015);

  Resampler high(48000);
  const auto cut = high.process(sine(12000, 48000, 9600, 10000));
  EXPECT_LT(rms(cut, 800), 0.01 * 10000.0 / 32768.0);
}

TEST(ResamplerTest, ChunkingDoesNotChangeOutput) {
  std::vector<int16_t> input = sine(440, 48000, 4801, 8000);
  Resampler whole(48000);
  const auto expected = whole.process(input);

  Resampler pieces(48000);
  std::vector<float> got;
  for (size_t i = 0; i < input.size(); i += 7) {
    const size_t n = std::min<size_t>(7, input.size() - i);
    const auto part = pieces.process(std::span<const int16_t>(input.data() + i, n));
    got.insert(got.end(), part.begin(), part.end());
  }
  ASSERT_EQ(got.size(), expected.size());
  for (size_t i = 0; i < got.size(); ++i) EXPECT_FLOAT_EQ(got[i], expected[i]);
}

TEST(ResamplerTest, SixteenKilohertzIsPassThrough) {
  Resampler r(16000);
  const auto out = r.process(std::vector<int16_t>{0, 16384, -16384});
  ASSERT_EQ(out.size(), 3u);
  EXPECT_FLOAT_EQ(out[1], 0.5f);
  EXPECT_FLOAT_EQ(out[2], -0.5f);
}

TEST(ResamplerTest, RejectsRatesThatAreNotMultiplesOfSixteenKilohertz) {
  EXPECT_THROW(Resampler(44100), std::invalid_argument);
  EXPECT_THROW(Resampler(0), std::invalid_argument);
}
```

In `test/CMakeLists.txt` append (inside no extra guard; the whole `test/` directory is added only with `PCM_BUILD_TESTS`, so guard on the module):

```cmake
if(SESSIO_TRANSCRIPTION_ENABLED)
  add_executable(Sessio_transcription_tests
      transcription_resampler_tests.cpp
  )
  target_link_libraries(Sessio_transcription_tests PRIVATE
      GTest::gtest
      GTest::gtest_main
      Sessio_transcription
  )
  gtest_discover_tests(Sessio_transcription_tests)
endif()
```

- [ ] **Step 2: Run to verify it fails**

```bash
distrobox-host-exec cmake -S . -B build-release -DPCM_BUILD_TESTS=ON
distrobox-host-exec cmake --build build-release --target Sessio_transcription_tests --parallel 3
```

Expected: FAIL — `resampler.h` not found.

- [ ] **Step 3: Implement**

`src/transcription/resampler.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace pcm::transcription {

// Converts mono int16 audio at a multiple of 16 kHz (the call delivers 48 kHz)
// to 16 kHz float for the recogniser. Stateful: feed consecutive chunks of one
// stream; chunk boundaries do not change the output.
class Resampler {
 public:
  explicit Resampler(int input_rate);

  std::vector<float> process(std::span<const int16_t> input);

 private:
  int ratio_;
  std::vector<float> taps_;
  std::vector<float> history_;  // last taps_.size() - 1 input samples
  size_t next_ = 0;             // index in the next input of the next output sample
};

}  // namespace pcm::transcription
```

`src/transcription/resampler.cpp`:

```cpp
#include "resampler.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

#include "transcription_types.h"

namespace pcm::transcription {

namespace {

// Hamming-windowed sinc low-pass, cut off at 0.45 of the output Nyquist
// frequency's input-rate equivalent, normalised to unit gain at DC.
std::vector<float> makeTaps(int ratio) {
  if (ratio == 1) return {1.0f};
  const int count = 32 * ratio + 1;
  const int middle = (count - 1) / 2;
  const double cutoff = 0.45 / ratio;  // cycles per input sample
  std::vector<double> h(count);
  double sum = 0;
  for (int j = 0; j < count; ++j) {
    const double x = j - middle;
    const double arg = 2.0 * std::numbers::pi * cutoff * x;
    const double sinc = x == 0 ? 1.0 : std::sin(arg) / arg;
    const double window = 0.54 - 0.46 * std::cos(2.0 * std::numbers::pi * j / (count - 1));
    h[j] = 2.0 * cutoff * sinc * window;
    sum += h[j];
  }
  std::vector<float> taps(count);
  for (int j = 0; j < count; ++j) taps[j] = static_cast<float>(h[j] / sum);
  return taps;
}

}  // namespace

Resampler::Resampler(int input_rate) {
  if (input_rate <= 0 || input_rate % kRecognizerSampleRate != 0) {
    throw std::invalid_argument("Resampler: input rate must be a multiple of 16000");
  }
  ratio_ = input_rate / kRecognizerSampleRate;
  taps_ = makeTaps(ratio_);
  history_.assign(taps_.size() - 1, 0.0f);
}

std::vector<float> Resampler::process(std::span<const int16_t> input) {
  const size_t history = taps_.size() - 1;
  std::vector<float> buffer;
  buffer.reserve(history + input.size());
  buffer.insert(buffer.end(), history_.begin(), history_.end());
  for (const int16_t s : input) buffer.push_back(static_cast<float>(s) / 32768.0f);

  std::vector<float> out;
  out.reserve(input.size() / ratio_ + 1);
  for (; next_ < input.size(); next_ += ratio_) {
    // Newest sample for this output is input[next_], at buffer[history + next_].
    float acc = 0.0f;
    const float* newest = buffer.data() + history + next_;
    for (size_t j = 0; j < taps_.size(); ++j) acc += taps_[j] * newest[-static_cast<ptrdiff_t>(j)];
    out.push_back(acc);
  }
  next_ -= input.size();
  history_.assign(buffer.end() - static_cast<ptrdiff_t>(history), buffer.end());
  return out;
}

}  // namespace pcm::transcription
```

In `src/transcription/CMakeLists.txt` replace the `add_library` source list and remove the `LINKER_LANGUAGE` line:

```cmake
add_library(${TARGET_NAME} STATIC
  transcription_types.h
  resampler.h
  resampler.cpp
)
```

- [ ] **Step 4: Run to verify it passes**

```bash
distrobox-host-exec cmake --build build-release --target Sessio_transcription_tests --parallel 3
distrobox-host-exec ctest --test-dir build-release -R ResamplerTest --output-on-failure
```

Expected: 6 tests PASS. If `ChunkingDoesNotChangeOutput` fails with tiny differences, the accumulation order differs between runs — it must not, as each output uses the same loop; fix the indexing instead of loosening the test.

- [ ] **Step 5: Commit**

```bash
git add src/transcription test/CMakeLists.txt test/transcription_resampler_tests.cpp
git commit -m "Add 16 kHz resampler for call audio (#118)"
```

---

### Task 3: Model fetch script, pinned hashes, and install rules (phase 0, part 2)

**Files:**
- Create: `cmake/FetchTranscriptionModels.cmake`
- Modify: `cmake/Transcription.cmake`, `CMakeLists.txt` (install rules next to the zoneinfo install, ~line 262)

**Interfaces:**
- Produces: CMake variable `SESSIO_MODELS_DIR` (build-tree directory with the layout from Global Constraints); target `sessio_models` (part of `ALL`); install rules for the models and for the sherpa-onnx/ONNX Runtime shared libraries into `${CMAKE_INSTALL_LIBDIR}/sessio`.

- [ ] **Step 1: Write the fetch script**

`cmake/FetchTranscriptionModels.cmake` (run as `cmake -DMODELS_DIR=<dir> -P ...`):

```cmake
# Downloads the GigaAM v3 RNN-T model and Silero VAD, verifies each file against a
# pinned SHA-256 and lays them out as:
#   <MODELS_DIR>/silero_vad.onnx
#   <MODELS_DIR>/gigaam-v3-rnnt/{encoder.int8.onnx,decoder.onnx,joiner.onnx,tokens.txt}
#   <MODELS_DIR>/gigaam-v3-rnnt/test_wavs/example.wav   (used by the model tests)
if(NOT MODELS_DIR)
  message(FATAL_ERROR "MODELS_DIR is required")
endif()

set(_base "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models")
set(_archive "sherpa-onnx-nemo-transducer-giga-am-v3-russian-2025-12-16")
set(_model "${MODELS_DIR}/gigaam-v3-rnnt")

# relative path inside _model -> sha256
set(_files
  "encoder.int8.onnx|0ec54e5f130a91c0f228d21795ede10b31c992a1f73e45dcd827e843a71a9b50"
  "decoder.onnx|5d1cfef155d8e07f213bd2f3229b5f6c5c29355c7fa5c1b9eb62cb1925725254"
  "joiner.onnx|fd1d02f45c2ad3d6b67cc149811ad794ab4b020ed49a0a9e2790a8619d1cddd8"
  "tokens.txt|17cc514451bcceac9c280068c71502f8448f99e9fb1456b8d0761651fd0392f2"
  "test_wavs/example.wav|d8aaaa18a5098d7c6de0595ae7ac1e64cacd0d4022af3595213bdaf23be77e69")
set(_vad_sha "9e2449e1087496d8d4caba907f23e0bd3f78d91fa552479bb9c23ac09cbb1fd6")

function(_file_ok path sha result_var)
  set(${result_var} FALSE PARENT_SCOPE)
  if(EXISTS "${path}")
    file(SHA256 "${path}" _actual)
    if(_actual STREQUAL sha)
      set(${result_var} TRUE PARENT_SCOPE)
    endif()
  endif()
endfunction()

set(_model_ok TRUE)
foreach(_entry IN LISTS _files)
  string(REPLACE "|" ";" _parts "${_entry}")
  list(GET _parts 0 _rel)
  list(GET _parts 1 _sha)
  _file_ok("${_model}/${_rel}" "${_sha}" _ok)
  if(NOT _ok)
    set(_model_ok FALSE)
  endif()
endforeach()

if(NOT _model_ok)
  file(MAKE_DIRECTORY "${MODELS_DIR}")
  set(_tarball "${MODELS_DIR}/${_archive}.tar.bz2")
  message(STATUS "Downloading GigaAM v3 RNN-T (about 160 MB)")
  file(DOWNLOAD "${_base}/${_archive}.tar.bz2" "${_tarball}" SHOW_PROGRESS STATUS _status)
  list(GET _status 0 _code)
  if(NOT _code EQUAL 0)
    file(REMOVE "${_tarball}")
    message(FATAL_ERROR "GigaAM download failed: ${_status}")
  endif()
  file(ARCHIVE_EXTRACT INPUT "${_tarball}" DESTINATION "${MODELS_DIR}/extract")
  file(REMOVE_RECURSE "${_model}")
  file(MAKE_DIRECTORY "${_model}/test_wavs")
  foreach(_entry IN LISTS _files)
    string(REPLACE "|" ";" _parts "${_entry}")
    list(GET _parts 0 _rel)
    list(GET _parts 1 _sha)
    file(COPY "${MODELS_DIR}/extract/${_archive}/${_rel}" DESTINATION "${_model}/")
  endforeach()
  file(REMOVE_RECURSE "${MODELS_DIR}/extract")
  file(REMOVE "${_tarball}")
endif()
```

Note `file(COPY ... DESTINATION)` with a nested relative path drops the subdirectory; copy test wav separately. Replace the copy loop body with:

```cmake
    get_filename_component(_dir "${_rel}" DIRECTORY)
    file(COPY "${MODELS_DIR}/extract/${_archive}/${_rel}" DESTINATION "${_model}/${_dir}")
```

and finish the script with verification and the VAD:

```cmake
foreach(_entry IN LISTS _files)
  string(REPLACE "|" ";" _parts "${_entry}")
  list(GET _parts 0 _rel)
  list(GET _parts 1 _sha)
  _file_ok("${_model}/${_rel}" "${_sha}" _ok)
  if(NOT _ok)
    message(FATAL_ERROR "Hash mismatch for ${_model}/${_rel} (expected ${_sha})")
  endif()
endforeach()

_file_ok("${MODELS_DIR}/silero_vad.onnx" "${_vad_sha}" _vad_ok)
if(NOT _vad_ok)
  message(STATUS "Downloading Silero VAD")
  file(DOWNLOAD "${_base}/silero_vad.onnx" "${MODELS_DIR}/silero_vad.onnx"
       EXPECTED_HASH SHA256=${_vad_sha} SHOW_PROGRESS)
endif()
```

(When `_dir` is empty, `file(COPY ... DESTINATION "${_model}/")` is the same as before.)

- [ ] **Step 2: Add the target and install rules**

Append to `cmake/Transcription.cmake`:

```cmake
set(SESSIO_MODELS_DIR "${CMAKE_BINARY_DIR}/transcription-models" CACHE PATH
    "Where the transcription model files are downloaded to (cache this in CI)")
set(_sessio_models_stamp "${SESSIO_MODELS_DIR}/.stamp")
add_custom_command(
  OUTPUT "${_sessio_models_stamp}"
  COMMAND ${CMAKE_COMMAND} -DMODELS_DIR=${SESSIO_MODELS_DIR}
          -P ${CMAKE_SOURCE_DIR}/cmake/FetchTranscriptionModels.cmake
  COMMAND ${CMAKE_COMMAND} -E touch "${_sessio_models_stamp}"
  DEPENDS ${CMAKE_SOURCE_DIR}/cmake/FetchTranscriptionModels.cmake
  COMMENT "Fetching transcription models"
  VERBATIM)
add_custom_target(sessio_models ALL DEPENDS "${_sessio_models_stamp}")

# Directory of ONNX Runtime fetched by sherpa-onnx.
FetchContent_GetProperties(onnxruntime SOURCE_DIR _sessio_ort_dir)
set(SESSIO_ONNXRUNTIME_LIB_DIR "${_sessio_ort_dir}/lib")
```

Pitfall: if `FetchContent_GetProperties(onnxruntime ...)` is empty, sherpa populated it with a different name; run `grep -n "FetchContent" build-release/_deps/sherpa_onnx-src/cmake/onnxruntime-linux-x86_64.cmake` and use that name; the spike's build located the library at `_deps/onnxruntime-src/lib/libonnxruntime.so`, so set `SESSIO_ONNXRUNTIME_LIB_DIR` to `${CMAKE_BINARY_DIR}/_deps/onnxruntime-src/lib` as a fallback.

In `CMakeLists.txt`, after the zoneinfo install block, add:

```cmake
if(SESSIO_TRANSCRIPTION_ENABLED)
  if(APPLE)
    set(_sessio_models_dest Sessio.app/Contents/Resources/models)
    set(_sessio_ml_libs_dest Sessio.app/Contents/Frameworks)
  elseif(WIN32)
    set(_sessio_models_dest ${CMAKE_INSTALL_BINDIR}/models)
    set(_sessio_ml_libs_dest ${CMAKE_INSTALL_BINDIR})
  else()
    set(_sessio_models_dest ${CMAKE_INSTALL_DATADIR}/sessio/models)
    set(_sessio_ml_libs_dest ${CMAKE_INSTALL_LIBDIR}/sessio)
  endif()
  install(DIRECTORY ${SESSIO_MODELS_DIR}/
      DESTINATION ${_sessio_models_dest}
      PATTERN ".stamp" EXCLUDE
      PATTERN "test_wavs" EXCLUDE)
  install(FILES
      $<TARGET_FILE:sherpa-onnx-c-api>
      $<TARGET_FILE:sherpa-onnx-cxx-api>
      DESTINATION ${_sessio_ml_libs_dest})
  # ONNX Runtime: libonnxruntime.so* (Linux), libonnxruntime*.dylib (macOS),
  # onnxruntime*.dll (Windows; sherpa places the DLL next to its own DLLs).
  file(GLOB _sessio_ort_libs
      "${SESSIO_ONNXRUNTIME_LIB_DIR}/libonnxruntime*.so*"
      "${SESSIO_ONNXRUNTIME_LIB_DIR}/libonnxruntime*.dylib"
      "${SESSIO_ONNXRUNTIME_LIB_DIR}/onnxruntime*.dll"
      "${SESSIO_ONNXRUNTIME_LIB_DIR}/../bin/onnxruntime*.dll")
  install(FILES ${_sessio_ort_libs} DESTINATION ${_sessio_ml_libs_dest})
endif()
```

Windows: `$<TARGET_FILE:...>` is the DLL for shared libraries, which is what is wanted. macOS: after the install, `qt_generate_deploy_app_script` may re-sign or rewrite rpaths; confirm in the first macOS CI run that `Sessio.app/Contents/Frameworks/libonnxruntime*.dylib` loads (`otool -L` on `libsherpa-onnx-c-api.dylib` must reference `@rpath/libonnxruntime...`; fix with `install_name_tool -add_rpath @loader_path` in an `install(CODE)` step if not).

- [ ] **Step 3: Run the fetch and check the layout**

```bash
distrobox-host-exec cmake --build build-release --target sessio_models
distrobox-host-exec find build-release/transcription-models -maxdepth 2 -type f
```

Expected: `silero_vad.onnx`, `.stamp`, `gigaam-v3-rnnt/{encoder.int8.onnx,decoder.onnx,joiner.onnx,tokens.txt}` and `gigaam-v3-rnnt/test_wavs/example.wav`. Run the target a second time: it must finish at once (stamp present, no download).

- [ ] **Step 4: Check the install tree**

```bash
distrobox-host-exec cmake --install build-release --prefix /tmp/sessio-install-check
distrobox-host-exec find /tmp/sessio-install-check -path '*sessio/*' \( -name '*.so*' -o -name '*.onnx' -o -name tokens.txt \)
```

Expected: the four model files and `silero_vad.onnx` under `share/sessio/models`, and `libsherpa-onnx-c-api.so`, `libsherpa-onnx-cxx-api.so`, `libonnxruntime.so*` under `lib/sessio` (or `lib64/sessio`). No `test_wavs`, no sherpa headers. Clean up `/tmp/sessio-install-check` afterwards.

- [ ] **Step 5: Runtime lookup of the bundled libraries**

The application must find `lib/sessio/*.so` at run time. Check how the executable's RPATH is set for the LiveKit private directory:

```bash
grep -rn "INSTALL_RPATH\|BUILD_RPATH" CMakeLists.txt src/app/CMakeLists.txt cmake
```

When `Sessio` already carries `$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/sessio` (set by `BundleLiveKitLinuxDeps.cmake` with patchelf), nothing more is needed. When not, in `src/app/CMakeLists.txt` add, guarded by `SESSIO_TRANSCRIPTION_ENABLED`:

```cmake
set_property(TARGET Sessio APPEND PROPERTY INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/sessio")
```

This is only verified end to end in Task 9's smoke test.

- [ ] **Step 6: Commit**

```bash
git add cmake CMakeLists.txt src/app/CMakeLists.txt
git commit -m "Fetch and install GigaAM, Silero VAD and sherpa-onnx libraries (#118)"
```

---

### Task 4: VAD interface and PhraseSegmenter

**Files:**
- Create: `src/transcription/voice_activity.h`, `src/transcription/phrase_segmenter.h`, `src/transcription/phrase_segmenter.cpp`, `test/transcription_test_support.h`, `test/transcription_segmenter_tests.cpp`
- Modify: `src/transcription/CMakeLists.txt`, `test/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct SpeechSegment { int64_t start_sample; std::vector<float> samples; }`
  - `class IVoiceActivityDetector { virtual size_t windowSize() const; virtual void accept(const float* window, size_t n); virtual bool hasSegment() const; virtual SpeechSegment popSegment(); virtual void flush(); }` — `accept` is called with exactly `windowSize()` samples; `start_sample` counts 16 kHz samples fed since the stream started.
  - `class PhraseSegmenter { explicit PhraseSegmenter(std::unique_ptr<IVoiceActivityDetector>); std::vector<SpeechSegment> feed(std::span<const float>); std::vector<SpeechSegment> flush(); }` — buffers a partial window between calls; `flush` pads the partial window with zeros, then flushes the VAD.
  - Test support (`transcription_test_support.h`): `FakeVad` (speech when the window's mean absolute value > 0.05), `FakeRecognizer`, `speech(ms)`, `silence(ms)`, `waitFor(pred, timeout)`.

- [ ] **Step 1: Write the interface and the test support**

`src/transcription/voice_activity.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pcm::transcription {

struct SpeechSegment {
  int64_t start_sample = 0;  // 16 kHz samples since the stream started
  std::vector<float> samples;
};

// Streaming voice-activity detector. One instance per track.
class IVoiceActivityDetector {
 public:
  virtual ~IVoiceActivityDetector() = default;

  virtual size_t windowSize() const = 0;
  // Exactly windowSize() samples at 16 kHz.
  virtual void accept(const float* window, size_t n) = 0;
  virtual bool hasSegment() const = 0;
  virtual SpeechSegment popSegment() = 0;
  // Closes a phrase that is still open (end of stream).
  virtual void flush() = 0;
};

}  // namespace pcm::transcription
```

`test/transcription_test_support.h`:

```cpp
#pragma once

#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "speech_recognizer.h"
#include "voice_activity.h"

namespace pcm::transcription::testing {

// 48 kHz int16 audio: constant 16000 for speech, zero for silence.
inline std::vector<int16_t> speech(int ms) { return std::vector<int16_t>(48 * ms, 16000); }
inline std::vector<int16_t> silence(int ms) { return std::vector<int16_t>(48 * ms, 0); }

inline std::vector<int16_t> concat(std::initializer_list<std::vector<int16_t>> parts) {
  std::vector<int16_t> out;
  for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
  return out;
}

// Speech when the window's mean absolute value exceeds 0.05; a phrase closes on
// the first window that is not speech.
class FakeVad : public IVoiceActivityDetector {
 public:
  size_t windowSize() const override { return 512; }

  void accept(const float* window, size_t n) override {
    double sum = 0;
    for (size_t i = 0; i < n; ++i) sum += std::fabs(window[i]);
    const bool is_speech = sum / static_cast<double>(n) > 0.05;
    if (is_speech) {
      if (!open_) {
        open_ = true;
        current_.start_sample = fed_;
        current_.samples.clear();
      }
      current_.samples.insert(current_.samples.end(), window, window + n);
    } else if (open_) {
      close();
    }
    fed_ += static_cast<int64_t>(n);
  }

  bool hasSegment() const override { return !ready_.empty(); }

  SpeechSegment popSegment() override {
    SpeechSegment s = std::move(ready_.front());
    ready_.pop_front();
    return s;
  }

  void flush() override {
    if (open_) close();
  }

 private:
  void close() {
    ready_.push_back(std::move(current_));
    current_ = {};
    open_ = false;
  }

  bool open_ = false;
  int64_t fed_ = 0;
  SpeechSegment current_;
  std::deque<SpeechSegment> ready_;
};

class FakeRecognizer : public ISpeechRecognizer {
 public:
  using Handler = std::function<std::string(int call, size_t samples)>;

  explicit FakeRecognizer(Handler handler = {}) : handler_(std::move(handler)) {}

  std::string transcribe(std::span<const float> samples) override {
    const int call = calls_++;
    if (handler_) return handler_(call, samples.size());
    return "phrase " + std::to_string(call);
  }

  int calls() const { return calls_; }

 private:
  Handler handler_;
  std::atomic<int> calls_{0};
};

template <typename Pred>
bool waitFor(Pred pred, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return pred();
}

}  // namespace pcm::transcription::testing
```

Add `#include <atomic>` at the top of the file (used by `FakeRecognizer`).

`src/transcription/speech_recognizer.h`:

```cpp
#pragma once

#include <span>
#include <string>

namespace pcm::transcription {

// Turns one finished phrase (16 kHz float mono) into text. Throws
// std::runtime_error when decoding fails. Called from the decode worker only.
class ISpeechRecognizer {
 public:
  virtual ~ISpeechRecognizer() = default;
  virtual std::string transcribe(std::span<const float> samples) = 0;
};

}  // namespace pcm::transcription
```

- [ ] **Step 2: Write the failing tests**

`test/transcription_segmenter_tests.cpp`:

```cpp
#include <gtest/gtest.h>

#include <memory>

#include "phrase_segmenter.h"
#include "transcription_test_support.h"

using namespace pcm::transcription;

namespace {

std::vector<float> floats(const std::vector<float>& v) { return v; }

std::vector<float> block(float value, size_t n) { return std::vector<float>(n, value); }

PhraseSegmenter makeSegmenter() {
  return PhraseSegmenter(std::make_unique<testing::FakeVad>());
}

}  // namespace

TEST(PhraseSegmenterTest, EmitsClosedPhraseWithStartSample) {
  auto seg = makeSegmenter();
  std::vector<float> audio = block(0.0f, 1024);
  const auto speech = block(0.5f, 1024);
  audio.insert(audio.end(), speech.begin(), speech.end());
  const auto quiet = block(0.0f, 512);
  audio.insert(audio.end(), quiet.begin(), quiet.end());

  const auto phrases = seg.feed(audio);
  ASSERT_EQ(phrases.size(), 1u);
  EXPECT_EQ(phrases[0].start_sample, 1024);
  EXPECT_EQ(phrases[0].samples.size(), 1024u);
}

TEST(PhraseSegmenterTest, BuffersPartialWindowsAcrossCalls) {
  auto seg = makeSegmenter();
  std::vector<float> all = block(0.5f, 1024);
  const auto quiet = block(0.0f, 512);
  all.insert(all.end(), quiet.begin(), quiet.end());

  std::vector<SpeechSegment> got;
  for (size_t i = 0; i < all.size(); i += 100) {
    const size_t n = std::min<size_t>(100, all.size() - i);
    auto part = seg.feed(std::span<const float>(all.data() + i, n));
    got.insert(got.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
  }
  ASSERT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0].start_sample, 0);
}

TEST(PhraseSegmenterTest, FlushClosesOpenPhraseAndPadsPartialWindow) {
  auto seg = makeSegmenter();
  EXPECT_TRUE(seg.feed(block(0.5f, 700)).empty());  // one full window + 188 pending
  const auto phrases = seg.flush();
  ASSERT_EQ(phrases.size(), 1u);
  EXPECT_EQ(phrases[0].samples.size(), 1024u);  // second window zero-padded
}

TEST(PhraseSegmenterTest, FlushWithNothingOpenReturnsNothing) {
  auto seg = makeSegmenter();
  EXPECT_TRUE(seg.feed(block(0.0f, 2048)).empty());
  EXPECT_TRUE(seg.flush().empty());
}
```

Remove the unused `floats` helper before running (it is not needed). Add to the `Sessio_transcription_tests` source list in `test/CMakeLists.txt`: `transcription_segmenter_tests.cpp` and the header `transcription_test_support.h`.

- [ ] **Step 3: Run to verify it fails**

```bash
distrobox-host-exec cmake --build build-release --target Sessio_transcription_tests --parallel 3
```

Expected: FAIL — `phrase_segmenter.h` not found.

- [ ] **Step 4: Implement**

`src/transcription/phrase_segmenter.h`:

```cpp
#pragma once

#include <memory>
#include <span>
#include <vector>

#include "voice_activity.h"

namespace pcm::transcription {

// Per-track: cuts a 16 kHz stream into fixed windows for the VAD and collects
// the phrases it closes.
class PhraseSegmenter {
 public:
  explicit PhraseSegmenter(std::unique_ptr<IVoiceActivityDetector> vad);

  std::vector<SpeechSegment> feed(std::span<const float> samples);
  // End of stream: pad the partial window with zeros, close an open phrase.
  std::vector<SpeechSegment> flush();

 private:
  std::vector<SpeechSegment> drain();

  std::unique_ptr<IVoiceActivityDetector> vad_;
  std::vector<float> pending_;
};

}  // namespace pcm::transcription
```

`src/transcription/phrase_segmenter.cpp`:

```cpp
#include "phrase_segmenter.h"

#include <utility>

namespace pcm::transcription {

PhraseSegmenter::PhraseSegmenter(std::unique_ptr<IVoiceActivityDetector> vad)
    : vad_(std::move(vad)) {}

std::vector<SpeechSegment> PhraseSegmenter::feed(std::span<const float> samples) {
  pending_.insert(pending_.end(), samples.begin(), samples.end());
  const size_t window = vad_->windowSize();
  size_t offset = 0;
  while (pending_.size() - offset >= window) {
    vad_->accept(pending_.data() + offset, window);
    offset += window;
  }
  pending_.erase(pending_.begin(), pending_.begin() + static_cast<ptrdiff_t>(offset));
  return drain();
}

std::vector<SpeechSegment> PhraseSegmenter::flush() {
  if (!pending_.empty()) {
    pending_.resize(vad_->windowSize(), 0.0f);
    vad_->accept(pending_.data(), pending_.size());
    pending_.clear();
  }
  vad_->flush();
  return drain();
}

std::vector<SpeechSegment> PhraseSegmenter::drain() {
  std::vector<SpeechSegment> out;
  while (vad_->hasSegment()) out.push_back(vad_->popSegment());
  return out;
}

}  // namespace pcm::transcription
```

Add `phrase_segmenter.h`, `phrase_segmenter.cpp`, `voice_activity.h`, `speech_recognizer.h` to the library sources.

- [ ] **Step 5: Run to verify it passes**

```bash
distrobox-host-exec cmake --build build-release --target Sessio_transcription_tests --parallel 3
distrobox-host-exec ctest --test-dir build-release -R "PhraseSegmenterTest|ResamplerTest" --output-on-failure
```

Expected: all PASS. (`FlushClosesOpenPhraseAndPadsPartialWindow`: 700 samples = one window fed, 188 pending; flush pads the second window with 324 zeros of 512 — mean abs 0.5·188/512 = 0.18 > 0.05, so it still counts as speech and the phrase has 1024 samples.)

- [ ] **Step 6: Commit**

```bash
git add src/transcription test
git commit -m "Add VAD interface and phrase segmenter (#118)"
```

---

### Task 5: ModelLocator

**Files:**
- Create: `src/transcription/model_locator.h`, `src/transcription/model_locator.cpp`, `test/transcription_locator_tests.cpp`
- Modify: `src/transcription/CMakeLists.txt`, `test/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct ModelPaths { std::filesystem::path encoder, decoder, joiner, tokens, vad; }`
  - `enum class Platform { Linux, MacOS, Windows }; Platform currentPlatform();`
  - `std::vector<std::filesystem::path> modelRootCandidates(const std::filesystem::path& app_dir, Platform platform, std::string_view env_override)` — override first when non-empty, then the platform default: Linux `app_dir/../share/sessio/models`, macOS `app_dir/../Resources/models`, Windows `app_dir/models`.
  - `struct LocateResult { std::optional<ModelPaths> paths; std::string error; }`
  - `LocateResult locateModels(const std::filesystem::path& app_dir, Platform platform = currentPlatform(), std::string_view env_override = {})` — the caller passes the value of `SESSIO_MODELS_DIR`; the function itself reads no environment.

- [ ] **Step 1: Write the failing tests**

`test/transcription_locator_tests.cpp`:

```cpp
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "model_locator.h"

namespace fs = std::filesystem;
using namespace pcm::transcription;

namespace {

struct TempDir {
  TempDir() {
    path = fs::temp_directory_path() / ("sessio-locator-" + std::to_string(::getpid()) + "-" +
                                        std::to_string(counter++));
    fs::create_directories(path);
  }
  ~TempDir() { fs::remove_all(path); }
  static inline int counter = 0;
  fs::path path;
};

void touch(const fs::path& p) {
  fs::create_directories(p.parent_path());
  std::ofstream(p) << "x";
}

void writeModelTree(const fs::path& root) {
  for (const char* f : {"encoder.int8.onnx", "decoder.onnx", "joiner.onnx", "tokens.txt"}) {
    touch(root / "gigaam-v3-rnnt" / f);
  }
  touch(root / "silero_vad.onnx");
}

}  // namespace

TEST(ModelLocatorTest, CandidatesPerPlatform) {
  const fs::path app = "/opt/app/bin";
  EXPECT_EQ(modelRootCandidates(app, Platform::Linux, {}).front(),
            fs::path("/opt/app/bin/../share/sessio/models"));
  EXPECT_EQ(modelRootCandidates("/A/Sessio.app/Contents/MacOS", Platform::MacOS, {}).front(),
            fs::path("/A/Sessio.app/Contents/MacOS/../Resources/models"));
  EXPECT_EQ(modelRootCandidates("C:/Sessio", Platform::Windows, {}).front(),
            fs::path("C:/Sessio/models"));
}

TEST(ModelLocatorTest, OverrideComesFirst) {
  const auto c = modelRootCandidates("/opt/app/bin", Platform::Linux, "/custom");
  ASSERT_EQ(c.size(), 2u);
  EXPECT_EQ(c.front(), fs::path("/custom"));
}

TEST(ModelLocatorTest, FindsCompleteTree) {
  TempDir dir;
  writeModelTree(dir.path / "share" / "sessio" / "models");
  const auto result = locateModels(dir.path / "bin", Platform::Linux);
  ASSERT_TRUE(result.paths.has_value()) << result.error;
  EXPECT_EQ(result.paths->encoder.filename(), "encoder.int8.onnx");
  EXPECT_EQ(result.paths->vad.filename(), "silero_vad.onnx");
  EXPECT_TRUE(result.error.empty());
}

TEST(ModelLocatorTest, ReportsMissingFileByName) {
  TempDir dir;
  const auto root = dir.path / "share" / "sessio" / "models";
  writeModelTree(root);
  fs::remove(root / "gigaam-v3-rnnt" / "joiner.onnx");
  const auto result = locateModels(dir.path / "bin", Platform::Linux);
  EXPECT_FALSE(result.paths.has_value());
  EXPECT_NE(result.error.find("joiner.onnx"), std::string::npos);
}

TEST(ModelLocatorTest, ReportsWhenNoCandidateExists) {
  TempDir dir;
  const auto result = locateModels(dir.path / "bin", Platform::Linux);
  EXPECT_FALSE(result.paths.has_value());
  EXPECT_FALSE(result.error.empty());
}

TEST(ModelLocatorTest, OverrideWinsOverPlatformDefault) {
  TempDir dir;
  writeModelTree(dir.path / "override");
  const auto result = locateModels(dir.path / "bin", Platform::Linux, (dir.path / "override").string());
  ASSERT_TRUE(result.paths.has_value()) << result.error;
  EXPECT_EQ(result.paths->tokens.parent_path().parent_path(), dir.path / "override");
}
```

Add `#include <unistd.h>` after `<fstream>`. Add `transcription_locator_tests.cpp` to the test sources.

- [ ] **Step 2: Run to verify it fails**

```bash
distrobox-host-exec cmake --build build-release --target Sessio_transcription_tests --parallel 3
```

Expected: FAIL — `model_locator.h` not found.

- [ ] **Step 3: Implement**

`src/transcription/model_locator.h`:

```cpp
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pcm::transcription {

struct ModelPaths {
  std::filesystem::path encoder;
  std::filesystem::path decoder;
  std::filesystem::path joiner;
  std::filesystem::path tokens;
  std::filesystem::path vad;
};

enum class Platform { Linux, MacOS, Windows };

Platform currentPlatform();

// Directories that may hold <root>/gigaam-v3-rnnt/* and <root>/silero_vad.onnx,
// in lookup order. `env_override` is the value of SESSIO_MODELS_DIR (may be empty).
std::vector<std::filesystem::path> modelRootCandidates(const std::filesystem::path& app_dir,
                                                       Platform platform,
                                                       std::string_view env_override);

struct LocateResult {
  std::optional<ModelPaths> paths;
  std::string error;  // user-readable, empty on success
};

LocateResult locateModels(const std::filesystem::path& app_dir,
                          Platform platform = currentPlatform(),
                          std::string_view env_override = {});

}  // namespace pcm::transcription
```

`src/transcription/model_locator.cpp`:

```cpp
#include "model_locator.h"

namespace fs = std::filesystem;

namespace pcm::transcription {

Platform currentPlatform() {
#if defined(_WIN32)
  return Platform::Windows;
#elif defined(__APPLE__)
  return Platform::MacOS;
#else
  return Platform::Linux;
#endif
}

std::vector<fs::path> modelRootCandidates(const fs::path& app_dir, Platform platform,
                                          std::string_view env_override) {
  std::vector<fs::path> out;
  if (!env_override.empty()) out.emplace_back(std::string(env_override));
  switch (platform) {
    case Platform::Linux: out.push_back(app_dir / ".." / "share" / "sessio" / "models"); break;
    case Platform::MacOS: out.push_back(app_dir / ".." / "Resources" / "models"); break;
    case Platform::Windows: out.push_back(app_dir / "models"); break;
  }
  return out;
}

LocateResult locateModels(const fs::path& app_dir, Platform platform, std::string_view env_override) {
  std::string error;
  for (const fs::path& root : modelRootCandidates(app_dir, platform, env_override)) {
    const fs::path dir = root / "gigaam-v3-rnnt";
    ModelPaths paths{dir / "encoder.int8.onnx", dir / "decoder.onnx", dir / "joiner.onnx",
                     dir / "tokens.txt", root / "silero_vad.onnx"};
    for (const fs::path* p : {&paths.encoder, &paths.decoder, &paths.joiner, &paths.tokens, &paths.vad}) {
      std::error_code ec;
      if (!fs::is_regular_file(*p, ec)) {
        error = "Model file not found: " + p->filename().string() + " (looked in " +
                root.string() + ")";
        goto next_candidate;
      }
    }
    return {std::move(paths), {}};
  next_candidate:;
  }
  if (error.empty()) error = "Transcription models are not installed";
  return {std::nullopt, error};
}

}  // namespace pcm::transcription
```

Replace the `goto` with a lambda `complete(paths)` returning the missing file or nullptr; use a normal loop — a `goto` out of a range-for is legal but the repo style avoids it:

```cpp
    const fs::path* missing = nullptr;
    for (const fs::path* p : {&paths.encoder, &paths.decoder, &paths.joiner, &paths.tokens, &paths.vad}) {
      std::error_code ec;
      if (!fs::is_regular_file(*p, ec)) { missing = p; break; }
    }
    if (!missing) return {std::move(paths), {}};
    error = "Model file not found: " + missing->filename().string() + " (looked in " + root.string() + ")";
```

Add the two files to the library sources.

- [ ] **Step 4: Run to verify it passes**

```bash
distrobox-host-exec cmake --build build-release --target Sessio_transcription_tests --parallel 3
distrobox-host-exec ctest --test-dir build-release -R ModelLocatorTest --output-on-failure
```

Expected: 6 PASS.

- [ ] **Step 5: Commit**

```bash
git add src/transcription test
git commit -m "Add ModelLocator for per-platform model lookup (#118)"
```

---

### Task 6: TranscriptionEngine

**Files:**
- Create: `src/transcription/transcription_engine.h`, `src/transcription/transcription_engine.cpp`, `test/transcription_engine_tests.cpp`
- Modify: `src/transcription/CMakeLists.txt`, `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `Resampler`, `PhraseSegmenter`, `IVoiceActivityDetector`, `ISpeechRecognizer`, types from `transcription_types.h`.
- Produces:
  ```cpp
  struct EngineCallbacks {
    std::function<void(const TranscribedPhrase&)> on_phrase;  // decode worker thread
    std::function<void(bool delayed)> on_delayed;             // decode worker thread, on change only
  };
  struct EngineConfig { std::chrono::milliseconds delayed_after{10000}; };
  struct EngineStats {
    uint64_t phrases = 0; uint64_t decode_failures = 0; uint64_t dropped_on_stop = 0;
    size_t queued = 0; bool delayed = false;
  };
  class TranscriptionEngine {
   public:
    using VadFactory = std::function<std::unique_ptr<IVoiceActivityDetector>()>;
    TranscriptionEngine(VadFactory, std::shared_ptr<ISpeechRecognizer>, EngineCallbacks, EngineConfig = {});
    ~TranscriptionEngine();                      // stop(0 ms)
    void addTrack(const TrackInfo&, int64_t start_offset_ms);  // calls the VadFactory on this thread
    void removeTrack(const TrackId&);            // flushes an open phrase
    void pushAudio(const TrackId&, const int16_t*, size_t, int sample_rate);  // unknown/closed tracks ignored
    void stop(std::chrono::milliseconds drain_timeout = std::chrono::seconds(5));
    EngineStats stats() const;
  };
  ```
  Phase times: `start_ms = start_offset_ms + seg.start_sample * 1000 / 16000`, `end_ms = start_ms + seg.samples.size() * 1000 / 16000`.

- [ ] **Step 1: Write the failing tests**

`test/transcription_engine_tests.cpp`:

```cpp
#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "transcription_engine.h"
#include "transcription_test_support.h"

using namespace pcm::transcription;
using namespace pcm::transcription::testing;
using namespace std::chrono_literals;

namespace {

struct Collector {
  void add(const TranscribedPhrase& p) {
    std::lock_guard lock(mutex);
    phrases.push_back(p);
  }
  size_t size() {
    std::lock_guard lock(mutex);
    return phrases.size();
  }
  std::vector<TranscribedPhrase> snapshot() {
    std::lock_guard lock(mutex);
    return phrases;
  }
  std::mutex mutex;
  std::vector<TranscribedPhrase> phrases;
  std::mutex delayed_mutex;
  std::vector<bool> delayed;
  void addDelayed(bool d) {
    std::lock_guard lock(delayed_mutex);
    delayed.push_back(d);
  }
  bool sawDelayed(bool value) {
    std::lock_guard lock(delayed_mutex);
    for (bool d : delayed) if (d == value) return true;
    return false;
  }
};

std::unique_ptr<TranscriptionEngine> makeEngine(Collector& c, std::shared_ptr<ISpeechRecognizer> rec,
                                                EngineConfig config = {}) {
  EngineCallbacks cb;
  cb.on_phrase = [&c](const TranscribedPhrase& p) { c.add(p); };
  cb.on_delayed = [&c](bool d) { c.addDelayed(d); };
  return std::make_unique<TranscriptionEngine>(
      [] { return std::make_unique<FakeVad>(); }, std::move(rec), cb, config);
}

void push(TranscriptionEngine& e, const TrackId& id, const std::vector<int16_t>& audio) {
  e.pushAudio(id, audio.data(), audio.size(), 48000);
}

}  // namespace

TEST(TranscriptionEngineTest, EmitsPhraseWithRoleNameAndTimes) {
  Collector c;
  auto engine = makeEngine(c, std::make_shared<FakeRecognizer>());
  engine->addTrack({"alice", TrackRole::Participant, "Alice"}, 1000);
  push(*engine, "alice", concat({silence(500), speech(1000), silence(800)}));

  ASSERT_TRUE(waitFor([&] { return c.size() == 1; }));
  const auto p = c.snapshot().front();
  EXPECT_EQ(p.track_id, "alice");
  EXPECT_EQ(p.role, TrackRole::Participant);
  EXPECT_EQ(p.speaker_name, "Alice");
  EXPECT_EQ(p.text, "phrase 0");
  EXPECT_NEAR(static_cast<double>(p.start_ms), 1500.0, 64.0);
  EXPECT_NEAR(static_cast<double>(p.end_ms), 2500.0, 64.0);
}

TEST(TranscriptionEngineTest, SeparatesTracksAndKeepsDecodeOrder) {
  Collector c;
  auto engine = makeEngine(c, std::make_shared<FakeRecognizer>());
  engine->addTrack({"me", TrackRole::Practitioner, "Dr"}, 0);
  engine->addTrack({"client", TrackRole::Participant, "C"}, 0);
  push(*engine, "me", concat({speech(500), silence(600)}));
  push(*engine, "client", concat({speech(500), silence(600)}));

  ASSERT_TRUE(waitFor([&] { return c.size() == 2; }));
  const auto phrases = c.snapshot();
  EXPECT_NE(phrases[0].track_id, phrases[1].track_id);
  EXPECT_EQ(phrases[0].text, "phrase 0");  // one worker decodes in queue order
  EXPECT_EQ(phrases[1].text, "phrase 1");
}

TEST(TranscriptionEngineTest, RemoveTrackFlushesOpenPhrase) {
  Collector c;
  auto engine = makeEngine(c, std::make_shared<FakeRecognizer>());
  engine->addTrack({"alice", TrackRole::Participant, "Alice"}, 0);
  push(*engine, "alice", speech(1000));  // no trailing silence: phrase still open
  engine->removeTrack("alice");
  ASSERT_TRUE(waitFor([&] { return c.size() == 1; }));
}

TEST(TranscriptionEngineTest, IgnoresUnknownAndRemovedTracks) {
  Collector c;
  auto engine = makeEngine(c, std::make_shared<FakeRecognizer>());
  push(*engine, "nobody", speech(500));
  engine->addTrack({"alice", TrackRole::Participant, "Alice"}, 0);
  engine->removeTrack("alice");
  push(*engine, "alice", concat({speech(500), silence(600)}));
  std::this_thread::sleep_for(150ms);
  EXPECT_EQ(c.size(), 0u);
}

TEST(TranscriptionEngineTest, StopDrainsQueuedPhrases) {
  Collector c;
  auto rec = std::make_shared<FakeRecognizer>([](int, size_t) {
    std::this_thread::sleep_for(20ms);
    return std::string("ok");
  });
  auto engine = makeEngine(c, rec);
  engine->addTrack({"alice", TrackRole::Participant, "Alice"}, 0);
  for (int i = 0; i < 5; ++i) push(*engine, "alice", concat({speech(200), silence(200)}));
  engine->stop(2s);
  EXPECT_EQ(c.size(), 5u);
  EXPECT_EQ(engine->stats().dropped_on_stop, 0u);
}

TEST(TranscriptionEngineTest, StopWithZeroTimeoutCountsDroppedPhrases) {
  Collector c;
  auto rec = std::make_shared<FakeRecognizer>([](int, size_t) {
    std::this_thread::sleep_for(100ms);
    return std::string("slow");
  });
  auto engine = makeEngine(c, rec);
  engine->addTrack({"alice", TrackRole::Participant, "Alice"}, 0);
  for (int i = 0; i < 6; ++i) push(*engine, "alice", concat({speech(200), silence(200)}));
  engine->stop(0ms);
  const auto stats = engine->stats();
  EXPECT_EQ(c.size() + stats.dropped_on_stop, 6u);
  EXPECT_GT(stats.dropped_on_stop, 0u);
}

TEST(TranscriptionEngineTest, DecodeFailureIsCountedAndSessionContinues) {
  Collector c;
  auto rec = std::make_shared<FakeRecognizer>([](int call, size_t) -> std::string {
    if (call == 1) throw std::runtime_error("boom");
    return "ok";
  });
  auto engine = makeEngine(c, rec);
  engine->addTrack({"alice", TrackRole::Participant, "Alice"}, 0);
  for (int i = 0; i < 3; ++i) push(*engine, "alice", concat({speech(300), silence(300)}));
  engine->stop(2s);
  EXPECT_EQ(c.size(), 2u);
  EXPECT_EQ(engine->stats().decode_failures, 1u);
}

TEST(TranscriptionEngineTest, EmptyTextIsNotEmitted) {
  Collector c;
  auto rec = std::make_shared<FakeRecognizer>([](int, size_t) { return std::string("  "); });
  auto engine = makeEngine(c, rec);
  engine->addTrack({"alice", TrackRole::Participant, "Alice"}, 0);
  push(*engine, "alice", concat({speech(300), silence(300)}));
  engine->stop(2s);
  EXPECT_EQ(c.size(), 0u);
}

TEST(TranscriptionEngineTest, ReportsDelayWhenQueueFallsBehindAndRecovers) {
  Collector c;
  auto rec = std::make_shared<FakeRecognizer>([](int, size_t) {
    std::this_thread::sleep_for(120ms);
    return std::string("slow");
  });
  EngineConfig config;
  config.delayed_after = 50ms;
  auto engine = makeEngine(c, rec, config);
  engine->addTrack({"alice", TrackRole::Participant, "Alice"}, 0);
  for (int i = 0; i < 3; ++i) push(*engine, "alice", concat({speech(200), silence(200)}));
  ASSERT_TRUE(waitFor([&] { return c.size() == 3; }));
  EXPECT_TRUE(c.sawDelayed(true));

  push(*engine, "alice", concat({speech(200), silence(200)}));
  ASSERT_TRUE(waitFor([&] { return c.size() == 4; }));
  EXPECT_TRUE(c.sawDelayed(false));
  EXPECT_FALSE(engine->stats().delayed);
}

TEST(TranscriptionEngineTest, TenTracksTwoSpeakingAllPhrasesArriveAndQueueDrains) {
  Collector c;
  auto engine = makeEngine(c, std::make_shared<FakeRecognizer>());
  for (int i = 0; i < 10; ++i) {
    engine->addTrack({"t" + std::to_string(i),
                      i == 0 ? TrackRole::Practitioner : TrackRole::Participant,
                      "N" + std::to_string(i)}, 0);
  }
  for (int round = 0; round < 3; ++round) {
    for (int i = 0; i < 10; ++i) {
      const bool speaks = i < 2;
      push(*engine, "t" + std::to_string(i),
           speaks ? concat({speech(300), silence(300)}) : silence(600));
    }
  }
  engine->stop(2s);
  EXPECT_EQ(c.size(), 6u);
  EXPECT_EQ(engine->stats().queued, 0u);
}

TEST(TranscriptionEngineTest, PushAfterStopIsIgnored) {
  Collector c;
  auto engine = makeEngine(c, std::make_shared<FakeRecognizer>());
  engine->addTrack({"alice", TrackRole::Participant, "Alice"}, 0);
  engine->stop(1s);
  push(*engine, "alice", concat({speech(300), silence(300)}));
  std::this_thread::sleep_for(100ms);
  EXPECT_EQ(c.size(), 0u);
}
```

Add `transcription_engine_tests.cpp` to the test sources.

- [ ] **Step 2: Run to verify it fails**

```bash
distrobox-host-exec cmake --build build-release --target Sessio_transcription_tests --parallel 3
```

Expected: FAIL — `transcription_engine.h` not found.

- [ ] **Step 3: Implement the header**

`src/transcription/transcription_engine.h`:

```cpp
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "phrase_segmenter.h"
#include "resampler.h"
#include "speech_recognizer.h"
#include "transcription_types.h"
#include "voice_activity.h"

namespace pcm::transcription {

struct EngineCallbacks {
  std::function<void(const TranscribedPhrase&)> on_phrase;  // decode worker thread
  std::function<void(bool delayed)> on_delayed;             // decode worker thread, on change only
};

struct EngineConfig {
  std::chrono::milliseconds delayed_after{10000};
};

struct EngineStats {
  uint64_t phrases = 0;
  uint64_t decode_failures = 0;
  uint64_t dropped_on_stop = 0;
  size_t queued = 0;
  bool delayed = false;
};

// Real-time transcription of several audio tracks with one shared recogniser.
// Threads: callers of pushAudio only copy samples; one segmenter thread
// resamples and runs every track's VAD; one decode worker runs the recogniser.
class TranscriptionEngine {
 public:
  using VadFactory = std::function<std::unique_ptr<IVoiceActivityDetector>()>;

  TranscriptionEngine(VadFactory vad_factory, std::shared_ptr<ISpeechRecognizer> recognizer,
                      EngineCallbacks callbacks, EngineConfig config = {});
  ~TranscriptionEngine();

  TranscriptionEngine(const TranscriptionEngine&) = delete;
  TranscriptionEngine& operator=(const TranscriptionEngine&) = delete;

  // Calls the VAD factory on the calling thread.
  void addTrack(const TrackInfo& info, int64_t start_offset_ms);
  // Flushes the track's open phrase. Later audio for it is ignored.
  void removeTrack(const TrackId& id);
  // Unknown, removed, or post-stop tracks are ignored. The first sample rate
  // seen for a track wins.
  void pushAudio(const TrackId& id, const int16_t* samples, size_t count, int sample_rate);
  // Stops accepting audio, processes what is queued, waits up to drain_timeout
  // for decoding, drops the rest (counted). Idempotent.
  void stop(std::chrono::milliseconds drain_timeout = std::chrono::seconds(5));

  EngineStats stats() const;

 private:
  using Clock = std::chrono::steady_clock;

  struct Track {
    TrackInfo info;
    int64_t start_offset_ms = 0;
    // Guarded by mutex_:
    std::vector<int16_t> pending;
    int rate = 0;
    bool closing = false;
    // Segmenter thread only:
    std::unique_ptr<Resampler> resampler;
    std::unique_ptr<PhraseSegmenter> segmenter;
  };

  struct Job {
    TrackInfo info;
    int64_t start_ms = 0;
    int64_t end_ms = 0;
    std::vector<float> samples;
    Clock::time_point queued;
  };

  struct Work {
    std::shared_ptr<Track> track;
    std::vector<int16_t> samples;
    int rate = 0;
  };

  void segmenterLoop();
  void decodeLoop();
  void process(Track& track, const std::vector<int16_t>& samples, int rate);
  void flush(Track& track);
  void enqueue(const Track& track, SpeechSegment&& segment);

  VadFactory vad_factory_;
  std::shared_ptr<ISpeechRecognizer> recognizer_;
  EngineCallbacks callbacks_;
  EngineConfig config_;

  mutable std::mutex mutex_;  // tracks_, per-track pending state, stopping_
  std::condition_variable cv_;
  std::map<TrackId, std::shared_ptr<Track>> tracks_;
  bool stopping_ = false;

  mutable std::mutex queue_mutex_;  // jobs_, flags below
  std::condition_variable queue_cv_;
  std::condition_variable idle_cv_;
  std::deque<Job> jobs_;
  bool segmenter_done_ = false;
  bool abort_ = false;
  bool decoding_ = false;
  bool delayed_ = false;

  std::atomic<uint64_t> phrases_{0};
  std::atomic<uint64_t> decode_failures_{0};
  std::atomic<uint64_t> dropped_on_stop_{0};

  std::thread segmenter_thread_;
  std::thread decode_thread_;
  bool stopped_ = false;  // guarded by mutex_
};

}  // namespace pcm::transcription
```

- [ ] **Step 4: Implement the engine**

`src/transcription/transcription_engine.cpp`:

```cpp
#include "transcription_engine.h"

#include <algorithm>
#include <utility>

namespace pcm::transcription {

namespace {

std::string trim(std::string text) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
  text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
  return text;
}

int64_t samplesToMs(int64_t samples) { return samples * 1000 / kRecognizerSampleRate; }

}  // namespace

TranscriptionEngine::TranscriptionEngine(VadFactory vad_factory,
                                         std::shared_ptr<ISpeechRecognizer> recognizer,
                                         EngineCallbacks callbacks, EngineConfig config)
    : vad_factory_(std::move(vad_factory)),
      recognizer_(std::move(recognizer)),
      callbacks_(std::move(callbacks)),
      config_(config) {
  segmenter_thread_ = std::thread([this] { segmenterLoop(); });
  decode_thread_ = std::thread([this] { decodeLoop(); });
}

TranscriptionEngine::~TranscriptionEngine() { stop(std::chrono::milliseconds(0)); }

void TranscriptionEngine::addTrack(const TrackInfo& info, int64_t start_offset_ms) {
  auto track = std::make_shared<Track>();
  track->info = info;
  track->start_offset_ms = start_offset_ms;
  track->segmenter = std::make_unique<PhraseSegmenter>(vad_factory_());
  std::lock_guard lock(mutex_);
  if (stopping_ || tracks_.count(info.id)) return;
  tracks_.emplace(info.id, std::move(track));
}

void TranscriptionEngine::removeTrack(const TrackId& id) {
  {
    std::lock_guard lock(mutex_);
    const auto it = tracks_.find(id);
    if (it == tracks_.end()) return;
    it->second->closing = true;
  }
  cv_.notify_one();
}

void TranscriptionEngine::pushAudio(const TrackId& id, const int16_t* samples, size_t count,
                                    int sample_rate) {
  if (count == 0) return;
  {
    std::lock_guard lock(mutex_);
    if (stopping_) return;
    const auto it = tracks_.find(id);
    if (it == tracks_.end() || it->second->closing) return;
    Track& track = *it->second;
    if (track.rate == 0) track.rate = sample_rate;
    if (track.rate != sample_rate) return;
    track.pending.insert(track.pending.end(), samples, samples + count);
  }
  cv_.notify_one();
}

void TranscriptionEngine::stop(std::chrono::milliseconds drain_timeout) {
  {
    std::lock_guard lock(mutex_);
    if (stopped_) return;
    stopped_ = true;
    stopping_ = true;
  }
  cv_.notify_all();
  segmenter_thread_.join();

  {
    std::unique_lock lock(queue_mutex_);
    idle_cv_.wait_for(lock, drain_timeout, [this] { return jobs_.empty() && !decoding_; });
    if (!jobs_.empty() || decoding_) abort_ = true;
  }
  queue_cv_.notify_all();
  decode_thread_.join();
}

EngineStats TranscriptionEngine::stats() const {
  EngineStats s;
  s.phrases = phrases_;
  s.decode_failures = decode_failures_;
  s.dropped_on_stop = dropped_on_stop_;
  std::lock_guard lock(queue_mutex_);
  s.queued = jobs_.size();
  s.delayed = delayed_;
  return s;
}

void TranscriptionEngine::segmenterLoop() {
  for (;;) {
    std::vector<Work> work;
    std::vector<std::shared_ptr<Track>> finished;
    bool stopping = false;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this] {
        if (stopping_) return true;
        for (const auto& [id, t] : tracks_) {
          if (!t->pending.empty() || t->closing) return true;
        }
        return false;
      });
      stopping = stopping_;
      for (auto it = tracks_.begin(); it != tracks_.end();) {
        Track& t = *it->second;
        if (!t.pending.empty()) {
          work.push_back({it->second, std::move(t.pending), t.rate});
          t.pending.clear();
        }
        if (t.closing || stopping) {
          finished.push_back(it->second);
          it = tracks_.erase(it);
        } else {
          ++it;
        }
      }
    }
    for (Work& w : work) process(*w.track, w.samples, w.rate);
    for (auto& t : finished) flush(*t);
    if (stopping) break;
  }
  {
    std::lock_guard lock(queue_mutex_);
    segmenter_done_ = true;
  }
  queue_cv_.notify_all();
}

void TranscriptionEngine::process(Track& track, const std::vector<int16_t>& samples, int rate) {
  if (!track.resampler) {
    try {
      track.resampler = std::make_unique<Resampler>(rate);
    } catch (const std::invalid_argument&) {
      return;  // unsupported rate: the track stays silent
    }
  }
  const std::vector<float> audio = track.resampler->process(samples);
  for (SpeechSegment& seg : track.segmenter->feed(audio)) enqueue(track, std::move(seg));
}

void TranscriptionEngine::flush(Track& track) {
  for (SpeechSegment& seg : track.segmenter->flush()) enqueue(track, std::move(seg));
}

void TranscriptionEngine::enqueue(const Track& track, SpeechSegment&& segment) {
  Job job;
  job.info = track.info;
  job.start_ms = track.start_offset_ms + samplesToMs(segment.start_sample);
  job.end_ms = job.start_ms + samplesToMs(static_cast<int64_t>(segment.samples.size()));
  job.samples = std::move(segment.samples);
  job.queued = Clock::now();
  {
    std::lock_guard lock(queue_mutex_);
    jobs_.push_back(std::move(job));
  }
  queue_cv_.notify_one();
}

void TranscriptionEngine::decodeLoop() {
  for (;;) {
    Job job;
    bool delayed_now = false;
    bool delayed_changed = false;
    {
      std::unique_lock lock(queue_mutex_);
      queue_cv_.wait(lock, [this] { return !jobs_.empty() || segmenter_done_ || abort_; });
      if (abort_) {
        dropped_on_stop_ += jobs_.size();
        jobs_.clear();
        break;
      }
      if (jobs_.empty()) {
        if (segmenter_done_) break;
        continue;
      }
      job = std::move(jobs_.front());
      jobs_.pop_front();
      decoding_ = true;
      delayed_now = Clock::now() - job.queued > config_.delayed_after;
      delayed_changed = delayed_now != delayed_;
      delayed_ = delayed_now;
    }
    if (delayed_changed && callbacks_.on_delayed) callbacks_.on_delayed(delayed_now);

    try {
      const std::string text = trim(recognizer_->transcribe(job.samples));
      if (!text.empty()) {
        TranscribedPhrase phrase{job.info.id, job.info.role, job.info.display_name,
                                 job.start_ms, job.end_ms, text};
        ++phrases_;
        if (callbacks_.on_phrase) callbacks_.on_phrase(phrase);
      }
    } catch (const std::exception&) {
      ++decode_failures_;
    }

    {
      std::lock_guard lock(queue_mutex_);
      decoding_ = false;
    }
    idle_cv_.notify_all();
  }
  {
    std::lock_guard lock(queue_mutex_);
    decoding_ = false;
  }
  idle_cv_.notify_all();
}

}  // namespace pcm::transcription
```

Add `#include <cctype>` and `#include <stdexcept>` at the top. Add `transcription_engine.h`/`.cpp` to the library sources.

Design notes the implementer must keep: `phrases_` counts only non-empty emitted phrases; the `on_phrase` callback runs without any engine lock held (callers may call back into the engine); `idle_cv_` is notified after each job so `stop` can wait for `jobs_.empty() && !decoding_`; the decode worker marks `decoding_ = true` under the same lock that pops the job, so `stop` never sees an empty queue with a job in flight.

- [ ] **Step 5: Run to verify it passes**

```bash
distrobox-host-exec cmake --build build-release --target Sessio_transcription_tests --parallel 3
distrobox-host-exec ctest --test-dir build-release -R TranscriptionEngineTest --output-on-failure --repeat until-fail:20
```

Expected: 11 tests PASS in 20 repetitions (the repeat exposes races). If `StopWithZeroTimeoutCountsDroppedPhrases` is flaky because the segmenter has not yet produced all jobs when `stop(0)` runs, that is the test's own timing: it asserts `delivered + dropped == 6`, which holds as `stop` joins the segmenter first; only `dropped > 0` depends on speed — raise the recogniser sleep to 200 ms if it fails.

- [ ] **Step 6: Thread-safety run**

```bash
distrobox-host-exec cmake -S . -B build-tsan -DPCM_BUILD_TESTS=ON -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" -DCMAKE_BUILD_TYPE=RelWithDebInfo
distrobox-host-exec cmake --build build-tsan --target Sessio_transcription_tests --parallel 3
distrobox-host-exec ctest --test-dir build-tsan -R "TranscriptionEngineTest" --output-on-failure
```

Expected: PASS with no ThreadSanitizer reports. If configuring `build-tsan` re-fetches sherpa and is too slow, set `-DSESSIO_ENABLE_TRANSCRIPTION=OFF` is not possible (the tests are guarded by it); instead copy the engine sources into a standalone mini CMake in the scratchpad that builds only `Sessio_transcription` and the engine tests. Do not commit `build-tsan`.

- [ ] **Step 7: Commit**

```bash
git add src/transcription test
git commit -m "Add TranscriptionEngine with one segmenter thread and one decode worker (#118)"
```

---

### Task 7: sherpa-onnx backend

**Files:**
- Create: `src/transcription/sherpa_backend.h`, `src/transcription/sherpa_backend.cpp`
- Modify: `src/transcription/CMakeLists.txt`

**Interfaces:**
- Consumes: `ModelPaths`, `IVoiceActivityDetector`, `ISpeechRecognizer`, `TranscriptionEngine::VadFactory`.
- Produces: target `Sessio_transcription_sherpa` (static, links `Sessio_transcription` and `sherpa-onnx-cxx-api`);
  `std::shared_ptr<ISpeechRecognizer> makeSherpaRecognizer(const ModelPaths&, int num_threads = 4)` — throws `std::runtime_error("Failed to load the recognition model")` when sherpa cannot create the recogniser;
  `TranscriptionEngine::VadFactory makeSherpaVadFactory(const ModelPaths&)` — each call builds a Silero VAD (threshold 0.5, min silence 0.4 s, min speech 0.25 s, max phrase 20 s, buffer 120 s); throws `std::runtime_error("Failed to load the voice detector")` on failure.

- [ ] **Step 1: Write the header and implementation**

`src/transcription/sherpa_backend.h`:

```cpp
#pragma once

#include <memory>

#include "model_locator.h"
#include "speech_recognizer.h"
#include "transcription_engine.h"

namespace pcm::transcription {

std::shared_ptr<ISpeechRecognizer> makeSherpaRecognizer(const ModelPaths& paths, int num_threads = 4);

TranscriptionEngine::VadFactory makeSherpaVadFactory(const ModelPaths& paths);

}  // namespace pcm::transcription
```

`src/transcription/sherpa_backend.cpp`:

```cpp
#include "sherpa_backend.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

#include "sherpa-onnx/c-api/cxx-api.h"

namespace pcm::transcription {

namespace {

namespace sc = sherpa_onnx::cxx;

class SherpaRecognizer : public ISpeechRecognizer {
 public:
  SherpaRecognizer(const ModelPaths& paths, int num_threads) {
    sc::OfflineRecognizerConfig config;
    config.feat_config.sample_rate = 16000;
    config.feat_config.feature_dim = 80;
    config.model_config.transducer.encoder = paths.encoder.string();
    config.model_config.transducer.decoder = paths.decoder.string();
    config.model_config.transducer.joiner = paths.joiner.string();
    config.model_config.tokens = paths.tokens.string();
    config.model_config.model_type = "nemo_transducer";
    config.model_config.num_threads = num_threads;
    recognizer_ = sc::OfflineRecognizer::Create(config);
    if (!recognizer_.Get()) throw std::runtime_error("Failed to load the recognition model");
  }

  std::string transcribe(std::span<const float> samples) override {
    sc::OfflineStream stream = recognizer_.CreateStream();
    stream.AcceptWaveform(16000, samples.data(), static_cast<int32_t>(samples.size()));
    recognizer_.Decode(&stream);
    return recognizer_.GetResult(&stream).text;
  }

 private:
  sc::OfflineRecognizer recognizer_;
};

class SherpaVad : public IVoiceActivityDetector {
 public:
  explicit SherpaVad(const ModelPaths& paths) {
    sc::VadModelConfig config;
    config.silero_vad.model = paths.vad.string();
    config.silero_vad.threshold = 0.5F;
    config.silero_vad.min_silence_duration = 0.4F;
    config.silero_vad.min_speech_duration = 0.25F;
    config.silero_vad.max_speech_duration = 20.0F;
    config.sample_rate = 16000;
    window_ = static_cast<size_t>(config.silero_vad.window_size);
    vad_ = sc::VoiceActivityDetector::Create(config, 120.0F);
    if (!vad_.Get()) throw std::runtime_error("Failed to load the voice detector");
  }

  size_t windowSize() const override { return window_; }

  void accept(const float* window, size_t n) override {
    vad_.AcceptWaveform(window, static_cast<int32_t>(n));
  }

  bool hasSegment() const override { return !vad_.IsEmpty(); }

  SpeechSegment popSegment() override {
    sc::SpeechSegment seg = vad_.Front();
    vad_.Pop();
    return {static_cast<int64_t>(seg.start), std::move(seg.samples)};
  }

  void flush() override { vad_.Flush(); }

 private:
  sc::VoiceActivityDetector vad_;
  size_t window_ = 512;
};

}  // namespace

std::shared_ptr<ISpeechRecognizer> makeSherpaRecognizer(const ModelPaths& paths, int num_threads) {
  return std::make_shared<SherpaRecognizer>(paths, num_threads);
}

TranscriptionEngine::VadFactory makeSherpaVadFactory(const ModelPaths& paths) {
  return [paths] { return std::make_unique<SherpaVad>(paths); };
}

}  // namespace pcm::transcription
```

Append to `src/transcription/CMakeLists.txt`:

```cmake
add_library(${TARGET_NAME}_sherpa STATIC
  sherpa_backend.h
  sherpa_backend.cpp
)
target_include_directories(${TARGET_NAME}_sherpa PRIVATE "${SESSIO_SHERPA_SOURCE_DIR}")
target_link_libraries(${TARGET_NAME}_sherpa PUBLIC ${TARGET_NAME} PRIVATE sherpa-onnx-cxx-api)
```

If the sherpa C++ wrapper's accessor names differ (`vad_.IsEmpty()` is non-const in some versions), mark the VAD member `mutable` and keep `hasSegment()` const. Check `build-release/_deps/sherpa_onnx-src/sherpa-onnx/c-api/cxx-api.h` for `class VoiceActivityDetector` method signatures first.

- [ ] **Step 2: Build**

```bash
distrobox-host-exec cmake --build build-release --target Sessio_transcription_sherpa --parallel 3
```

Expected: builds clean. (Behaviour is verified in Task 8 against the real model.)

- [ ] **Step 3: Commit**

```bash
git add src/transcription
git commit -m "Add sherpa-onnx recogniser and VAD backend (#118)"
```

---

### Task 8: Real-model tests and the ten-track load test

**Files:**
- Create: `test/transcription_model_tests.cpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `locateModels`, `makeSherpaRecognizer`, `makeSherpaVadFactory`, `TranscriptionEngine`.
- Produces: executable `Sessio_transcription_model_tests`, registered with `gtest_discover_tests(... PROPERTIES LABELS models)`. Environment: `SESSIO_MODELS_DIR` (root of the model tree, e.g. `build-release/transcription-models`); tests skip with `GTEST_SKIP()` when models are missing. The load test also needs `SESSIO_LOADTEST=1`.

- [ ] **Step 1: Write the tests**

`test/transcription_model_tests.cpp`:

```cpp
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

#include "model_locator.h"
#include "sherpa-onnx/c-api/cxx-api.h"
#include "sherpa_backend.h"
#include "transcription_engine.h"

namespace fs = std::filesystem;
using namespace pcm::transcription;
using namespace std::chrono_literals;

namespace {

std::optional<ModelPaths> models() {
  const char* env = std::getenv("SESSIO_MODELS_DIR");
  return locateModels(fs::current_path(), currentPlatform(), env ? env : "").paths;
}

std::vector<int16_t> loadSample() {
  const char* env = std::getenv("SESSIO_MODELS_DIR");
  const fs::path wav = fs::path(env ? env : "") / "gigaam-v3-rnnt" / "test_wavs" / "example.wav";
  auto wave = sherpa_onnx::cxx::ReadWave(wav.string());
  std::vector<int16_t> out(wave.samples.size());
  for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<int16_t>(wave.samples[i] * 32767.0f);
  return out;  // 16 kHz
}

bool hasCyrillic(const std::string& text) {
  for (size_t i = 0; i + 1 < text.size(); ++i) {
    const auto c = static_cast<unsigned char>(text[i]);
    if (c == 0xD0 || c == 0xD1) return true;
  }
  return false;
}

struct Lag {
  std::mutex mutex;
  std::vector<double> seconds;
  std::vector<TranscribedPhrase> phrases;
};

double percentile(std::vector<double> v, double p) {
  std::sort(v.begin(), v.end());
  return v[std::min(v.size() - 1, static_cast<size_t>(p * static_cast<double>(v.size())))];
}

// Feeds `tracks` (silence for everyone except `speakers`) in real time, 100 ms at
// a time, and records how late each phrase arrives after its audio ended.
std::vector<double> runRealtime(const ModelPaths& paths, int tracks, int speakers,
                                const std::vector<int16_t>& sample, Lag& lag) {
  EngineCallbacks cb;
  const auto start = std::chrono::steady_clock::now();
  cb.on_phrase = [&](const TranscribedPhrase& p) {
    const double audio_end = static_cast<double>(p.end_ms) / 1000.0;
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::lock_guard lock(lag.mutex);
    lag.seconds.push_back(now - audio_end);
    lag.phrases.push_back(p);
  };
  TranscriptionEngine engine(makeSherpaVadFactory(paths), makeSherpaRecognizer(paths), cb);
  for (int t = 0; t < tracks; ++t) {
    engine.addTrack({"t" + std::to_string(t), t == 0 ? TrackRole::Practitioner : TrackRole::Participant,
                     "N" + std::to_string(t)}, 0);
  }
  const size_t step = 1600;  // 100 ms at 16 kHz
  const std::vector<int16_t> quiet(step, 0);
  const size_t total = sample.size() + 16000 * 2;  // two seconds of tail
  for (size_t pos = 0; pos < total; pos += step) {
    for (int t = 0; t < tracks; ++t) {
      const std::string id = "t" + std::to_string(t);
      if (t < speakers && pos < sample.size()) {
        const size_t n = std::min(step, sample.size() - pos);
        engine.pushAudio(id, sample.data() + pos, n, 16000);
      } else {
        engine.pushAudio(id, quiet.data(), step, 16000);
      }
    }
    std::this_thread::sleep_until(start + std::chrono::milliseconds((pos + step) / 16));
  }
  engine.stop(10s);
  std::lock_guard lock(lag.mutex);
  return lag.seconds;
}

}  // namespace

TEST(TranscriptionModelTest, RecognisesRussianSample) {
  const auto paths = models();
  if (!paths) GTEST_SKIP() << "models not installed; set SESSIO_MODELS_DIR";
  auto recognizer = makeSherpaRecognizer(*paths);
  const auto sample = loadSample();
  std::vector<float> floats(sample.size());
  for (size_t i = 0; i < sample.size(); ++i) floats[i] = static_cast<float>(sample[i]) / 32768.0f;
  const std::string text = recognizer->transcribe(floats);
  EXPECT_FALSE(text.empty());
  EXPECT_TRUE(hasCyrillic(text)) << "text had no Cyrillic letters";
}

TEST(TranscriptionModelTest, EngineProducesTimedPhrasesFromSample) {
  const auto paths = models();
  if (!paths) GTEST_SKIP() << "models not installed; set SESSIO_MODELS_DIR";
  const auto sample = loadSample();
  std::mutex m;
  std::vector<TranscribedPhrase> phrases;
  EngineCallbacks cb;
  cb.on_phrase = [&](const TranscribedPhrase& p) {
    std::lock_guard lock(m);
    phrases.push_back(p);
  };
  TranscriptionEngine engine(makeSherpaVadFactory(*paths), makeSherpaRecognizer(*paths), cb);
  engine.addTrack({"a", TrackRole::Participant, "A"}, 5000);
  engine.pushAudio("a", sample.data(), sample.size(), 16000);
  engine.removeTrack("a");
  engine.stop(15s);
  ASSERT_FALSE(phrases.empty());
  for (const auto& p : phrases) {
    EXPECT_GE(p.start_ms, 5000);
    EXPECT_GT(p.end_ms, p.start_ms);
    EXPECT_TRUE(hasCyrillic(p.text));
  }
}

TEST(TranscriptionModelTest, TwoSpeakersAmongTenTracksStayWithinOneSecond) {
  const auto paths = models();
  if (!paths) GTEST_SKIP() << "models not installed; set SESSIO_MODELS_DIR";
  if (!std::getenv("SESSIO_LOADTEST")) GTEST_SKIP() << "set SESSIO_LOADTEST=1 (runs in real time)";
  Lag lag;
  const auto lags = runRealtime(*paths, 10, 2, loadSample(), lag);
  ASSERT_GE(lags.size(), 2u);
  EXPECT_LE(percentile(lags, 0.95), 1.0);
}
```

Add `#include <optional>` at the top. In `test/CMakeLists.txt` inside the `if(SESSIO_TRANSCRIPTION_ENABLED)` block add:

```cmake
  add_executable(Sessio_transcription_model_tests transcription_model_tests.cpp)
  target_include_directories(Sessio_transcription_model_tests PRIVATE "${SESSIO_SHERPA_SOURCE_DIR}")
  target_link_libraries(Sessio_transcription_model_tests PRIVATE
      GTest::gtest
      GTest::gtest_main
      Sessio_transcription
      Sessio_transcription_sherpa
      sherpa-onnx-cxx-api
  )
  gtest_discover_tests(Sessio_transcription_model_tests
      PROPERTIES LABELS models
                 ENVIRONMENT "SESSIO_MODELS_DIR=${SESSIO_MODELS_DIR}")
  add_dependencies(Sessio_transcription_model_tests sessio_models)
```

Both test executables must find `libsherpa-onnx-*.so` and `libonnxruntime.so` at run time; add `BUILD_RPATH` for the model tests target:

```cmake
  set_target_properties(Sessio_transcription_model_tests PROPERTIES
      BUILD_RPATH "${CMAKE_BINARY_DIR}/lib;${SESSIO_ONNXRUNTIME_LIB_DIR}")
```

- [ ] **Step 2: Build and run**

```bash
distrobox-host-exec cmake --build build-release --target Sessio_transcription_model_tests --parallel 3
distrobox-host-exec ctest --test-dir build-release -L models --output-on-failure
```

Expected: `RecognisesRussianSample` and `EngineProducesTimedPhrasesFromSample` PASS; the load test is SKIPPED. If they skip with "models not installed", check that `SESSIO_MODELS_DIR` resolves to `build-release/transcription-models` and that `sessio_models` ran.

- [ ] **Step 3: Run the load test**

```bash
SESSIO_LOADTEST=1 distrobox-host-exec ctest --test-dir build-release -R TwoSpeakersAmongTenTracks --output-on-failure
```

Expected: PASS, p95 lag ≤ 1.0 s (the spike measured about 0.65 s). `example.wav` is about 11 s so the test takes about 13 s. If the bound fails on this machine, record the measured p95 in the commit message and in the plan and decide with the user — do not widen the bound silently.

- [ ] **Step 4: Commit**

```bash
git add test
git commit -m "Test the engine against the real GigaAM model and ten tracks (#118)"
```

---

### Task 9: CI on all platforms, packaging smoke test, Windows installer

**Files:**
- Modify: `.github/workflows/cmake-multi-platform.yml` (Linux steps only), `docs/asciidoc/15-realtime-asr-spike.adoc` (status line only if the file exists on this branch; otherwise skip)
- Create: `test/transcription_install_smoke.cmake`

**Interfaces:**
- Produces: a CI step that caches the models and runs the model tests on Linux; a script test that checks an install tree contains everything the module needs.

- [ ] **Step 1: Install-tree smoke script**

`test/transcription_install_smoke.cmake` (run as `cmake -DINSTALL_ROOT=<prefix> -P ...`):

```cmake
foreach(_rel
    share/sessio/models/silero_vad.onnx
    share/sessio/models/gigaam-v3-rnnt/encoder.int8.onnx
    share/sessio/models/gigaam-v3-rnnt/decoder.onnx
    share/sessio/models/gigaam-v3-rnnt/joiner.onnx
    share/sessio/models/gigaam-v3-rnnt/tokens.txt)
  if(NOT EXISTS "${INSTALL_ROOT}/${_rel}")
    message(FATAL_ERROR "Missing from install tree: ${_rel}")
  endif()
endforeach()

file(GLOB_RECURSE _libs "${INSTALL_ROOT}/lib*/sessio/libsherpa-onnx-cxx-api.so*")
file(GLOB_RECURSE _ort "${INSTALL_ROOT}/lib*/sessio/libonnxruntime.so*")
if(NOT _libs OR NOT _ort)
  message(FATAL_ERROR "sherpa-onnx or ONNX Runtime libraries missing from the install tree")
endif()
message(STATUS "Transcription install tree OK")
```

- [ ] **Step 2: Run it locally**

```bash
distrobox-host-exec cmake --install build-release --prefix /tmp/sessio-install-check
distrobox-host-exec cmake -DINSTALL_ROOT=/tmp/sessio-install-check -P test/transcription_install_smoke.cmake
distrobox-host-exec rm -rf /tmp/sessio-install-check
```

Expected: `Transcription install tree OK`.

- [ ] **Step 3: CI steps**

In `.github/workflows/cmake-multi-platform.yml`, after the `Configure` step add (guarded `if: runner.os == 'Linux'`):

```yaml
      - name: Cache transcription models
        if: runner.os == 'Linux'
        uses: actions/cache@v4
        with:
          path: build-release/transcription-models
          key: transcription-models-${{ hashFiles('cmake/FetchTranscriptionModels.cmake') }}
```

After the existing build step add:

```yaml
      - name: Transcription model tests
        if: runner.os == 'Linux'
        run: |
          cmake --build build-release --target Sessio_transcription_model_tests --parallel
          ctest --test-dir build-release -L models --output-on-failure

      - name: Verify transcription install tree
        if: runner.os == 'Linux'
        run: |
          cmake --install build-release --prefix "${{ github.workspace }}/build-release/transcription-install"
          cmake -DINSTALL_ROOT="${{ github.workspace }}/build-release/transcription-install" -P test/transcription_install_smoke.cmake
```

The sherpa-onnx build and the model download now run in the Windows and macOS jobs too (no `if:` on the build; they already run `cmake --preset vcpkg-release` and `cmake --build`). Add the same models cache step for them without the `runner.os == 'Linux'` condition (use `path: build-release/transcription-models`; the key stays the same). Model tests run on Linux only in CI; on Windows and macOS they are built (to prove the link works) but not run — the user tests them by hand, and `ctest -L models` can be run locally with `SESSIO_MODELS_DIR` set.

Check that the workflow passes `-DPCM_BUILD_TESTS=ON` (preset `vcpkg-release`); if tests are not enabled there, add it to the `Configure` step for Linux only.

Platform build pitfalls to expect on the first CI run (fix, do not disable): on MSVC, `/W4` or `/WX` flags from the project may trip on sherpa — apply them to our targets only; the `std::filesystem` and `<numbers>` headers need `/std:c++20` (already set); `ssize_t`/`getpid` in the locator test needs `<unistd.h>` guarded by `#ifndef _WIN32` with `_getpid` from `<process.h>`; on macOS the shared libs need an rpath (Task 3 note).

- [ ] **Step 3b: Windows installer**

In `packaging/Sessio.iss` add the model directory and the sherpa/ONNX Runtime DLLs to `[Files]`, next to the existing zoneinfo entry (copy its form; the source is the install tree's `models` directory and the DLLs beside `Sessio.exe`, with `Flags: recursesubdirs`). Verify by building the installer in CI and checking that `models\gigaam-v3-rnnt\encoder.int8.onnx` and `onnxruntime.dll` are in the Inno Setup file list log.

- [ ] **Step 4: Check the Linux RPM still builds**

```bash
distrobox-host-exec cmake --build build-release --parallel 3
distrobox-host-exec cpack --config build-release/CPackConfig.cmake --generator RPM
distrobox-host-exec rpm -qlp build-release/packages/*.rpm | grep -E "sessio/(models|lib)|onnxruntime|sherpa" | head -20
```

Expected: the RPM lists the models under `/usr/share/sessio/models` and the three libraries under `/usr/lib*/sessio`. Note the RPM size in the final report (expected to grow by roughly 195 MB; this is the accepted trade-off against PR #71).

- [ ] **Step 5: Commit**

```bash
git add .github test
git commit -m "Run transcription model tests and install check in the Linux CI job (#118)"
```

---

## Self-Review

**Spec coverage (phases 0–1).**
- Build gate on all three platforms (FetchContent, pinned model hashes, data-file install per platform, option ON everywhere, CI on Linux/Windows/macOS, Windows installer): Tasks 1, 3, 9. Runtime behaviour on Windows and macOS is verified by the user by hand.
- `Resampler`, `PhraseSegmenter`, `ISpeechRecognizer`, sherpa implementation, `TranscriptionEngine`, `ModelLocator`: Tasks 2, 4, 5, 6, 7.
- Threading rules (copy-only `pushAudio`, one segmenter thread, one decode worker), delay notice, drain with a 5 s default, counted decode failures: Task 6.
- Phrase times from VAD offsets plus track start: Task 6 (`enqueue`).
- Fakes-based tests (order, several tracks, add/remove while running, stop with queue, delay notice) and the real-model and ten-track load tests with p95 ≤ 1.0 s: Tasks 6, 8.
- Deliberately not in this plan: the forced 20 s split (done by the Silero VAD configuration `max_speech_duration`, verified only through the real model); `AudioSink`, `TranscriptionSession`, repository, UI, settings, translations, version bump and CHANGELOG (phases 2–5). The version bump and CHANGELOG are required for the MR and belong to the release phase; do not open the MR before then.
- ONNX Runtime archive hash pinning mentioned in the spec: sherpa's own CMake downloads it with a hash of its own; this plan does not add a second pin. Check the sherpa file `cmake/onnxruntime-linux-x86_64.cmake` in Task 1 Step 4 for a `URL_HASH`, and record it in the ADR if present.

**Placeholder scan.** No TBD/TODO; every code step shows code. Conditional paths (library locations, RPATH, wrapper accessor constness) have an explicit check command and an exact fallback.

**Type consistency.** `TrackId`, `TrackRole`, `TrackInfo`, `TranscribedPhrase`, `ModelPaths`, `SpeechSegment`, `EngineCallbacks`, `EngineConfig`, `EngineStats`, `VadFactory`, and function names (`addTrack`, `removeTrack`, `pushAudio`, `stop`, `stats`, `locateModels`, `modelRootCandidates`, `makeSherpaRecognizer`, `makeSherpaVadFactory`) are used identically across tasks. The spec says `phraseReady` signal; this plan uses `EngineCallbacks::on_phrase` and leaves the Qt signal to `TranscriptionSession` (phase 3).

## Execution

Next plans (written when reached): phase 2 data (schema, repository, backup), phase 3 session and consent (`AudioSink`, taps, `VideoSession` integration), phase 4 interface, phase 5 release.
