# Call layout prototype — issue #95

Throwaway HTML prototype on `codex/95-call-layout-prototype`, outside the CMake application targets. It does not use cameras, microphones, LiveKit, storage, or real participant data.

Open `prototype-layout.html` directly in a browser. Optional local server:

```sh
rtk proxy python3 -m http.server 8895 --bind 127.0.0.1 --directory src/pages/calls_page
```

Then visit `http://127.0.0.1:8895/prototype-layout.html?variant=B&count=3&panel=1`.

- A: remote participant grid with local picture in picture.
- B (proposal): picture in picture for 1:1; equal tiles including local video for three or more participants.
- C: selected participant and thumbnail strip. Click a thumbnail to change focus.

Switch variants using the bottom arrows or keyboard left/right arrows. Try 2, 3, 6, and 10 participants, join/leave, notes visibility, microphone/camera toggles, and the end-call state. Notes remain in memory only.

Verification: inline JavaScript passes `node --check`; `git diff --check` passes. Browser inspection was blocked by the desktop browser permission policy, so screenshots and visual/runtime approval are pending. This artifact is a design proposal, not the production implementation of #95.
