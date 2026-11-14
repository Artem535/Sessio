# Call UI Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the gap between [2026-09-27-native-call-ui-and-client-mode-design.md](../specs/2026-09-27-native-call-ui-and-client-mode-design.md)'s §3 prose and what actually shipped, per [2026-09-29-call-ui-polish-design.md](../specs/2026-09-29-call-ui-polish-design.md): mute/camera toggles, mid-call device switching, local self-preview, fullscreen, consistent typography/spinners, a transparency bug fix, and default-open notes for client-linked calls.

**Architecture:** Extend `pcm::video::VideoProvider`'s interface with new virtual methods, implement them in `LiveKitVideoProvider` via its existing `VideoCaptureAdapter`/`AudioCaptureAdapter`, then rebuild `CallPage`'s `Connected`/status screens on top of that richer interface.

**Tech Stack:** C++20, Qt Widgets/Multimedia, GoogleTest, the vendored `livekit-sdk` (`livekit::LocalAudioTrack`/`LocalVideoTrack::mute()`/`unmute()`), Qlementine's bundled Inter/Inter Display fonts.

## Global Constraints

- Do not modify `VideoSession`'s state machine or its existing signals (`stateChanged`, `joinFailed`, `reconnectFailed`, `connectionLost`, `mediaError`) — every new capability is an additional `VideoProvider` method, never a change to an existing one.
- `FakeVideoProvider` (`test/fake_video_provider.h`) must grow a trivial override for every new `VideoProvider` virtual method, with call-tracking fields, so `CallPageTest` can assert on wiring without a real SDK. No existing `FakeVideoProvider`/`CallPageTest` test may change behavior.
- No new external asset/font/icon dependency — control-bar icons are drawn with `QPainter`, and the typography reuses `Inter`/`Inter Display`, which `QlementineStyle` already registers process-wide.
- The waveform/audio-level widget (cava-style) is explicitly out of scope for this plan.
- Every step that touches `tr()`-wrapped strings must end with both `translation/app_ru.ts` and `translation/app_en.ts` fully synced (Task 10) — CI fails the build on any remaining `type="unfinished"` entry, per `AGENTS.md`.

---

### Task 1: `VideoProvider` interface extension + `FakeVideoProvider`

**Files:**
- Modify: `src/video/video_provider.h`
- Modify: `test/fake_video_provider.h`
- Test: `test/fake_video_provider_tests.cpp` (new)
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Produces (added to `pcm::video::VideoProvider`):
  ```cpp
  virtual QWidget *localVideoWidget() { return nullptr; }
  virtual void setMicrophoneEnabled(bool enabled) { Q_UNUSED(enabled); }
  virtual void setCameraEnabled(bool enabled) { Q_UNUSED(enabled); }
  [[nodiscard]] virtual bool isMicrophoneEnabled() const { return true; }
  [[nodiscard]] virtual bool isCameraEnabled() const { return true; }
  virtual void switchCamera(const QCameraDevice &device) { Q_UNUSED(device); }
  virtual void switchMicrophone(const QAudioDevice &device) { Q_UNUSED(device); }
  virtual void switchSpeaker(const QAudioDevice &device) { Q_UNUSED(device); }
  ```
  Later tasks (2, 6, 7, 8) consume these on `pcm::video::VideoProvider*`/`FakeVideoProvider*`.

- [ ] **Step 1: Write the failing test**

```cpp
// test/fake_video_provider_tests.cpp
#include "fake_video_provider.h"

#include <QCameraDevice>
#include <QAudioDevice>
#include <QLabel>
#include <gtest/gtest.h>

using pcm::video::test::FakeVideoProvider;

TEST(FakeVideoProviderTest, DefaultsToMicrophoneAndCameraEnabled) {
  FakeVideoProvider provider;
  EXPECT_TRUE(provider.isMicrophoneEnabled());
  EXPECT_TRUE(provider.isCameraEnabled());
  EXPECT_EQ(provider.localVideoWidget(), nullptr);
}

TEST(FakeVideoProviderTest, TracksMicrophoneAndCameraToggleCalls) {
  FakeVideoProvider provider;
  provider.setMicrophoneEnabled(false);
  EXPECT_FALSE(provider.isMicrophoneEnabled());
  EXPECT_EQ(provider.mSetMicrophoneEnabledCallCount, 1);

  provider.setCameraEnabled(false);
  EXPECT_FALSE(provider.isCameraEnabled());
  EXPECT_EQ(provider.mSetCameraEnabledCallCount, 1);
}

TEST(FakeVideoProviderTest, TracksDeviceSwitchCalls) {
  FakeVideoProvider provider;
  const QCameraDevice camera;
  const QAudioDevice microphone;
  const QAudioDevice speaker;

  provider.switchCamera(camera);
  provider.switchMicrophone(microphone);
  provider.switchSpeaker(speaker);

  EXPECT_EQ(provider.mSwitchCameraCallCount, 1);
  EXPECT_EQ(provider.mSwitchMicrophoneCallCount, 1);
  EXPECT_EQ(provider.mSwitchSpeakerCallCount, 1);
}

TEST(FakeVideoProviderTest, ExposesTestSuppliedLocalVideoWidget) {
  FakeVideoProvider provider;
  QLabel widget;
  provider.mLocalVideoWidget = &widget;
  EXPECT_EQ(provider.localVideoWidget(), &widget);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

- [ ] **Step 2: Add the new test target to `test/CMakeLists.txt`**

Add near the other `Sessio_video`-linked GoogleTest targets (e.g. right after the `Sessio_video_session_tests` target):

```cmake
# FakeVideoProvider tests
add_executable(Sessio_fake_video_provider_tests
    fake_video_provider_tests.cpp
)
target_link_libraries(Sessio_fake_video_provider_tests PRIVATE
    GTest::gtest
    Qt6::Widgets
    Sessio_video
)
set_target_properties(Sessio_fake_video_provider_tests PROPERTIES AUTOMOC ON)
add_test(NAME FakeVideoProviderTests COMMAND Sessio_fake_video_provider_tests)
```

(Uses `GTest::gtest` and its own `main()`, not `GTest::gtest_main`, matching `call_page_tests.cpp`'s pattern of needing a `QApplication`-free but Qt-widget-touching `main()`; here no `QApplication` is actually required since no widget is shown, but linking `Qt6::Widgets` is still needed for `QLabel`.)

- [ ] **Step 3: Run test to verify it fails (compile error: no such members)**

Run: `cmake --build build --target Sessio_fake_video_provider_tests`
Expected: FAIL — `video_provider.h` has no `localVideoWidget()`/`setMicrophoneEnabled()`/etc., and `FakeVideoProvider` doesn't override them or expose the tracking fields.

- [ ] **Step 4: Extend `VideoProvider`**

In `src/video/video_provider.h`, add includes and the new virtual methods:

```cpp
#pragma once

#include <QObject>
#include <QString>
#include <QAudioDevice>
#include <QCameraDevice>

class QWidget;

namespace pcm::video {

class VideoProvider : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  ~VideoProvider() override = default;

  virtual void join(const QString &url, const QString &token) = 0;
  virtual void leave() = 0;
  virtual QWidget *remoteVideoWidget() { return nullptr; }

  // The widget this provider renders the LOCAL camera preview into (the
  // same capture that is being published, not a second parallel camera
  // session), or nullptr if it has none (e.g. a test double). Same
  // ownership contract as remoteVideoWidget(): the provider owns it: a UI
  // embedding it may reparent it, but must hand it back
  // (setParent(nullptr)) rather than delete it when swapping it out.
  virtual QWidget *localVideoWidget() { return nullptr; }

  // Mutes/unmutes the corresponding locally published track. Never stops
  // physically capturing the device (matches the LiveKit SDK's own
  // documented mute() contract) — only whether the track is transmitted.
  virtual void setMicrophoneEnabled(bool enabled) { Q_UNUSED(enabled); }
  virtual void setCameraEnabled(bool enabled) { Q_UNUSED(enabled); }
  [[nodiscard]] virtual bool isMicrophoneEnabled() const { return true; }
  [[nodiscard]] virtual bool isCameraEnabled() const { return true; }

  // Switches the corresponding local capture device mid-call. Errors
  // (device removed, already in use) surface through the existing
  // mediaError() signal below — no new error signal.
  virtual void switchCamera(const QCameraDevice &device) { Q_UNUSED(device); }
  virtual void switchMicrophone(const QAudioDevice &device) { Q_UNUSED(device); }
  virtual void switchSpeaker(const QAudioDevice &device) { Q_UNUSED(device); }

signals:
  void joined();
  void joinFailed(QString reason);
  void left();
  void remoteParticipantConnected();
  void remoteParticipantDisconnected();
  void reconnecting();
  void reconnected();
  void connectionLost(QString reason);
  void mediaError(QString reason);
};

} // namespace pcm::video
```

(Only additions relative to the current file — the existing signals/doc comments on `join()`/`leave()`/`remoteVideoWidget()`/the signals are unchanged; keep them as-is and insert the new members among the existing ones as shown.)

- [ ] **Step 5: Extend `FakeVideoProvider`**

In `test/fake_video_provider.h`, add after the existing `remoteVideoWidget()` override:

```cpp
  QWidget *localVideoWidget() override { return mLocalVideoWidget; }

  void setMicrophoneEnabled(bool enabled) override {
    mMicrophoneEnabled = enabled;
    ++mSetMicrophoneEnabledCallCount;
  }
  void setCameraEnabled(bool enabled) override {
    mCameraEnabled = enabled;
    ++mSetCameraEnabledCallCount;
  }
  [[nodiscard]] bool isMicrophoneEnabled() const override { return mMicrophoneEnabled; }
  [[nodiscard]] bool isCameraEnabled() const override { return mCameraEnabled; }

  void switchCamera(const QCameraDevice &device) override {
    mLastSwitchedCamera = device;
    ++mSwitchCameraCallCount;
  }
  void switchMicrophone(const QAudioDevice &device) override {
    mLastSwitchedMicrophone = device;
    ++mSwitchMicrophoneCallCount;
  }
  void switchSpeaker(const QAudioDevice &device) override {
    mLastSwitchedSpeaker = device;
    ++mSwitchSpeakerCallCount;
  }
