# Issue 95 verification — 2026-10-01

The desktop and token backend implement multi-participant calls. These results
were obtained in the dependent `codex/95-multi-participant-calls` worktree.
No production deployment or real participant data was used.

## Automated gates

- Desktop call/model/media/session and client-mode suites: **100/100 passed**,
  15.21 seconds. Includes four structural database-dependency checks and the
  new populated client-window test proving practitioner display roles cannot
  enable notes or the practitioner meetings list.
- Token backend: **116/116 passed**, 33.17 seconds, including sequential and
  concurrent unique signed identities for one meeting and existing HTTP/auth
  tests.
- Full desktop `Sessio` target and backend targets built. Desktop version is
  **0.2.5** in both declarations. Translation update is idempotent; both locales
  contained 493 finished entries after the translation follow-up; the later
  scene/name follow-up below raises this to 498, with no unfinished entries.
- `git diff --check` passed. Independent whole-branch review and draft PR
  delivery are handled separately by the controller; PR94 remains a dependency.

## Translation follow-up

The first CI run removed three test-only `QObject` entries: Unmute microphone,
Turn on camera and Exit fullscreen. Local translation generation included test
targets (`PCM_BUILD_TESTS=ON`), while CI used the default OFF setting. The
application's `CallPage` translations already existed; the extra entries came
from the tests using `QObject::tr()` for expected CallPage labels.

The tests now use `CallPage::tr()` and at that revision both catalogs contained the same 493
finished entries with tests ON or OFF. The earlier 496-entry local result
included those three duplicates. The OFF configuration reproduced exactly the
CI deletion before the fix; subsequent ON/OFF updates preserve identical file
hashes. The three existing button-label tests cover the corrected context.

## Scene and display-name follow-up

The user approved preserving the whole camera frame while enlarging the 1:1
stage. Its main tile now fills the stage, with self-preview and controls as
overlays. The renderer retains aspect-fit scaling. Group layout retains equal
tiles and clear control space. Labels are compact plain-text badges; controls
have larger hit areas and a red leave button. Hardware OpenGL checks passed
for 2, 3 and 10 participants; the 1:1 main tile is exactly 1280x800 and the
self-preview is 256x144 in that probe. These are framebuffer/geometry results,
not compositor screenshots or human visual acceptance.

Both settings dialogs save `calls/displayName` using the existing QSettings
store. The join form reloads the saved default and accepts a per-call override,
which does not overwrite that default. The field permits 64 UTF-16 units;
the backend accepts at most 256 UTF-8 bytes and rejects ASCII control characters.
Empty names retain the existing Participant fallback. The optional HTTP
`displayName` field is accepted by both token endpoints; old body-less
specialist requests and passcode-only client requests remain supported.
The JWT `name` claim is signed, with unique `sub` identities and grants retained.
Names are presentation data, not authenticated real-world identities.

The subscribed audio track remains associated with participant identity.
Synthetic room verification passed late-join name delivery, a name update
received by two production providers, and retention of the same audio track
after that update, followed by tone, output-switch and lifecycle checks.
Only the synthetic tokens permit in-call metadata updates for this probe;
production permissions are unchanged. The app edits names before token issuance.
Future recording/transcription must retain participant identity and publication
SID with a display-name snapshot; file recording/transcription is not added here.

Backend tests passed 119/119; scoped desktop scene/name/token tests and settings
round-trip checks passed. Both translation catalogs have 498 finished entries.
The release application and backend built. The deployed token backend has not
been updated: new clients talking to an older backend can join, but that backend
ignores the new name field. Name transmission requires deploying this backend
version as well as distributing the desktop build.

## Remote audio crash follow-up

A native user run crashed when another participant joined. The private core
showed the GUI-thread queued remote PCM delivery calling `QIODevice::write()`
through an already freed device (its vtable storage had been reused). The sink
was retained, but its backend-owned push device was stored as a raw pointer.
The core and user runtime data are not included in this repository.

