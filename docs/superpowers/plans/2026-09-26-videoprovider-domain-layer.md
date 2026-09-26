# VideoProvider/VideoSession Domain Layer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a new `src/video` module providing `VideoProvider` (abstract interface + `LiveKitVideoProvider`, ported from the proven `spike/77-livekit-cpp-spike` branch), `VideoSession` (a `QStateMachine`-driven call lifecycle), and `DeviceManager` (Qt Multimedia device enumeration) — the domain layer issue #80's native call UI will build on, per `docs/video-roadmap.md` §5.2/§5.5.

**Architecture:** `VideoProvider` is a `QObject`-based abstract interface with exactly one production implementation (`LiveKitVideoProvider`), plus a test-only `FakeVideoProvider`. `LiveKitVideoProvider` wraps the LiveKit C++ SDK, ported almost verbatim from the spike (camera/mic capture, remote render/playback), but replaces every stderr-only error path with a Qt signal so failures are observable. `VideoSession` owns a `QStateMachine` over `VideoSessionState` and drives a `VideoProvider`; it never touches `Event`/persistence — recording call outcomes on an `Event` is the native UI's job (#80). `DeviceManager` is a standalone `QMediaDevices` wrapper with no `VideoProvider`/LiveKit dependency.

**Tech Stack:** C++20, Qt6 (Core, Multimedia, MultimediaWidgets, OpenGLWidgets), LiveKit C++ SDK (prebuilt binary release, vendored CMake downloader — no vcpkg port exists), GoogleTest.

## Global Constraints

- No native call UI in this plan — that is issue #80, a separate plan. This plan produces `src/video/` only, consumed by nothing yet (matches #79's Tasks 1-4 building `src/meeting/` before later tasks wired it in — here, the "wiring in" is explicitly out of scope, deferred to #80's own plan).
- No participant page, no mid-call device switching (interface must not preclude adding it later, per ADR discussion, but no `switchCamera()`/`switchMicrophone()` methods in this plan).
- `VideoSession`/`VideoProvider` take no `Event`/`eventId`/database dependency of any kind.
- `LiveKitVideoProvider::join(token)` takes a pre-obtained JWT as a parameter; it never fetches a token itself. It does not take a `url` in this plan's tests since no real token backend exists yet to provide one — the LiveKit server `url` is a second constructor/method parameter alongside `token`, matching what a real caller (built in #80) will have once #78/PR #89 is merged.
- `VideoSession`'s reconnect timeout is a `std::chrono::milliseconds` constructor parameter, default 30000 (30s).
- `FakeVideoProvider` lives only in `test/`, never in the production `Sessio_video` library.
- Every new file follows this codebase's naming/namespace conventions: `pcm::video` namespace (matching `pcm::meeting`), `snake_case.h`/`.cpp` filenames, `PascalCase` classes.
- Raise the application version and update `CHANGELOG.md` per `AGENTS.md` (current version 0.1.33 → 0.1.34).
- Every `tr()` string added must be covered by `cmake --build build-release --target update_translations`, with `translation/app_ru.ts`/`app_en.ts` fully translated (no `type="unfinished"`) before the final commit — this plan adds no user-facing UI strings (no widgets), so this should be a no-op, but the final task verifies it explicitly.
- Use `/usr/bin/git` (not bare `git`) for every git command inside a worktree-isolated SDD session — a shell hook rewrites bare `git` in a way such sessions reject.

---

### Task 1: CMake/vcpkg/CI plumbing for the LiveKit C++ SDK and Qt Multimedia