```

And after the existing `QWidget *mRemoteVideoWidget{nullptr};` field:

```cpp
  QWidget *mLocalVideoWidget{nullptr};
  bool mMicrophoneEnabled{true};
  bool mCameraEnabled{true};
  int mSetMicrophoneEnabledCallCount{0};
  int mSetCameraEnabledCallCount{0};
  int mSwitchCameraCallCount{0};
  int mSwitchMicrophoneCallCount{0};
  int mSwitchSpeakerCallCount{0};
  QCameraDevice mLastSwitchedCamera;
  QAudioDevice mLastSwitchedMicrophone;
  QAudioDevice mLastSwitchedSpeaker;
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build build --target Sessio_fake_video_provider_tests && ctest --test-dir build -R FakeVideoProviderTests --output-on-failure`
Expected: PASS (4/4 tests).

- [ ] **Step 7: Commit**

```bash
git add src/video/video_provider.h test/fake_video_provider.h test/fake_video_provider_tests.cpp test/CMakeLists.txt
git commit -m "Add mute/camera-toggle/device-switch/local-preview to VideoProvider"
```

---

### Task 2: `LiveKitVideoProvider` implementation

**Files:**
- Modify: `src/video/livekit_video_provider.h`
- Modify: `src/video/livekit_video_provider.cpp`
- Test: `test/livekit_video_provider_smoke_test.cpp` (existing smoke-test binary, extended)

**Interfaces:**
- Consumes: Task 1's `VideoProvider` interface; existing `mVideoCapture`/`mAudioCapture`/`mAudioTrack`/`mVideoTrack`/`mDeviceManager`/`mRemoteAudio`/`mRemoteAudioTrack` members.
- Produces: `LiveKitVideoProvider` now satisfies every new `VideoProvider` virtual method for real, consumed by Task 6/7/8's `CallPage` code.

- [ ] **Step 1: Extend the header**

In `src/video/livekit_video_provider.h`, add to the `public:` section (after `remoteVideoWidget()`):

```cpp
  QWidget *localVideoWidget() override;
  void setMicrophoneEnabled(bool enabled) override;
  void setCameraEnabled(bool enabled) override;
  [[nodiscard]] bool isMicrophoneEnabled() const override { return mMicrophoneEnabled; }
  [[nodiscard]] bool isCameraEnabled() const override { return mCameraEnabled; }
  void switchCamera(const QCameraDevice &device) override;
  void switchMicrophone(const QAudioDevice &device) override;
  void switchSpeaker(const QAudioDevice &device) override;
```

Add `#include <QPointer>` (already present), `#include <optional>` (already present via `<memory>`'s transitive use elsewhere — add explicitly: `#include <optional>`), and a forward declaration `class QLabel;` near the other forward declarations. Add to the private members (after `QPointer<RemoteVideoRenderer> mRemoteVideo;`):

```cpp
  QPointer<QLabel> mLocalPreviewWidget;
  bool mMicrophoneEnabled{true};
  bool mCameraEnabled{true};
  std::optional<QAudioDevice> mSelectedSpeaker;
```

- [ ] **Step 2: Implement in the `.cpp`**

In `src/video/livekit_video_provider.cpp`, add `#include <QLabel>` and `#include <QPixmap>` to the includes, then in the constructor body (after the two `connect(mVideoCapture.get(), ...)`/`connect(mAudioCapture.get(), ...)` lines, before the closing comment), add:

```cpp
  mLocalPreviewWidget = new QLabel();
  mLocalPreviewWidget->setObjectName("localVideoWidget");
  connect(mVideoCapture->previewSink(), &QVideoSink::videoFrameChanged, this,
          [this](const QVideoFrame &frame) {
            if (mLocalPreviewWidget && frame.isValid()) {
              mLocalPreviewWidget->setPixmap(
                  QPixmap::fromImage(frame.toImage())
                      .scaled(mLocalPreviewWidget->size(), Qt::KeepAspectRatio,
                              Qt::SmoothTransformation));
            }
          });
```

