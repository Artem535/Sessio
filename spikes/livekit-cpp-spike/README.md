# LiveKit C++/Qt Capture-and-Render Spike

Throwaway exploratory code for issue #77. Not part of the main
PsyClientManager build — never wired into the top-level CMakeLists.txt.

Design: `docs/superpowers/specs/2026-09-25-livekit-cpp-spike-design.md`
Plan: `docs/superpowers/plans/2026-09-25-livekit-cpp-spike.md`

## Build

    cmake -S spikes/livekit-cpp-spike -B spike-build
    cmake --build spike-build --parallel

## Run

    LIVEKIT_URL=wss://your-project.livekit.cloud \
    LIVEKIT_TOKEN=<a manually generated room JWT> \
    ./spike-build/livekit_spike

Without `LIVEKIT_URL`/`LIVEKIT_TOKEN` set, the app still runs — it just
skips connecting to a room.

## Manual verification checklist (roadmap §5.3's six proof points)

CI (`.github/workflows/cmake-multi-platform.yml`, `Build LiveKit spike`
step) proves point 1 only — that the SDK builds and links on all three
OSes. It cannot exercise a camera, a microphone, or a second machine.
Everything below needs a human on real hardware:

Run the spike from a terminal (not by double-clicking the binary) and keep
an eye on it throughout: every failure path in this spike reports to
**stderr only** — there are no GUI error dialogs. A silent failure (bad
device, dropped connection, FFI error) looks identical to "nothing has
happened yet" unless you're watching the terminal.

1. Confirm CI's `Build LiveKit spike` step is green on all three OS matrix
   entries. A red step here means the SDK didn't build on that OS —
   everything below is moot for that OS until it's fixed.
2. Launch the spike; confirm camera + microphone capture starts with no
   crash, and no leaked device handle after repeated start/stop (watch
   for the OS's camera-in-use indicator staying lit after the app exits).
   The status label's counters should climb at roughly the camera's fps
   for video (e.g. ~30/s) and roughly 100/s for audio (10ms chunks at
   48kHz) — updated about once a second. If one counter is stuck at 0
   while the other climbs, that stream isn't actually capturing; check
   stderr.
3. Confirm local preview and remote video render correctly, including
   during window resize.
4. Mid-session, switch camera and microphone from the combo boxes;
   confirm the stream keeps working. The speaker combo box re-attaches
   the currently-subscribed remote audio track to the newly selected
   output device live, so switching it mid-session should also keep
   audio playing (through the new device) without needing to leave and
   rejoin.
5. Click Leave, then close the window; confirm tracks stop, no dangling
   callback fires afterward, and the process exits cleanly.
6. Run a real call between two machines over the staging LiveKit Cloud
   project — once on a network with direct connectivity, once forcing
   TURN/TLS.

## Known gap: Qt module availability in CI

The CI steps above reuse the job's existing Qt installation rather than
adding a new install step, to avoid risking the main app's Windows/macOS
Qt setup with an unverified module list. `CMakeLists.txt` requires three
Qt Multimedia-related components: `Multimedia`, `MultimediaWidgets`
(needed for `QVideoWidget`), and `OpenGLWidgets`. If `Configure LiveKit
spike` fails with a `Could not find a package configuration file provided
by "Qt6Multimedia"` (or `Qt6MultimediaWidgets`, or `Qt6OpenGLWidgets`)
error on some OS, that Qt installation is missing one of those modules —
extend that OS's existing Qt-install step in `cmake-multi-platform.yml`
(the `jurplel/install-qt-action` step for Windows/macOS takes a
`modules:` input; the Linux official-installer step needs the matching
`.addons.qtmultimedia` component id for Qt 6.10.2) and re-run CI to
confirm.

## Decision gate

Per `docs/video-roadmap.en.md` §5.3: P1-B (#78, #79, #80) proceeds only if
all six points above are reproducible and SDK artifacts can be packaged
for Linux/Windows/macOS. A failed point is an architecture decision to
bring back to the team — not something to quietly route around.