**Files:**
- Create: `cmake/LiveKitSDK.cmake`
- Create: `src/video/CMakeLists.txt`
- Create: `src/video/video_provider_kind.h` (a single trivial file, just to prove the module builds and links `LiveKit::livekit` — deleted/replaced by real content in Task 7; kept minimal here since this task's only job is to prove the SDK plumbing works, not to design the domain types yet)
- Modify: `CMakeLists.txt:12` (add Qt Multimedia components), `CMakeLists.txt:124` (add `add_subdirectory(src/video)`)
- Modify: `.github/workflows/cmake-multi-platform.yml` (add Qt Multimedia modules to the Linux/Windows/macOS Qt install steps)
- Modify: `CMakeLists.txt` (version bump), `src/app/application.cpp` (version bump), `CHANGELOG.md`

**Interfaces:**
- Produces: a `Sessio_video` CMake static library target, linkable via `target_link_libraries(<consumer> PRIVATE Sessio_video)`, exposing `LiveKit::livekit` transitively so downstream code can `#include <livekit/room.h>` etc.

- [ ] **Step 1: Vendor the LiveKit SDK CMake downloader**

Create `cmake/LiveKitSDK.cmake` with this exact content (copied from the proven spike, which itself copied it verbatim from `livekit-examples/cpp-example-collection`, commit `7abd4b97aebd33a681cfbbed9b739fdc5c2c17e3`, Apache 2.0 — do not hand-edit beyond what's shown):

```cmake
# Vendored from livekit-examples/cpp-example-collection
# (commit 7abd4b97aebd33a681cfbbed9b739fdc5c2c17e3, Apache 2.0). Do not
# hand-edit beyond adjusting default arguments — see spikes/livekit-cpp-spike
# on branch spike/77-livekit-cpp-spike for the original, proven copy.

function(livekit_sdk_setup)
  set(options NO_DOWNLOAD)
  set(oneValueArgs VERSION SDK_DIR REPO SHA256 TRIPLE DOWNLOAD_DIR GITHUB_TOKEN)
  cmake_parse_arguments(LK "${options}" "${oneValueArgs}" "" ${ARGN})

  if(NOT LK_VERSION)
    message(FATAL_ERROR "livekit_sdk_setup: VERSION is required")
  endif()
  if(NOT LK_SDK_DIR)
    message(FATAL_ERROR "livekit_sdk_setup: SDK_DIR is required")
  endif()
  if(NOT LK_REPO)
    set(LK_REPO "livekit/client-sdk-cpp")
  endif()

  if(NOT LK_TRIPLE)
    if(WIN32)
      set(_lk_os "windows")
    elseif(APPLE)
      set(_lk_os "macos")
    else()
      set(_lk_os "linux")
    endif()

    set(_lk_arch "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    if(_lk_arch MATCHES "^(x86_64|amd64|AMD64)$")
      set(_lk_arch "x64")
    elseif(_lk_arch MATCHES "^(arm64|aarch64|ARM64)$")
      set(_lk_arch "arm64")
    endif()

    set(LK_TRIPLE "${_lk_os}-${_lk_arch}")
  endif()

  set(_resolved_version "${LK_VERSION}")
  if(WIN32)
    set(_ext "zip")
  else()
    set(_ext "tar.gz")
  endif()

  set(_archive "livekit-sdk-${LK_TRIPLE}-${_resolved_version}.${_ext}")
  set(_url "https://github.com/${LK_REPO}/releases/download/v${_resolved_version}/${_archive}")
  set(_extracted_root "${LK_SDK_DIR}/livekit-sdk-${LK_TRIPLE}-${_resolved_version}")

  if(NOT LK_NO_DOWNLOAD)
    file(MAKE_DIRECTORY "${LK_SDK_DIR}")
    set(_archive_path "${LK_SDK_DIR}/${_archive}")

    if(NOT EXISTS "${_extracted_root}/lib/cmake")
      message(STATUS "livekit_sdk_setup: downloading ${_url}")
      file(DOWNLOAD "${_url}" "${_archive_path}" SHOW_PROGRESS TLS_VERIFY ON STATUS _st LOG _log)
      list(GET _st 0 _st_code)
      if(NOT _st_code EQUAL 0)
        message(FATAL_ERROR "livekit_sdk_setup: download failed (${_st}): ${_log}")
      endif()

      file(ARCHIVE_EXTRACT INPUT "${_archive_path}" DESTINATION "${LK_SDK_DIR}")

      if(NOT EXISTS "${_extracted_root}/lib/cmake")
        message(FATAL_ERROR "livekit_sdk_setup: expected '${_extracted_root}/lib/cmake' after extraction, not found")
      endif()
    endif()
  endif()

  list(PREPEND CMAKE_PREFIX_PATH "${_extracted_root}")
  set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
  set(LiveKit_DIR "${_extracted_root}/lib/cmake/LiveKit" PARENT_SCOPE)
endfunction()
```

- [ ] **Step 2: Wire the SDK into the top-level build**

In the top-level `CMakeLists.txt`, find this line (currently line 12):

```cmake
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Svg PrintSupport LinguistTools)
```

Replace it with:

```cmake
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Svg PrintSupport LinguistTools Multimedia MultimediaWidgets OpenGLWidgets)
```

Then, immediately after the `include(FetchContent)` line (currently line 15), add:

```cmake
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
set(LIVEKIT_SDK_VERSION "1.12.0" CACHE STRING "LiveKit C++ SDK version")
include(LiveKitSDK)
livekit_sdk_setup(
  VERSION "${LIVEKIT_SDK_VERSION}"
  SDK_DIR "${CMAKE_BINARY_DIR}/_deps/livekit-sdk"
)
find_package(LiveKit CONFIG REQUIRED)
```

- [ ] **Step 3: Create the `src/video` module skeleton**

Create `src/video/video_provider_kind.h` (a placeholder trivial header solely to prove the module links `LiveKit::livekit` end to end — Task 7 replaces this file's content with the real `VideoProvider` interface; keeping this task narrowly about build plumbing, not domain design, is deliberate):

```cpp
#pragma once

// Placeholder: proves Sessio_video links LiveKit::livekit successfully.
// Replaced with the real VideoProvider interface in Task 7 of this plan.

#include <livekit/room.h>

namespace pcm::video {

[[nodiscard]] inline bool liveKitSdkLinked() {
  return true;
}

} // namespace pcm::video
```

Create `src/video/CMakeLists.txt`, matching `src/meeting/CMakeLists.txt`'s exact structure:

```cmake
cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME ${PROJECT_NAME}_video)

find_package(Qt6 REQUIRED COMPONENTS Core Multimedia MultimediaWidgets OpenGLWidgets)

qt_add_library(${TARGET_NAME} STATIC
        video_provider_kind.h
)

target_link_libraries(${TARGET_NAME} PUBLIC
        Qt6::Core
        Qt6::Multimedia
        Qt6::MultimediaWidgets
        Qt6::OpenGLWidgets
        LiveKit::livekit
)

target_include_directories(${TARGET_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

get_filename_component(_lk_cmake_dir "${LiveKit_DIR}" DIRECTORY)
get_filename_component(_lk_lib_dir "${_lk_cmake_dir}" DIRECTORY)
target_link_directories(${TARGET_NAME} PUBLIC "${_lk_lib_dir}")

if(WIN32)
  get_filename_component(_lk_prefix "${_lk_lib_dir}" DIRECTORY)
  set(_lk_bin_dir "${_lk_prefix}/bin")
  foreach(_lk_runtime_dll IN ITEMS livekit.dll livekit_ffi.dll)
    if(EXISTS "${_lk_bin_dir}/${_lk_runtime_dll}")
      add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_lk_bin_dir}/${_lk_runtime_dll}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>")
    endif()
  endforeach()
endif()
```

In the top-level `CMakeLists.txt`, find:

```cmake
add_subdirectory(src/meeting)
```

Add immediately after it:

```cmake
add_subdirectory(src/video)
```

- [ ] **Step 4: Add Qt Multimedia to CI**

In `.github/workflows/cmake-multi-platform.yml`, the Windows/macOS Qt install step currently reads:

```yaml
      - name: Install Qt (Windows)
        if: runner.os == 'Windows' || runner.os == 'macOS'
        uses: jurplel/install-qt-action@v4
        with:
          aqtversion: '==3.3.*'
          version: ${{ matrix.qt_version }}
          host: ${{ matrix.qt_host }}
          target: ${{ matrix.qt_target }}
          arch: ${{ matrix.qt_arch }}
          cache: true
```

Add a `modules:` line:

```yaml
      - name: Install Qt (Windows)
        if: runner.os == 'Windows' || runner.os == 'macOS'
        uses: jurplel/install-qt-action@v4
        with:
          aqtversion: '==3.3.*'
          version: ${{ matrix.qt_version }}
          host: ${{ matrix.qt_host }}
          target: ${{ matrix.qt_target }}
          arch: ${{ matrix.qt_arch }}
          modules: 'qtmultimedia'
          cache: true
```

For Linux, the matrix entry currently reads:

```yaml
          - os: ubuntu-latest
            qt_version: 6.10.2
            artifact_name: Sessio-linux-appimage
            qt_component: qt.qt6.6102.linux_gcc_64
```

Change `qt_component` to a space-separated list including the multimedia addon:

```yaml
          - os: ubuntu-latest
            qt_version: 6.10.2
            artifact_name: Sessio-linux-appimage
            qt_component: "qt.qt6.6102.linux_gcc_64 qt.qt6.6102.addons.qtmultimedia"
```

**Verification note (this exact package/module naming cannot be confirmed without running CI):** Qt's installer package and `install-qt-action` module names for `qtmultimedia`/`OpenGLWidgets` availability per-OS/per-version have not been previously exercised in this repo's CI (the spike's own CI step used `continue-on-error: true` specifically to avoid needing to get this right — see the spike's README "Known gap"). After pushing, check the CI run for all three OSes; if `find_package(Qt6 ... COMPONENTS Multimedia MultimediaWidgets OpenGLWidgets)` fails to find a component, adjust the module/package name for that OS (e.g. Linux's official installer sometimes needs `qt.qt6.6102.addons.qtmultimedia` under a different top-level path, or Windows/macOS may need `qtmultimedia` split differently) and re-push. Do not proceed to Task 2 until all three platforms build green.

- [ ] **Step 5: Bump version and CHANGELOG**

In the top-level `CMakeLists.txt`, change:
```cmake
project(Sessio VERSION 0.1.33 LANGUAGES CXX)
```
to:
```cmake
project(Sessio VERSION 0.1.34 LANGUAGES CXX)
```

In `src/app/application.cpp`, find the line setting `app.setApplicationVersion("0.1.33");` and change it to `app.setApplicationVersion("0.1.34");`.

At the top of `CHANGELOG.md`, add:
```markdown
## [0.1.34] - 2026-09-26

### Added

- Internal groundwork for native LiveKit video calls: a new `src/video`
  module (`VideoProvider`/`LiveKitVideoProvider`, `VideoSession`,
  `DeviceManager`) for the in-call media session, ported from the proven
  `spike/77-livekit-cpp-spike` branch. Not yet wired into the application —
  no visible behavior changes.
```

- [ ] **Step 6: Build and verify**

```bash
cmake -S . -B build -DPCM_BUILD_TESTS=ON
cmake --build build --target Sessio_video --parallel
```

Expected: `Sessio_video` builds successfully, proving `find_package(LiveKit CONFIG REQUIRED)` resolves and `LiveKit::livekit` links. This step downloads a real ~100+ MB SDK archive on first run — expect it to take a few minutes.

- [ ] **Step 7: Commit**

```bash
/usr/bin/git add cmake/LiveKitSDK.cmake src/video/CMakeLists.txt src/video/video_provider_kind.h CMakeLists.txt .github/workflows/cmake-multi-platform.yml src/app/application.cpp CHANGELOG.md
/usr/bin/git commit -m "build: vendor LiveKit C++ SDK and add Qt Multimedia for src/video"
```

---

### Task 2: Pure utilities — `AudioChunker` and `frameConvert`

**Files:**
- Create: `src/video/audio_chunker.h`, `src/video/audio_chunker.cpp`
- Create: `src/video/frame_convert.h`, `src/video/frame_convert.cpp`
- Test: `test/audio_chunker_tests.cpp`, `test/frame_convert_tests.cpp`
- Modify: `src/video/CMakeLists.txt`, `test/CMakeLists.txt`

**Interfaces:**
- Produces: `pcm::video::AudioChunker` (fixed-size PCM frame chunking, framework-free) and `pcm::video::videoFrameToLiveKitRGBA(const QImage&)` (RGBA conversion), both consumed by Task 4/Task 6's capture/render adapters.

- [ ] **Step 1: Write the failing tests**

Create `test/audio_chunker_tests.cpp`:

```cpp
#include "audio_chunker.h"

#include <gtest/gtest.h>

TEST(AudioChunkerTest, BelowThresholdProducesNoFrames) {
  pcm::video::AudioChunker chunker(480, 1);
  const auto frames = chunker.push(std::vector<int16_t>(100, 0));
  EXPECT_TRUE(frames.empty());
}

TEST(AudioChunkerTest, ExactlyOneFrameWorthProducesOneFrame) {
  pcm::video::AudioChunker chunker(480, 1);
  const auto frames = chunker.push(std::vector<int16_t>(480, 7));
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0].size(), 480u);
  EXPECT_EQ(frames[0][0], 7);
}

TEST(AudioChunkerTest, RemainderCarriesAcrossCalls) {
  pcm::video::AudioChunker chunker(480, 1);
  EXPECT_TRUE(chunker.push(std::vector<int16_t>(300, 1)).empty());
  const auto frames = chunker.push(std::vector<int16_t>(300, 2));
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0].size(), 480u);
  EXPECT_EQ(frames[0][0], 1);
  EXPECT_EQ(frames[0][299], 1);
  EXPECT_EQ(frames[0][300], 2);
}

TEST(AudioChunkerTest, MultipleFramesFromOnePush) {
  pcm::video::AudioChunker chunker(100, 1);
  const auto frames = chunker.push(std::vector<int16_t>(250, 9));
  ASSERT_EQ(frames.size(), 2u);
  EXPECT_EQ(frames[0].size(), 100u);
  EXPECT_EQ(frames[1].size(), 100u);
}

TEST(AudioChunkerTest, StereoFrameSizingUsesSamplesPerChannelTimesChannels) {
  pcm::video::AudioChunker chunker(100, 2);
  const auto frames = chunker.push(std::vector<int16_t>(200, 3));
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0].size(), 200u);
}

TEST(AudioChunkerTest, ResetDiscardsPartialRemainder) {
  pcm::video::AudioChunker chunker(480, 1);
  EXPECT_TRUE(chunker.push(std::vector<int16_t>(300, 1)).empty());
  chunker.reset();
  EXPECT_TRUE(chunker.push(std::vector<int16_t>(300, 2)).empty());
  const auto frames = chunker.push(std::vector<int16_t>(180, 2));
  ASSERT_EQ(frames.size(), 1u);
  for (const auto sample : frames[0]) {
    EXPECT_EQ(sample, 2);
  }
}
```

Create `test/frame_convert_tests.cpp`:

```cpp
#include "frame_convert.h"

#include <gtest/gtest.h>

TEST(FrameConvertTest, SolidRedRgb32ConvertsToExpectedRgbaBytes) {
  QImage image(4, 2, QImage::Format_RGB32);
  image.fill(QColor(255, 0, 0));

  const auto frame = pcm::video::videoFrameToLiveKitRGBA(image);

  ASSERT_EQ(frame.width(), 4u);
  ASSERT_EQ(frame.height(), 2u);
  const auto *data = frame.data();
  for (int i = 0; i < 4 * 2; ++i) {
    EXPECT_EQ(data[i * 4 + 0], 255) << "pixel " << i << " red";
    EXPECT_EQ(data[i * 4 + 1], 0) << "pixel " << i << " green";
    EXPECT_EQ(data[i * 4 + 2], 0) << "pixel " << i << " blue";
    EXPECT_EQ(data[i * 4 + 3], 255) << "pixel " << i << " alpha";
  }
}

TEST(FrameConvertTest, Argb32SourceReordersChannelsCorrectly) {
  QImage image(2, 2, QImage::Format_ARGB32);
  image.fill(QColor(10, 20, 30, 200));

  const auto frame = pcm::video::videoFrameToLiveKitRGBA(image);

  const auto *data = frame.data();
  EXPECT_EQ(data[0], 10);
  EXPECT_EQ(data[1], 20);
  EXPECT_EQ(data[2], 30);
  EXPECT_EQ(data[3], 200);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Add the test targets to `test/CMakeLists.txt` first (see Step 3 below for exact content), then:

```bash
cmake -S . -B build -DPCM_BUILD_TESTS=ON
cmake --build build --target Sessio_audio_chunker_tests Sessio_frame_convert_tests --parallel
```

Expected: FAIL — `audio_chunker.h`/`frame_convert.h` do not exist yet.

- [ ] **Step 3: Add the test targets**

In `test/CMakeLists.txt`, after the `Sessio_provider_kind_tests` block (or any convenient location among the meeting-test blocks), add:

```cmake
# Audio chunker tests (framework-free, no Qt/LiveKit needed)
add_executable(Sessio_audio_chunker_tests
    ${CMAKE_SOURCE_DIR}/src/video/audio_chunker.cpp
    audio_chunker_tests.cpp
)
target_include_directories(Sessio_audio_chunker_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_audio_chunker_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(Sessio_audio_chunker_tests)

# Frame conversion tests (needs Qt::Gui for QImage, and LiveKit for VideoFrame)
add_executable(Sessio_frame_convert_tests
    ${CMAKE_SOURCE_DIR}/src/video/frame_convert.cpp
    frame_convert_tests.cpp
)
target_include_directories(Sessio_frame_convert_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_frame_convert_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Core
    Qt6::Gui
    LiveKit::livekit
)
gtest_discover_tests(Sessio_frame_convert_tests)
```

- [ ] **Step 4: Implement `AudioChunker`**

Create `src/video/audio_chunker.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace pcm::video {

// Accumulates interleaved PCM samples and emits fixed-size frames as soon as
// enough samples have arrived. Framework-free (no Qt, no LiveKit) so it is
// unit-testable without a running application.
class AudioChunker {
public:
  AudioChunker(std::size_t samplesPerChannel, int channels);

  [[nodiscard]] std::vector<std::vector<int16_t>>
  push(const std::vector<int16_t> &newSamples);

  // Discards any partial-frame remainder. Call this when switching capture
  // devices so stale samples from the old device are never prepended to the
  // new device's stream.
  void reset();

private:
  std::size_t mFrameSize;
  std::deque<int16_t> mBuffer;
};

} // namespace pcm::video
```

Create `src/video/audio_chunker.cpp`:

```cpp
#include "audio_chunker.h"

namespace pcm::video {

AudioChunker::AudioChunker(const std::size_t samplesPerChannel, const int channels)
    : mFrameSize(samplesPerChannel * static_cast<std::size_t>(channels)) {}

std::vector<std::vector<int16_t>>
AudioChunker::push(const std::vector<int16_t> &newSamples) {
  mBuffer.insert(mBuffer.end(), newSamples.begin(), newSamples.end());

  std::vector<std::vector<int16_t>> frames;
  while (mBuffer.size() >= mFrameSize) {
    frames.emplace_back(mBuffer.begin(), mBuffer.begin() + static_cast<long>(mFrameSize));
    mBuffer.erase(mBuffer.begin(), mBuffer.begin() + static_cast<long>(mFrameSize));
  }
  return frames;
}

void AudioChunker::reset() {
  mBuffer.clear();
}

} // namespace pcm::video
```

- [ ] **Step 5: Implement `frameConvert`**

Create `src/video/frame_convert.h`:

```cpp
#pragma once

#include <QImage>
#include <livekit/video_frame.h>

namespace pcm::video {

// Converts a QImage to a livekit::VideoFrame in RGBA byte order. RGBA is used
// directly (no I420 conversion) since LiveKit's VideoSource accepts
// RGBA-format frames, matching the LiveKit SDK's own examples.
[[nodiscard]] livekit::VideoFrame videoFrameToLiveKitRGBA(const QImage &image);

} // namespace pcm::video
```

Create `src/video/frame_convert.cpp`:

```cpp
#include "frame_convert.h"

namespace pcm::video {

livekit::VideoFrame videoFrameToLiveKitRGBA(const QImage &image) {
  const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
  const auto width = static_cast<uint32_t>(rgba.width());
  const auto height = static_cast<uint32_t>(rgba.height());

  auto frame = livekit::VideoFrame::create(width, height, livekit::VideoBufferType::RGBA);

  const auto stride = width * 4;
  for (uint32_t y = 0; y < height; ++y) {
    std::memcpy(frame.data() + y * stride, rgba.constScanLine(static_cast<int>(y)), stride);
  }

  return frame;
}

} // namespace pcm::video
```

- [ ] **Step 6: Run tests to verify they pass**

```bash
cmake --build build --target Sessio_audio_chunker_tests Sessio_frame_convert_tests --parallel
ctest --test-dir build -R "AudioChunkerTest|FrameConvertTest" --output-on-failure
```

Expected: PASS, all 8 cases.

- [ ] **Step 7: Add the two new files to `src/video/CMakeLists.txt`**

Update the `qt_add_library` call in `src/video/CMakeLists.txt` to:

```cmake
qt_add_library(${TARGET_NAME} STATIC
        video_provider_kind.h
        audio_chunker.h
        audio_chunker.cpp
        frame_convert.h
        frame_convert.cpp
)
```

- [ ] **Step 8: Commit**

```bash
/usr/bin/git add src/video/audio_chunker.h src/video/audio_chunker.cpp src/video/frame_convert.h src/video/frame_convert.cpp src/video/CMakeLists.txt test/audio_chunker_tests.cpp test/frame_convert_tests.cpp test/CMakeLists.txt
/usr/bin/git commit -m "feat: add AudioChunker and frameConvert utilities to src/video"
```

---

### Task 3: `DeviceManager`

**Files:**
- Create: `src/video/device_manager.h`, `src/video/device_manager.cpp`
- Test: `test/device_manager_tests.cpp`
- Modify: `src/video/CMakeLists.txt`, `test/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `pcm::video::DeviceManager` — `cameras()`, `microphones()`, `speakers()` (each returning `QList<QCameraDevice>`/`QList<QAudioDevice>`), consumed later by `LiveKitVideoProvider` (Task 7) for default-device selection. No dependency on `VideoProvider` or LiveKit.

- [ ] **Step 1: Write the failing test**

Create `test/device_manager_tests.cpp`:

```cpp
#include "device_manager.h"

#include <QCoreApplication>
#include <gtest/gtest.h>

TEST(DeviceManagerTest, ListsCamerasMicrophonesAndSpeakersWithoutCrashing) {
  int argc = 0;
  QCoreApplication app(argc, nullptr);

  pcm::video::DeviceManager manager;

  // No real devices are guaranteed in a CI/sandboxed environment — this test
  // proves DeviceManager doesn't crash and returns well-formed (possibly
  // empty) lists, not that specific hardware is present.
  EXPECT_NO_THROW(static_cast<void>(manager.cameras()));
  EXPECT_NO_THROW(static_cast<void>(manager.microphones()));
  EXPECT_NO_THROW(static_cast<void>(manager.speakers()));
}

TEST(DeviceManagerTest, DefaultCameraIsNulloptWhenNoCamerasPresent) {
  int argc = 0;
  QCoreApplication app(argc, nullptr);

  pcm::video::DeviceManager manager;
  if (manager.cameras().isEmpty()) {
    EXPECT_FALSE(manager.defaultCamera().has_value());
  } else {
    EXPECT_TRUE(manager.defaultCamera().has_value());
  }
}
```

- [ ] **Step 2: Add the test target and run to verify it fails**

In `test/CMakeLists.txt`, add:

```cmake
# Device manager tests
add_executable(Sessio_device_manager_tests
    ${CMAKE_SOURCE_DIR}/src/video/device_manager.cpp
    device_manager_tests.cpp
)
target_include_directories(Sessio_device_manager_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_device_manager_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Core
    Qt6::Multimedia
)
gtest_discover_tests(Sessio_device_manager_tests)
```

```bash
cmake -S . -B build -DPCM_BUILD_TESTS=ON
cmake --build build --target Sessio_device_manager_tests --parallel
```

Expected: FAIL — `device_manager.h` does not exist yet.

- [ ] **Step 3: Implement `DeviceManager`**

Create `src/video/device_manager.h`:

```cpp
#pragma once

#include <QAudioDevice>
#include <QCameraDevice>
#include <QList>
#include <optional>

namespace pcm::video {

// Thin wrapper over QMediaDevices for enumerating and selecting the
// camera/microphone/speaker. Has no dependency on VideoProvider or the
// LiveKit SDK, so device-selection UI is testable independent of any real
// call.
class DeviceManager {
public:
  [[nodiscard]] QList<QCameraDevice> cameras() const;
  [[nodiscard]] QList<QAudioDevice> microphones() const;
  [[nodiscard]] QList<QAudioDevice> speakers() const;

  [[nodiscard]] std::optional<QCameraDevice> defaultCamera() const;
  [[nodiscard]] std::optional<QAudioDevice> defaultMicrophone() const;
  [[nodiscard]] std::optional<QAudioDevice> defaultSpeaker() const;
};

} // namespace pcm::video
```

Create `src/video/device_manager.cpp`:

```cpp
#include "device_manager.h"

#include <QMediaDevices>

namespace pcm::video {

QList<QCameraDevice> DeviceManager::cameras() const {
  return QMediaDevices::videoInputs();
}

QList<QAudioDevice> DeviceManager::microphones() const {
  return QMediaDevices::audioInputs();
}

QList<QAudioDevice> DeviceManager::speakers() const {
  return QMediaDevices::audioOutputs();
}

std::optional<QCameraDevice> DeviceManager::defaultCamera() const {
  const auto devices = cameras();
  if (devices.isEmpty()) {
    return std::nullopt;
  }
  return devices.first();
}

std::optional<QAudioDevice> DeviceManager::defaultMicrophone() const {
  const auto devices = microphones();
  if (devices.isEmpty()) {
    return std::nullopt;
  }
  return devices.first();
}

std::optional<QAudioDevice> DeviceManager::defaultSpeaker() const {
  const auto devices = speakers();
  if (devices.isEmpty()) {
    return std::nullopt;
  }
  return devices.first();
}

} // namespace pcm::video
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build --target Sessio_device_manager_tests --parallel
ctest --test-dir build -R DeviceManagerTest --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Add the new files to `src/video/CMakeLists.txt`**

```cmake
qt_add_library(${TARGET_NAME} STATIC
        video_provider_kind.h
        audio_chunker.h
        audio_chunker.cpp
        frame_convert.h
        frame_convert.cpp
        device_manager.h
        device_manager.cpp
)
```

- [ ] **Step 6: Commit**

```bash
/usr/bin/git add src/video/device_manager.h src/video/device_manager.cpp src/video/CMakeLists.txt test/device_manager_tests.cpp test/CMakeLists.txt
/usr/bin/git commit -m "feat: add DeviceManager for camera/mic/speaker enumeration"
```

---

### Task 4: `VideoCaptureAdapter`/`VideoCaptureWorker` (camera capture)

**Files:**
- Create: `src/video/video_capture_adapter.h`, `src/video/video_capture_adapter.cpp`
- Create: `src/video/video_capture_worker.h`, `src/video/video_capture_worker.cpp`
- Test: `test/video_capture_adapter_smoke_test.cpp`
- Modify: `src/video/CMakeLists.txt`, `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `pcm::video::videoFrameToLiveKitRGBA` (Task 2).
- Produces: `pcm::video::VideoCaptureAdapter` — `start(QCameraDevice)`, `stop()`, `videoSource()` (a `std::shared_ptr<livekit::VideoSource>`, consumed by `LiveKitVideoProvider` in Task 7 to publish a local video track), `previewSink()` (a `QVideoSink*`, for local preview — not consumed by anything in this plan, but part of the interface #80's UI will need), and a `captureFailed(QString)` signal (this plan's departure from the spike's stderr-only error reporting).

This is a direct, deliberate port of the spike's `VideoCaptureAdapter`/`VideoCaptureWorker` pair (the spike's git history shows this exact split — worker thread offloading — was itself a real bug fix, commit `b7d2f5c`: earlier single-threaded versions blocked the GUI thread on `captureFrame()`'s FFI call). The only behavioral change from the spike: `VideoCaptureWorker::processFrame`'s caught exception is turned into a `captureFailed(QString)` signal instead of a bare `std::cerr` write, since this plan's Global Constraints require observable failures.

- [ ] **Step 1: Write the failing smoke test**

This class needs a running Qt event loop and `QThread` machinery that doesn't fit GoogleTest's own `main()` cleanly — following the spike's own precedent, this is a standalone smoke test with its own `main()`, driven by a watchdog timer that fails the test if start/stop cycling hangs. It does **not** prove frames are captured correctly (no real camera is guaranteed in CI/sandboxed environments) — only that repeated start/stop/destroy cycles never hang or crash. This directly covers this plan's Task 9 acceptance criterion (the #77 spike's untested "clean leave/destruction" gate item) for the video-capture half of that requirement.

Create `test/video_capture_adapter_smoke_test.cpp`:

```cpp
#include "video_capture_adapter.h"

#include <QCoreApplication>
#include <QMediaDevices>
#include <QTimer>
#include <iostream>

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);

  // 10-second watchdog: if repeated start/stop/destroy cycling hangs, this
  // fires and the process exits non-zero instead of hanging CI forever.
  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, [] {
    std::cerr << "video_capture_adapter_smoke_test: watchdog fired, hang detected" << std::endl;
    std::exit(1);
  });
  watchdog.start(10000);

  const auto devices = QMediaDevices::videoInputs();

  for (int cycle = 0; cycle < 20; ++cycle) {
    auto adapter = std::make_unique<pcm::video::VideoCaptureAdapter>();
    if (!devices.isEmpty()) {
      adapter->start(devices.first());
    }
    // Destroy immediately without an explicit stop() — the destructor must
    // tear down the worker thread cleanly on its own, mirroring how
    // LiveKitVideoProvider's own destructor (Task 7) will rely on this.
    adapter.reset();
  }

  std::cout << "video_capture_adapter_smoke_test: 20 start/destroy cycles completed" << std::endl;
  return 0;
}
```

- [ ] **Step 2: Add the test target and run to verify it fails**

In `test/CMakeLists.txt`, add:

```cmake
# Video capture adapter smoke test (own main(), not GoogleTest — needs a
# running Qt event loop and QThread lifecycle, doesn't fit gtest's own
# main()). Proves repeated start/stop/destroy cycles never hang, per this
# plan's Task 9 lifecycle-stress acceptance criterion.
add_executable(Sessio_video_capture_adapter_smoke_test
    ${CMAKE_SOURCE_DIR}/src/video/video_capture_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/video/video_capture_worker.cpp
    ${CMAKE_SOURCE_DIR}/src/video/frame_convert.cpp
    video_capture_adapter_smoke_test.cpp
)
target_include_directories(Sessio_video_capture_adapter_smoke_test PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_video_capture_adapter_smoke_test PRIVATE
    Qt6::Core
    Qt6::Multimedia
    LiveKit::livekit
)
add_test(NAME VideoCaptureAdapterSmokeTest COMMAND Sessio_video_capture_adapter_smoke_test)
set_tests_properties(VideoCaptureAdapterSmokeTest PROPERTIES TIMEOUT 30)
```

```bash
cmake -S . -B build -DPCM_BUILD_TESTS=ON
cmake --build build --target Sessio_video_capture_adapter_smoke_test --parallel
```

Expected: FAIL — `video_capture_adapter.h`/`video_capture_worker.h` do not exist yet.

- [ ] **Step 3: Implement `VideoCaptureWorker`**

Create `src/video/video_capture_worker.h`:

```cpp
#pragma once

#include <QObject>
#include <QVideoFrame>
#include <atomic>
#include <livekit/video_source.h>
#include <memory>

namespace pcm::video {

// Runs on a dedicated QThread (owned by VideoCaptureAdapter). Does the
// CPU-heavy work of converting a captured QVideoFrame and calling into the
// LiveKit FFI, off the GUI thread, so GUI repaint is never blocked.
class VideoCaptureWorker final : public QObject {
  Q_OBJECT
public:
  VideoCaptureWorker(std::shared_ptr<livekit::VideoSource> videoSource,
                     std::atomic<int> &framesCaptured);

  void setRunning(bool running) { mRunning.store(running); }

public slots:
  void processFrame(const QVideoFrame &frame);

signals:
  void frameCaptured();
  void captureFailed(QString reason);

private:
  std::shared_ptr<livekit::VideoSource> mVideoSource;
  std::atomic<int> &mFramesCaptured;
  std::atomic<bool> mRunning{true};
};

} // namespace pcm::video
```

Create `src/video/video_capture_worker.cpp`:

```cpp
#include "video_capture_worker.h"

#include "frame_convert.h"

namespace pcm::video {

VideoCaptureWorker::VideoCaptureWorker(std::shared_ptr<livekit::VideoSource> videoSource,
                                       std::atomic<int> &framesCaptured)
    : mVideoSource(std::move(videoSource)), mFramesCaptured(framesCaptured) {}

void VideoCaptureWorker::processFrame(const QVideoFrame &frame) {
  if (!mRunning.load() || !frame.isValid()) {
    return;
  }

  const QImage image = frame.toImage();
  if (image.isNull()) {
    return;
  }

  try {
    auto liveKitFrame = videoFrameToLiveKitRGBA(image);
    mVideoSource->captureFrame(liveKitFrame, 0, livekit::VideoRotation::VIDEO_ROTATION_0);
    mFramesCaptured.fetch_add(1);
    emit frameCaptured();
  } catch (const std::exception &e) {
    emit captureFailed(QString::fromUtf8(e.what()));
  }
}

} // namespace pcm::video
```

- [ ] **Step 4: Implement `VideoCaptureAdapter`**

Create `src/video/video_capture_adapter.h`:

```cpp
#pragma once

#include <QCamera>
#include <QCameraDevice>
#include <QMediaCaptureSession>
#include <QObject>
#include <QThread>
#include <QVideoSink>
#include <atomic>
#include <livekit/video_source.h>
#include <memory>

namespace pcm::video {

class VideoCaptureWorker;

// Captures camera frames via Qt Multimedia and feeds them into a
// livekit::VideoSource. The conversion + LiveKit FFI call happens on a
// dedicated worker thread (VideoCaptureWorker) so the GUI thread is never
// blocked by capture.
class VideoCaptureAdapter final : public QObject {
  Q_OBJECT
public:
  explicit VideoCaptureAdapter(QObject *parent = nullptr);
  ~VideoCaptureAdapter() override;

  [[nodiscard]] std::shared_ptr<livekit::VideoSource> videoSource() const { return mVideoSource; }
  [[nodiscard]] QVideoSink *previewSink() { return &mSink; }
  [[nodiscard]] int framesCaptured() const { return mFramesCaptured.load(); }

  // Starts (or restarts, for a device switch) capture from the given device.
  void start(const QCameraDevice &device);
  void stop();

signals:
  void frameCaptured();
  void captureFailed(QString reason);

private slots:
  void onVideoFrameChanged(const QVideoFrame &frame);

private:
  static constexpr int kVideoWidth = 1280;
  static constexpr int kVideoHeight = 720;

  std::shared_ptr<livekit::VideoSource> mVideoSource;
  std::unique_ptr<QCamera> mCamera;
  QMediaCaptureSession mSession;
  QVideoSink mSink;
  std::atomic<int> mFramesCaptured{0};
  QThread mWorkerThread;
  std::unique_ptr<VideoCaptureWorker> mWorker;
};

} // namespace pcm::video
```

Create `src/video/video_capture_adapter.cpp`:

```cpp
#include "video_capture_adapter.h"

#include "video_capture_worker.h"

namespace pcm::video {

VideoCaptureAdapter::VideoCaptureAdapter(QObject *parent)
    : QObject(parent),
      mVideoSource(std::make_shared<livekit::VideoSource>(kVideoWidth, kVideoHeight)) {
  mSession.setVideoSink(&mSink);
  connect(&mSink, &QVideoSink::videoFrameChanged, this,
          &VideoCaptureAdapter::onVideoFrameChanged);
}

VideoCaptureAdapter::~VideoCaptureAdapter() {
  stop();
}

void VideoCaptureAdapter::start(const QCameraDevice &device) {
  stop();

  mCamera = std::make_unique<QCamera>(device);
  mSession.setCamera(mCamera.get());

  mWorker = std::make_unique<VideoCaptureWorker>(mVideoSource, mFramesCaptured);
  connect(mWorker.get(), &VideoCaptureWorker::frameCaptured, this,
          &VideoCaptureAdapter::frameCaptured);
  connect(mWorker.get(), &VideoCaptureWorker::captureFailed, this,
          &VideoCaptureAdapter::captureFailed);
  mWorker->moveToThread(&mWorkerThread);
  mWorkerThread.start();

  mCamera->start();
}

void VideoCaptureAdapter::stop() {
  if (mCamera) {
    mCamera->stop();
    mSession.setCamera(nullptr);
    mCamera.reset();
  }

  if (mWorker) {
    mWorker->setRunning(false);
    mWorkerThread.quit();
    mWorkerThread.wait();
    mWorker.reset();
  }
}

void VideoCaptureAdapter::onVideoFrameChanged(const QVideoFrame &frame) {
  if (!mWorker) {
    return;
  }
  QMetaObject::invokeMethod(mWorker.get(), "processFrame", Qt::QueuedConnection,
                            Q_ARG(QVideoFrame, frame));
}

} // namespace pcm::video
```

- [ ] **Step 5: Run the smoke test to verify it passes**

```bash
cmake --build build --target Sessio_video_capture_adapter_smoke_test --parallel
ctest --test-dir build -R VideoCaptureAdapterSmokeTest --output-on-failure
```

Expected: PASS ("20 start/destroy cycles completed"), well under the 30-second timeout, on a machine with no camera present as well as one with a camera.

- [ ] **Step 6: Add the new files to `src/video/CMakeLists.txt`**

```cmake
qt_add_library(${TARGET_NAME} STATIC
        video_provider_kind.h
        audio_chunker.h
        audio_chunker.cpp
        frame_convert.h
        frame_convert.cpp
        device_manager.h
        device_manager.cpp
        video_capture_worker.h
        video_capture_worker.cpp
        video_capture_adapter.h
        video_capture_adapter.cpp
)
```

- [ ] **Step 7: Commit**

```bash
/usr/bin/git add src/video/video_capture_worker.h src/video/video_capture_worker.cpp src/video/video_capture_adapter.h src/video/video_capture_adapter.cpp src/video/CMakeLists.txt test/video_capture_adapter_smoke_test.cpp test/CMakeLists.txt
/usr/bin/git commit -m "feat: add VideoCaptureAdapter/VideoCaptureWorker for camera capture"
```

---

### Task 5: `AudioCaptureAdapter` (microphone capture)

**Files:**
- Create: `src/video/audio_capture_adapter.h`, `src/video/audio_capture_adapter.cpp`
- Test: `test/audio_capture_adapter_smoke_test.cpp`
- Modify: `src/video/CMakeLists.txt`, `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `pcm::video::AudioChunker` (Task 2).
- Produces: `pcm::video::AudioCaptureAdapter` — `start(QAudioDevice)`, `stop()`, `audioSource()` (a `std::shared_ptr<livekit::AudioSource>`, consumed by `LiveKitVideoProvider` in Task 7), and a `captureFailed(QString)` signal.

**Critical gotcha, confirmed from the spike's own bug-fix history (commit `6b63162`):** `livekit::AudioSource`'s third constructor argument is a `queue_size_ms` buffered-queue window, **not** a frame duration. It must be `0` for real-time/synchronous capture mode — passing the 10ms frame duration there (an earlier, buggy version of this exact code did) puts `AudioSource` into buffered mode where `captureFrame()` blocks the calling thread up to a 20ms timeout and can throw, which is disastrous on this class's GUI-thread call path (unlike video capture, audio capture in this design runs on the GUI thread, since real-time-mode `captureFrame()` doesn't block — see Task 4's threading model note in that task's file header).

- [ ] **Step 1: Write the failing smoke test**

Create `test/audio_capture_adapter_smoke_test.cpp`, mirroring Task 4's video smoke test pattern:

```cpp
#include "audio_capture_adapter.h"

#include <QCoreApplication>
#include <QMediaDevices>
#include <QTimer>
#include <iostream>

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);

  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, [] {
    std::cerr << "audio_capture_adapter_smoke_test: watchdog fired, hang detected" << std::endl;
    std::exit(1);
  });
  watchdog.start(10000);

  const auto devices = QMediaDevices::audioInputs();

  for (int cycle = 0; cycle < 20; ++cycle) {
    auto adapter = std::make_unique<pcm::video::AudioCaptureAdapter>();
    if (!devices.isEmpty()) {
      adapter->start(devices.first());
    }
    adapter.reset();
  }

  std::cout << "audio_capture_adapter_smoke_test: 20 start/destroy cycles completed" << std::endl;
  return 0;
}
```

- [ ] **Step 2: Add the test target and run to verify it fails**

```cmake
add_executable(Sessio_audio_capture_adapter_smoke_test
    ${CMAKE_SOURCE_DIR}/src/video/audio_capture_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/video/audio_chunker.cpp
    audio_capture_adapter_smoke_test.cpp
)
target_include_directories(Sessio_audio_capture_adapter_smoke_test PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_audio_capture_adapter_smoke_test PRIVATE
    Qt6::Core
    Qt6::Multimedia
    LiveKit::livekit
)
add_test(NAME AudioCaptureAdapterSmokeTest COMMAND Sessio_audio_capture_adapter_smoke_test)
set_tests_properties(AudioCaptureAdapterSmokeTest PROPERTIES TIMEOUT 30)
```

```bash
cmake -S . -B build -DPCM_BUILD_TESTS=ON
cmake --build build --target Sessio_audio_capture_adapter_smoke_test --parallel
```

Expected: FAIL — `audio_capture_adapter.h` does not exist yet.

- [ ] **Step 3: Implement `AudioCaptureAdapter`**

Create `src/video/audio_capture_adapter.h`:

```cpp
#pragma once

#include "audio_chunker.h"

#include <QAudioDevice>
#include <QAudioSource>
#include <QObject>
#include <atomic>
#include <livekit/audio_source.h>
#include <memory>

namespace pcm::video {

// Captures microphone audio via Qt Multimedia and feeds fixed-size PCM
// frames into a livekit::AudioSource. Runs entirely on the GUI thread: the
// AudioSource is constructed in real-time (non-buffered) mode specifically
// so captureFrame() never blocks — no worker thread is needed here, unlike
// VideoCaptureAdapter.
class AudioCaptureAdapter final : public QObject {
  Q_OBJECT
public:
  static constexpr int kSampleRate = 48000;
  static constexpr int kChannels = 1;
  static constexpr int kFrameMs = 10;

  explicit AudioCaptureAdapter(QObject *parent = nullptr);
  ~AudioCaptureAdapter() override;

  [[nodiscard]] std::shared_ptr<livekit::AudioSource> audioSource() const { return mAudioSource; }
  [[nodiscard]] int framesCaptured() const { return mFramesCaptured.load(); }

  void start(const QAudioDevice &device);
  void stop();

signals:
  void frameCaptured();
  void captureFailed(QString reason);

private slots:
  void onReadyRead();

private:
  // Real-time capture mode: the 3rd AudioSource constructor argument is a
  // queue_size_ms buffered-queue window, NOT a frame duration. It must stay
  // 0 — passing kFrameMs here puts AudioSource into buffered mode, where
  // captureFrame() can block the calling (GUI) thread for up to 20ms and
  // throw. mChunker's frame sizing below is unrelated to this argument.
  std::shared_ptr<livekit::AudioSource> mAudioSource{
      std::make_shared<livekit::AudioSource>(kSampleRate, kChannels, 0)};
  std::unique_ptr<QAudioSource> mSource;
  QIODevice *mIoDevice{nullptr};
  AudioChunker mChunker{kSampleRate * kFrameMs / 1000, kChannels};
  std::atomic<int> mFramesCaptured{0};
};

} // namespace pcm::video
```

Create `src/video/audio_capture_adapter.cpp`:

```cpp
#include "audio_capture_adapter.h"

#include <QAudioFormat>
#include <cstring>

namespace pcm::video {

AudioCaptureAdapter::AudioCaptureAdapter(QObject *parent) : QObject(parent) {}

AudioCaptureAdapter::~AudioCaptureAdapter() {
  stop();
}

void AudioCaptureAdapter::start(const QAudioDevice &device) {
  stop();
  mChunker.reset();

  QAudioFormat format;
  format.setSampleRate(kSampleRate);
  format.setChannelCount(kChannels);
  format.setSampleFormat(QAudioFormat::Int16);

  mSource = std::make_unique<QAudioSource>(device, format, nullptr);
  mIoDevice = mSource->start();
  if (mIoDevice) {
    connect(mIoDevice, &QIODevice::readyRead, this, &AudioCaptureAdapter::onReadyRead);
  }
}

void AudioCaptureAdapter::stop() {
  if (mSource) {
    mSource->stop();
    mSource.reset();
  }
  mIoDevice = nullptr;
}

void AudioCaptureAdapter::onReadyRead() {
  if (!mIoDevice) {
    return;
  }

  const QByteArray bytes = mIoDevice->readAll();
  std::vector<int16_t> samples(static_cast<std::size_t>(bytes.size()) / sizeof(int16_t));
  std::memcpy(samples.data(), bytes.constData(), samples.size() * sizeof(int16_t));

  for (const auto &pcmFrame : mChunker.push(samples)) {
    try {
      auto liveKitFrame = livekit::AudioFrame::create(
          kSampleRate, kChannels, pcmFrame.size() / static_cast<std::size_t>(kChannels));
      std::memcpy(liveKitFrame.data(), pcmFrame.data(), pcmFrame.size() * sizeof(int16_t));
      mAudioSource->captureFrame(liveKitFrame);
      mFramesCaptured.fetch_add(1);
      emit frameCaptured();
    } catch (const std::exception &e) {
      emit captureFailed(QString::fromUtf8(e.what()));
    }
  }
}

} // namespace pcm::video
```

- [ ] **Step 4: Run the smoke test to verify it passes**

```bash
cmake --build build --target Sessio_audio_capture_adapter_smoke_test --parallel
ctest --test-dir build -R AudioCaptureAdapterSmokeTest --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Add the new files to `src/video/CMakeLists.txt`**

```cmake
qt_add_library(${TARGET_NAME} STATIC
        video_provider_kind.h
        audio_chunker.h
        audio_chunker.cpp
        frame_convert.h
        frame_convert.cpp
        device_manager.h
        device_manager.cpp
        video_capture_worker.h
        video_capture_worker.cpp
        video_capture_adapter.h
        video_capture_adapter.cpp
        audio_capture_adapter.h
        audio_capture_adapter.cpp
)
```

- [ ] **Step 6: Commit**

```bash
/usr/bin/git add src/video/audio_capture_adapter.h src/video/audio_capture_adapter.cpp src/video/CMakeLists.txt test/audio_capture_adapter_smoke_test.cpp test/CMakeLists.txt
/usr/bin/git commit -m "feat: add AudioCaptureAdapter for microphone capture"
```

---

### Task 6: `RemoteVideoRenderer` and `RemoteAudioPlayer`

**Files:**
- Create: `src/video/remote_video_renderer.h`, `src/video/remote_video_renderer.cpp`
- Create: `src/video/remote_audio_player.h`, `src/video/remote_audio_player.cpp`
- Test: `test/remote_video_renderer_smoke_test.cpp`
- Modify: `src/video/CMakeLists.txt`, `test/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing from earlier tasks directly (both operate on `livekit::Track` handles supplied by `LiveKitVideoProvider` in Task 7).
- Produces: `pcm::video::RemoteVideoRenderer` (a `QOpenGLWidget` — `attachTrack(shared_ptr<livekit::Track>)`, `detach()`) and `pcm::video::RemoteAudioPlayer` (`attachTrack(shared_ptr<livekit::Track>, QAudioDevice)`, `detach()`), both consumed by `LiveKitVideoProvider` in Task 7 when a remote track is subscribed.

**Load-bearing detail, confirmed from the spike's bug-fix history:** `detach()` must call `mStream->close()` **before** joining the reader thread. Without it, the reader thread can be blocked forever inside `mStream->read()` if the remote track goes silent with no more frames coming, and `detach()` (or the destructor) would hang waiting for `mReaderThread.join()`. This is not decorative — preserve the exact order.

- [ ] **Step 1: Write the failing smoke test**

Neither class can be meaningfully exercised without a real, subscribed remote LiveKit track (out of reach for an automated test with no LiveKit server/second participant). The test here proves what CAN be proven without one: repeated attach-with-null/detach/destroy cycles never hang, covering the same "clean teardown" concern as Task 4/5's smoke tests, for the remote-track half of the pipeline.

Create `test/remote_video_renderer_smoke_test.cpp`:

```cpp
#include "remote_video_renderer.h"

#include <QApplication>
#include <QTimer>
#include <iostream>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);

  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, [] {
    std::cerr << "remote_video_renderer_smoke_test: watchdog fired, hang detected" << std::endl;
    std::exit(1);
  });
  watchdog.start(10000);

  for (int cycle = 0; cycle < 20; ++cycle) {
    auto renderer = std::make_unique<pcm::video::RemoteVideoRenderer>();
    // No real track available in this environment — attachTrack(nullptr) and
    // an immediate detach()/destroy prove the null-track and never-attached
    // paths don't hang or crash, which is what a Leave-before-any-remote-
    // track-ever-subscribed race would exercise in production.
    renderer->attachTrack(nullptr);
    renderer->detach();
    renderer.reset();
  }

  std::cout << "remote_video_renderer_smoke_test: 20 attach/detach/destroy cycles completed"
            << std::endl;
  return 0;
}
```

- [ ] **Step 2: Add the test target and run to verify it fails**

```cmake
add_executable(Sessio_remote_video_renderer_smoke_test
    ${CMAKE_SOURCE_DIR}/src/video/remote_video_renderer.cpp
    remote_video_renderer_smoke_test.cpp
)
target_include_directories(Sessio_remote_video_renderer_smoke_test PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_remote_video_renderer_smoke_test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGLWidgets
    LiveKit::livekit
)
add_test(NAME RemoteVideoRendererSmokeTest COMMAND Sessio_remote_video_renderer_smoke_test)
set_tests_properties(RemoteVideoRendererSmokeTest PROPERTIES TIMEOUT 30)
```

```bash
cmake -S . -B build -DPCM_BUILD_TESTS=ON
cmake --build build --target Sessio_remote_video_renderer_smoke_test --parallel
```

Expected: FAIL — `remote_video_renderer.h` does not exist yet.

- [ ] **Step 3: Implement `RemoteVideoRenderer`**

Create `src/video/remote_video_renderer.h`:

```cpp
#pragma once

#include <QMutex>
#include <QOpenGLWidget>
#include <atomic>
#include <livekit/video_stream.h>
#include <memory>
#include <thread>

namespace pcm::video {

// Renders a subscribed remote LiveKit video track. Pulls frames on a
// dedicated std::thread (blocking VideoStream::read()) and repaints via
// plain QPainter — deliberately not hand-written GL texture upload, which
// would be a large scope increase for no measured benefit yet.
class RemoteVideoRenderer final : public QOpenGLWidget {
  Q_OBJECT
public:
  explicit RemoteVideoRenderer(QWidget *parent = nullptr);
  ~RemoteVideoRenderer() override;

  void attachTrack(const std::shared_ptr<livekit::Track> &track);
  void detach();

protected:
  void paintGL() override;

private:
  std::shared_ptr<livekit::VideoStream> mStream;
  std::thread mReaderThread;
  std::atomic<bool> mRunning{false};
  QMutex mFrameMutex;
  QImage mLatestFrame;

  void readerLoop();
  void setLatestFrame(const QImage &image);
};

} // namespace pcm::video
```

Create `src/video/remote_video_renderer.cpp`:

```cpp
#include "remote_video_renderer.h"

#include <QMutexLocker>
#include <QPainter>

namespace pcm::video {

RemoteVideoRenderer::RemoteVideoRenderer(QWidget *parent) : QOpenGLWidget(parent) {}

RemoteVideoRenderer::~RemoteVideoRenderer() {
  detach();
}

void RemoteVideoRenderer::attachTrack(const std::shared_ptr<livekit::Track> &track) {
  detach();

  if (!track) {
    return;
  }

  livekit::VideoStream::Options options;
  options.format = livekit::VideoBufferType::RGBA;
  mStream = livekit::VideoStream::fromTrack(track, options);
  if (!mStream) {
    return;
  }

  mRunning.store(true);
  mReaderThread = std::thread(&RemoteVideoRenderer::readerLoop, this);
}

void RemoteVideoRenderer::detach() {
  mRunning.store(false);
  // Wakes a thread currently blocked in mStream->read() — without this,
  // detach() (or the destructor) can hang forever if no more frames arrive.
  if (mStream) {
    mStream->close();
  }
  if (mReaderThread.joinable()) {
    mReaderThread.join();
  }
  mStream.reset();
}

void RemoteVideoRenderer::readerLoop() {
  livekit::VideoFrameEvent event;
  while (mRunning.load()) {
    if (!mStream->read(event)) {
      break;
    }
    const auto &frame = event.frame;
    QImage image(frame.data(), static_cast<int>(frame.width()), static_cast<int>(frame.height()),
                QImage::Format_RGBA8888);
    setLatestFrame(image.copy());
  }
}

void RemoteVideoRenderer::setLatestFrame(const QImage &image) {
  {
    QMutexLocker locker(&mFrameMutex);
    mLatestFrame = image;
  }
  QMetaObject::invokeMethod(this, QOverload<>::of(&QOpenGLWidget::update),
                            Qt::QueuedConnection);
}

void RemoteVideoRenderer::paintGL() {
  QImage frame;
  {
    QMutexLocker locker(&mFrameMutex);
    frame = mLatestFrame;
  }

  QPainter painter(this);
  if (frame.isNull()) {
    return;
  }
  const auto scaled = frame.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
  const QPoint topLeft((width() - scaled.width()) / 2, (height() - scaled.height()) / 2);
  painter.drawImage(topLeft, scaled);
}

} // namespace pcm::video
```

- [ ] **Step 4: Implement `RemoteAudioPlayer`**

Create `src/video/remote_audio_player.h`:

```cpp
#pragma once

#include <QAudioDevice>
#include <QAudioSink>
#include <QObject>
#include <atomic>
#include <livekit/audio_stream.h>
#include <memory>
#include <thread>

namespace pcm::video {

// Plays a subscribed remote LiveKit audio track. Pulls frames on a
// dedicated std::thread (blocking AudioStream::read()) and marshals decoded
// PCM to the GUI thread, since QAudioSink's push-mode QIODevice interface
// must only be touched from the thread that owns the sink.
class RemoteAudioPlayer final : public QObject {
  Q_OBJECT
public:
  explicit RemoteAudioPlayer(QObject *parent = nullptr);
  ~RemoteAudioPlayer() override;

  void attachTrack(const std::shared_ptr<livekit::Track> &track, const QAudioDevice &outputDevice);
  void detach();

signals:
  void playbackFailed(QString reason);

private:
  std::shared_ptr<livekit::AudioStream> mStream;
  QAudioDevice mOutputDevice;
  std::unique_ptr<QAudioSink> mSink;
  QIODevice *mSinkDevice{nullptr};
  std::thread mReaderThread;
  std::atomic<bool> mRunning{false};

  void readerLoop();
  void deliverAudioOnGuiThread(QByteArray pcmBytes, int sampleRate, int numChannels);
};

} // namespace pcm::video
```

Create `src/video/remote_audio_player.cpp`:

```cpp
#include "remote_audio_player.h"

#include <QAudioFormat>
#include <QMetaObject>

namespace pcm::video {

RemoteAudioPlayer::RemoteAudioPlayer(QObject *parent) : QObject(parent) {}

RemoteAudioPlayer::~RemoteAudioPlayer() {
  detach();
}

void RemoteAudioPlayer::attachTrack(const std::shared_ptr<livekit::Track> &track,
                                    const QAudioDevice &outputDevice) {
  detach();

  if (!track) {
    return;
  }

  livekit::AudioStream::Options options;
  mStream = livekit::AudioStream::fromTrack(track, options);
  if (!mStream) {
    return;
  }

  mOutputDevice = outputDevice;
  mRunning.store(true);
  mReaderThread = std::thread(&RemoteAudioPlayer::readerLoop, this);
}

void RemoteAudioPlayer::detach() {
  mRunning.store(false);
  if (mStream) {
    mStream->close();
  }
  if (mReaderThread.joinable()) {
    mReaderThread.join();
  }
  mSink.reset();
  mSinkDevice = nullptr;
  mStream.reset();
}

void RemoteAudioPlayer::readerLoop() {
  livekit::AudioFrameEvent event;
  while (mRunning.load()) {
    if (!mStream->read(event)) {
      break;
    }

    const auto &frame = event.frame;
    QByteArray bytes(reinterpret_cast<const char *>(frame.data()),
                     static_cast<qsizetype>(frame.size() * sizeof(int16_t)));
    const int sampleRate = static_cast<int>(frame.sampleRate());
    const int numChannels = static_cast<int>(frame.numChannels());

    QMetaObject::invokeMethod(
        this,
        [this, bytes = std::move(bytes), sampleRate, numChannels]() mutable {
          deliverAudioOnGuiThread(std::move(bytes), sampleRate, numChannels);
        },
        Qt::QueuedConnection);
  }
}

void RemoteAudioPlayer::deliverAudioOnGuiThread(QByteArray pcmBytes, const int sampleRate,
                                                const int numChannels) {
  if (!mRunning.load()) {
    return;
  }

  if (!mSink) {
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(numChannels);
    format.setSampleFormat(QAudioFormat::Int16);

    mSink = std::make_unique<QAudioSink>(mOutputDevice, format, this);
    mSinkDevice = mSink->start();
  }

  if (mSinkDevice) {
    mSinkDevice->write(pcmBytes);
  }
}

} // namespace pcm::video
```

- [ ] **Step 5: Run the smoke test to verify it passes**

```bash
cmake --build build --target Sessio_remote_video_renderer_smoke_test --parallel
ctest --test-dir build -R RemoteVideoRendererSmokeTest --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Add the new files to `src/video/CMakeLists.txt`**

```cmake
qt_add_library(${TARGET_NAME} STATIC
        video_provider_kind.h
        audio_chunker.h
        audio_chunker.cpp
        frame_convert.h
        frame_convert.cpp
        device_manager.h
        device_manager.cpp
        video_capture_worker.h
        video_capture_worker.cpp
        video_capture_adapter.h
        video_capture_adapter.cpp
        audio_capture_adapter.h
        audio_capture_adapter.cpp
        remote_video_renderer.h
        remote_video_renderer.cpp
        remote_audio_player.h
        remote_audio_player.cpp
)
```

- [ ] **Step 7: Commit**

```bash
/usr/bin/git add src/video/remote_video_renderer.h src/video/remote_video_renderer.cpp src/video/remote_audio_player.h src/video/remote_audio_player.cpp src/video/CMakeLists.txt test/remote_video_renderer_smoke_test.cpp test/CMakeLists.txt
/usr/bin/git commit -m "feat: add RemoteVideoRenderer and RemoteAudioPlayer"
```

---

### Task 7: `VideoProvider` interface, `LiveKitVideoProvider`, and `FakeVideoProvider`

**Files:**
- Modify: `src/video/video_provider_kind.h` → replaced by real content, see below (this task deletes the Task 1 placeholder)
- Create: `src/video/video_provider.h`
- Create: `src/video/livekit_video_provider.h`, `src/video/livekit_video_provider.cpp`
- Create: `test/fake_video_provider.h` (test-only, per this plan's Global Constraints)
- Test: `test/livekit_video_provider_smoke_test.cpp`
- Modify: `src/video/CMakeLists.txt`, `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `pcm::video::DeviceManager` (Task 3), `VideoCaptureAdapter`/`AudioCaptureAdapter` (Tasks 4-5), `RemoteVideoRenderer`/`RemoteAudioPlayer` (Task 6).
- Produces: `pcm::video::VideoProvider` (abstract) — `join(QString url, QString token)`, `leave()`, signals `joined()`, `joinFailed(QString)`, `left()`, `remoteParticipantConnected()`, `reconnecting()`, `reconnected()`, `connectionLost(QString)` — consumed by `VideoSession` in Task 8. `LiveKitVideoProvider` is the sole production implementation. `test::FakeVideoProvider` (test-only) is consumed by Task 8's `VideoSession` tests.

**Delete `src/video/video_provider_kind.h`** (Task 1's placeholder — its only job was proving the SDK links; this task's `video_provider.h` supersedes it). Remove it from `src/video/CMakeLists.txt`'s source list in Step 6 below.

- [ ] **Step 1: Design and write the `VideoProvider` interface**

Create `src/video/video_provider.h`:

```cpp
#pragma once

#include <QObject>
#include <QString>

namespace pcm::video {

// Abstraction over the actual in-call media session: connecting to the
// video backend, publishing local tracks, subscribing to and rendering
// remote tracks. Unlike MeetingProvider (pcm::meeting), this is NOT
// polymorphic across multiple real backends — LiveKitVideoProvider is the
// only production implementation. The interface exists solely so
// VideoSession and the future native call UI (#80) can be tested against a
// FakeVideoProvider without a real SDK, network, camera, or microphone.
//
// join()/leave() are asynchronous: callers observe the outcome via signals,
// never a return value.
class VideoProvider : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  ~VideoProvider() override = default;

  // Connects to the given server url with the given (pre-obtained) JWT
  // token, and publishes local audio/video tracks. This provider does not
  // fetch the token itself — the caller obtains it via the MeetingProvider/
  // token-backend layer before calling join().
  virtual void join(const QString &url, const QString &token) = 0;

  // Disconnects and releases all local devices/tracks. Safe to call even if
  // never successfully joined.
  virtual void leave() = 0;

signals:
  void joined();
  void joinFailed(QString reason);
  void left();
  void remoteParticipantConnected();
  void reconnecting();
  void reconnected();
  void connectionLost(QString reason);
};

} // namespace pcm::video
```

- [ ] **Step 2: Write the failing smoke test for `LiveKitVideoProvider`**

A real `join()` needs a real LiveKit server and a real JWT — out of reach for an automated test. This smoke test proves what's fully automatable and directly required by this plan's global constraints: repeated construct/destroy cycles (with and without an intervening `join()` call against an unreachable URL, which must fail cleanly via `joinFailed` rather than hang or crash) never hang. This is the primary vehicle for Task 9's overall lifecycle-stress acceptance criterion, exercising the real `LiveKitVideoProvider` (not `FakeVideoProvider`) end-to-end teardown path, including the `livekit::initialize()`/`shutdown()` reference-counted guard from Step 4 below.

Create `test/livekit_video_provider_smoke_test.cpp`:

```cpp
#include "livekit_video_provider.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <iostream>

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);

  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, [] {
    std::cerr << "livekit_video_provider_smoke_test: watchdog fired, hang detected" << std::endl;
    std::exit(1);
  });
  watchdog.start(20000);

  for (int cycle = 0; cycle < 5; ++cycle) {
    auto provider = std::make_unique<pcm::video::LiveKitVideoProvider>();

    // A deliberately unreachable URL: proves join() against a real SDK call
    // that will fail reports joinFailed() rather than hanging, and that a
    // provider can be safely destroyed either mid-attempt or after failure.
    QEventLoop loop;
    QObject::connect(provider.get(), &pcm::video::VideoProvider::joinFailed, &loop, &QEventLoop::quit);
    provider->join("wss://127.0.0.1:1", "not-a-real-token");
    loop.exec();

    provider->leave();
    provider.reset();
  }

  std::cout << "livekit_video_provider_smoke_test: 5 construct/join/destroy cycles completed"
            << std::endl;
  return 0;
}
```

- [ ] **Step 3: Add the test target and run to verify it fails**

```cmake
add_executable(Sessio_livekit_video_provider_smoke_test
    ${CMAKE_SOURCE_DIR}/src/video/livekit_video_provider.cpp
    ${CMAKE_SOURCE_DIR}/src/video/video_capture_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/video/video_capture_worker.cpp
    ${CMAKE_SOURCE_DIR}/src/video/audio_capture_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/video/audio_chunker.cpp
    ${CMAKE_SOURCE_DIR}/src/video/frame_convert.cpp
    ${CMAKE_SOURCE_DIR}/src/video/remote_video_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/video/remote_audio_player.cpp
    ${CMAKE_SOURCE_DIR}/src/video/device_manager.cpp
    livekit_video_provider_smoke_test.cpp
)
target_include_directories(Sessio_livekit_video_provider_smoke_test PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_livekit_video_provider_smoke_test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::OpenGLWidgets
    LiveKit::livekit
)
add_test(NAME LiveKitVideoProviderSmokeTest COMMAND Sessio_livekit_video_provider_smoke_test)
set_tests_properties(LiveKitVideoProviderSmokeTest PROPERTIES TIMEOUT 40)
```

```bash
cmake -S . -B build -DPCM_BUILD_TESTS=ON
cmake --build build --target Sessio_livekit_video_provider_smoke_test --parallel
```

Expected: FAIL — `livekit_video_provider.h` does not exist yet.

- [ ] **Step 4: Implement `LiveKitVideoProvider`**

Create `src/video/livekit_video_provider.h`:

```cpp
#pragma once