(`#include <QVideoSink>` and `#include <QVideoFrame>` are already pulled in transitively via `video_capture_adapter.h`; add them explicitly to `livekit_video_provider.cpp`'s includes for clarity since this lambda uses `QVideoFrame`/`QVideoSink` directly.)

Update the destructor's existing `delete mRemoteVideo.data();` line — add right after it:

```cpp
  delete mLocalPreviewWidget.data();
```

with the same ownership-safety comment already given for `mRemoteVideo` (it applies identically): copy that comment above this new line.

Add the new method bodies (after `QWidget *LiveKitVideoProvider::remoteVideoWidget() { return mRemoteVideo.data(); }`):

```cpp
QWidget *LiveKitVideoProvider::localVideoWidget() { return mLocalPreviewWidget.data(); }

void LiveKitVideoProvider::setMicrophoneEnabled(bool enabled) {
  mMicrophoneEnabled = enabled;
  if (mAudioTrack) {
    enabled ? mAudioTrack->unmute() : mAudioTrack->mute();
  }
}

void LiveKitVideoProvider::setCameraEnabled(bool enabled) {
  mCameraEnabled = enabled;
  if (mVideoTrack) {
    enabled ? mVideoTrack->unmute() : mVideoTrack->mute();
  }
}

void LiveKitVideoProvider::switchCamera(const QCameraDevice &device) {
  // Same livekit::VideoSource the whole call publishes from — start()
  // only restarts the Qt-side QCamera/capture worker, so no republish is
  // needed.
  mVideoCapture->start(device);
}

void LiveKitVideoProvider::switchMicrophone(const QAudioDevice &device) {
  mAudioCapture->start(device);
}

void LiveKitVideoProvider::switchSpeaker(const QAudioDevice &device) {
  mSelectedSpeaker = device;
  if (mRemoteAudioTrack) {
    mRemoteAudio->detach();
    mRemoteAudio->attachTrack(mRemoteAudioTrack, device);
  }
}
```

In `publishTracks()`, after the successful `localParticipant->publishTrack(mAudioTrack, audioOptions);` line (inside the `try` block, after that call), add:

```cpp
    if (!mMicrophoneEnabled) {
      mAudioTrack->mute();
    }
```

and symmetrically after `localParticipant->publishTrack(mVideoTrack, videoOptions);`:

```cpp
    if (!mCameraEnabled) {
      mVideoTrack->mute();
    }
```

(Covers the edge case of a toggle being flipped before `join()`'s async publish completes — cheap and correct, not exercised by any UI path added in this plan today, but keeps the two "current desired state" booleans honest.)

In `onTrackSubscribed()`'s audio branch, replace:

```cpp
        } else if (kind == livekit::TrackKind::KIND_AUDIO) {
          mRemoteAudioTrack = track;
          const auto device = mDeviceManager->defaultSpeaker();
          if (device) {
            mRemoteAudio->attachTrack(track, *device);
          }
        }
```

with:

```cpp
        } else if (kind == livekit::TrackKind::KIND_AUDIO) {
          mRemoteAudioTrack = track;
          const auto device = mSelectedSpeaker ? mSelectedSpeaker : mDeviceManager->defaultSpeaker();
          if (device) {
            mRemoteAudio->attachTrack(track, *device);
          }
        }
```

- [ ] **Step 3: Extend the smoke test**

`test/livekit_video_provider_smoke_test.cpp` has its own `main()`, not GoogleTest — mirror its existing style. Read the existing file first, then append (before its final summary/return): a construct-only check that `provider.localVideoWidget()` returns non-null and `provider.isMicrophoneEnabled()`/`isCameraEnabled()` both start `true`, and that `provider.setMicrophoneEnabled(false)` / `setCameraEnabled(false)` don't crash or throw when called before any `join()` (no `mAudioTrack`/`mVideoTrack` yet — must hit the `if (mAudioTrack)`/`if (mVideoTrack)` guards, not dereference a null track).

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target Sessio_livekit_video_provider_smoke_test && ctest --test-dir build -R LiveKitVideoProviderSmokeTest --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/video/livekit_video_provider.h src/video/livekit_video_provider.cpp test/livekit_video_provider_smoke_test.cpp
git commit -m "Implement mute/camera-toggle/device-switch/local-preview in LiveKitVideoProvider"
```

---

### Task 3: `BusySpinner` widget

**Files:**
- Create: `src/widgets/busy_spinner.h`, `src/widgets/busy_spinner.cpp`
- Modify: `src/widgets/CMakeLists.txt`
- Test: `test/busy_spinner_tests.cpp` (new)
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  namespace pcm::widgets {
  class BusySpinner final : public QWidget {
    Q_OBJECT
  public:
    explicit BusySpinner(QWidget *parent = nullptr);
    [[nodiscard]] bool isAnimating() const; // true iff the rotation animation is currently running
  };
  }
  ```
  Consumed by Task 5 (`CallPage`'s connecting screen and reconnecting banner).

- [ ] **Step 1: Write the failing test**

```cpp
// test/busy_spinner_tests.cpp
#include "busy_spinner.h"

#include <QApplication>
#include <gtest/gtest.h>

using pcm::widgets::BusySpinner;

TEST(BusySpinnerTest, NotAnimatingBeforeShown) {
  BusySpinner spinner;
  EXPECT_FALSE(spinner.isAnimating());
}

TEST(BusySpinnerTest, AnimatesWhileShownAndStopsWhenHidden) {
  BusySpinner spinner;
  spinner.show();
  EXPECT_TRUE(spinner.isAnimating());

  spinner.hide();
  EXPECT_FALSE(spinner.isAnimating());
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

- [ ] **Step 2: Add the CMake wiring**

In `src/widgets/CMakeLists.txt`, add `busy_spinner.cpp` and `busy_spinner.h` to the `qt_add_library(${TARGET_NAME} STATIC ...)` source list.

In `test/CMakeLists.txt`, add:

```cmake
# BusySpinner tests
add_executable(Sessio_busy_spinner_tests
    busy_spinner_tests.cpp
)
target_include_directories(Sessio_busy_spinner_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/widgets)
target_link_libraries(Sessio_busy_spinner_tests PRIVATE
    GTest::gtest
    Qt6::Widgets
    Sessio_widgets
)
set_target_properties(Sessio_busy_spinner_tests PROPERTIES AUTOMOC ON)
add_test(NAME BusySpinnerTests COMMAND Sessio_busy_spinner_tests)
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target Sessio_busy_spinner_tests`
Expected: FAIL — `busy_spinner.h` does not exist yet.

- [ ] **Step 4: Write `BusySpinner`**

```cpp
// src/widgets/busy_spinner.h
#pragma once

#include <QWidget>

class QPropertyAnimation;

namespace pcm::widgets {

// Small indeterminate circular progress indicator: a rotating arc drawn via
// QPainter. The rotation animation only runs while the widget is actually
// visible (started on showEvent(), stopped on hideEvent()) so a spinner on
// a screen that isn't currently shown costs nothing.
class BusySpinner final : public QWidget {
  Q_OBJECT
  Q_PROPERTY(int angle READ angle WRITE setAngle)

public:
  explicit BusySpinner(QWidget *parent = nullptr);

  [[nodiscard]] int angle() const { return mAngle; }
  void setAngle(int angle);

  [[nodiscard]] bool isAnimating() const;

protected:
  void paintEvent(QPaintEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  int mAngle{0};
  QPropertyAnimation *mAnimation;
};

} // namespace pcm::widgets
```

```cpp
// src/widgets/busy_spinner.cpp
#include "busy_spinner.h"

#include <QPainter>
#include <QPropertyAnimation>

namespace pcm::widgets {

namespace {
constexpr int kArcSpanDegrees = 270;
constexpr int kRotationPeriodMs = 1000;
} // namespace

BusySpinner::BusySpinner(QWidget *parent) : QWidget(parent), mAnimation(new QPropertyAnimation(this, "angle", this)) {
  setFixedSize(16, 16);
  mAnimation->setStartValue(0);
  mAnimation->setEndValue(360);
  mAnimation->setDuration(kRotationPeriodMs);
  mAnimation->setLoopCount(-1);
}

void BusySpinner::setAngle(int angle) {
  mAngle = angle;
  update();
}

bool BusySpinner::isAnimating() const {
  return mAnimation->state() == QAbstractAnimation::Running;
}

void BusySpinner::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  QPen pen(palette().color(QPalette::WindowText));
  pen.setWidth(2);
  pen.setCapStyle(Qt::RoundCap);
  painter.setPen(pen);
  const QRectF arcRect = rect().adjusted(1, 1, -1, -1);
  // QPainter::drawArc()'s angles are in 1/16ths of a degree and increase
  // counter-clockwise from the 3-o'clock position.
  painter.drawArc(arcRect, -mAngle * 16, kArcSpanDegrees * 16);
}

void BusySpinner::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  mAnimation->start();
}

void BusySpinner::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
  mAnimation->stop();
}

} // namespace pcm::widgets
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --target Sessio_busy_spinner_tests && ctest --test-dir build -R BusySpinnerTests --output-on-failure`
Expected: PASS (2/2 tests).

- [ ] **Step 6: Commit**

```bash
git add src/widgets/busy_spinner.h src/widgets/busy_spinner.cpp src/widgets/CMakeLists.txt test/busy_spinner_tests.cpp test/CMakeLists.txt
git commit -m "Add BusySpinner indeterminate progress widget"
```

---

### Task 4: Fix `RemoteVideoRenderer` transparency artifact

**Files:**
- Modify: `src/video/remote_video_renderer.h`
- Modify: `src/video/remote_video_renderer.cpp`
- Test: `test/remote_video_renderer_letterbox_test.cpp` (new — pure-function test, no GL context needed)

**Interfaces:**
- Produces (added to `pcm::video::RemoteVideoRenderer`, as a private static helper so it's callable from a plain GoogleTest without an OpenGL context):
  ```cpp
  // Exposed for testing only.
  static QRect scaledFrameRect(const QSize &frameSize, const QSize &widgetSize);
  ```

**Root cause (see design doc §6):** `paintGL()` never fills the widget's background — it `return`s outright when there's no frame yet, and when there is a frame, `Qt::KeepAspectRatio` scaling leaves letterbox bars unpainted. Both leave `QOpenGLWidget`'s buffer showing whatever was there before, i.e. a see-through region.

- [ ] **Step 1: Write the failing test**

```cpp
// test/remote_video_renderer_letterbox_test.cpp
#include "remote_video_renderer.h"

#include <gtest/gtest.h>

using pcm::video::RemoteVideoRenderer;

// scaledFrameRect() is the pure geometry calculation paintGL() uses to
// place a KeepAspectRatio-scaled frame inside the widget. No QOpenGLWidget/
// GL context is exercised here — this only pins down the math that
// determines the letterbox region paintGL() must now fill.
TEST(RemoteVideoRendererLetterboxTest, WidthConstrainedFrameIsCenteredWithVerticalBars) {
  const QRect placed = RemoteVideoRenderer::scaledFrameRect(QSize(1280, 720), QSize(1280, 1000));
  // Widget is taller than the frame's aspect ratio requires: full width, bars top/bottom.
  EXPECT_EQ(placed.width(), 1280);
  EXPECT_LT(placed.height(), 1000);
  EXPECT_GT(placed.top(), 0);
}

TEST(RemoteVideoRendererLetterboxTest, HeightConstrainedFrameIsCenteredWithHorizontalBars) {
  const QRect placed = RemoteVideoRenderer::scaledFrameRect(QSize(1280, 720), QSize(2000, 720));
  EXPECT_EQ(placed.height(), 720);
  EXPECT_LT(placed.width(), 2000);
  EXPECT_GT(placed.left(), 0);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

- [ ] **Step 2: Add the test target**

In `test/CMakeLists.txt`, add (near `Sessio_remote_video_renderer_smoke_test`):

```cmake
add_executable(Sessio_remote_video_renderer_letterbox_test
    ${CMAKE_SOURCE_DIR}/src/video/remote_video_renderer.cpp
    remote_video_renderer_letterbox_test.cpp
)
target_include_directories(Sessio_remote_video_renderer_letterbox_test PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_remote_video_renderer_letterbox_test PRIVATE
    GTest::gtest
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGLWidgets
    LiveKit::livekit
)
add_test(NAME RemoteVideoRendererLetterboxTest COMMAND Sessio_remote_video_renderer_letterbox_test)
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target Sessio_remote_video_renderer_letterbox_test`
Expected: FAIL — no `scaledFrameRect()` member exists yet.

- [ ] **Step 4: Fix `remote_video_renderer.h`**

Add to the `private:` section, before `void readerLoop();`:

```cpp
public:
  // Pure geometry helper (no GL/paint dependency) — where a frame of
  // `frameSize`, scaled with Qt::KeepAspectRatio, lands inside a widget of
  // `widgetSize`. Exposed only so paintGL()'s letterbox math is directly
  // testable without an OpenGL context.
  static QRect scaledFrameRect(const QSize &frameSize, const QSize &widgetSize);

private:
```

(Add `#include <QRect>` and `#include <QSize>` to the header's includes.)

- [ ] **Step 5: Fix `remote_video_renderer.cpp`**

Replace `paintGL()`:

```cpp
QRect RemoteVideoRenderer::scaledFrameRect(const QSize &frameSize, const QSize &widgetSize) {
  const QSize scaled = frameSize.scaled(widgetSize, Qt::KeepAspectRatio);
  const QPoint topLeft((widgetSize.width() - scaled.width()) / 2,
                        (widgetSize.height() - scaled.height()) / 2);
  return QRect(topLeft, scaled);
}

void RemoteVideoRenderer::paintGL() {
  QImage frame;
  {
    QMutexLocker locker(&mFrameMutex);
    frame = mLatestFrame;
  }

  QPainter painter(this);
  // Always fill first: QOpenGLWidget shows whatever was in its buffer
  // before this call for any pixel paintGL() doesn't touch — with no frame
  // yet, or with a KeepAspectRatio-scaled frame leaving letterbox bars,
  // that used to be a see-through/garbage region instead of a solid
  // background (see this plan's Task 4 / design doc §6).
  painter.fillRect(rect(), Qt::black);
  if (frame.isNull()) {
    return;
  }
  const QRect target = scaledFrameRect(frame.size(), size());
  const QImage scaled = frame.scaled(target.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
  painter.drawImage(target.topLeft(), scaled);
}
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build build --target Sessio_remote_video_renderer_letterbox_test && ctest --test-dir build -R RemoteVideoRendererLetterboxTest --output-on-failure`
Expected: PASS (2/2 tests).

Also re-run the existing GL smoke test to confirm no regression: `cmake --build build --target Sessio_remote_video_renderer_smoke_test && ctest --test-dir build -R RemoteVideoRendererSmokeTest --output-on-failure` — Expected: PASS (unchanged).

- [ ] **Step 7: Commit**

```bash
git add src/video/remote_video_renderer.h src/video/remote_video_renderer.cpp test/remote_video_renderer_letterbox_test.cpp test/CMakeLists.txt
git commit -m "Fix RemoteVideoRenderer leaving letterbox/no-frame areas unpainted"
```

---

### Task 5: `CallPage` typography and spinner integration

**Files:**
- Modify: `src/pages/calls_page/call_page.h`
- Modify: `src/pages/calls_page/call_page.cpp`
- Modify: `src/pages/calls_page/CMakeLists.txt` (link `Sessio_widgets`)
- Test: `test/call_page_tests.cpp` (extended)

**Interfaces:**
- Consumes: Task 3's `pcm::widgets::BusySpinner`.
- No public `CallPage` API change.

- [ ] **Step 1: Write the failing tests**

Append to `test/call_page_tests.cpp` (before the `int main(...)` at the end):

```cpp
TEST(CallPageTest, ConnectingScreenShowsCenteredSpinnerAndHeadline) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);

  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);

  auto *connectingLabel = page.findChild<QLabel *>("connectingLabel");
  ASSERT_NE(connectingLabel, nullptr);
  EXPECT_EQ(connectingLabel->alignment() & Qt::AlignHCenter, Qt::AlignHCenter);

  auto *spinner = page.findChild<pcm::widgets::BusySpinner *>("connectingSpinner");
  ASSERT_NE(spinner, nullptr);
  EXPECT_TRUE(spinner->isVisibleTo(spinner->parentWidget()));
}

TEST(CallPageTest, ReconnectingBannerHasASpinner) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *spinner = page.findChild<pcm::widgets::BusySpinner *>("reconnectingSpinner");
  ASSERT_NE(spinner, nullptr);
}
```

Add `#include "busy_spinner.h"` to `test/call_page_tests.cpp`'s includes.

- [ ] **Step 2: Link `Sessio_widgets` into the build**

In `src/pages/calls_page/CMakeLists.txt`, add `${PROJECT_NAME}_widgets` to `target_link_libraries(${TARGET_NAME} PUBLIC ...)`.

In `test/CMakeLists.txt`, add `Sessio_widgets` to `Sessio_call_page_tests`'s `target_link_libraries(...)`, and add `target_include_directories(Sessio_call_page_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/widgets)`.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target Sessio_call_page_tests`
Expected: FAIL — no `connectingSpinner`/`reconnectingSpinner`, and `busy_spinner.h` not yet includable from this target until Step 2's CMake change plus Step 4/5's actual widget additions land.

- [ ] **Step 4: Update `call_page.h`**

Add `#include "busy_spinner.h"` and change the `mReconnectingBanner` field's declared type from `QWidget *` to keep it a `QWidget*` (it already is a `QLabel*` typed as `QWidget*` isn't quite right — it's currently declared `QWidget *mReconnectingBanner{nullptr};` per the existing header, constructed as a `QLabel` and assigned to that base-typed pointer). Add a new field for the label inside it:

```cpp
  QWidget *mReconnectingBanner{nullptr};
  QLabel *mReconnectingLabel{nullptr};
```

(`mReconnectingLabel` is new; `mReconnectingBanner` keeps its existing declared type and role as the whole banner row, now a plain container `QWidget` instead of being the `QLabel` itself.)

- [ ] **Step 5: Update `call_page.cpp`**

Add includes: `#include <QFont>`.

Replace the constructor's status-label construction. Current:

```cpp
  mConnectingScreen = new QWidget(this);
  new QVBoxLayout(mConnectingScreen);
  mConnectingLabel = new QLabel(tr("Connecting..."), mConnectingScreen);
  mConnectingLabel->setObjectName("connectingLabel");
  static_cast<QVBoxLayout *>(mConnectingScreen->layout())->addWidget(mConnectingLabel);
  mStack->addWidget(mConnectingScreen);
```

Replace with:

```cpp
  mConnectingScreen = new QWidget(this);
  auto *connectingLayout = new QVBoxLayout(mConnectingScreen);
  connectingLayout->addStretch();
  auto *connectingSpinner = new pcm::widgets::BusySpinner(mConnectingScreen);
  connectingSpinner->setObjectName("connectingSpinner");
  auto *spinnerRow = new QHBoxLayout();
  spinnerRow->addStretch();
  spinnerRow->addWidget(connectingSpinner);
  spinnerRow->addStretch();
  connectingLayout->addLayout(spinnerRow);
  mConnectingLabel = new QLabel(tr("Connecting..."), mConnectingScreen);
  mConnectingLabel->setObjectName("connectingLabel");
  mConnectingLabel->setAlignment(Qt::AlignCenter);
  mConnectingLabel->setFont(QFont(QStringLiteral("Inter Display"), 20));
  connectingLayout->addWidget(mConnectingLabel);
  connectingLayout->addStretch();
  mStack->addWidget(mConnectingScreen);
```

Replace the constructor's reconnecting-banner construction. Current:

```cpp
  mReconnectingBanner = new QLabel(tr("Reconnecting..."), this);
  mReconnectingBanner->setObjectName("reconnectingBanner");
  mReconnectingBanner->setVisible(false);
  outer->addWidget(mReconnectingBanner);
```

Replace with:

```cpp
  mReconnectingBanner = new QWidget(this);
  mReconnectingBanner->setObjectName("reconnectingBanner");
  mReconnectingBanner->setVisible(false);
  auto *reconnectingLayout = new QHBoxLayout(mReconnectingBanner);
  reconnectingLayout->addStretch();
  auto *reconnectingSpinner = new pcm::widgets::BusySpinner(mReconnectingBanner);
  reconnectingSpinner->setObjectName("reconnectingSpinner");
  reconnectingLayout->addWidget(reconnectingSpinner);
  mReconnectingLabel = new QLabel(tr("Reconnecting..."), mReconnectingBanner);
  mReconnectingLabel->setFont(QFont(QStringLiteral("Inter"), 16));
  reconnectingLayout->addWidget(mReconnectingLabel);
  reconnectingLayout->addStretch();
  outer->addWidget(mReconnectingBanner);
```

Update the ended-screen label's font/alignment. Current:

```cpp
  mEndedScreen = new QWidget(this);
  auto *endedLayout = new QVBoxLayout(mEndedScreen);
  endedLayout->addWidget(new QLabel(tr("Call ended."), mEndedScreen));
```

Replace with:

```cpp
  mEndedScreen = new QWidget(this);
  auto *endedLayout = new QVBoxLayout(mEndedScreen);
  endedLayout->addStretch();
  auto *endedHeadline = new QLabel(tr("Call ended."), mEndedScreen);
  endedHeadline->setAlignment(Qt::AlignCenter);
  endedHeadline->setFont(QFont(QStringLiteral("Inter Display"), 20));
  endedLayout->addWidget(endedHeadline);
```

(Leave the rest of `mEndedScreen`'s construction — `mEndedReasonLabel` and `mStack->addWidget(mEndedScreen)` — as-is, but add `endedLayout->addStretch();` after `endedLayout->addWidget(mEndedReasonLabel);` to keep the whole block vertically centered, and give `mEndedReasonLabel` `setAlignment(Qt::AlignCenter)` and `setFont(QFont(QStringLiteral("Inter"), 16))` alongside its existing `setWordWrap(true)`.)

`onSessionStateChanged()`'s `mConnectingLabel->setText(...)` calls (for `Joining`/`WaitingForClient`) are unchanged — the label's font/alignment were set once at construction and persist across `setText()`.

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build build --target Sessio_call_page_tests && ctest --test-dir build -R CallPageTest --output-on-failure`
Expected: PASS (all `CallPageTest` cases, including the two new ones).

- [ ] **Step 7: Commit**

```bash
git add src/pages/calls_page/call_page.h src/pages/calls_page/call_page.cpp src/pages/calls_page/CMakeLists.txt test/call_page_tests.cpp test/CMakeLists.txt
git commit -m "Center CallPage status text and add busy spinners"
```

---

### Task 6: `VideoStage` + self-preview picture-in-picture

**Files:**
- Modify: `src/pages/calls_page/call_page.h`
- Modify: `src/pages/calls_page/call_page.cpp`
- Test: `test/call_page_tests.cpp` (extended)

**Interfaces:**
- Consumes: Task 1/2's `VideoProvider::localVideoWidget()`.
- Produces (new private nested type in `call_page.h`, not part of `CallPage`'s own public API):
  ```cpp
  class VideoStage final : public QWidget {
  public:
    explicit VideoStage(QWidget *parent = nullptr);
    void setRemoteWidget(QWidget *widget);       // does not take ownership
    void setLocalPreviewWidget(QWidget *widget); // nullptr hides the PIP; does not take ownership
  };
  ```

- [ ] **Step 1: Update the four existing tests that inspect `mVideoRow`'s layout directly**

`mVideoStage` (Step 3-6 below) has no `QLayout` of its own — it positions its two children directly in `resizeEvent()` — so every existing assertion of the shape `placeholder->parentWidget()->layout()->itemAt(0)->layout()->indexOf(...)` breaks once `placeholder`'s parent becomes `mVideoStage` instead of `mConnectedView`. Four existing tests in `test/call_page_tests.cpp` use that pattern and must be rewritten (not just left to fail) as part of this task, since they exist to guard the exact borrowed-widget lifecycle contract this task's `updateRemoteVideoWidget()` change preserves — just expressed through parentage/geometry instead of layout-item indices.

Replace `EmbedsProviderRemoteVideoWidgetInPlaceOfPlaceholder` with:

```cpp
TEST(CallPageTest, EmbedsProviderRemoteVideoWidgetInPlaceOfPlaceholder) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *placeholder = page.findChild<QWidget *>("remoteVideoPlaceholder");
  ASSERT_NE(placeholder, nullptr);
  EXPECT_TRUE(placeholder->isVisibleTo(placeholder->parentWidget()));

  auto *provider = new FakeVideoProvider();
  QPointer<QLabel> remoteVideo = new QLabel("remote video"); // unparented, like LiveKit's
  provider->mRemoteVideoWidget = remoteVideo;
  VideoSession session(provider);
  page.attachSession(&session);

  auto *videoStage = page.findChild<QWidget *>("videoStage");
  ASSERT_NE(videoStage, nullptr);

  EXPECT_TRUE(page.isAncestorOf(remoteVideo));
  EXPECT_EQ(remoteVideo->parentWidget(), videoStage);
  EXPECT_TRUE(remoteVideo->isVisibleTo(videoStage));
  EXPECT_FALSE(placeholder->isVisibleTo(videoStage));
  // It takes the placeholder's slot: VideoStage stretches exactly one
  // remote widget to fill it at a time.
  EXPECT_EQ(remoteVideo->geometry(), videoStage->rect());

  // The widget is only borrowed: CallPage never deletes it. (Here the test
  // plays the provider's role and releases it.)
  delete remoteVideo.data();
}
```

Replace `KeepsPlaceholderWhenProviderHasNoRemoteVideoWidget` with:

```cpp
TEST(CallPageTest, KeepsPlaceholderWhenProviderHasNoRemoteVideoWidget) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider(); // remoteVideoWidget() == nullptr
  VideoSession session(provider);
  page.attachSession(&session);

  auto *placeholder = page.findChild<QWidget *>("remoteVideoPlaceholder");
  auto *videoStage = page.findChild<QWidget *>("videoStage");
  ASSERT_NE(placeholder, nullptr);
  ASSERT_NE(videoStage, nullptr);
  EXPECT_TRUE(placeholder->isVisibleTo(videoStage));
  EXPECT_EQ(placeholder->parentWidget(), videoStage);
  EXPECT_EQ(placeholder->geometry(), videoStage->rect());
}
```

Replace `ReattachingHandsBorrowedRemoteVideoWidgetBackToItsProvider` with:

```cpp
TEST(CallPageTest, ReattachingHandsBorrowedRemoteVideoWidgetBackToItsProvider) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *placeholder = page.findChild<QWidget *>("remoteVideoPlaceholder");
  auto *videoStage = page.findChild<QWidget *>("videoStage");
  ASSERT_NE(videoStage, nullptr);

  auto *firstProvider = new FakeVideoProvider();
  QPointer<QLabel> firstVideo = new QLabel("first");
  firstProvider->mRemoteVideoWidget = firstVideo;
  VideoSession firstSession(firstProvider);
  page.attachSession(&firstSession);
  ASSERT_TRUE(page.isAncestorOf(firstVideo));

  auto *secondProvider = new FakeVideoProvider();
  VideoSession secondSession(secondProvider);
  page.attachSession(&secondSession);
  ASSERT_FALSE(firstVideo.isNull());
  EXPECT_EQ(firstVideo->parent(), nullptr);
  EXPECT_TRUE(placeholder->isVisibleTo(videoStage));
  EXPECT_EQ(placeholder->parentWidget(), videoStage);
  delete firstVideo.data();
}
```

Replace `ToleratesEmbeddedRemoteVideoWidgetDestroyedWithItsProvider` with:

```cpp
TEST(CallPageTest, ToleratesEmbeddedRemoteVideoWidgetDestroyedWithItsProvider) {
  // Mirrors CallsPage::startJoin(): the old session (and with it the old
  // provider and its remote-video widget) is destroyed while that widget is
  // still embedded, and only then is the new session attached.
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *placeholder = page.findChild<QWidget *>("remoteVideoPlaceholder");
  auto *videoStage = page.findChild<QWidget *>("videoStage");
  ASSERT_NE(videoStage, nullptr);

  auto *oldProvider = new FakeVideoProvider();
  QPointer<QLabel> oldVideo = new QLabel("old");
  oldProvider->mRemoteVideoWidget = oldVideo;
  auto oldSession = std::make_unique<VideoSession>(oldProvider);
  page.attachSession(oldSession.get());
  ASSERT_TRUE(page.isAncestorOf(oldVideo));

  // What LiveKitVideoProvider's destructor does to its embedded renderer.
  delete oldVideo.data();
  oldSession.reset();

  auto *newProvider = new FakeVideoProvider();
  QPointer<QLabel> newVideo = new QLabel("new");
  newProvider->mRemoteVideoWidget = newVideo;
  VideoSession newSession(newProvider);
  page.attachSession(&newSession);
  EXPECT_EQ(newVideo->parentWidget(), videoStage);
  EXPECT_EQ(newVideo->geometry(), videoStage->rect());
  EXPECT_FALSE(placeholder->isVisibleTo(videoStage));
  delete newVideo.data();
}
```

- [ ] **Step 2: Write the new failing tests**

Append to `test/call_page_tests.cpp`:

```cpp
TEST(CallPageTest, ShowsLocalPreviewWhenProviderSuppliesOne) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  QPointer<QLabel> localPreview = new QLabel("local");
  provider->mLocalVideoWidget = localPreview;
  VideoSession session(provider);
  page.attachSession(&session);

  EXPECT_TRUE(page.isAncestorOf(localPreview));
  EXPECT_TRUE(localPreview->isVisibleTo(localPreview->parentWidget()));
  delete localPreview.data();
}

