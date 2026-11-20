# Jitsi Web Calls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A specialist can schedule a Jitsi meeting, both sides join it from Sessio, and live transcription receives each participant's audio separately, exactly as with LiveKit.

**Architecture:** The token backend gains a per-meeting `provider` (`livekit` | `jitsi`) and signs Jitsi JWTs with a server-held secret; token responses tell the app which provider to build. In the app, `WebCallProvider` implements the existing `pcm::video::VideoProvider`: it hosts our own `lib-jitsi-meet` page in a `QWebEngineView`, receives control events over `QWebChannel`, and receives per-participant PCM over a local WebSocket served on its own thread (`AudioBridgeServer`). `VideoSession`, `CallTranscriptionController`, the prompter and notes are untouched.

**Tech Stack:** C++20, Qt 6.10 (WebEngineCore, WebEngineWidgets, WebChannel, Network), oatpp + SQLite (token backend), plain ES modules + `lib-jitsi-meet` (page), vitest (page logic tests), GoogleTest.

**Spec:** `docs/superpowers/specs/2026-10-27-web-calls-kb-hints-cloud-design.md` (part 1). Transport choice and wire format: `spikes/webengine-audio-bridge/README.md` (spike code is the reference for the bridge, the worklet and the scheme handler).

**Scope and deviations from the spec:** SaluteJazz is a separate plan (needs a licensed SDK key). The spec put the Jitsi server address in app settings; per the 2026-10-28 decision the token backend owns it (`JITSI_URL`, returned as `endpointUrl`), so the app has no Jitsi setting.

## Global Constraints