#include "video_provider.h"

#include <QPointer>
#include <livekit/room.h>
#include <memory>

namespace pcm::video {

class VideoCaptureAdapter;
class AudioCaptureAdapter;
class RemoteVideoRenderer;
class RemoteAudioPlayer;
class DeviceManager;

// Production VideoProvider implementation: connects to a real LiveKit
// server, captures the default camera/microphone, publishes local tracks,
// and renders/plays the first subscribed remote video/audio track. Ported
// from spike/77-livekit-cpp-spike, with every stderr-only error path turned
// into a signal.
class LiveKitVideoProvider final : public VideoProvider, private livekit::RoomDelegate {
  Q_OBJECT
public:
  explicit LiveKitVideoProvider(QObject *parent = nullptr);
  ~LiveKitVideoProvider() override;

  void join(const QString &url, const QString &token) override;
  void leave() override;

private:
  // livekit::RoomDelegate overrides — invoked on a LiveKit-internal thread;
  // every override marshals to the GUI thread via QMetaObject::invokeMethod
  // and guards its body with `if (!mRoom) return;` since leave() may run
  // before a queued callback executes.
  void onTrackSubscribed(livekit::Room &room, const livekit::TrackSubscribedEvent &event) override;
  void onParticipantConnected(livekit::Room &room,
                              const livekit::ParticipantConnectedEvent &event) override;