TEST(CallPageTest, NoLocalPreviewWidgetWhenProviderHasNone) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider(); // localVideoWidget() == nullptr
  VideoSession session(provider);
  page.attachSession(&session);

  auto *videoStage = page.findChild<QWidget *>("videoStage");
  ASSERT_NE(videoStage, nullptr);
  // No crash and no orphaned preview child beyond the remote-video slot.
  EXPECT_EQ(videoStage->findChildren<QLabel *>().size(), 0);
}

TEST(CallPageTest, SwappingProviderHandsLocalPreviewBackUnparented) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);

  auto *firstProvider = new FakeVideoProvider();
  QPointer<QLabel> firstPreview = new QLabel("first-local");
  firstProvider->mLocalVideoWidget = firstPreview;
  VideoSession firstSession(firstProvider);
  page.attachSession(&firstSession);
  ASSERT_TRUE(page.isAncestorOf(firstPreview));

  auto *secondProvider = new FakeVideoProvider(); // no local preview
  VideoSession secondSession(secondProvider);
  page.attachSession(&secondSession);

  ASSERT_FALSE(firstPreview.isNull());
  EXPECT_EQ(firstPreview->parent(), nullptr);
  delete firstPreview.data();
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target Sessio_call_page_tests`
Expected: FAIL to compile — no `videoStage` object yet, and the four rewritten tests from Step 1 reference a `"videoStage"` child that doesn't exist.

- [ ] **Step 4: Add `VideoStage` to `call_page.h`**

Add near the top of the file, after the includes and before the `CallPage` class:

```cpp
namespace pcm::video::detail {

// Hosts the remote-video widget stretched to fill the available area, with
// the local self-preview overlaid as a fixed-size tile in the bottom-right
// corner. A plain QWidget with no layout manager: QLayout has no way to
// express "fill entirely" and "float pinned to a corner" for two children
// at once, so both are positioned directly in resizeEvent().
class VideoStage final : public QWidget {
public:
  explicit VideoStage(QWidget *parent = nullptr);

