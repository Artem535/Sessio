# Sessio #95 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Deliver independently rendered/audio-played participants and the approved layout B for native group calls.
**Architecture:** Provider-owned identity model and bounded frame sources; UI-owned tiles and replaceable grid strategy. Presence comes from the model; each remote track has its own lifecycle. Unique token identities permit simultaneous clients.
**Tech Stack:** C++20, Qt Widgets/Multimedia/OpenGL, LiveKit SDK 1.12, GoogleTest, existing token backend.
**Spec:** docs/superpowers/specs/2026-09-30-multi-participant-calls-design.md

## Global Constraints
- Keep 1:1 large remote plus local PiP; three or more participants use equal tiles including local.
- Side panel, floating controls and client-mode separation remain functional.
- Model roles are display-only; no PII in tokens, production logs or test artifacts.
- Stream capacity = 2; latest copied image; at most one queued frame notification per source.
- Close readers before joining threads; reject stale room generations and replaced track SIDs.
- Recording, transcription, consent, screen sharing, active-speaker selection and quality tuning are outside scope.
- Reuse the isolated worktree and branch codex/95-multi-participant-calls above PR94; never stage third_party/qlementine.
- Bump version 0.2.4 to 0.2.5 in both declarations; CHANGELOG and complete ru/en translations required.
- User authorizes immediate implementation after this plan, without another approval question.

## Review Focus
- Duplicate or unknown participant events: stable rows and accurate remote count (Task 1).
- Frame-source destruction while UI remains visible: no dangling pointers or stale frame (Task 3).
- Old queued callbacks after leave/rejoin: cannot attach old tracks to new room (Task 2).
- Audio-only room occupants and reconnect to an empty room: presence stays accurate (Task 1).
- Narrow window with notes and ten occupants: all tiles and controls remain within available bounds (Task 3).

### Task 1: Participant and frame contracts, presence state
**Files:** Create src/video/participant_model.{h,cpp}, src/video/video_frame_source.{h,cpp}, test/participant_model_tests.cpp, test/video_frame_source_tests.cpp. Modify src/video/video_provider.h, video_session.{h,cpp}, video_session_state.h, test/fake_video_provider.h, video_session_tests.cpp, CMake/video target definitions and test/CMakeLists.txt.
**Interfaces:**
- Participant {QString id, displayName, role; bool isLocal=false, microphoneEnabled=true, cameraEnabled=true}.
- ParticipantModel::upsert(const Participant&), remove(const QString&), clear(), participant(const QString&) -> std::optional<Participant>, remoteCount() -> int; QAbstractListModel roles IdRole, DisplayNameRole, ParticipantRole, IsLocalRole, MicrophoneEnabledRole, CameraEnabledRole; remoteCountChanged(int).
- VideoFrameSource QObject: submitFrame(const QImage&) thread-safe copied latest frame, clear(), latestFrame() -> QImage; frameAvailable(). No SDK/Widgets.
- VideoProvider constructor owns ParticipantModel; participants() returns pointer; virtual frameSource(const QString&) -> VideoFrameSource* defaults nullptr; participantJoined(QString), participantLeft(QString). Remove identity-free signals once fakes/session migrated. Legacy widget methods temporarily retained for Task 3.
- VideoSession::participants() relays provider model. WaitingForParticipants replaces WaitingForClient; joining/reconnected resolve target based on remoteCount, including updates arriving before joined. Terminal teardown clears model.
- [ ] Add failing model/source tests (duplicate upserts, order retained on metadata update, unknown removal, clear, local count excluded; flood frames delivers latest with bounded notifications; independent sources).
- [ ] Run tests and observe expected failure before implementing contracts.
- [ ] Implement contracts and migrate FakeVideoProvider/state tests; temporary aliases only when other targets need migration, remove by Task 3.
- [ ] Test presence 0→1→2→1→0, presence before joined, duplicate events, audio-only participant, reconnect after last participant leaves, terminal clear.
- [ ] Build/run these targets with coherent Qt environment described below and commit focused changes.