  void publishTracks();
  void unpublishTracks();

  std::unique_ptr<DeviceManager> mDeviceManager;
  std::unique_ptr<VideoCaptureAdapter> mVideoCapture;
  std::unique_ptr<AudioCaptureAdapter> mAudioCapture;
  QPointer<RemoteVideoRenderer> mRemoteVideo;
  std::unique_ptr<RemoteAudioPlayer> mRemoteAudio;

  std::unique_ptr<livekit::Room> mRoom;
  std::shared_ptr<livekit::LocalAudioTrack> mAudioTrack;
  std::shared_ptr<livekit::LocalVideoTrack> mVideoTrack;
  std::shared_ptr<livekit::Track> mRemoteAudioTrack;
};

} // namespace pcm::video
```

Create `src/video/livekit_video_provider.cpp`:

```cpp
#include "livekit_video_provider.h"

#include "audio_capture_adapter.h"
#include "device_manager.h"
#include "remote_audio_player.h"
#include "remote_video_renderer.h"
#include "video_capture_adapter.h"

#include <QMetaObject>
#include <atomic>

namespace pcm::video {

namespace {
// Reference-counted so livekit::initialize()/shutdown() run exactly once
// each, regardless of how many LiveKitVideoProvider instances exist over
// the app's lifetime — required because livekit::shutdown() must only run
// after every livekit-holding object has been destroyed (see this plan's
// Task 7 design notes / the spike's main.cpp shutdown-ordering fix,
// commit 6b63162).
std::atomic<int> gLiveKitRefCount{0};

void acquireLiveKitRuntime() {
  if (gLiveKitRefCount.fetch_add(1) == 0) {
    livekit::initialize(livekit::LogLevel::Info);
  }
}

void releaseLiveKitRuntime() {
  if (gLiveKitRefCount.fetch_sub(1) == 1) {
    livekit::shutdown();
  }
}
} // namespace

LiveKitVideoProvider::LiveKitVideoProvider(QObject *parent)
    : VideoProvider(parent), mDeviceManager(std::make_unique<DeviceManager>()),
      mVideoCapture(std::make_unique<VideoCaptureAdapter>()),
      mAudioCapture(std::make_unique<AudioCaptureAdapter>()),
      mRemoteVideo(new RemoteVideoRenderer()),
      mRemoteAudio(std::make_unique<RemoteAudioPlayer>()) {
  acquireLiveKitRuntime();

  connect(mVideoCapture.get(), &VideoCaptureAdapter::captureFailed, this,
          &VideoProvider::connectionLost);
  connect(mAudioCapture.get(), &AudioCaptureAdapter::captureFailed, this,
          &VideoProvider::connectionLost);

  if (const auto camera = mDeviceManager->defaultCamera()) {
    mVideoCapture->start(*camera);
  }
  if (const auto microphone = mDeviceManager->defaultMicrophone()) {
    mAudioCapture->start(*microphone);
  }
}

LiveKitVideoProvider::~LiveKitVideoProvider() {
  leave();
  mVideoCapture->stop();
  mAudioCapture->stop();
  delete mRemoteVideo.data();
  releaseLiveKitRuntime();
}

void LiveKitVideoProvider::join(const QString &url, const QString &token) {
  mRoom = std::make_unique<livekit::Room>();
  mRoom->setDelegate(this);

  livekit::RoomOptions options;
  options.auto_subscribe = true;
  options.dynacast = false;

  const bool connected = mRoom->connect(url.toStdString(), token.toStdString(), options);
  if (!connected) {
    mRoom->setDelegate(nullptr);
    mRoom.reset();
    emit joinFailed(QStringLiteral("Failed to connect to the video server."));
    return;
  }

  publishTracks();
  emit joined();
}

void LiveKitVideoProvider::leave() {
  if (mRemoteVideo) {
    mRemoteVideo->detach();
  }
  mRemoteAudio->detach();
  mRemoteAudioTrack.reset();

  if (mRoom) {
    unpublishTracks();
    mRoom->setDelegate(nullptr);
    mRoom.reset();
    emit left();
  }
}

void LiveKitVideoProvider::publishTracks() {
  auto localParticipant = mRoom->localParticipant().lock();
  if (!localParticipant) {
    return;
  }

  try {
    mAudioTrack = livekit::LocalAudioTrack::createLocalAudioTrack("mic", mAudioCapture->audioSource());
    livekit::TrackPublishOptions audioOptions;
    audioOptions.source = livekit::TrackSource::SOURCE_MICROPHONE;
    audioOptions.dtx = false;
    audioOptions.simulcast = false;
    localParticipant->publishTrack(mAudioTrack, audioOptions);
  } catch (const std::exception &e) {
    emit connectionLost(QStringLiteral("Failed to publish audio: %1").arg(e.what()));
  }

  try {
    mVideoTrack = livekit::LocalVideoTrack::createLocalVideoTrack("cam", mVideoCapture->videoSource());
    livekit::TrackPublishOptions videoOptions;
    videoOptions.source = livekit::TrackSource::SOURCE_CAMERA;
    videoOptions.dtx = false;
    videoOptions.simulcast = true;
    localParticipant->publishTrack(mVideoTrack, videoOptions);
  } catch (const std::exception &e) {
    emit connectionLost(QStringLiteral("Failed to publish video: %1").arg(e.what()));
  }
}

void LiveKitVideoProvider::unpublishTracks() {
  auto localParticipant = mRoom->localParticipant().lock();
  if (localParticipant) {
    if (mAudioTrack) {
      localParticipant->unpublishTrack(mAudioTrack->sid());
    }
    if (mVideoTrack) {
      localParticipant->unpublishTrack(mVideoTrack->sid());
    }
  }
  mAudioTrack.reset();
  mVideoTrack.reset();
}

void LiveKitVideoProvider::onTrackSubscribed(livekit::Room &, const livekit::TrackSubscribedEvent &event) {
  if (!event.track) {
    return;
  }
  const auto kind = event.track->kind();
  auto track = event.track;

  QMetaObject::invokeMethod(
      this,
      [this, track, kind]() {
        if (!mRoom) {
          return;
        }
        if (kind == livekit::TrackKind::KIND_VIDEO) {
          if (mRemoteVideo) {
            mRemoteVideo->attachTrack(track);
          }
        } else if (kind == livekit::TrackKind::KIND_AUDIO) {
          mRemoteAudioTrack = track;
          const auto device = mDeviceManager->defaultSpeaker();
          if (device) {
            mRemoteAudio->attachTrack(track, *device);
          }
        }
      },
      Qt::QueuedConnection);
}

void LiveKitVideoProvider::onParticipantConnected(livekit::Room &,
                                                  const livekit::ParticipantConnectedEvent &) {
  QMetaObject::invokeMethod(
      this,
      [this]() {
        if (!mRoom) {
          return;
        }
        emit remoteParticipantConnected();
      },
      Qt::QueuedConnection);
}

} // namespace pcm::video
```

- [ ] **Step 5: Write `FakeVideoProvider`**

Create `test/fake_video_provider.h`:

```cpp
#pragma once