  void setRemoteWidget(QWidget *widget);
  void setLocalPreviewWidget(QWidget *widget);

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  void layoutChildren();

  QPointer<QWidget> mRemoteWidget;
  QPointer<QWidget> mLocalPreviewWidget;
};

} // namespace pcm::video::detail
```

Add `#include <QResizeEvent>` to the header's includes. Change the `mVideoRow`-related private members: replace

```cpp
  QHBoxLayout *mVideoRow{nullptr};
  // CallPage-owned blank renderer, ...
  QWidget *mRemoteVideoPlaceholder{nullptr};
  // Whichever widget currently occupies slot 0 of mVideoRow. ...
  QPointer<QWidget> mActiveRemoteVideoWidget;
```

with:

```cpp
  QHBoxLayout *mVideoRow{nullptr};
  pcm::video::detail::VideoStage *mVideoStage{nullptr};
  // CallPage-owned blank renderer, shown whenever the attached provider
  // offers no remote-video widget of its own (or before any session is
  // attached). Never reparented away from mVideoStage.
  QWidget *mRemoteVideoPlaceholder{nullptr};
  // Whichever widget currently occupies mVideoStage's remote slot.
  QPointer<QWidget> mActiveRemoteVideoWidget;
  // Whichever widget currently occupies mVideoStage's local-preview slot
  // (nullptr when the attached provider has none).
  QPointer<QWidget> mActiveLocalPreviewWidget;
```

Add a new private method declaration next to `updateRemoteVideoWidget()`:

```cpp
  void updateLocalPreviewWidget();
```

- [ ] **Step 5: Implement `VideoStage` in `call_page.cpp`**