The handle now uses `QPointer<QIODevice>` and delivery requires a writable
device. Backend destruction invalidates the handle before subsequent queued
PCM is processed. The deterministic lifecycle regression first proves live
output receives PCM, then destroys that output while PCM is queued; it failed
on the original raw pointer and passes with the fix. The existing attachment
generation regression remains covered.

The explicit hardware probe also started the production player on the default
output, processed silence for 500 ms, stopped its real sink with PCM queued,
and exited successfully. Run the lifecycle executable with `--output-smoke`
using the dependency environment below. This probe is deliberately separate
from device-independent CTest. The specific backend event that released the
device in the user call is not established; a repeat of that two-person call
is still required to verify the reported scenario end to end.

## Real synthetic room

`test/group_call_runtime_smoke.cpp` is an explicit manual executable target,
`Sessio_group_call_runtime_smoke`; it is not a default CTest entry. `--room`
contacts only `ws://127.0.0.1:17980`, using the disposable LiveKit development
credentials. Do not use this server configuration for production.

One real `LiveKitVideoProvider` subscribed to two independently connected SDK
publishers. The production provider received distinct red/blue video frames,
and probes of the actual subscribed remote tracks decoded distinct 440/660 Hz
tones. Both production QAudioSink counters advanced, then resumed after each
of the two available speaker reattachments. Remote mute/unmute, same-identity
leave/rejoin, survivor source retention, departed source destruction and final
provider teardown passed. See [room-smoke.log](room-smoke.log).

Publisher worker exceptions are retained and checked on the owner thread
before and after every media gate. A stopped producer can no longer let cached
frames or previously buffered PCM satisfy subsequent gates. The explicit
`--room-inject-capture-failure` regression injects an audio capture failure
after real patterns/tones arrive; it must exit **1** with the publisher identity
and original failure. The verified run did so and never printed the final
lifecycle success message; see [capture-failure.log](capture-failure.log).
The amended healthy `--room` run then exited **0** with all media/lifecycle
assertions passing. No desktop/backend suites were repeated for this harness
error-propagation correction.

The prepared rootless Podman server was reachable over WebSocket but the host
client failed ICE with `wait_pc_connection timed out`. Running the executable
in the **existing server network namespace** resolved the local transport issue;
no host bindings or global network configuration were changed:

```sh
rtk proxy podman inspect sessio-95-livekit --format '{{.State.Pid}}'
# This run returned PID 382499; use the current PID when recreating the server.
rtk proxy podman unshare nsenter -t 382499 -n -- env \
  LD_LIBRARY_PATH=/home/a.durynin/Projects/C++/PsyClientManager/.claude/worktrees/videoprovider-domain-layer/build-release/_deps/livekit-sdk/livekit-sdk-linux-x64-1.12.0/lib:/usr/lib64 \
  QT_PLUGIN_PATH=/usr/lib64/qt6/plugins QT_QPA_PLATFORM=offscreen \
  timeout 90 build-release/test/Sessio_group_call_runtime_smoke --room
```

The runtime log retains SDK warnings about unknown FFI handles/publications and
an offer-negotiation error during disconnect. The observable media/lifecycle
assertions passed and the process exited 0; the cause of those SDK diagnostics
has not been established. This is not proof of every SDK teardown path.

**Manual gate remains:** sound heard by a person, audible simultaneous mixing,
and physical camera/microphone/device quality. Automated sink counters and tone
decode do not establish those results. The sink telemetry helper mutes sinks
once observed, so this executable is not an audible acceptance test. Local
camera and microphone transmission are disabled before provider join; artifacts
contain only synthetic patterns and labels.

## Hardware OpenGL and capture limitation