#include "video_provider.h"

namespace pcm::video::test {

// Test-only VideoProvider double: join()/leave() are driven entirely by
// explicit method calls on this class rather than a real SDK/network, so
// VideoSession's state machine can be tested deterministically and without
// a display, camera, or microphone. Never part of the production library.
class FakeVideoProvider final : public VideoProvider {
public:
  using VideoProvider::VideoProvider;

  void join(const QString &url, const QString &token) override {
    mLastJoinUrl = url;
    mLastJoinToken = token;
    ++mJoinCallCount;
  }

  void leave() override {
    ++mLeaveCallCount;
  }

  // Test-driving methods: call these to simulate the real provider emitting
  // its outcome signals asynchronously, exactly as LiveKitVideoProvider
  // would once its own SDK callbacks fire.
  void simulateJoined() { emit joined(); }
  void simulateJoinFailed(const QString &reason) { emit joinFailed(reason); }
  void simulateLeft() { emit left(); }
  void simulateRemoteParticipantConnected() { emit remoteParticipantConnected(); }
  void simulateReconnecting() { emit reconnecting(); }
  void simulateReconnected() { emit reconnected(); }
  void simulateConnectionLost(const QString &reason) { emit connectionLost(reason); }

  QString mLastJoinUrl;
  QString mLastJoinToken;
  int mJoinCallCount{0};
  int mLeaveCallCount{0};
};

} // namespace pcm::video::test
```

- [ ] **Step 6: Delete the Task 1 placeholder and update `src/video/CMakeLists.txt`**

```bash
rm src/video/video_provider_kind.h
```

Update `src/video/CMakeLists.txt`'s `qt_add_library` call:

```cmake
qt_add_library(${TARGET_NAME} STATIC
        video_provider.h
        audio_chunker.h
        audio_chunker.cpp
        frame_convert.h
        frame_convert.cpp
        device_manager.h
        device_manager.cpp
        video_capture_worker.h
        video_capture_worker.cpp
        video_capture_adapter.h
        video_capture_adapter.cpp
        audio_capture_adapter.h
        audio_capture_adapter.cpp
        remote_video_renderer.h
        remote_video_renderer.cpp
        remote_audio_player.h
        remote_audio_player.cpp
        livekit_video_provider.h
        livekit_video_provider.cpp
)
```

- [ ] **Step 7: Run the smoke test to verify it passes**

```bash
cmake --build build --target Sessio_livekit_video_provider_smoke_test --parallel
ctest --test-dir build -R LiveKitVideoProviderSmokeTest --output-on-failure
```

Expected: PASS — five construct/join-against-unreachable-url/destroy cycles complete without hanging, and each `join()` call reports `joinFailed` rather than hanging or crashing.

- [ ] **Step 8: Commit**

```bash
/usr/bin/git add src/video/video_provider.h src/video/livekit_video_provider.h src/video/livekit_video_provider.cpp src/video/CMakeLists.txt test/fake_video_provider.h test/livekit_video_provider_smoke_test.cpp test/CMakeLists.txt
/usr/bin/git rm src/video/video_provider_kind.h
/usr/bin/git commit -m "feat: add VideoProvider interface, LiveKitVideoProvider, and FakeVideoProvider"
```

---

### Task 8: `VideoSession` state machine

**Files:**
- Create: `src/video/video_session_state.h`
- Create: `src/video/video_session.h`, `src/video/video_session.cpp`
- Test: `test/video_session_tests.cpp`
- Modify: `src/video/CMakeLists.txt`, `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `pcm::video::VideoProvider` (Task 7's interface), `pcm::video::test::FakeVideoProvider` (Task 7, test-only) for its own tests.
- Produces: `pcm::video::VideoSession` — `join(QString url, QString token)`, `leave()`, `state()`, `stateChanged(VideoSessionState)` signal, `joinFailed(QString)`/`reconnectFailed(QString)` signals — the class #80's native call UI will consume.

This is the plan's only use of `QStateMachine` (per ADR-14, `docs/asciidoc/14-video-session-state-machine-adr.adoc`, already committed to this branch).

- [ ] **Step 1: Write `VideoSessionState`**

Create `src/video/video_session_state.h`:

```cpp
#pragma once

namespace pcm::video {

// Drives VideoSession's state machine. See docs/video-roadmap.md §5.5 and
// docs/asciidoc/14-video-session-state-machine-adr.adoc.
enum class VideoSessionState {
  NoMeeting,
  Provisioned,
  PrejoinCheck,
  Joining,
  WaitingForClient,
  Connected,
  Reconnecting,
  Leaving,
  Ended,
  Failed,
};

} // namespace pcm::video
```

- [ ] **Step 2: Write the failing tests**

`QStateMachine` runs on the Qt event loop, so these tests need a `QCoreApplication` and must pump the event loop (`QSignalSpy`) to observe transitions — see ADR-14's Consequences section.

**Note on async testing:** `QStateMachine` processes transitions through the Qt event loop (posted events), not synchronously at the point a signal fires — see ADR-14's Consequences section. A synchronous assertion immediately after `session.join(...)` will not yet reflect the state change. Every test below drives the event loop with `QSignalSpy::wait()` (which pumps events until the signal fires or a timeout elapses) rather than asserting immediately.

Create `test/video_session_tests.cpp`:

```cpp
#include "video_session.h"

#include "fake_video_provider.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <gtest/gtest.h>

namespace {

class VideoSessionTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    static int argc = 0;
    static QCoreApplication app(argc, nullptr);
  }

  // Pumps the event loop (via stateSpy.wait()) until VideoSession reaches
  // `target`, or fails the test if 1s elapses without reaching it. Needed
  // because QStateMachine's Provisioned/PrejoinCheck/Joining cascade (and
  // every provider-driven transition) settles asynchronously.
  static void waitForState(pcm::video::VideoSession &session, QSignalSpy &stateSpy,
                           pcm::video::VideoSessionState target) {
    while (session.state() != target) {
      ASSERT_TRUE(stateSpy.wait(1000)) << "timed out waiting for state "
                                       << static_cast<int>(target);
    }
  }
};

} // namespace

TEST_F(VideoSessionTest, StartsInNoMeetingState) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  EXPECT_EQ(session.state(), pcm::video::VideoSessionState::NoMeeting);
}

TEST_F(VideoSessionTest, JoinReachesJoiningStateThroughProvisionedAndPrejoinCheck) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);

  EXPECT_EQ(fake->mJoinCallCount, 1);
  EXPECT_EQ(fake->mLastJoinUrl, QStringLiteral("wss://example.invalid"));
  EXPECT_EQ(fake->mLastJoinToken, QStringLiteral("token"));
}

TEST_F(VideoSessionTest, JoinedThenRemoteParticipantConnectedReachesConnected) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);

  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForClient);

  fake->simulateRemoteParticipantConnected();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);
}

TEST_F(VideoSessionTest, JoinFailureTransitionsToFailed) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);
  QSignalSpy failedSpy(&session, &pcm::video::VideoSession::joinFailed);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);

  fake->simulateJoinFailed("no route to host");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Failed);

  ASSERT_EQ(failedSpy.count(), 1);
  EXPECT_EQ(failedSpy.first().at(0).toString(), QStringLiteral("no route to host"));
}

TEST_F(VideoSessionTest, ConnectionLostEntersReconnectingThenReconnectedReturnsToConnected) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake, std::chrono::milliseconds(200));
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForClient);
  fake->simulateRemoteParticipantConnected();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);

  fake->simulateConnectionLost("network blip");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Reconnecting);

  fake->simulateReconnected();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);
}

TEST_F(VideoSessionTest, ReconnectTimeoutTransitionsToFailedWithoutReconnection) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake, std::chrono::milliseconds(50));
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForClient);
  fake->simulateRemoteParticipantConnected();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);

  QSignalSpy reconnectFailedSpy(&session, &pcm::video::VideoSession::reconnectFailed);
  fake->simulateConnectionLost("network blip");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Reconnecting);

  // No simulateReconnected() call — the 50ms timer must fire on its own.
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Failed);
  ASSERT_EQ(reconnectFailedSpy.count(), 1);
}

TEST_F(VideoSessionTest, LeaveFromConnectedTransitionsToEnded) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForClient);
  fake->simulateRemoteParticipantConnected();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);

  session.leave();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Leaving);
  EXPECT_EQ(fake->mLeaveCallCount, 1);

  fake->simulateLeft();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Ended);
}

TEST_F(VideoSessionTest, RepeatedJoinLeaveDestroyCyclesDoNotHang) {
  // Lifecycle-stress acceptance criterion (this plan's Task 9 section, and
  // the #77 spike's outstanding "clean leave/destruction" gate item) for
  // VideoSession's own state machine, independent of the real SDK's own
  // lifecycle (covered separately by Task 7's LiveKitVideoProvider smoke
  // test).
  for (int i = 0; i < 50; ++i) {
    auto *fake = new pcm::video::test::FakeVideoProvider();
    pcm::video::VideoSession session(fake, std::chrono::milliseconds(20));
    QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);

    session.join("wss://example.invalid", "token");
    waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
    fake->simulateJoined();
    waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForClient);
    fake->simulateRemoteParticipantConnected();
    waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);

    session.leave();
    waitForState(session, stateSpy, pcm::video::VideoSessionState::Leaving);
    fake->simulateLeft();
    waitForState(session, stateSpy, pcm::video::VideoSessionState::Ended);
  }
}
```

- [ ] **Step 3: Add the test target and run to verify it fails**

```cmake
add_executable(Sessio_video_session_tests
    ${CMAKE_SOURCE_DIR}/src/video/video_session.cpp
    video_session_tests.cpp
)
target_include_directories(Sessio_video_session_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_video_session_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Core
    Qt6::Test
)
set_target_properties(Sessio_video_session_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_video_session_tests)
```

`Qt6::Test` is needed because `QSignalSpy` (`<QSignalSpy>`) is part of the Qt Test module, not Qt Core — this test file uses it to pump the event loop while waiting for `QStateMachine`-driven transitions (see the async-testing note above). The top-level `CMakeLists.txt`'s `find_package(Qt6 ...)` (Task 1) does not request `Test`; add a scoped `find_package(Qt6 REQUIRED COMPONENTS Test)` call at the top of this `test/CMakeLists.txt` block, following `src/meeting/CMakeLists.txt`'s pattern of scoped `find_package` calls for test-only Qt components.

```bash
cmake -S . -B build -DPCM_BUILD_TESTS=ON
cmake --build build --target Sessio_video_session_tests --parallel
```

Expected: FAIL — `video_session.h` does not exist yet.

- [ ] **Step 4: Implement `VideoSession`**

Per ADR-14, `VideoSession` owns a real `QStateMachine` with one `QState` per `VideoSessionState` value, fully encapsulated — no `QState`/`QStateMachine` type appears on `VideoSession`'s public interface. `Provisioned` and `PrejoinCheck` are unconditional pass-through states (no external trigger of their own in this task — the native call UI in #80 is the only future consumer that might insert a real device-check gate at `PrejoinCheck`; for #92 they collapse into `join()`'s cascade with no observable delay). `join()`/`leave()` cannot call into the state machine's transitions directly (a `QState` transition must be wired to an object's signal), so they emit private internal signals (`requestJoin`/`requestLeave`) that both drive the machine and cache the pending url/token for the `Joining` state's `entered()` handler to use.