### Task 2: LiveKit per-participant media and unique backend identities
**Files:** Modify src/video/livekit_video_provider.{h,cpp}, remote_video_renderer.{h,cpp}, capture adapter if needed; create src/video/livekit_video_frame_source.{h,cpp}; relevant video CMake and smoke tests. Modify token-backend/src/service/meeting_service.cpp, crypto/livekit_jwt.{h,cpp}, backend tests.
**Interfaces:** Consume Task 1 contracts. LiveKitVideoFrameSource : VideoFrameSource exposes attachTrack(shared_ptr<livekit::Track>), detach(). RemoteVideoRenderer exposes attachSource(VideoFrameSource*), detach(), scaledFrameRect unchanged; QPointer source, no SDK reader/widgets owned by provider.
- [ ] Write failing tests proving distinct same-meeting client/practitioner token identities and signed role metadata, unchanged room/grants/authorization.
- [ ] Implement cryptographically random per-issuance suffix using existing random helper; identity stable within reused token; absent role metadata accepted by desktop.
- [ ] Move SDK read loop to LiveKitVideoFrameSource with capacity 2, RGBA copied frame, clear on detach, close before join.
- [ ] Replace provider's singleton media with per-identity source/player/track state. Local capture uses previewSink and shared source without second camera.
- [ ] Populate model from snapshot and copied callbacks; handle metadata, track subscribe/unsubscribe/mute/unmute, participant leave; generation and track-SID guards. Output switching updates every player; teardown destroys all streams before SDK shutdown.
- [ ] Make renderer Qt-only; drawImage(targetRect, frame) with smooth QPainter scaling, preserve black letterbox.
- [ ] Migrate media smoke/letterbox tests, run backend and video tests; commit. No renderer/source SDK integration claim from offscreen tests alone.

### Task 3: Approved adaptive grid and owned participant tiles
**Files:** Modify src/pages/calls_page/call_page.{h,cpp}; create participant_tile.{h,cpp}, call_layout_strategy.{h,cpp} beside call_page; test/call_page_tests.cpp and dedicated layout tests; video/UI CMake.
**Interfaces:** Consume participants()/frameSource(id) and RemoteVideoRenderer::attachSource(). CallLayoutStrategy pure selection returns rows/columns and PiP mode for participant list and stage size. ParticipantTile QWidget renders plain-text name, local marker and camera-off placeholder, UI owns renderer and holds source via QPointer.
- [ ] Add failing UI/layout tests: one remote+local PiP, two remote+local three equal tiles; six/ten visible; landscape/portrait stage; last row centered; notes resize, no controls overlap.
- [ ] Implement QGridLayout host with strategy choosing largest fitting 16:9 area; 1:1 PiP; separate overlay controls above tile host.
- [ ] Bind model row/data changes by identity; preserve surviving tile on first participant leaving; clear on session swap/deletion and source destruction.
- [ ] Waiting state keeps local preview visible. Preserve device check, all controls, notes and client mode; migrate WaitingForParticipants uses and old borrowed-widget tests.
- [ ] Remove legacy widget methods/provider-widget ownership. Run call/session suites; commit.

### Task 4: Review, runtime verification and delivery
**Files:** Version declarations, CHANGELOG.md, translation/app_{ru,en}.ts, task verification notes and a synthetic call smoke harness if necessary.
- [ ] Verify relevant desktop/backend builds, run targeted suites and meaningful synthetic three-participant room smoke with independent patterns/tones, join/leave/rejoin/mute and speaker switching. Use a disposable room; no production participant data. If actual audio/devices unavailable, state that gate explicitly.
- [ ] Run populated hardware-OpenGL UI for 1:1, 1:2 and ten tile geometry; capture screenshots if browser/native access policy permits; no policy workaround.
- [ ] Bump both versions, update CHANGELOG and run update_translations; translate all unfinished entries.
- [ ] Whole-branch independent review and fixes; git diff --check.
- [ ] Push and create dependent draft PR against worktree-videoprovider-domain-layer, Closes #95; attach PR. Keep PR94/manual device gate explicit, never merge or close issue.

## Local build environment
Use rtk for shell commands, rtk proxy for native tools, login:false.
Existing build-release is configured against PR94 dependency caches. Prefix runtime/build commands:
LD_LIBRARY_PATH=/home/a.durynin/Projects/C++/PsyClientManager/.claude/worktrees/videoprovider-domain-layer/build-release/_deps/livekit-sdk/livekit-sdk-linux-x64-1.12.0/lib:/usr/lib64
QT_PLUGIN_PATH=/usr/lib64/qt6/plugins
QT_QPA_PLATFORM=offscreen for headless tests only.
Baseline VideoSessionTest/CallPageTest/CallsPageTest: 65 passing tests.
Use temporary HOME/XDG_CONFIG_HOME when testing settings.