The populated production CallPage used a fake participant model with synthetic
frames in a native Wayland session. All 2, 3 and 10 renderers had valid hardware
contexts and independently captured usable red/blue framebuffers on AMD Radeon
780M (`radeonsi`, `phoenix`, `ACO`). The test verifies complete stage bounds,
control avoidance, group tile non-overlap, client notes-toggle absence and
renderer geometry matching every tile. It also waits for a native frame-swap
signal. See [native-gl.log](native-gl.log).

```sh
rtk proxy env \
  LD_LIBRARY_PATH=/home/a.durynin/Projects/C++/PsyClientManager/.claude/worktrees/videoprovider-domain-layer/build-release/_deps/livekit-sdk/livekit-sdk-linux-x64-1.12.0/lib:/usr/lib64 \
  QT_PLUGIN_PATH=/usr/lib64/qt6/plugins QT_QPA_PLATFORM=wayland \
  timeout 45 build-release/test/Sessio_group_call_runtime_smoke \
  --native-ui docs/verification/95
```

**The images below are Qt widget/framebuffer reconstructions, not screenshots
of the desktop compositor.** Each renderer framebuffer and participant-name
QLabel grab is placed at its actual native widget geometry; the control-bar
grab is placed at its measured geometry. They show verified component pixels
and layout, but do not prove the final visible desktop composition.

- [Two participants: main remote and local PiP](participants-2-reconstruction.png)
- [Three participants: equal tiles, local centered last](participants-3-reconstruction.png)
- [Ten participants: equal tiles, centered last row](participants-10-reconstruction.png)

`QWidget::grab()` produced an incorrectly composed PiP despite its correct
160x90 widget/renderer bounds and framebuffer. A native compositor capture
attempt encountered the locked desktop session, so visible composition could
not be verified; the lock screen capture was discarded. No unlock or capture
restriction bypass was attempted. A production geometry defect was not
confirmed. PiP stage geometry was x1068/y582/w160/h90 within a 1240x760 stage;
the reconstruction's exact red bounds were x1088/y602/w160/h60 (its lower
30 pixels contain the name bar). **Visible compositor verification remains a
manual gate in an unlocked session.**

## Rebuild commands

Use the configured dependency runtime and Qt plugins above for desktop builds:

```sh
rtk proxy env LD_LIBRARY_PATH=/home/a.durynin/Projects/C++/PsyClientManager/.claude/worktrees/videoprovider-domain-layer/build-release/_deps/livekit-sdk/livekit-sdk-linux-x64-1.12.0/lib:/usr/lib64 QT_PLUGIN_PATH=/usr/lib64/qt6/plugins QT_QPA_PLATFORM=offscreen cmake --build build-release --target Sessio_group_call_runtime_smoke Sessio_client_mode_window_tests Sessio --parallel 4
rtk proxy env LD_LIBRARY_PATH=/home/a.durynin/Projects/C++/PsyClientManager/.claude/worktrees/videoprovider-domain-layer/build-release/_deps/livekit-sdk/livekit-sdk-linux-x64-1.12.0/lib:/usr/lib64 QT_PLUGIN_PATH=/usr/lib64/qt6/plugins QT_QPA_PLATFORM=offscreen ctest --test-dir build-release --output-on-failure -R '^(VideoSessionTest|ParticipantModelTest|VideoFrameSourceTest|CallPageTest|CallLayoutStrategyTest|CallsPageTest|FakeVideoProviderTests|LiveKitVideoProviderSmokeTest|LiveKitVideoFrameSourceSmokeTest|RemoteVideoRendererSmokeTest|RemoteVideoRendererLetterboxTest|RemoteAudioPlayerLifecycleTest|ClientModeWindow|ClientModeSettingsDialog)'
rtk cmake --build token-backend/build --parallel 4
rtk proxy ctest --test-dir token-backend/build --output-on-failure
```

Existing configure warnings about utf8proc and StateMachine, vendored qcustomplot
deprecations, lupdate parser warnings in dependency headers, and the unavailable
VDPAU backend were observed. They did not prevent these scoped builds/checks;
no unrelated dependency cleanup was included.