Create `src/video/video_session.h`:

```cpp
#pragma once

#include "video_provider.h"
#include "video_session_state.h"

#include <QState>
#include <QStateMachine>
#include <QTimer>
#include <chrono>

namespace pcm::video {

class VideoSession final : public QObject {
  Q_OBJECT
public:
  explicit VideoSession(VideoProvider *provider,
                        std::chrono::milliseconds reconnectTimeout = std::chrono::seconds(30),
                        QObject *parent = nullptr);

  [[nodiscard]] VideoSessionState state() const { return mState; }

  void join(const QString &url, const QString &token);
  void leave();

signals:
  void stateChanged(pcm::video::VideoSessionState state);
  void joinFailed(QString reason);
  void reconnectFailed(QString reason);
  // Internal drive signals for the QStateMachine's transitions — not part
  // of the class's conceptual public contract (join()/leave() are), but
  // Qt requires signals to be either public or protected, never private.
  // QState::addTransition binds to an object's signal, so join()/leave()
  // emit these instead of touching mMachine directly.
  void requestJoin(QString url, QString token);
  void requestLeave();

private:
  VideoProvider *mProvider;
  QString mPendingUrl;
  QString mPendingToken;
  VideoSessionState mState{VideoSessionState::NoMeeting};
  QTimer mReconnectTimer;
  QStateMachine mMachine;
};

} // namespace pcm::video

Q_DECLARE_METATYPE(pcm::video::VideoSessionState)
```

