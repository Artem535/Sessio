# Screen sharing #126 verification

**Status: capability gate BLOCKED; feature incomplete.** See
[exact runtime counterexample](2026-10-08-126-livekit-capabilities.md).
The approved server-authoritative single-owner design needs an authorization
decision before backend routes or native/web screen publishing can be completed.

## Diagnostic draft PR boundaries

The diagnostic draft branch `codex/126-screen-capability-mr` starts from reviewed
#123/#124/#125 source HEAD `08eecac81261ff1dc69418f4d72689c7c01f44a8` and is intended
to target the published #125 stack branch `codex/125-browser-guest-mr`.
Only capability evidence commit `0b0acd36b5a535c4e8f16006b1f55a3c8299d906` was
cherry-picked from the blocked implementation worktree, followed by release and
report updates. Its #126 diff contains the opt-in runner, these two reports,
version **0.2.20** in CMake/application and the matching changelog entry.

The separate local implementation worktree
`/home/a.durynin/.codex/worktrees/126-screen-sharing/PsyClientManager` retains
receive-model commit `e976bf2` and groundwork checkpoint `599f727` (call-session
hash store, SQLite lease repository/migration, fake-client coordinator and tests).
**Neither commit is included in this diagnostic draft PR.** They remain local,
unpublished and pending a design decision; neither is a delivered HTTP/session/
publishing system. Foreign qlementine changes and build files stay in that
original worktree and are not part of the diagnostic draft.

Publishing the diagnostic PR is authorized; feature deployment, merge and issue
closure are not performed by this preparatory worker. The version update meets
the repository's every-MR requirement and does not mark screen sharing complete.

## Original blocked-worktree checks (historical evidence)

```bash
rtk proxy cmake --build build-token-backend --target token_backend_tests --parallel 4
rtk proxy ctest --test-dir build-token-backend --output-on-failure \
  -R 'CallSession|ScreenShare|LiveKitJwt'
rtk proxy git diff --check
```

- Backend focused build succeeded; **9/9 tests passed**, 0.04 seconds:
  four JWT tests, four lease coordinator tests and one call-session test.
- The lease tests use a fake RoomService client: simultaneous acquire/CAS,
  no DB lock across blocked remote API, delayed grant/expiry, retaining Revoking,
  stale release, API failure and disabled rollout. Their success does not
  establish real-server JWT revocation and does not override the runtime block.
- Actual opt-in permission harness on pinned LiveKit 1.13.7, JS SDK 2.22.3,
  Chromium 156.0.8078.4: selective grant/revoke preserves camera/microphone SIDs;
  **security assertion fails** because old server-refreshed grant JWT reopens
  screen publishing. Final local HTTPS/WSS run confirms the new SCREEN_SHARE
  publication through real `GetParticipant`.
- After failure, `podman ps --filter name=sessio-screen-gate` returned no test
  containers. Browser and ephemeral certificate directory are cleaned in finally.
- `git diff --check` passed. No new native `tr()` strings in this continuation;
  no translation regeneration or native build was performed here.

## Diagnostic draft branch checks

```bash
rtk proxy node --check web/call-client/test/screen-permission-runtime.mjs
rtk proxy git diff --check
```

Both checks pass. No backend/native build, broad test rerun or new runtime run
was performed for this evidence-only cherry-pick. The historical 9/9 focused
result above belongs to the unpublished groundwork worktree, whose tests are
not included in this PR. No new or changed native `tr()` strings; translation
regeneration is not required for the version-only application change.

## Incomplete and unverified work

Backend HTTPS screen/session routes, actual RoomService HTTP implementation,
webhook verification and production sweep/startup reconciliation are incomplete.
Native Qt screen/window capture, async session client, web capture controller,
lease-driven UI and independent screen publication are not implemented by this
continuation. The capability test is a browser integration harness rather than
the plan's proposed C++ standalone executable; it demonstrates the blocking
server/JWT primitive without introducing unsafe feature routes.

Native receive/CallPage regressions stay in the unpublished worktree and are
excluded from this PR; no fresh native suite pass is claimed. The earlier #125
full native suite had unrelated standalone
curl-link blockers; its focused/synthetic evidence belongs to its own report.

Windows, Wayland portal, X11 packaged capture, actual hardware, external TLS/WSS/
TURN, staging, three-device publication, simultaneous room-owner UI, 30-minute
ASR, queue latency/RSS and production rollout remain unverified. Broader testing
cannot satisfy the failed authorization gate. Rollout must remain off until an
approved replacement closes the cached grant-token bypass and passes a new gate.

Ruling: stop dependent publishing and retain reviewable evidence after the D1
counterexample, as required by the approved plan. No ownership/privacy invariant
was weakened to continue implementation.