Add near the top of the file, in an anonymous-namespace-free block right after the includes (it needs external linkage within this translation unit since it's declared in the header):

```cpp
namespace pcm::video::detail {

namespace {
constexpr int kLocalPreviewWidth = 160;
constexpr int kLocalPreviewHeight = 90;
constexpr int kLocalPreviewMargin = 12;
} // namespace

VideoStage::VideoStage(QWidget *parent) : QWidget(parent) {}

void VideoStage::setRemoteWidget(QWidget *widget) {
  mRemoteWidget = widget;
  layoutChildren();
}

void VideoStage::setLocalPreviewWidget(QWidget *widget) {
  mLocalPreviewWidget = widget;
  layoutChildren();
}

void VideoStage::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  layoutChildren();
}

void VideoStage::layoutChildren() {
  if (mRemoteWidget) {
    mRemoteWidget->setGeometry(rect());
  }
  if (mLocalPreviewWidget) {
    mLocalPreviewWidget->setGeometry(width() - kLocalPreviewWidth - kLocalPreviewMargin,
                                      height() - kLocalPreviewHeight - kLocalPreviewMargin,
                                      kLocalPreviewWidth, kLocalPreviewHeight);
    mLocalPreviewWidget->raise();
  }
}

} // namespace pcm::video::detail
```

- [ ] **Step 6: Wire `VideoStage` into `buildConnectedScreen()`**

Replace:

```cpp
  mVideoRow = new QHBoxLayout();
  mRemoteVideoPlaceholder = new pcm::video::RemoteVideoRenderer(mConnectedView);
  mRemoteVideoPlaceholder->setObjectName("remoteVideoPlaceholder");
  mVideoRow->addWidget(mRemoteVideoPlaceholder, 1);
  mActiveRemoteVideoWidget = mRemoteVideoPlaceholder;
  mSidePanelHost = new QWidget(mConnectedView);
```

with:

```cpp
  mVideoRow = new QHBoxLayout();
  mVideoStage = new pcm::video::detail::VideoStage(mConnectedView);
  mVideoStage->setObjectName("videoStage");
  mRemoteVideoPlaceholder = new pcm::video::RemoteVideoRenderer(mVideoStage);
  mRemoteVideoPlaceholder->setObjectName("remoteVideoPlaceholder");
  mVideoStage->setRemoteWidget(mRemoteVideoPlaceholder);
  mActiveRemoteVideoWidget = mRemoteVideoPlaceholder;
  mVideoRow->addWidget(mVideoStage, 1);
  mSidePanelHost = new QWidget(mConnectedView);
```

(`mRemoteVideoPlaceholder` is now parented directly to `mVideoStage`, not `mConnectedView` — `mVideoStage->setRemoteWidget()` is the one call that both parents-if-needed... actually `setRemoteWidget()` above does not reparent; since the placeholder is already constructed with `mVideoStage` as its Qt parent via the constructor call, no reparent is needed for this initial widget. `updateRemoteVideoWidget()` below still handles reparenting for provider-supplied widgets, same as before, just targeting `mVideoStage` instead of `mConnectedView`.)

- [ ] **Step 7: Update `updateRemoteVideoWidget()` and add `updateLocalPreviewWidget()`**

Replace `updateRemoteVideoWidget()`'s body — everywhere it referenced `mVideoRow` or `mConnectedView` as the reparent target now uses `mVideoStage`:

```cpp
void CallPage::updateRemoteVideoWidget() {
  QWidget *provided = nullptr;
  if (mSession && mSession->provider()) {
    provided = mSession->provider()->remoteVideoWidget();
  }
  QWidget *target = provided ? provided : mRemoteVideoPlaceholder;
  if (target == mActiveRemoteVideoWidget) {
    return;
  }

  if (QWidget *previous = mActiveRemoteVideoWidget.data()) {
    previous->hide();
    if (previous != mRemoteVideoPlaceholder) {
      previous->setParent(nullptr);
    }
  }

  if (target->parentWidget() != mVideoStage) {
    target->setParent(mVideoStage);
  }
  mVideoStage->setRemoteWidget(target);
  target->show();
  mActiveRemoteVideoWidget = target;
}

void CallPage::updateLocalPreviewWidget() {
  QWidget *provided = nullptr;
  if (mSession && mSession->provider()) {
    provided = mSession->provider()->localVideoWidget();
  }
  if (provided == mActiveLocalPreviewWidget) {
    return;
  }

  if (QWidget *previous = mActiveLocalPreviewWidget.data()) {
    previous->hide();
    previous->setParent(nullptr);
  }

  mActiveLocalPreviewWidget = provided;
  if (provided) {
    if (provided->parentWidget() != mVideoStage) {
      provided->setParent(mVideoStage);
    }
    provided->show();
  }
  mVideoStage->setLocalPreviewWidget(provided);
}
```

(The old `mVideoRow->removeWidget(previous);` call is dropped: `previous` is no longer a `QHBoxLayout` item once it lives inside `mVideoStage`, which has no layout — `setParent(nullptr)` alone is sufficient to detach it, and `VideoStage::setRemoteWidget()`'s subsequent call updates which widget it positions.)

In `attachSession()`, add a call to the new method right after the existing `updateRemoteVideoWidget();` call at the end of the method:

```cpp
  updateRemoteVideoWidget();
  updateLocalPreviewWidget();
}
```

- [ ] **Step 8: Run test to verify it passes**

Run: `cmake --build build --target Sessio_call_page_tests && ctest --test-dir build -R CallPageTest --output-on-failure`
Expected: PASS (all cases — Step 2's three new tests, and Step 1's four rewritten pre-existing tests, whose underlying guarantee — a borrowed remote-video widget is correctly shown/hidden/reparented/handed-back exactly once at a time — is unchanged, only how each test observes it, since there's no `QLayout` to index into `mVideoStage` anymore).

- [ ] **Step 9: Commit**

```bash
git add src/pages/calls_page/call_page.h src/pages/calls_page/call_page.cpp test/call_page_tests.cpp
git commit -m "Add VideoStage with self-preview picture-in-picture"
```

---

### Task 7: Control bar — mute/camera toggles, fullscreen, glyph icons

**Files:**
- Create: `src/widgets/call_control_icons.h`, `src/widgets/call_control_icons.cpp`
- Modify: `src/widgets/CMakeLists.txt`
- Modify: `src/pages/calls_page/call_page.h`
- Modify: `src/pages/calls_page/call_page.cpp`
- Test: `test/call_control_icons_tests.cpp` (new)
- Test: `test/call_page_tests.cpp` (extended)
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  namespace pcm::widgets {
  [[nodiscard]] QIcon microphoneIcon(bool enabled);
  [[nodiscard]] QIcon cameraIcon(bool enabled);
  [[nodiscard]] QIcon fullscreenIcon(bool active);
  }
  ```
- Consumes: Task 1/2's `setMicrophoneEnabled`/`setCameraEnabled`/`isMicrophoneEnabled`/`isCameraEnabled`.

- [ ] **Step 1: Write the failing icon test**

```cpp
// test/call_control_icons_tests.cpp
#include "call_control_icons.h"

#include <gtest/gtest.h>

using namespace pcm::widgets;

TEST(CallControlIconsTest, EveryIconIsNonNull) {
  EXPECT_FALSE(microphoneIcon(true).isNull());
  EXPECT_FALSE(microphoneIcon(false).isNull());
  EXPECT_FALSE(cameraIcon(true).isNull());
  EXPECT_FALSE(cameraIcon(false).isNull());
  EXPECT_FALSE(fullscreenIcon(true).isNull());
  EXPECT_FALSE(fullscreenIcon(false).isNull());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

- [ ] **Step 2: Add CMake wiring**

In `src/widgets/CMakeLists.txt`, add `call_control_icons.cpp` and `call_control_icons.h` to the source list.

In `test/CMakeLists.txt`:

```cmake
add_executable(Sessio_call_control_icons_tests
    call_control_icons_tests.cpp
)
target_include_directories(Sessio_call_control_icons_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/widgets)
target_link_libraries(Sessio_call_control_icons_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Gui
    Sessio_widgets
)
gtest_discover_tests(Sessio_call_control_icons_tests)
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target Sessio_call_control_icons_tests`
Expected: FAIL — `call_control_icons.h` does not exist.

- [ ] **Step 4: Write the icon helpers**

```cpp
// src/widgets/call_control_icons.h
#pragma once

#include <QIcon>

namespace pcm::widgets {

// Self-drawn (no external SVG asset) glyph icons for the in-call control
// bar. Composed from basic QPainter primitives so the icon set never
// depends on a network fetch or a new third-party license.
[[nodiscard]] QIcon microphoneIcon(bool enabled);
[[nodiscard]] QIcon cameraIcon(bool enabled);
[[nodiscard]] QIcon devicesIcon();
[[nodiscard]] QIcon fullscreenIcon(bool active);

} // namespace pcm::widgets
```

```cpp
// src/widgets/call_control_icons.cpp
#include "call_control_icons.h"

#include <QPainter>
#include <QPixmap>

namespace pcm::widgets {

namespace {
constexpr int kIconSize = 24;

template <typename Draw>
QIcon renderIcon(bool danger, Draw draw) {
  QPixmap pixmap(kIconSize, kIconSize);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  const QColor color = danger ? QColor(220, 53, 69) : QColor(230, 230, 230);
  draw(painter, color);
  return QIcon(pixmap);
}

void drawSlash(QPainter &painter, const QColor &color) {
  painter.setPen(QPen(color, 2));
  painter.drawLine(QPointF(4, 4), QPointF(20, 20));
}
} // namespace

QIcon microphoneIcon(bool enabled) {
  return renderIcon(!enabled, [enabled](QPainter &painter, const QColor &color) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(9, 3, 6, 12), 3, 3);
    painter.setPen(QPen(color, 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(QRectF(6, 9, 12, 12), 0, -180 * 16);
    painter.drawLine(QPointF(12, 21), QPointF(12, 18));
    painter.drawLine(QPointF(8, 21), QPointF(16, 21));
    if (!enabled) {
      drawSlash(painter, color);
    }
  });
}

QIcon cameraIcon(bool enabled) {
  return renderIcon(!enabled, [enabled](QPainter &painter, const QColor &color) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(2, 7, 14, 10), 2, 2);
    QPolygonF lens;
    lens << QPointF(16, 10) << QPointF(22, 6) << QPointF(22, 18) << QPointF(16, 14);
    painter.drawPolygon(lens);
    if (!enabled) {
      drawSlash(painter, color);
    }
  });
}

QIcon devicesIcon() {
  return renderIcon(false, [](QPainter &painter, const QColor &color) {
    const int knobX[3] = {8, 16, 10};
    for (int row = 0; row < 3; ++row) {
      const int y = 6 + row * 6;
      painter.setPen(QPen(color, 2));
      painter.drawLine(QPointF(3, y), QPointF(21, y));
      painter.setPen(Qt::NoPen);
      painter.setBrush(color);
      painter.drawEllipse(QPointF(knobX[row], y), 2.5, 2.5);
    }
  });
}

QIcon fullscreenIcon(bool active) {
  return renderIcon(false, [active](QPainter &painter, const QColor &color) {
    painter.setPen(QPen(color, 2));
    const int o = active ? 7 : 3;
    const int l = 5;
    painter.drawLine(QPointF(o, o + l), QPointF(o, o));
    painter.drawLine(QPointF(o, o), QPointF(o + l, o));
    painter.drawLine(QPointF(24 - o, o + l), QPointF(24 - o, o));
    painter.drawLine(QPointF(24 - o, o), QPointF(24 - o - l, o));
    painter.drawLine(QPointF(o, 24 - o - l), QPointF(o, 24 - o));
    painter.drawLine(QPointF(o, 24 - o), QPointF(o + l, 24 - o));
    painter.drawLine(QPointF(24 - o, 24 - o - l), QPointF(24 - o, 24 - o));
    painter.drawLine(QPointF(24 - o, 24 - o), QPointF(24 - o - l, 24 - o));
  });
}

} // namespace pcm::widgets
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --target Sessio_call_control_icons_tests && ctest --test-dir build -R CallControlIconsTest --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit the icons**

```bash
git add src/widgets/call_control_icons.h src/widgets/call_control_icons.cpp src/widgets/CMakeLists.txt test/call_control_icons_tests.cpp test/CMakeLists.txt
git commit -m "Add self-drawn glyph icons for the call control bar"
```

- [ ] **Step 7: Write the failing `CallPage` control-bar tests**

Append to `test/call_page_tests.cpp` (add `#include "call_control_icons.h"` if referencing icons directly is needed — the tests below only check button state, not pixel content, so no new include is required beyond `QToolButton`):

```cpp
TEST(CallPageTest, MicrophoneToggleButtonCallsProviderAndStartsEnabled) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);

  auto *micButton = page.findChild<QToolButton *>("microphoneToggleButton");
  ASSERT_NE(micButton, nullptr);
  EXPECT_TRUE(micButton->isChecked());

  micButton->setChecked(false);
  EXPECT_EQ(provider->mSetMicrophoneEnabledCallCount, 1);
  EXPECT_FALSE(provider->isMicrophoneEnabled());
}

TEST(CallPageTest, CameraToggleButtonCallsProvider) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);

  auto *cameraButton = page.findChild<QToolButton *>("cameraToggleButton");
  ASSERT_NE(cameraButton, nullptr);
  EXPECT_TRUE(cameraButton->isChecked());

  cameraButton->setChecked(false);
  EXPECT_EQ(provider->mSetCameraEnabledCallCount, 1);
  EXPECT_FALSE(provider->isCameraEnabled());
}

TEST(CallPageTest, FullscreenToggleButtonExistsAndIsCheckable) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *fullscreenButton = page.findChild<QToolButton *>("fullscreenToggleButton");
  ASSERT_NE(fullscreenButton, nullptr);
  EXPECT_TRUE(fullscreenButton->isCheckable());
  EXPECT_FALSE(fullscreenButton->isChecked());
}
```

Add `#include <QToolButton>` to the test file's includes.

- [ ] **Step 8: Run test to verify it fails**

Run: `cmake --build build --target Sessio_call_page_tests`
Expected: FAIL — no such buttons yet.

- [ ] **Step 9: Add the buttons to `call_page.h`**

Add `#include <QToolButton>` (forward declare is enough: `class QToolButton;`). Add three new private fields next to `mNotesToggleButton`:

```cpp
  QToolButton *mMicrophoneToggleButton{nullptr};
  QToolButton *mCameraToggleButton{nullptr};
  QToolButton *mFullscreenToggleButton{nullptr};
```

- [ ] **Step 10: Wire the buttons in `buildConnectedScreen()`**

Add `#include "call_control_icons.h"` and `#include <QToolButton>` to `call_page.cpp`'s includes.

Insert, in `buildConnectedScreen()`, immediately before the existing `auto *controls = new QHBoxLayout();` line's first use (i.e. right after `auto *controls = new QHBoxLayout();` and before `auto *leaveButton = ...`):

```cpp
  mMicrophoneToggleButton = new QToolButton(mConnectedView);
  mMicrophoneToggleButton->setObjectName("microphoneToggleButton");
  mMicrophoneToggleButton->setCheckable(true);
  mMicrophoneToggleButton->setChecked(true);
  mMicrophoneToggleButton->setIcon(pcm::widgets::microphoneIcon(true));
  connect(mMicrophoneToggleButton, &QToolButton::toggled, this, [this](bool checked) {
    mMicrophoneToggleButton->setIcon(pcm::widgets::microphoneIcon(checked));
    if (mSession && mSession->provider()) {
      mSession->provider()->setMicrophoneEnabled(checked);
    }
  });
  controls->addWidget(mMicrophoneToggleButton);

  mCameraToggleButton = new QToolButton(mConnectedView);
  mCameraToggleButton->setObjectName("cameraToggleButton");
  mCameraToggleButton->setCheckable(true);
  mCameraToggleButton->setChecked(true);
  mCameraToggleButton->setIcon(pcm::widgets::cameraIcon(true));
  connect(mCameraToggleButton, &QToolButton::toggled, this, [this](bool checked) {
    mCameraToggleButton->setIcon(pcm::widgets::cameraIcon(checked));
    if (mSession && mSession->provider()) {
      mSession->provider()->setCameraEnabled(checked);
    }
  });
  controls->addWidget(mCameraToggleButton);

  mFullscreenToggleButton = new QToolButton(mConnectedView);
  mFullscreenToggleButton->setObjectName("fullscreenToggleButton");
  mFullscreenToggleButton->setCheckable(true);
  mFullscreenToggleButton->setIcon(pcm::widgets::fullscreenIcon(false));
  connect(mFullscreenToggleButton, &QToolButton::toggled, this, [this](bool checked) {
    mFullscreenToggleButton->setIcon(pcm::widgets::fullscreenIcon(checked));
    if (!window()) {
      return;
    }
    if (checked) {
      window()->showFullScreen();
    } else {
      window()->showNormal();
    }
  });
  controls->addWidget(mFullscreenToggleButton);

  controls->addStretch();
```

(`leaveButton`'s existing construction, immediately after, is unchanged — it now visually follows this group with a stretch separating them, per the design's "Leave stays visually separate" requirement.)

Since a new session's `FakeVideoProvider`/`LiveKitVideoProvider` always starts with `isMicrophoneEnabled()`/`isCameraEnabled()` both `true` (Task 1/2's defaults), and `attachSession()` doesn't currently re-sync these buttons' checked state from the newly attached provider, add to `attachSession()` (right after `updateLocalPreviewWidget();`):

```cpp
  if (mMicrophoneToggleButton && mSession->provider()) {
    const QSignalBlocker blocker(mMicrophoneToggleButton);
    mMicrophoneToggleButton->setChecked(mSession->provider()->isMicrophoneEnabled());
    mMicrophoneToggleButton->setIcon(pcm::widgets::microphoneIcon(mMicrophoneToggleButton->isChecked()));
  }
  if (mCameraToggleButton && mSession->provider()) {
    const QSignalBlocker blocker(mCameraToggleButton);
    mCameraToggleButton->setChecked(mSession->provider()->isCameraEnabled());
    mCameraToggleButton->setIcon(pcm::widgets::cameraIcon(mCameraToggleButton->isChecked()));
  }
```

(`QSignalBlocker` prevents this re-sync from itself calling `setMicrophoneEnabled()`/`setCameraEnabled()` back on the provider it just read the value from. Add `#include <QSignalBlocker>`.)

- [ ] **Step 11: Run test to verify it passes**

Run: `cmake --build build --target Sessio_call_page_tests && ctest --test-dir build -R CallPageTest --output-on-failure`
Expected: PASS (all cases).

- [ ] **Step 12: Commit**

```bash
git add src/pages/calls_page/call_page.h src/pages/calls_page/call_page.cpp test/call_page_tests.cpp
git commit -m "Add mute/camera toggle and fullscreen buttons to CallPage"
```

---

### Task 8: Device-switch popover

**Files:**
- Modify: `src/pages/calls_page/call_page.h`
- Modify: `src/pages/calls_page/call_page.cpp`
- Test: `test/call_page_tests.cpp` (extended)

**Interfaces:**
- Consumes: Task 1/2's `switchCamera`/`switchMicrophone`/`switchSpeaker`; existing `pcm::video::DeviceManager` (already passed into `CallPage`'s constructor).

- [ ] **Step 1: Write the failing tests**

Append to `test/call_page_tests.cpp`:

```cpp
TEST(CallPageTest, DevicesButtonOpensPopoverWithThreeCombos) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *devicesButton = page.findChild<QToolButton *>("devicesButton");
  ASSERT_NE(devicesButton, nullptr);

  devicesButton->click();

  auto *cameraCombo = page.findChild<QComboBox *>("deviceCameraCombo");
  auto *microphoneCombo = page.findChild<QComboBox *>("deviceMicrophoneCombo");
  auto *speakerCombo = page.findChild<QComboBox *>("deviceSpeakerCombo");
  EXPECT_NE(cameraCombo, nullptr);
  EXPECT_NE(microphoneCombo, nullptr);
  EXPECT_NE(speakerCombo, nullptr);
}

TEST(CallPageTest, SelectingADeviceCallsSwitchOnTheAttachedProvider) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);

  auto *devicesButton = page.findChild<QToolButton *>("devicesButton");
  ASSERT_NE(devicesButton, nullptr);
  devicesButton->click();

  auto *microphoneCombo = page.findChild<QComboBox *>("deviceMicrophoneCombo");
  ASSERT_NE(microphoneCombo, nullptr);
  if (microphoneCombo->count() > 1) {
    microphoneCombo->setCurrentIndex(microphoneCombo->currentIndex() == 0 ? 1 : 0);
    EXPECT_EQ(provider->mSwitchMicrophoneCallCount, 1);
  }
}
```

Add `#include <QComboBox>` to the test file's includes.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target Sessio_call_page_tests`
Expected: FAIL — no `devicesButton`/combos exist yet.

- [ ] **Step 3: Add fields to `call_page.h`**

Add forward declarations `class QComboBox; class QMenu;`. Add private fields next to `mFullscreenToggleButton`:

```cpp
  QToolButton *mDevicesButton{nullptr};
  QComboBox *mDeviceCameraCombo{nullptr};
  QComboBox *mDeviceMicrophoneCombo{nullptr};
  QComboBox *mDeviceSpeakerCombo{nullptr};
```

Add a private method declaration:

```cpp
  void showDevicesPopover();
```

- [ ] **Step 4: Implement in `call_page.cpp`**

Add includes: `#include <QComboBox>`, `#include <QMenu>`, `#include <QWidgetAction>`, `#include <QCameraDevice>`, `#include <QAudioDevice>`.

Insert construction of `mDevicesButton` right after the fullscreen button block from Task 7 (before `controls->addStretch();`):

```cpp
  mDevicesButton = new QToolButton(mConnectedView);
  mDevicesButton->setObjectName("devicesButton");
  mDevicesButton->setIcon(pcm::widgets::devicesIcon());
  connect(mDevicesButton, &QToolButton::clicked, this, &CallPage::showDevicesPopover);
  controls->addWidget(mDevicesButton);
```

Add the new method, using the constructor-supplied `pcm::video::DeviceManager *deviceManager` — `CallPage` already stores no direct pointer to it today, so add a field for it: in `call_page.h`, add `pcm::video::DeviceManager *mDeviceManager{nullptr};` next to `mSession`, and in the constructor (`call_page.cpp`'s `CallPage::CallPage(...)`), add `mDeviceManager = deviceManager;` as its first line.

```cpp
void CallPage::showDevicesPopover() {
  auto *menu = new QMenu(this);
  menu->setAttribute(Qt::WA_DeleteOnClose);

  auto addDeviceRow = [menu](const QString &objectName, const auto &devices, const QByteArray &currentId,
                              auto onSelected) -> QComboBox * {
    auto *combo = new QComboBox(menu);
    combo->setObjectName(objectName);
    for (const auto &device : devices) {
      combo->addItem(device.description(), device.id());
      if (device.id() == currentId) {
        combo->setCurrentIndex(combo->count() - 1);
      }
    }
    QObject::connect(combo, &QComboBox::currentIndexChanged, menu,
                      [combo, onSelected](int index) {
                        if (index < 0) {
                          return;
                        }
                        onSelected(combo->itemData(index).toByteArray());
                      });
    auto *action = new QWidgetAction(menu);
    action->setDefaultWidget(combo);
    menu->addAction(action);
    return combo;
  };

  auto *provider = mSession ? mSession->provider() : nullptr;

  mDeviceCameraCombo = addDeviceRow(
      QStringLiteral("deviceCameraCombo"), mDeviceManager->cameras(), QByteArray(),
      [provider](const QByteArray &id) {
        if (!provider) {
          return;
        }
        for (const auto &device : QMediaDevices::videoInputs()) {
          if (device.id() == id) {
            provider->switchCamera(device);
            return;
          }
        }
      });
  mDeviceMicrophoneCombo = addDeviceRow(
      QStringLiteral("deviceMicrophoneCombo"), mDeviceManager->microphones(), QByteArray(),
      [provider](const QByteArray &id) {
        if (!provider) {
          return;
        }
        for (const auto &device : QMediaDevices::audioInputs()) {
          if (device.id() == id) {
            provider->switchMicrophone(device);
            return;
          }
        }
      });
  mDeviceSpeakerCombo = addDeviceRow(
      QStringLiteral("deviceSpeakerCombo"), mDeviceManager->speakers(), QByteArray(),
      [provider](const QByteArray &id) {
        if (!provider) {
          return;
        }
        for (const auto &device : QMediaDevices::audioOutputs()) {
          if (device.id() == id) {
            provider->switchSpeaker(device);
            return;
          }
        }
      });

  menu->popup(mDevicesButton->mapToGlobal(QPoint(0, mDevicesButton->height())));
}
```

Add `#include <QMediaDevices>` and `#include <QPoint>`.

(This re-looks-up the full `QMediaDevices::videoInputs()/audioInputs()/audioOutputs()` list by id inside each combo's callback, rather than capturing `DeviceList` by value, because `DeviceManager::cameras()`/`microphones()`/`speakers()` are themselves thin per-call wrappers over `QMediaDevices` — see `device_manager.h`'s doc comment "live-queried on every call, exactly as before" — so re-querying is both the cheapest and the most up-to-date option, consistent with that existing design choice; the alternative of capturing `mDeviceManager->cameras()` by value in the lambda would go stale if a device was unplugged between opening the popover and picking an entry.)

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --target Sessio_call_page_tests && ctest --test-dir build -R CallPageTest --output-on-failure`
Expected: PASS (all cases). Note: `SelectingADeviceCallsSwitchOnTheAttachedProvider` is a no-op assertion (nothing runs inside the `if`) on any CI runner with fewer than 2 microphone devices — acceptable, since a sandboxed CI runner commonly has zero or one audio input device; the test still exercises the popover-construction path unconditionally via Step 1's other test.

- [ ] **Step 6: Commit**

```bash
git add src/pages/calls_page/call_page.h src/pages/calls_page/call_page.cpp test/call_page_tests.cpp
git commit -m "Add mid-call device-switch popover to CallPage"
```

---

### Task 9: Default-open notes for client-linked calls

**Files:**
- Modify: `src/pages/calls_page/call_page.h`
- Modify: `src/pages/calls_page/call_page.cpp`
- Modify: `src/pages/calls_page/calls_page.h`
- Modify: `src/pages/calls_page/calls_page.cpp`
- Modify: `src/app/application.cpp`
- Test: `test/call_page_tests.cpp` (extended)

**Interfaces:**
- Produces:
  ```cpp
  // CallPage
  void setSidePanelExpandedByDefault(bool expanded);
  // CallsPage
  void setSidePanelExpandedByDefault(bool expanded); // forwards to CallPage
  ```

- [ ] **Step 1: Write the failing test**

Append to `test/call_page_tests.cpp`:

```cpp
TEST(CallPageTest, SetSidePanelExpandedByDefaultChecksTheNotesToggle) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  page.setSidePanelToggleVisible(true);
  auto *toggle = page.findChild<QPushButton *>("notesToggleButton");
  ASSERT_NE(toggle, nullptr);
  EXPECT_FALSE(toggle->isChecked());

  page.setSidePanelExpandedByDefault(true);
  EXPECT_TRUE(toggle->isChecked());
}

TEST(CallPageTest, SetSidePanelExpandedByDefaultIsANoOpWithoutATotoggleYet) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  // No crash when called before setSidePanelToggleVisible(true) (client
  // mode, or before Application's eventKnownForCurrentCall handler ever
  // fires).
  page.setSidePanelExpandedByDefault(true);
  EXPECT_EQ(page.findChild<QPushButton *>("notesToggleButton"), nullptr);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target Sessio_call_page_tests`
Expected: FAIL — no such method.

- [ ] **Step 3: Add `CallPage::setSidePanelExpandedByDefault()`**

In `call_page.h`, add to the `public:` section (after `setSidePanelToggleVisible`):

```cpp
  // Expands (or collapses) the notes side panel immediately if the toggle
  // button already exists (setSidePanelToggleVisible(true) was already
  // called); a no-op otherwise. Called by CallsPage once it knows whether
  // the current call is linked to a real client/event.
  void setSidePanelExpandedByDefault(bool expanded);
```

In `call_page.cpp`, add after `setSidePanelToggleVisible()`:

```cpp
void CallPage::setSidePanelExpandedByDefault(bool expanded) {
  if (mNotesToggleButton) {
    mNotesToggleButton->setChecked(expanded);
  }
}
```

- [ ] **Step 4: Forward through `CallsPage`**

In `calls_page.h`, add to the public section (near `setSidePanelWidget`):

```cpp
  void setSidePanelExpandedByDefault(bool expanded);
```

In `calls_page.cpp`, add near `setSidePanelWidget(...)`'s existing one-line forwarding implementation:

```cpp
void CallsPage::setSidePanelExpandedByDefault(bool expanded) { mCallPage->setSidePanelExpandedByDefault(expanded); }
```

- [ ] **Step 5: Wire it in `Application::connectSignals()`**

In `src/app/application.cpp`, inside the existing `connect(callsPage, &CallsPage::eventKnownForCurrentCall, this, [...])` lambda, after the existing `mCallNotesPanel->setClientInfo(client);` line, add:

```cpp
              callsPage->setSidePanelExpandedByDefault(client.has_value());
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build build --target Sessio_call_page_tests && ctest --test-dir build -R CallPageTest --output-on-failure`
Expected: PASS (all cases).

- [ ] **Step 7: Commit**

```bash
git add src/pages/calls_page/call_page.h src/pages/calls_page/call_page.cpp src/pages/calls_page/calls_page.h src/pages/calls_page/calls_page.cpp src/app/application.cpp test/call_page_tests.cpp
git commit -m "Default-open the notes side panel for client-linked calls"
```

---

### Task 10: Version bump, changelog, and translations

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/app/application.cpp`
- Modify: `CHANGELOG.md`
- Modify: `translation/app_ru.ts`, `translation/app_en.ts`

**Interfaces:** None (no code interface — process/metadata only).

- [ ] **Step 1: Bump the version**

Per `AGENTS.md`, raise the version in both places, from `0.2.1` to `0.2.2`:
- `CMakeLists.txt:11`: `project(Sessio VERSION 0.2.1 LANGUAGES CXX)` → `project(Sessio VERSION 0.2.2 LANGUAGES CXX)`
- `src/app/application.cpp:141`: `app.setApplicationVersion("0.2.1");` → `app.setApplicationVersion("0.2.2");`

If either line no longer reads `0.2.1` by the time this task runs (an unrelated version bump landed on `main` first), bump from whatever the current value actually is to its next patch version instead — the exact line numbers/current value above are current as of this plan's writing, not a fixed requirement.

- [ ] **Step 2: Update `CHANGELOG.md`**

Add a new entry (read the file first to match its existing heading/bullet style exactly) describing, in user-facing language: mute/camera controls, mid-call device switching, self-preview, fullscreen mode, and the fix for the transparent-area rendering artifact during calls. Do not mention internal class/method names.

- [ ] **Step 3: Sync translations**

Run:

```bash
cmake --build build-release --target update_translations
```

Open `translation/app_ru.ts` and `translation/app_en.ts`, find every entry this plan's `tr()` calls introduced (`"Connecting..."`/`"Reconnecting..."`/`"Call ended."` already existed and are unchanged; this plan adds no *new* `tr()`-wrapped string — Tasks 5–9 only restyle/reposition existing labels and add non-`tr()` object names/icons). If `update_translations` reports any `type="unfinished"` entry regardless, translate it into Russian in `app_ru.ts` and set the matching `app_en.ts` entry's translation equal to its own source text, matching the established convention from prior fixwave translation work.

- [ ] **Step 4: Rebuild and run the full test suite**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`
Expected: PASS (every test target, including all ones added in Tasks 1–9).

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/app/application.cpp CHANGELOG.md translation/app_ru.ts translation/app_en.ts
git commit -m "Bump version and changelog for call UI polish"
```