Create `src/video/video_session.cpp`:

```cpp
#include "video_session.h"

namespace pcm::video {

VideoSession::VideoSession(VideoProvider *provider, const std::chrono::milliseconds reconnectTimeout,
                           QObject *parent)
    : QObject(parent), mProvider(provider) {
  mProvider->setParent(this);

  mReconnectTimer.setSingleShot(true);
  mReconnectTimer.setInterval(static_cast<int>(reconnectTimeout.count()));

  auto *noMeeting = new QState(&mMachine);
  auto *provisioned = new QState(&mMachine);
  auto *prejoinCheck = new QState(&mMachine);
  auto *joining = new QState(&mMachine);
  auto *waitingForClient = new QState(&mMachine);
  auto *connected = new QState(&mMachine);
  auto *reconnecting = new QState(&mMachine);
  auto *leaving = new QState(&mMachine);
  auto *ended = new QState(&mMachine);
  auto *failed = new QState(&mMachine);

  const auto wireEntered = [this](QState *state, const VideoSessionState value) {
    connect(state, &QState::entered, this, [this, value]() {
      mState = value;
      emit stateChanged(mState);
    });
  };
  wireEntered(noMeeting, VideoSessionState::NoMeeting);
  wireEntered(provisioned, VideoSessionState::Provisioned);
  wireEntered(prejoinCheck, VideoSessionState::PrejoinCheck);
  wireEntered(joining, VideoSessionState::Joining);
  wireEntered(waitingForClient, VideoSessionState::WaitingForClient);
  wireEntered(connected, VideoSessionState::Connected);
  wireEntered(reconnecting, VideoSessionState::Reconnecting);
  wireEntered(leaving, VideoSessionState::Leaving);
  wireEntered(ended, VideoSessionState::Ended);
  wireEntered(failed, VideoSessionState::Failed);

  // NoMeeting -> Provisioned -> PrejoinCheck -> Joining: the middle two
  // legs are unconditional (fire immediately on entry, no external event),
  // since neither has an observable gate in this task — see this task's
  // note above the header.
  noMeeting->addTransition(this, &VideoSession::requestJoin, provisioned);
  provisioned->addTransition(prejoinCheck);
  prejoinCheck->addTransition(joining);

  connect(this, &VideoSession::requestJoin, this, [this](const QString &url, const QString &token) {
    mPendingUrl = url;
    mPendingToken = token;
  });
  connect(joining, &QState::entered, this,
          [this]() { mProvider->join(mPendingUrl, mPendingToken); });

  joining->addTransition(mProvider, &VideoProvider::joined, waitingForClient);
  joining->addTransition(mProvider, &VideoProvider::joinFailed, failed);
  waitingForClient->addTransition(mProvider, &VideoProvider::remoteParticipantConnected, connected);
  connect(mProvider, &VideoProvider::joinFailed, this,
          [this](const QString &reason) { emit joinFailed(reason); });

  connected->addTransition(mProvider, &VideoProvider::connectionLost, reconnecting);
  reconnecting->addTransition(mProvider, &VideoProvider::reconnected, connected);
  reconnecting->addTransition(&mReconnectTimer, &QTimer::timeout, failed);
  connect(reconnecting, &QState::entered, this, [this]() { mReconnectTimer.start(); });
  connect(connected, &QState::entered, this, [this]() { mReconnectTimer.stop(); });
  connect(&mReconnectTimer, &QTimer::timeout, this,
          [this]() { emit reconnectFailed(QStringLiteral("Reconnection timed out.")); });

  joining->addTransition(this, &VideoSession::requestLeave, leaving);
  waitingForClient->addTransition(this, &VideoSession::requestLeave, leaving);
  connected->addTransition(this, &VideoSession::requestLeave, leaving);
  reconnecting->addTransition(this, &VideoSession::requestLeave, leaving);

  connect(leaving, &QState::entered, this, [this]() { mProvider->leave(); });
  leaving->addTransition(mProvider, &VideoProvider::left, ended);

  mMachine.setInitialState(noMeeting);
  mMachine.start();
}

void VideoSession::join(const QString &url, const QString &token) {
  emit requestJoin(url, token);
}

void VideoSession::leave() {
  emit requestLeave();
}

} // namespace pcm::video
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build --target Sessio_video_session_tests --parallel
ctest --test-dir build -R VideoSessionTest --output-on-failure
```