- Page served from a secure custom scheme (`sessio-call://app/...`); never inject into third-party clients.
- Audio transport: WebSocket on `127.0.0.1`, random port, one-shot token in the URL, `Origin` must equal `sessio-call://app`, server on its own `QThread`, binary frames only. No QtWebSockets (not in the toolchains); the server is ported from the spike.
- Chunk wire format v1 (little endian): `u8 version=1 | u8 idLen | u16 reserved | u32 seq | f64 sentMs | id (UTF-8) | int16 mono PCM`, 16 kHz, 20 ms chunks.
- Jitsi JWT secret lives only on the server (`JITSI_APP_SECRET`); the app never signs tokens.
- Series (recurring) meetings stay LiveKit in this plan; only one-off meetings can be Jitsi.
- Native device check (#80) does not apply to web calls; camera/mic are Chromium's.
- New UI strings translated EN/RU; bump both app versions and CHANGELOG (spec acceptance).
- Build with `-j4`; launch GUI test builds only via `scripts/run-dev-isolated.sh`; never kill a running Sessio without asking.
- Configure builds against the prebuilt vcpkg tree (`-DVCPKG_MANIFEST_INSTALL=OFF -DVCPKG_INSTALLED_DIR=/home/a.durynin/Projects/C++/PsyClientManager/build/vcpkg_installed`).

## Review Focus

1. **Track churn for one participant** (P2P ↔ JVB switch, mute/unmute): seen live in the spike. Expect one `participantJoined`, one continuous audio stream keyed by participant id, no duplicate rows. Pinned in Task 6 (page) and Task 7 (provider integration).
2. **Renderer process crash mid-call** (`QWebEnginePage::renderProcessTerminated`): expect `connectionLost`, no further sink callbacks. Pinned in Task 7.
3. **Camera/microphone denied by the OS**: the call still receives remote audio; expect `mediaError`, not `joinFailed`. Pinned in Task 6/7.
4. **Page reload or WebSocket drop**: the one-shot token is spent, so the page must ask for a fresh one; expect audio to resume. Pinned in Task 5 (rotate) and Task 7.
5. **Sink cleared while chunks are in flight** (consent revoked, call ends): no `onAudio` after `setAudioSink(nullptr)` returns. Pinned in Task 7.

---

### Task 1: Jitsi JWT minting in the token backend

**Files:**
- Create: `token-backend/src/crypto/jwt_hs256.h`, `token-backend/src/crypto/jwt_hs256.cpp`
- Create: `token-backend/src/crypto/jitsi_jwt.h`, `token-backend/src/crypto/jitsi_jwt.cpp`
- Modify: `token-backend/src/crypto/livekit_jwt.cpp` (use the shared signer), `token-backend/CMakeLists.txt`, `token-backend/test/CMakeLists.txt`
- Test: `token-backend/test/jitsi_jwt_tests.cpp`

**Interfaces:**
- Produces: `std::string signHs256Jwt(const std::string &payloadJson, const std::string &secret);` (header `{"alg":"HS256","typ":"JWT"}`), moved out of `livekit_jwt.cpp` together with `base64UrlEncode`/`jsonEscape`.
- Produces:
  ```cpp
  struct JitsiUser { std::string id; std::string name; bool moderator = false; };
  std::string mintJitsiJwt(const std::string &appId, const std::string &appSecret,
                           const std::string &room, const JitsiUser &user, int ttlSeconds,
                           std::optional<int64_t> nowUnixSeconds = {});
  ```
  Claims: `aud:"jitsi"`, `iss:appId`, `sub:"*"`, `room`, `nbf: now-10`, `exp: now+ttl`, `context.user:{id,name,moderator}`.

- [ ] **Step 1:** Write `jitsi_jwt_tests.cpp`: `MintsHs256WithJitsiClaims` (decode payload: `aud=="jitsi"`, `iss=="sessio"`, `sub=="*"`, `room=="rm_abc"`, `exp==1000+600`, `nbf==990`, `context.user.name=="Анна"`, `moderator==true`); `SignatureVerifiesWithSecret` (recompute HMAC over `header.payload`); `EscapesNameJson` (name `a"b\c` round-trips). Existing `livekit_jwt_tests.cpp` must keep passing unchanged.
- [ ] **Step 2:** Run `ctest --test-dir build-tb -R jitsi_jwt -j4` → FAIL (not defined).
- [ ] **Step 3:** Extract `signHs256Jwt` into `jwt_hs256.cpp`; implement `mintJitsiJwt`.
- [ ] **Step 4:** Run `ctest --test-dir build-tb -R "jwt" -j4` → PASS (both Jitsi and LiveKit tests).
- [ ] **Step 5:** Commit `Mint Jitsi JWTs in the token backend`.

### Task 2: Per-meeting provider and Jitsi tokens in the backend API

**Files:**
- Modify: `token-backend/src/config.h`, `token-backend/src/config.cpp`, `token-backend/src/db/migrations.cpp`, `token-backend/src/db/meetings_repository.{h,cpp}`, `token-backend/src/controller/dto.h`, `token-backend/src/controller/meetings_controller.h`, `token-backend/src/controller/invitations_controller.h`, `token-backend/src/service/meeting_service.{h,cpp}`, `token-backend/README.md`
- Test: `token-backend/test/config_tests.cpp`, `token-backend/test/meeting_service_tests.cpp`, `token-backend/test/http_integration_tests.cpp`

**Interfaces:**
- Consumes: `mintJitsiJwt` (Task 1).
- Produces (HTTP):
  - `POST /v1/meetings` accepts optional `"provider": "livekit" | "jitsi"` (default `livekit`). `jitsi` without Jitsi config → `400 {"error":"jitsi_not_configured"}`; any other value → `400 {"error":"invalid_provider"}`.
  - `TokenResponseDto` gains `provider` (always `"livekit"` or `"jitsi"`). For Jitsi meetings: `endpointUrl` = `JITSI_URL`, `roomName` = lowercase room name, `token` = Jitsi JWT (`moderator=true` for the specialist, `false` for the client), same `expiresAt` rule.
- Produces (config): `JITSI_URL`, `JITSI_APP_ID`, `JITSI_APP_SECRET` — all three or none; a partial set aborts startup naming the missing variable. `Config::jitsiConfigured()`.
- Produces (DB): `meetings.provider TEXT NOT NULL DEFAULT 'livekit'`; `Meeting::provider`.

- [ ] **Step 1:** Tests: config — `JitsiAllOrNothing` (only `JITSI_URL` set → throws mentioning `JITSI_APP_ID`), `JitsiOptional` (none set → `jitsiConfigured()==false`); service — `CreatesJitsiMeeting`, `RejectsJitsiWhenUnconfigured`, `RejectsUnknownProvider`, `SpecialistTokenForJitsiIsJitsiJwt` (decoded `room` equals lowercase room, `moderator==true`), `ClientTokenForJitsiIsJitsiJwt` (`moderator==false`, passcode rules unchanged); HTTP — `JitsiMeetingEndToEnd` (create with provider → specialist-token and client-token respond `provider:"jitsi"`, `endpointUrl:"https://jitsi.test"`), `LiveKitResponsesCarryProvider` (`provider:"livekit"` on existing flows).
- [ ] **Step 2:** Run `ctest --test-dir build-tb -j4` → the new tests FAIL.
- [ ] **Step 3:** Implement: migration (idempotent `ALTER TABLE` guarded by column check, as other migrations do), repository field, DTO fields, service branch on `meeting->provider` in `issueSpecialistToken`/`issueClientToken`; series-mapped meetings keep the LiveKit path. Document the variables and the `provider` field in `README.md`.
- [ ] **Step 4:** Run `ctest --test-dir build-tb -j4` → all PASS.
- [ ] **Step 5:** Commit `Let token-backend meetings run on Jitsi`.

### Task 3: App side: provider in token results and meeting creation

**Files:**
- Modify: `src/token_client/token_result.h`, `src/token_client/token_backend_client.{h,cpp}`, `src/meeting/provider_kind.h`, `src/meeting/livekit_meeting_provider.{h,cpp}`, `src/meeting/meeting_coordinator.{h,cpp}`, `src/pages/event_info_page/qevent_details_widget.cpp`, `src/app/application.cpp:560`
- Test: `test/token_backend_client_tests.cpp`, `test/provider_kind_tests.cpp`, `test/meeting_coordinator_tests.cpp`, `test/livekit_meeting_provider_tests.cpp`

**Interfaces:**
- Produces: `TokenResult::provider` (`QString`, `"livekit"` when the field is absent — older backends).
- Produces: `TokenBackendClient::requestCreateMeeting(const QString &bearer, const QString &startIso, const QString &endIso, const QString &provider = QStringLiteral("livekit"))` — sends `provider` only when not `livekit`.
- Produces: `ProviderKind::Jitsi` (string `"Jitsi"`); `MeetingCoordinator` holds three providers; Jitsi is a `LiveKitMeetingProvider` constructed with `backendProvider = "jitsi"` (new trailing ctor parameter, default `"livekit"`). Anything that checks `== ProviderKind::LiveKit` to mean "backend-hosted meeting" uses a new `bool isBackendHosted(ProviderKind)` in `provider_kind.h`.
- Event editor: the provider combo gains «Jitsi» (EN "Jitsi") after LiveKit; selecting it creates/cancels through the backend like LiveKit.

- [ ] **Step 1:** Tests: `ParsesProviderField` (reply with `"provider":"jitsi"` → `result.provider=="jitsi"`), `DefaultsProviderToLiveKit`, `CreateMeetingSendsJitsiProvider` (fake server sees `"provider":"jitsi"`; livekit request body has no `provider`), `JitsiRoundTripsAsString`, `isBackendHostedCoversLiveKitAndJitsi`, `CoordinatorRoutesJitsiToBackend`.
- [ ] **Step 2:** Run `ctest --test-dir build -R "token_backend_client|provider_kind|meeting_coordinator|livekit_meeting_provider" -j4` → FAIL.
- [ ] **Step 3:** Implement; replace provider equality checks with `isBackendHosted` where they mean backend-hosted (keep LiveKit-only logic in series code as is).
- [ ] **Step 4:** Same ctest command → PASS; also the event details tests (`-R event`).
- [ ] **Step 5:** Commit `Schedule Jitsi meetings through the token backend`.

### Task 4: QtWebEngine dependency across builds and installers

**Files:**
- Create: `src/webcall/CMakeLists.txt` (library `Sessio_webcall`, initially one source `webcall_version.cpp` with `QString webEngineChromiumVersion()` returning `qWebEngineChromiumVersion()`)
- Modify: top-level `CMakeLists.txt` (`add_subdirectory(src/webcall)`, link into Sessio), `.github/workflows/cmake-multi-platform.yml` (aqt `modules`: add `qtwebengine qtwebchannel qtpositioning`; Linux `qt_component` adds `qt.qt6.6102.addons.qtwebengine qt.qt6.6102.addons.qtwebchannel qt.qt6.6102.addons.qtpositioning`; RPM deps add `qt6-qtwebengine`), `packaging/` scripts only if a deploy step lists Qt modules explicitly
- Test: `test/webcall_smoke_tests.cpp`

**Interfaces:**
- Produces: target `Sessio_webcall` linking `Qt6::WebEngineCore Qt6::WebEngineWidgets Qt6::WebChannel Qt6::Network`; later tasks add sources to it.

- [ ] **Step 1:** Test `ChromiumVersionIsKnown`: `EXPECT_FALSE(webEngineChromiumVersion().isEmpty())`.
- [ ] **Step 2:** Configure and build locally (`cmake --build build -j4 --target Sessio Sessio_webcall_smoke_tests`), run the test → PASS.
- [ ] **Step 3:** Push the branch; CI must be green on Linux, Windows, macOS, and each installer artifact must contain `QtWebEngineProcess` (`.exe` on Windows, inside `Sessio.app/Contents/Frameworks/QtWebEngineCore.framework` on macOS, in the AppImage). Record installer size deltas in the commit body. On macOS confirm `NSCameraUsageDescription` and `NSMicrophoneUsageDescription` are in the bundle's `Info.plist` (Chromium asks for both).
- [ ] **Step 4:** Commit `Add QtWebEngine to builds and installers`.

### Task 5: AudioBridgeServer

**Files:**
- Create: `src/webcall/audio_bridge_server.h`, `src/webcall/audio_bridge_server.cpp`, `src/webcall/audio_chunk.h`, `src/webcall/audio_chunk.cpp`
- Test: `test/audio_bridge_server_tests.cpp`, `test/audio_chunk_tests.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct AudioChunk { QString participantId; uint32_t seq; double sentMs; const int16_t *samples; std::size_t count; };
  std::optional<AudioChunk> parseAudioChunk(const QByteArray &frame); // view into frame
  class AudioBridgeServer final : public QObject {   // lives on its own QThread
  public:
    using Sink = std::function<void(const AudioChunk &)>;
    AudioBridgeServer(QString allowedOrigin, Sink sink, QObject *parent = nullptr);
    Q_INVOKABLE quint16 listen();          // 127.0.0.1, random port; 0 on failure
    Q_INVOKABLE QString issueToken();      // new one-shot token; previous unused token stays valid until used
    Q_INVOKABLE void closeAll();
  signals:
    void clientConnected(); void clientDisconnected(); void malformedFrame();
  };
  ```
- Port the spike's `WsServer` (handshake, masking, 16/64-bit lengths, ping/pong, close). Additions: 1 MiB frame cap → abort; fragmented frames → abort; the sink is called on the server thread.

- [ ] **Step 1:** Tests (drive with a raw `QTcpSocket` client helper that builds masked frames): `AcceptsValidHandshake` (101 + correct `Sec-WebSocket-Accept`), `RejectsWrongOrigin` (403), `RejectsReusedToken`, `RotatedTokenAcceptsReconnect`, `DeliversChunkToSink` (participant `p:Анна`, seq 7, 320 samples equal to the sent ones), `MalformedChunkIsCountedNotDelivered`, `OversizeFrameAborts`, `FragmentedFrameAborts`; chunk tests: `ParsesV1`, `RejectsWrongVersion`, `RejectsOddSampleBytes`, `RejectsShortHeader`.
- [ ] **Step 2:** Run `ctest --test-dir build -R "audio_bridge|audio_chunk" -j4` → FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Same command → PASS.
- [ ] **Step 5:** Commit `Add the local audio bridge for web calls`.

### Task 6: The Jitsi call page

**Files:**
- Create: `web/jitsi-call/package.json` (vitest only, no bundler), `web/jitsi-call/src/index.html`, `web/jitsi-call/src/app.js`, `web/jitsi-call/src/pcm-tap.js` (worklet from the spike), `web/jitsi-call/src/chunk.js`, `web/jitsi-call/src/participants.js`, `web/jitsi-call/src/jitsi-adapter.js`, `web/jitsi-call/src/fake-adapter.js`, `web/jitsi-call/src/styles.css`, `web/jitsi-call/test/chunk.test.js`, `web/jitsi-call/test/participants.test.js`
- Modify: `.github/workflows/browser-call.yml` (run `npm ci && npm test` in `web/jitsi-call`)

**Interfaces:**
- Page ⇄ C++ over `QWebChannel` object `bridge`:
  - C++ → page (page calls nothing; C++ invokes `window.sessioCall.*` via `runJavaScript`): `join({serverUrl, room, jwt, displayName, wsPort, wsToken, adapter})` (`adapter` is `"jitsi"` or `"fake"`), `leave()`, `setMicrophoneEnabled(bool)`, `setCameraEnabled(bool)`, `setAudioCapture(bool)`, `reconnectAudio({wsPort, wsToken})`.
  - Page → C++ (`bridge` slots): `joined(localId)`, `joinFailed(reason)`, `left()`, `connectionLost(reason)`, `participantJoined(id, displayName, isLocal)`, `participantLeft(id)`, `mediaError(reason)`, `audioBridgeLost()`.
- `participants.js`: `class ParticipantRegistry { addTrack(participantId, track): {isNewParticipant}; removeTrack(participantId, track): {participantGone}; seq(participantId): number /* monotonic per participant across track re-adds */ }`.
- `chunk.js`: `encodeChunk(participantId, seq, sentMs, int16Buffer): Uint8Array` (format from Global Constraints).
- `jitsi-adapter.js` and `fake-adapter.js` export the same `createConference(options, handlers)`; the fake one synthesizes N tone participants (query `fakeParticipants`, default 2) and supports scripted track churn for tests.
- UI: participant video grid, local mic/camera/leave buttons, connection status; strings via a small `copy.js` keyed by `navigator.language` (`ru`/`en`).

- [ ] **Step 1:** vitest: `encodeChunk` layout (version byte 1, idLen, seq LE, `sentMs` f64 LE, id bytes, PCM bytes); registry: second `addTrack` for the same participant → `isNewParticipant=false` and `seq` continues; `removeTrack` of the last track → `participantGone=true`; a stale `removeTrack` after a newer `addTrack` → `participantGone=false`.
- [ ] **Step 2:** `cd web/jitsi-call && npm test` → FAIL.
- [ ] **Step 3:** Implement the modules and `app.js` (spike `app.js` is the reference for the worklet and Jitsi wiring; one AudioWorklet per participant; local mic tapped as the local participant id; taps exist only while `setAudioCapture(true)`; WebSocket close → `audioBridgeLost()`; `getUserMedia`/`createLocalTracks` failure → `mediaError`, join continues receive-only).
- [ ] **Step 4:** `npm test` → PASS.
- [ ] **Step 5:** Commit `Add the Jitsi call page`.

### Task 7: WebCallProvider

**Files:**
- Create: `src/webcall/web_call_provider.h`, `src/webcall/web_call_provider.cpp`, `src/webcall/web_call_bridge.h`, `src/webcall/web_call_bridge.cpp`, `src/webcall/call_scheme_handler.h`, `src/webcall/call_scheme_handler.cpp`
- Modify: `src/webcall/CMakeLists.txt` (`qt_add_resources` of `web/jitsi-call/src/*` under prefix `/jitsi-call`), `src/video/video_provider.h` (add `virtual QWidget *callView() { return nullptr; }`), `src/main.cpp` or app init (register scheme `sessio-call` before `QApplication`)
- Test: `test/web_call_bridge_tests.cpp`, `test/web_call_provider_tests.cpp` (offscreen QtWebEngine, `fake` adapter; CTest label `webengine`)

**Interfaces:**
- Consumes: `AudioBridgeServer`, `parseAudioChunk` (Task 5); page API (Task 6); `VideoProvider`, `AudioSinkSlot`, `ParticipantModel` (existing).
- Produces:
  ```cpp
  void registerCallScheme();   // QWebEngineUrlScheme "sessio-call", Secure|CorsEnabled|FetchApiAllowed|LocalAccessAllowed
  class WebCallProvider final : public pcm::video::VideoProvider {
  public:
    struct Options { QString adapter = QStringLiteral("jitsi"); QString displayName; };
    explicit WebCallProvider(Options options, QObject *parent = nullptr);
    void join(const QString &url, const QString &token) override; // url = "<serverUrl>/<room>"
    void leave() override;
    void setMicrophoneEnabled(bool) override; void setCameraEnabled(bool) override;
    bool isMicrophoneEnabled() const override; bool isCameraEnabled() const override;
    void setAudioSink(std::shared_ptr<pcm::video::AudioSink> sink) override; // also toggles page capture
    qint64 callElapsedMs() const override;
    QWidget *callView() override;
  };
  ```
- `WebCallBridge` (QObject registered as `bridge`) holds the page → C++ slots from Task 6 and translates them into `VideoProvider` signals and `ParticipantModel` upserts; it has no WebEngine dependency so it is unit-testable.
- `renderProcessTerminated` → `connectionLost(tr("The call page stopped unexpectedly"))`; `audioBridgeLost` → `issueToken()` then `reconnectAudio`.
- Audio path: server thread → `parseAudioChunk` → `AudioSinkSlot::get()` → `sink->onAudio(participantId, samples, count, 16000)`.
- Page loads `sessio-call://app/jitsi-call/index.html`; the `jitsi` adapter loads `lib-jitsi-meet.min.js` and `config.js` from `serverUrl`. Permission requests (mic/camera) from our origin are granted; others denied.

- [ ] **Step 1:** Bridge tests: `JoinedEmitsJoined`, `ParticipantJoinedUpsertsModelOnce` (two calls for the same id → one row, one `participantJoined`), `ParticipantLeftRemoves`, `MediaErrorIsNotFatal` (emits `mediaError`, not `joinFailed`), `ConnectionLostRelays`. Provider tests (offscreen, fake adapter, 2 tone participants): `FakeCallDeliversPerParticipantAudio` (a recording sink gets ≥1 s of audio for each of the two remote ids within 10 s), `TrackChurnKeepsOneStream` (fake adapter churns a track; sink ids unchanged, `participantJoined` count == 2), `NoAudioAfterSinkCleared` (after `setAudioSink(nullptr)` returns, the sink records nothing for 1 s), `RendererCrashEmitsConnectionLost` (test seam `void simulateRenderProcessTerminated()` runs the same handler as `renderProcessTerminated`; expect one `connectionLost` and no sink calls afterwards), `BridgeReconnectsAfterDrop` (server `closeAll()` → audio resumes within 5 s).
- [ ] **Step 2:** `ctest --test-dir build -R "web_call" -j4` → FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Same command → PASS (run with `QT_QPA_PLATFORM=offscreen`).
- [ ] **Step 5:** Commit `Add WebCallProvider for Jitsi calls`.

### Task 8: Use the web provider for Jitsi calls in the call UI

**Files:**
- Modify: `src/pages/calls_page/calls_page.{h,cpp}`, `src/pages/calls_page/call_page.{h,cpp}`, `translation/*.ts`
- Test: `test/calls_page_tests.cpp`, `test/call_page_tests.cpp`, `test/fake_video_provider.h`

**Interfaces:**
- Consumes: `TokenResult::provider` (Task 3), `WebCallProvider`, `VideoProvider::callView()` (Task 7).
- `CallsPage`: `startJoin(const pcm::tokenclient::TokenResult &result)`; provider factory becomes `std::function<pcm::video::VideoProvider *(const QString &provider)>` (default: `"jitsi"` → `WebCallProvider({.adapter="jitsi", .displayName=entry name})`, anything else → `LiveKitVideoProvider`). Jitsi join URL = `endpointUrl + "/" + roomName`. `setVideoProviderFactoryForTesting` takes the new signature.
- `CallPage::attachSession`: when `session->provider()->callView()` is non-null, the device-check screen is skipped (join fires immediately), the view replaces the tile area, the native mic/camera buttons are hidden; side panel, transcription button and Leave stay.

- [ ] **Step 1:** Tests: `JitsiTokenBuildsWebProvider` (factory receives `"jitsi"`; join URL `https://jitsi.test/rm_abc`), `LiveKitTokenUnchanged`, `WebProviderSkipsDeviceCheck` (fake provider with a `callView()` → `join` called without `joinConfirmed`, view is visible), `WebProviderKeepsSidePanelAndLeave`.
- [ ] **Step 2:** `ctest --test-dir build -R "calls_page|call_page" -j4` → FAIL.
- [ ] **Step 3:** Implement; add EN/RU strings, run the translation target.
- [ ] **Step 4:** Same command → PASS; full `ctest --test-dir build -j4` → PASS.
- [ ] **Step 5:** Commit `Open Jitsi meetings in the web call view`.

### Task 9: Deploy and verify end to end

**Files:**
- Modify: `CHANGELOG.md`, version files for both apps, `docs/` user guide page for calls (Jitsi section)

- [ ] **Step 1:** **Ask the user before touching the server.** Then: add `JITSI_URL=https://jitsi.sessio-pcm.ru`, `JITSI_APP_ID=sessio`, `JITSI_APP_SECRET` (copied server-side from `/opt/jitsi/.env`, never printed) to the `pcm-token-backend` container environment; rebuild and restart it; `curl https://livekit.sessio-pcm.ru/healthz` → 200. Do not run `firewall-cmd --reload`.
- [ ] **Step 2:** Isolated dev build (`scripts/run-dev-isolated.sh`): specialist creates a Jitsi meeting, a second machine joins with the invitation in client mode, both talk, transcription is started. Expected: two speakers with correct names in the transcript; prompter receives phrases; leaving and rejoining the client keeps one speaker.
- [ ] **Step 3:** 60-minute call with three participants (spec acceptance): no audio gaps in the transcript, transcription lag stays flat; note CPU of the WebEngine process tree.
- [ ] **Step 4:** CHANGELOG, versions, docs; commit `Release Jitsi web calls`.
