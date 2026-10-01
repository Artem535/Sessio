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
  contain 496 finished entries and no unfinished entries.
- `git diff --check` passed. Independent whole-branch review and draft PR
  delivery are handled separately by the controller; PR94 remains a dependency.

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