Expected: PASS, all 8 cases.

- [ ] **Step 6: Add the new files to `src/video/CMakeLists.txt`**

```cmake
qt_add_library(${TARGET_NAME} STATIC
        video_provider.h
        video_session_state.h
        video_session.h
        video_session.cpp
        audio_chunker.h
        audio_chunker.cpp
        frame_convert.h
        frame_convert.cpp
        device_manager.h
        device_manager.cpp
        video_capture_worker.h
        video_capture_worker.cpp
        video_capture_adapter.h
        video_capture_adapter.cpp
        audio_capture_adapter.h
        audio_capture_adapter.cpp
        remote_video_renderer.h
        remote_video_renderer.cpp
        remote_audio_player.h
        remote_audio_player.cpp
        livekit_video_provider.h
        livekit_video_provider.cpp
)
```

- [ ] **Step 7: Commit**

```bash
/usr/bin/git add src/video/video_session_state.h src/video/video_session.h src/video/video_session.cpp src/video/CMakeLists.txt test/video_session_tests.cpp test/CMakeLists.txt
/usr/bin/git commit -m "feat: add VideoSession call-lifecycle state machine"
```

---

### Task 9: Docs, translations, and full-suite verification

**Files:**
- Modify: `docs/asciidoc/05-modules.adoc`
- Modify: `docs/asciidoc/14-video-session-state-machine-adr.adoc` (only if Task 8's design note above required reconciling the ADR with the implementation)

**Interfaces:** none — this task only documents and verifies what Tasks 1-8 already built.

- [ ] **Step 1: Add the `src/video` module entry**

In `docs/asciidoc/05-modules.adoc`, following the exact format of the `src/meeting` entry added by issue #79 (heading + bullet list, each bullet a backtick'd name followed by a `:`-prefixed one-line description), add a new `=== \`src/video\`` section (placed after `src/meeting`, since `src/video` depends conceptually on nothing from `src/meeting` but is the next domain-layer module chronologically):

```adoc
=== `src/video`

- `VideoProvider`: abstract interface over the in-call media session (join/leave, local capture, remote render) — one production implementation, `LiveKitVideoProvider`.
- `LiveKitVideoProvider`: connects to a LiveKit server, publishes local camera/microphone tracks, and renders/plays the first subscribed remote video/audio track.
- `VideoSession`: owns the call lifecycle for one Meeting, translating `VideoProvider` signals into `VideoSessionState` transitions. Knows nothing about `Event`/persistence.
- `VideoSessionState`: the enum driving `VideoSession` (`NoMeeting` through `Ended`/`Failed`).
- `DeviceManager`: enumerates and selects the camera/microphone/speaker via Qt Multimedia, independent of `VideoProvider`.
- `VideoCaptureAdapter`/`VideoCaptureWorker`: camera capture, with frame conversion and the LiveKit FFI call offloaded to a dedicated worker thread.
- `AudioCaptureAdapter`: microphone capture, running on the GUI thread in LiveKit's real-time (non-buffered) capture mode.
- `RemoteVideoRenderer`/`RemoteAudioPlayer`: render/play a subscribed remote track, pulling frames on a dedicated `std::thread`.
```

- [ ] **Step 2: Reconcile ADR-14 with the actual implementation**

Re-read `docs/asciidoc/14-video-session-state-machine-adr.adoc` against `src/video/video_session.cpp` as it now stands after Task 8. If Task 8's implementation used direct signal-driven state assignment rather than an explicit `QState`/`QStateMachine` graph (see Task 8 Step 4's design note), update ADR-14's Decision section to accurately describe the implementation actually shipped, rather than leaving the ADR describing a `QState`/`QStateMachine` graph that doesn't exist in the code. Do not skip this — an ADR that contradicts the code it documents is worse than no ADR.

- [ ] **Step 3: Verify translations are unaffected**

This plan added no `tr()`-wrapped strings anywhere (no widgets, no user-facing UI). Confirm this holds:

```bash
grep -rn "tr(" src/video/
```

Expected: no output. If any `tr()` calls were introduced, run `cmake --build build-release --target update_translations` and translate any resulting `type="unfinished"` entries in `translation/app_ru.ts`/`app_en.ts` before proceeding, per `AGENTS.md`.

- [ ] **Step 4: Run the full test suite**

```bash
cmake -S . -B build -DPCM_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: 100% pass, including all new `src/video` tests and every pre-existing test (no regressions — this plan added a wholly new, self-contained module that nothing else yet depends on).

- [ ] **Step 5: Commit**

```bash
/usr/bin/git add docs/asciidoc/05-modules.adoc docs/asciidoc/14-video-session-state-machine-adr.adoc
/usr/bin/git commit -m "docs: document src/video module and reconcile VideoSession ADR"
```
