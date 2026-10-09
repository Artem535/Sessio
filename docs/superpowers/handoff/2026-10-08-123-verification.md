# Issue 123: microphone capture verification

Date: 2026-10-08. Branch: `codex/123-microphone-switch`.
Base: `77bbf5e` (`feat/118-call-transcription`). Version: 0.2.17.
Implementation commit: `0549049`; diagnosis commit: `0a3c9f0`.
Host-direct Fedora 44, Qt 6.11.2, LiveKit C++ SDK 1.12.0.

## Delivered behavior

- Capture invalidates the old generation and disconnects callbacks before backend
  stop. Reentrant stop/destruction cancels an already-read batch. The injectable
  backend exercises the real readyRead connection and real LiveKit AudioSource.
- Microphone requests coalesce on the Qt owner thread. One failed new selection
  attempts the previously successful device once. Total failure disables local
  microphone; successful recovery reports the actual selected device to the UI.
  Published AudioSource, participant identity, mute and transcript remain intact.
- A provider QPointer guards the queued transition across synchronous source
  signals; leave/destruction cancels completion, and teardown clears pending work.
- The session blocks interrupted local PCM under the same lock used to enqueue
  resumed reset/PCM. Engine reset flushes the old phrase and recreates only that
  track's segmenter/resampler, in its PCM queue. Remote tracks continue.
- Phrase time starts at successful call join, including late transcription start
  and capture resume. The gap notice is separate from writer/model errors.
  Late resume after revoke cannot restore the sink or change the closed session.

## Evidence and results

The independent baseline reentrant lifetime defect remains reproducible:
`build-lifecycle/baseline/repro`, compiled from `77bbf5e`, again reports ASan
`heap-use-after-free` during first-frame destruction. The previous failing
old-batch assertion and reset phrase regression are documented in
`2026-10-08-123-crash-diagnosis.md`. This evidence does **not** identify the cause
of the user's original physical A-to-B microphone crash.

- Full Debug `Sessio` and focused test targets built successfully in `build-host`.
- Focused CTest: **113/113 passed**, 41.63 seconds. Includes lifecycle, synthetic
  runtime, provider coalescing/leave/destruction, session/SerialExecutor,
  controller/panel, engine, resampler, segmenter and model locator tests.
- AudioTap/AudioSinkSlot/downmix: **14/14 passed**. AudioChunker and existing
  physical-input start/destroy smoke: **7/7 passed** (20 adapter cycles).
- Clang ASan/UBSan capture harness: **6/6 passed**, no memory/UB diagnostics.
  Leak detection is disabled; external Qt/SDK binaries are not instrumented.
- Synthetic source/engine runtime: 25 capture restarts with active fake ASR and
  synthetic remote PCM, source preserved, 225 frames / 42 phrases, exit 0.
  The CTest repeat recorded 229 frames / 42 phrases, exit 0. Counts vary with timer
  scheduling; the gate is nonzero delivery, preserved source and watchdog success.
- Default physical input runtime: 25 restarts, source preserved, 198 frames /
  33 phrases, exit 0. Uses fake ASR and synthetic remote PCM; raw hardware PCM is
  neither persisted nor logged. This verifies one input's restart lifecycle,
  rather than two different physical inputs or an actual network call.
- Isolated offscreen app startup stayed alive until the deliberate 4-second
  timeout (exit 124), with no crash. This is a startup check, not active-call QA.
- `cmake --build build-release --target update_translations` scanned 686 sources.
  Both TS files have the new notice translated and zero unfinished messages.
  `git diff --check` passed.

Initial standalone provider/adapter smoke links lacked `CURL_OPENSSL_4`, because
they compile source directly rather than using Sessio_video's existing explicit
curl dependency. Their CMake targets now link the configured compatible library.
No SDK/ABI suppression or unrelated refactor was introduced.

## Reproduction commands

Run from this worktree. The existing cache uses host Qt and read-only dependencies:

```bash
rtk cmake --build build-host --target Sessio Sessio_transcription_session_tests \
  Sessio_call_transcription_controller_tests Sessio_transcript_panel_tests \
  Sessio_livekit_video_provider_smoke_test Sessio_transcription_tests \
  Sessio_audio_capture_lifecycle_tests Sessio_microphone_switch_runtime_smoke --parallel 4
rtk proxy env QT_QPA_PLATFORM=offscreen \
  LD_LIBRARY_PATH=/home/a.durynin/.local/share/sessio-dev/curl \
  ctest --test-dir build-host --output-on-failure \
  -R '^(AudioCaptureLifecycleTests|MicrophoneSwitchRuntimeSmoke|LiveKitVideoProviderSmokeTest|(SessionTest|SerialExecutorTest|TranscriptionEngineTest|ResamplerTest|PhraseSegmenterTest|ModelLocatorTest|ControllerTest|TranscriptPanelTest)\.)'
rtk cmake --build build-host --target Sessio_audio_tap_tests \
  Sessio_audio_chunker_tests Sessio_audio_capture_adapter_smoke_test --parallel 4
rtk proxy env LD_LIBRARY_PATH=/home/a.durynin/.local/share/sessio-dev/curl \
  ctest --test-dir build-host --output-on-failure \
  -R '^(AudioCaptureAdapterSmokeTest|(AudioTapTest|AudioSinkSlotTest|DownmixTest|AudioChunkerTest)\.)'
rtk proxy scripts/test-microphone-lifecycle-sanitized.sh build-host \
  /home/a.durynin/Projects/C++/PsyClientManager/.claude/worktrees/videoprovider-domain-layer/build/vcpkg_installed/x64-linux \
  /home/a.durynin/.local/share/sessio-dev/curl/libcurl.so.4
rtk proxy env LD_LIBRARY_PATH=/home/a.durynin/.local/share/sessio-dev/curl \
  build-host/test/Sessio_microphone_switch_runtime_smoke --hardware
```

