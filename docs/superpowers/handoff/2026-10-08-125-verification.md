# Browser guest call #125 verification

Base: `feat/118-call-transcription` at `77bbf5e`. Isolated branch
`codex/125-browser-guest`; implementation commits `c069c5c`, `87e5ed9` plus
the release/verification follow-up. No push, merge or installation performed.

## Cumulative integration and review boundaries

Original #125 owned range: `77bbf5e..914e2c6` (`c069c5c`, `87e5ed9`,
`914e2c6`). Already reviewed #123/#124 dependency source HEAD:
`78127b947c833b466a1977a0345c193271454202`. Cherry-picked in its existing
ancestry order, once each:

| Source | Integrated |
| --- | --- |
| 1e3047c (#124) | defd643 |
| a4a5f13 (#123 documentation) | 9aa2b0a |
| 5bbc606 (#123) | 40f061e |
| b145752 (#123 verification) | 51a3234 |
| 92b8fe4 (#123 follow-up) | d7e62a5 |
| 5acf643 (#124 follow-up) | ce8cce3 |
| d54aaca (#124 verification) | 5f700ab |
| 78127b9 (#124 atomic detach) | 74e1187 |

Release-only conflicts resolved to 0.2.19 in CMake/application; changelog retains
0.2.17 microphone lifecycle and 0.2.18 standalone transcripts. #125 follow-up
owned range begins at `74e1187..0022c71`; review this together with the original
#125 range, without attributing reviewed dependency implementation to #125.

## Fresh follow-up evidence (2026-10-08)

### Bounded review fix wave 1 (base 326f5e9)

Independent review found P1: replacing a preview device reset explicit media-off
choices and enabled both replacement tracks. Fixed by retaining checkbox intent
independently of stream acquisition; the first check retains its default enable
behavior only while no explicit choice exists. Intent is applied before stream
attachment and refreshed after asynchronous enumeration. Generation guards still
stop obsolete streams.

Permanent regressions cover both microphone/camera selectors, old-track stop,
replacement track enabled values, checkbox states, submitted Join settings,
explicit off during pending first permission request and stale preview completion.
Device replacement and pending first-preview regressions were observed RED before
the fix; focused suite now **15/15 GREEN**. No broad/native/production QA rerun
for this bounded frontend correction; prior native blockers/manual gates stand.
The formal report was read; the owned correction awaits scoped independent
rereview before #126.

- Backend full build and CTest: **178/178**, 52.64 seconds.
- Native URL/series target builds and focused CTest: **22/22**, 4.25 seconds.
- `update_translations` completed; both catalogs contain no unfinished entries;
  compiled catalogs report 713 finished, zero unfinished each.
- Vitest **11/11**: connection failure/retry, full listener cleanup, disconnect
  during media activation, deadline and late completion, signaling reconnect,
  permission rejection preserving connected room, late preview enumeration cleanup.
  Listener leak and disconnect-during-media tests failed before their fix.
- Web build passed; Playwright mocked prejoin **3/3**; runtime npm audit zero
  vulnerabilities. The ~793 kB bundle warning remains.
- Actual ephemeral room integration passed using Chromium **156.0.8078.4**,
  LiveKit **1.13.7** pinned by image digest, real test C++ backend with new SQLite
  database/account, locally generated certificate and HTTPS/WSS proxy.
  Two independent guests with duplicate names published synthetic camera/mic;
  remote video rendered and audio tracks subscribed; actual signaling socket
  termination showed reconnect and resumed room without another token exchange;
  leave propagated to the peer and local camera tracks reached `ended`.
- Extended integration included built **C++ LiveKit native synthetic publisher**
  redeeming the legacy native token endpoint. Browser guests rendered its video
  in the same room. Native leg uses loopback WS; certificate trust is bypassed
  only for test browsers. This is synthetic SDK interoperability, not specialist
  GUI, audible playback quality, real devices, external TLS/TURN or production QA.
- Actual signaling-drop test initially timed out because only Reconnecting was
  handled; added SDK SignalReconnecting and reran successfully.
- Native synthetic target initially failed CURL_OPENSSL_4 linking; fixed its
  explicit curl linkage using the same configured compatibility library as the
  existing smoke targets, then built and exercised it successfully.
- Full cumulative native build reached application linking but failed on
  pre-existing standalone `Sessio_frame_convert_tests` and
  `Sessio_video_capture_adapter_smoke_test` curl linkage (`CURL_OPENSSL_4`).
  Those targets bypass Sessio_video's configured curl dependency. No full native
  suite pass is claimed; the #125 focused targets and actual native publisher
  evidence stand independently. Wider unrelated linker cleanup is deferred.
- Harness removes its named container, database, certificate and child processes
  in finally; no generated invitations, JWTs, account credentials or PII are
  printed or persisted in tracked artifacts.

Reproduce after backend/native build and `npm ci` (from `web/call-client`):

```bash
rtk proxy npm run build
rtk proxy npm test
rtk proxy npm run test:e2e
rtk proxy env SESSIO_TEST_NATIVE=../../build-release/test/Sessio_fake_client \
  LD_LIBRARY_PATH=/home/a.durynin/.local/share/sessio-dev/curl npm run test:integration
```

Runner prerequisites: Podman, OpenSSL, available loopback ports 19080/19081/
19082/19443 and the pinned image. `SESSIO_TEST_BACKEND` overrides backend binary;
omit `SESSIO_TEST_NATIVE` for the two-browser run. Nothing contacts production.
The default browser CI retains mocked checks; running this integration in CI
requires provisioning the backend binary and Podman. Ruling: ephemeral local
keys replace external CI room credentials for reproducible integration; external
network acceptance remains a gate.

## Implemented

- Same-origin browser token exchange with explicit `clientKind=web`, required
  trimmed 1–80 Unicode scalar name, safe errors and WSS validation.
- Native requests without clientKind and empty name remain compatible. Shared
  backend name validation covers specialist, single and recurring routes.
- HTTPS fragment templates percent-encode values. Browser clears fragment at
  startup and retains invitation/JWT only in memory. No external analytics.
- Device preview/selection, microphone meter, permission-denial fallback,
  independent Room per guest, participant tracks/names, mute/camera/device controls,
  reconnect notice, audio activation and leave cleanup.
- Trusted native HTTPS parser defaults to `calls.sessio-pcm.ru`, overridden by
  explicit `SESSIO_CALL_HOST`; Open in app maps backend to deployment origin.
- Recurring token exchange now assigns a unique identity to each guest; automatic
  SDK reconnect continues with the current Room identity.
- Nginx staging template and deployment instructions mask invitation access paths,
  omit queries/body logs, restrict CSP and preserve no-store token responses.

## Evidence

Linux host, GCC 16.2.1, Qt installed under `/usr/lib64/cmake/Qt6`.
Node/npm and resolved SDK versions are recorded by package-lock.json; Node 22+
is required. Browser: Playwright Chromium 156.0.8078.4, desktop 1280×720.
Browser skill/plugin absent; regular Playwright used as specified by frontend QA
skill. Screenshot `/tmp/sessio-125-prejoin.png` inspected; no framework overlay
or page errors, form controls work.

- Standalone GTest contract RED→GREEN: 6/6; native URL RED→GREEN: 10/10.
- Backend configured with vcpkg toolchain and read-only installed dependency
  prefixes; full build succeeded. Full CTest: 177/177, 86.77 seconds. Shared JSON
  vector test was added afterwards and verified in focused suite.
- Vitest: 5/5 across parser/name/API suites, including shared backend vectors.
- Playwright: 3/3 (required name/fragment clearing, invalid link, permission denial
  with one safe token request). These are mocked media/API checks.
- `npm run build` succeeds (static dist); bundle-size warning (~793 kB) remains.
- `npm audit --omit=dev`: zero vulnerabilities. Vitest upgraded to 5.0.3 after
  audit identified vulnerabilities in older test-only dependency chain.
- Native transcription-enabled configure succeeds. Fresh cumulative focused
  native evidence is recorded above; standalone evidence is kept as history.
- `git diff --check` passes. No new/changed native tr() strings; no unfinished
  entries found in either TS catalog. No native translation regeneration required.

## Rulings and remaining gates

Existing recurring test expected one stable client identity per meeting; this
conflicts with approved simultaneous guest access. Updated it to distinct subjects
in the same room; compatibility cost: independent new joins no longer replace a
previous client participant. Automatic reconnect uses the existing token.

No staging host, live room admin credentials or external test devices were
provided to this worker. HTTPS trust, WSS/TURN reachability, bidirectional real
audio/video among specialist app + browser guest + app guest, Windows browser
matrix, reconnect media continuity and production installation are unverified.
The deployment files are staging artifacts, not evidence of a deployment.
Local connected-room lifecycle and synthetic native/browser integration are now
verified above. Specialist GUI + browser + native guest on real devices, external
TLS/WSS/TURN, Windows browser matrix and production installation remain manual
gates. Author follow-up awaits root's independent review before #126.