Cache essentials: toolchain `/home/a.durynin/vcpkg/scripts/buildsystems/vcpkg.cmake`,
`VCPKG_MANIFEST_INSTALL=OFF`, `Qt6_DIR=/usr/lib64/cmake/Qt6`,
`LIVEKIT_CURL_LIBRARY=/home/a.durynin/.local/share/sessio-dev/curl/libcurl.so.4`.
`build-release` was configured to run the required translation scan; no full
Release build or package validation is claimed.

## Remaining release gates

- Original-OS active-call A/B crash reproduction with two physical inputs and
  sanitized stacks, both without and with ASR. Original cause is unproven.
- At least 30 minutes with a remote participant and two inputs, ten A/B switches
  including rapid A/B/A, unavailable device, mute, stop/revoke/leave transitions.
- Promised Windows/Linux package/device QA, production recognition/model QA and
  full-suite/CI. Synthetic/fake-ASR runs do not establish these.

The pre-existing dirty qlementine submodule was preserved and is excluded from
issue commits. The host full build used that existing dependency state; it is
not evidence of a pristine package build. Nothing was pushed, merged or deployed;
issue 123 remains open pending review and real-device release gates.

## Review fix wave 1

Base for this wave: `d1d3835`. Both P2 findings in the scoped review are addressed.

- `VideoProvider::selectedMicrophone()` exposes the confirmed capture device
  during a call (null when no input is active), and the selected device before
  joining. The fake provider retains and signals the confirmed outcome. The
  microphone popover initializes from this state under a signal blocker, including
  a null/absent device. Its selection callback re-queries DeviceManager's current
  inputs; camera and speaker behavior is unchanged.
- `switchMicrophone` rechecks QPointer, call generation and room after complete
  failure mutes the microphone. A synchronous model/error notification cannot
  produce a stale completion after leave or provider destruction.

RED evidence before the fixes: the new Qt selector test reported all three
incorrect reopened states (A instead of B after selection, A instead of B after
simulated recovery, index 0 instead of -1 after complete failure). The new provider
failure/leave regression returned exit 1 because `microphoneChanged` was emitted
after the direct mute-state listener left the call.

GREEN evidence after the fixes:

- `Sessio_call_page_tests` and `Sessio_livekit_video_provider_smoke_test` rebuilt.
- Four focused CallPage tests passed: accessible device button, three popover
  combos, device selection dispatch, and reopened selection/recovery/no-input.
  The synthetic A/B test executed on host Qt 6.11.2 and was **not skipped**.
- `--microphone-failure-only` passed both complete-failure variants: direct
  participant mute notification leaves the call or destroys the provider. Each
  has exactly three opening attempts, one mute notification and zero completions.
- The same failure/leave/destruction variants passed with all provider smoke
  translation units instrumented by Clang ASan/UBSan. No memory/UB diagnostics;
  leak detection disabled, external Qt and SDK binaries remain uninstrumented.
- `git diff --check` passed. No new `tr()` strings; translation files unchanged.

Focused commands:

```bash
rtk cmake --build build-host --target Sessio_call_page_tests \
  Sessio_livekit_video_provider_smoke_test --parallel 4
rtk proxy env QT_QPA_PLATFORM=offscreen \
  LD_LIBRARY_PATH=/home/a.durynin/.local/share/sessio-dev/curl \
  build-host/test/Sessio_call_page_tests \
  --gtest_filter=CallPageTest.ReopenedMicrophoneSelectorShowsConfirmedSelectionRecoveryAndNoInput:CallPageTest.SelectingADeviceCallsSwitchOnTheAttachedProvider:CallPageTest.DevicesButtonHasAccessibleLabel:CallPageTest.DevicesButtonOpensPopoverWithThreeCombos
rtk proxy env LD_LIBRARY_PATH=/home/a.durynin/.local/share/sessio-dev/curl \
  build-host/test/Sessio_livekit_video_provider_smoke_test --microphone-failure-only
rtk proxy python3 scripts/test-microphone-provider-sanitized.py build-host \
  /home/a.durynin/.local/share/sessio-dev/curl/libcurl.so.4
```

The optional Qt6MultimediaPrivate >= 6.11 package is used only by the selector
test to construct synthetic device descriptors. Older/minimal Qt installations
explicitly skip that test; production has no private Qt dependency. DeviceManager
input enumeration is virtual to permit those descriptors without hardware.
The sanitizer runner uses the configured Ninja build's compile database and MOC
output (`CMAKE_EXPORT_COMPILE_COMMANDS=ON`); it drops GCC module-scanner flags
because these test sources contain ordinary C++ translation units.

This wave reran the focused cases only. Earlier full Debug/134 CTest results are
the previous implementation evidence, rather than a claim of a repeated full
suite on this wave. Original two-input crash, real 30-minute call, package/Windows
and CI gates remain unchanged. Dirty qlementine is preserved and excluded; no
push, merge or deployment.
