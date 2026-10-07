# Call Transcription — Session Layer (Phase 3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Connect the finished engine (phase 1) and data layer (phase 2) to a live LiveKit call: audio taps, a consent-gated `TranscriptionSession` that never loads or runs the model on the GUI thread, a dedicated phrase-writer thread, materialisation of a recurring-series occurrence into a real event, and the application-start recovery hook.

**Architecture:** `AudioTap`s on the local mic capture and on each remote-audio reader feed a thread-safe `AudioSink` installed on the `VideoProvider` only while a session exists. `TranscriptionSession` (QObject, GUI thread) owns one `SerialExecutor` thread that creates the engine (model/VAD load), adds/removes tracks, and stops it; audio goes straight from the capture/reader thread into `TranscriptionEngine::pushAudio` (a microsecond copy). Engine callbacks run on the decode thread and hand phrases to `PhraseWriter`, which owns one thread and one private DuckDB connection. `CallEventResolver` turns a (possibly virtual, negative) call event id into a real event id.

**Tech Stack:** C++20, Qt 6 (Core, Multimedia, StateMachine), GoogleTest, DuckDB via `pcm::database::Database`, `pcm::transcription` engine.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-10-06-call-transcription-design.md`. Module is ON on every platform (`SESSIO_ENABLE_TRANSCRIPTION`).
- **Thread rule (user requirement):** the speech model (recogniser and Silero VAD) must never be loaded or run on the GUI thread, and the GUI thread must never block on decoding. Loading, `addTrack`, `removeTrack` and `TranscriptionEngine::stop` all run on the session's executor thread; phrases are written on the writer thread.
- Engine API (do not change): `TranscriptionEngine(VadFactory, shared_ptr<ISpeechRecognizer>, EngineCallbacks, EngineConfig)`, `addTrack(TrackInfo, start_offset_ms)` (calls the VAD factory on the calling thread), `removeTrack(id)`, `pushAudio(id, const int16_t*, count, rate)`, `stop(drain_timeout)` (must not be called from a callback), callbacks `on_phrase(const TranscribedPhrase&)` / `on_delayed(bool)` run on the decode thread with no lock held.
- Phrase times written to the DB are milliseconds from the call transcript clock start (`TranscribedPhrase::start_ms` already includes the `start_offset_ms` given to `addTrack`).
- Consent: no `AudioSink` is installed and no engine exists unless `start()` was called with a non-empty consent scope after the caller obtained consent. Stopping/revoking must stop and join the engine and the writer **before** `set_transcript_status` / `revoke_transcript_consent` / `delete_transcript` (see comment in `src/database/database.h`).
- Mic tap must respect microphone mute: while the local microphone is disabled nothing from the local mic reaches the sink.
- Tracks: local participant → `TrackRole::Practitioner`; every remote participant → `TrackRole::Participant`. Track id = participant id string. Russian only; no language switching.
- Logs must not contain phrase text, names or meeting URLs.
- Style: C++20, two-space indent, `PascalCase` classes, `camelCase` Qt methods, `snake_case` database APIs. Qt-free classes in `pcm::transcription`/own namespace as noted. Run `git diff --check` before committing. New/changed `tr()` strings: none in this phase.
- Build (host, prebuilt vcpkg — never let vcpkg rebuild): worktree build dir `build-tr` is already configured. Reconfigure only if CMake files change (it re-runs automatically on build). Build with `distrobox-host-exec cmake --build build-tr --target <target> --parallel 3`; run tests with `distrobox-host-exec ctest --test-dir build-tr -R <regex> --output-on-failure`. Never `pkill -f`.
- TDD is mandatory: write the test, run it and **see it fail for the expected reason**, then implement.

## File Structure

- Create `src/video/audio_sink.h` — `AudioSink` interface, `AudioSinkSlot` (thread-safe holder), `AudioTap`.
- Modify `src/video/{audio_capture_adapter,remote_audio_player}.{h,cpp}`, `video_provider.h`, `livekit_video_provider.{h,cpp}` — tap points and `setAudioSink`.
- Modify `test/fake_video_provider.h` — records the sink, `simulateAudio`.
- Modify `src/database/database.cpp` — `add_transcript_phrase` uses a private connection.
- Create `src/call_transcription/` (library `Sessio_call_transcription`): `serial_executor.h`, `phrase_writer.{h,cpp}`, `transcription_session.{h,cpp}`, `engine_factory.{h,cpp}`, `call_event_resolver.{h,cpp}`, `CMakeLists.txt`.
- Modify `src/event_view/recurrence_utils.{h,cpp}` — `virtualOccurrenceForId`.
- Modify `src/CMakeLists.txt` (add subdirectory), `src/app/application.cpp` (startup hook), `test/CMakeLists.txt`.
- Create tests: `test/audio_tap_tests.cpp`, `test/phrase_writer_tests.cpp`, `test/transcription_session_tests.cpp`, `test/call_event_resolver_tests.cpp`; extend `test/database_transcript_tests.cpp`, `test/fake_video_provider_tests.cpp`.

---

### Task 1: Audio sink, taps and provider wiring

**Files:**
- Create: `src/video/audio_sink.h`, `test/audio_tap_tests.cpp`
- Modify: `src/video/audio_capture_adapter.{h,cpp}`, `src/video/remote_audio_player.{h,cpp}`, `src/video/video_provider.h`, `src/video/livekit_video_provider.{h,cpp}`, `test/fake_video_provider.h`, `test/fake_video_provider_tests.cpp`, `test/CMakeLists.txt`

**Interfaces:**
- Produces:
```cpp
namespace pcm::video {
class AudioSink {
 public:
  virtual ~AudioSink() = default;
  // May be called from any thread. `samples` is mono int16 valid only for the call.
  virtual void onAudio(const QString &participantId, const int16_t *samples,
                       std::size_t count, int sampleRate) = 0;
};
class AudioSinkSlot {  // thread-safe holder, shared by the provider and the taps
 public:
  void set(std::shared_ptr<AudioSink> sink);
  [[nodiscard]] std::shared_ptr<AudioSink> get() const;
};
class AudioTap {  // one per audio source; push() is lock-light and thread-safe
 public:
  using Callback = std::function<void(const int16_t *, std::size_t, int)>;
  void setCallback(Callback cb);   // empty = off
  void setEnabled(bool enabled);   // default true
  void push(const int16_t *samples, std::size_t count, int sampleRate) const;
};
}
// VideoProvider: virtual void setAudioSink(std::shared_ptr<AudioSink> sink) { Q_UNUSED(sink); }
// FakeVideoProvider: std::shared_ptr<AudioSink> audioSink() const; void simulateAudio(const QString &id, const std::vector<int16_t> &samples, int rate = 48000);
```

- [ ] **Step 1: Failing tests** — `test/audio_tap_tests.cpp` (links only `Qt6::Core`, GTest; header-only so no moc needed). Cases: `TapIsSilentWithoutCallback`; `TapDeliversSamplesToCallback` (count/rate/content); `DisabledTapDeliversNothing` then re-enable delivers; `SlotReturnsNullUntilSet` / `SlotHoldsSink`; `TapSurvivesConcurrentSetCallback` (one thread pushes 10 000 times while another toggles `setCallback` — must not crash, run under normal build). Register in `test/CMakeLists.txt`:
```cmake
add_executable(Sessio_audio_tap_tests audio_tap_tests.cpp)
target_include_directories(Sessio_audio_tap_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_audio_tap_tests PRIVATE GTest::gtest GTest::gtest_main Qt6::Core)
gtest_discover_tests(Sessio_audio_tap_tests)
```
Also add to `test/fake_video_provider_tests.cpp` a test `FakeProviderForwardsSimulatedAudioToSink` (install a recording sink via `setAudioSink`, call `simulateAudio("p1", {1,2,3}, 48000)`, expect one call with id `p1`, 3 samples, rate 48000) and `FakeProviderWithoutSinkDropsAudio`.
- [ ] **Step 2:** Build & run → FAIL (header missing).
- [ ] **Step 3: Implement `audio_sink.h`** (header-only). `AudioTap` holds `mutable std::mutex mutex_; std::shared_ptr<const Callback> cb_; std::atomic<bool> enabled_{true};`. `push` returns early if `!enabled_`, copies the `shared_ptr` under the lock, calls outside the lock. `setCallback` stores `make_shared<const Callback>(std::move(cb))` or null for an empty callback.
- [ ] **Step 4: Wire the taps.**
  - `AudioCaptureAdapter`: add member `AudioTap mTap;` and `AudioTap &tap()`; in `onReadyRead()` after building `samples` and before chunking, `mTap.push(samples.data(), samples.size(), kSampleRate);`.
  - `RemoteAudioPlayer`: add `AudioTap mTap;` + `AudioTap &tap()`; in `readerLoop` after reading the frame, `mTap.push(samples.data(), samples.size(), sampleRate)` for `numChannels == 1`; for `numChannels > 1` first downmix to mono (average of channels) into a local `std::vector<int16_t>` and push that. This runs on the reader thread, not the GUI thread.
  - `VideoProvider`: add `virtual void setAudioSink(std::shared_ptr<AudioSink> sink) { Q_UNUSED(sink); }` (include `audio_sink.h`).
  - `LiveKitVideoProvider`: member `std::shared_ptr<AudioSinkSlot> mSinkSlot = std::make_shared<AudioSinkSlot>();`. `setAudioSink` calls `mSinkSlot->set(std::move(sink))`. Install once in the constructor the mic callback: `mAudioCapture->tap().setCallback([slot = mSinkSlot, this](auto *s, auto n, int rate){ if (auto sink = slot->get()) sink->onAudio(mLocalIdentity, s, n, rate); })` (this runs on the GUI thread, so reading `mLocalIdentity` is safe). `setMicrophoneEnabled(bool)` must also call `mAudioCapture->tap().setEnabled(enabled)` (apply wherever `mMicrophoneEnabled` is assigned, including the failure/rollback paths so the tap always equals the real mic state). Where a `RemoteAudioPlayer` is created (`media->audio = std::make_unique<RemoteAudioPlayer>()`), install `media->audio->tap().setCallback([slot = mSinkSlot, id](auto *s, auto n, int rate){ if (auto sink = slot->get()) sink->onAudio(id, s, n, rate); })` with `id` the participant identity captured **by value**.
  - `FakeVideoProvider`: `setAudioSink` stores the pointer; `simulateAudio` calls `mSink->onAudio(...)` if set.
- [ ] **Step 5:** Build targets `Sessio_audio_tap_tests`, `Sessio_fake_video_provider_tests`, `Sessio_video`, `Sessio_video_session_tests`; run tests → PASS. Build `Sessio_video` proves the LiveKit wiring compiles.
- [ ] **Step 6: Commit** `feat(video): add audio sink and taps for call audio (#118)`.

---

### Task 2: Phrase inserts use a private connection

**Files:** Modify `src/database/database.cpp` (`add_transcript_phrase`), `test/database_transcript_tests.cpp`.

Reason (phase-2 review I1): `write_connection()` returns the active schedule-transaction connection and asserts the same thread; a writer thread would hit that assert/race. Transcript phrase writes are independent of schedule transactions and must never join them.

- [ ] **Step 1: Failing test** `PhraseInsertWorksFromAnotherThreadDuringScheduleTransaction`: find how existing tests open a schedule transaction (`grep -n "begin_schedule\|mTxConn\|run_in_schedule_transaction" src/database/database.h test/*.cpp`); open one on the main thread, and while it is open insert a phrase from `std::thread` for a valid recording transcript; expect a positive id and no assert/crash (tests run in Debug-asserting build only if asserts are on — also assert via `get_transcript_phrases` that the phrase is stored). If the schedule transaction API cannot be held open across a thread in a test, instead add `PhraseInsertDoesNotUseTransactionConnection` by calling `add_transcript_phrase` from a second thread while the main thread holds the transaction through the same hook the schedule tests use.
- [ ] **Step 2:** Run → FAIL (assert fires or phrase missing).
- [ ] **Step 3:** In `add_transcript_phrase` replace `auto &conn = write_connection(ownedConn);` with `ownedConn.emplace(*mDb); auto &conn = *ownedConn;` and add a comment: `// Own connection on purpose: phrases are written from a dedicated writer thread and must not join (or race) a schedule transaction.`
- [ ] **Step 4:** Run `Sessio_database_transcript_tests` and `Sessio_backup_tests` → PASS.
- [ ] **Step 5: Commit** `fix(database): write transcript phrases on a private connection (#118)`.

---

### Task 3: PhraseWriter (single writer thread)

**Files:** Create `src/call_transcription/{CMakeLists.txt,phrase_writer.h,phrase_writer.cpp}`, `test/phrase_writer_tests.cpp`; modify `src/CMakeLists.txt` (add `add_subdirectory(call_transcription)` guarded like the other transcription targets — follow how `src/transcription` is added), `test/CMakeLists.txt`.

**Interfaces:**
- Produces (Qt-free, namespace `pcm::calltranscription`):
```cpp
struct WriterStats { uint64_t written = 0; uint64_t failed = 0; uint64_t dropped = 0; };
class PhraseWriter {
 public:
  using Store = std::function<int64_t(const pcm::database::DuckTranscriptPhrase&)>;  // returns id, <=0 on failure
  using Written = std::function<void(const pcm::database::DuckTranscriptPhrase&)>;   // row with id; writer thread
  PhraseWriter(Store store, Written on_written);
  ~PhraseWriter();                       // equivalent to stop(0 ms)
  void submit(pcm::database::DuckTranscriptPhrase phrase);  // any thread, non-blocking
  void stop(std::chrono::milliseconds drain_timeout);       // idempotent; joins the thread; unwritten items counted as dropped
  WriterStats stats() const;
};
}
```
Library `Sessio_call_transcription` (STATIC) links `Sessio_transcription`, `Sessio_database`, `Sessio_video`, `Qt6::Core`; include dir PUBLIC `${CMAKE_CURRENT_SOURCE_DIR}`.

- [ ] **Step 1: Failing tests** (`phrase_writer_tests.cpp`, fake `Store` recording ids/threads): `WritesInSubmissionOrder`; `StoreRunsOffTheCallingThread`; `FailedStoreIsCountedAndDoesNotStopTheWriter`; `ThrowingStoreAndThrowingCallbackAreContained` (counted as failed, later items still written); `StopDrainsQueuedItems` (all written when store is fast); `StopWithZeroTimeoutDropsPendingAndCountsThem` (store blocked on a latch; items remain; after releasing the latch, `stats().dropped == pending`); `SubmitAfterStopIsDropped`; `StopIsIdempotent`; `WrittenCallbackReceivesAssignedId`. Link `GTest::gtest GTest::gtest_main Sessio_call_transcription`.
- [ ] **Step 2:** Run → FAIL (target missing).
- [ ] **Step 3: Implement** with `std::mutex`, `std::condition_variable`, `std::deque`, one `std::thread`; store/callback calls wrapped in `try/catch (...)` counting `failed`. `stop` sets `stopping_`, waits up to `drain_timeout` for the queue to empty, then sets `abort_` (remaining items → `dropped`), notifies, joins. `Store` must not be invoked after `stop` returns.
- [ ] **Step 4:** Build/run → PASS. **Step 5: Commit** `feat(transcription): add single-thread phrase writer (#118)`.

---

### Task 4: SerialExecutor + TranscriptionSession

**Files:** Create `src/call_transcription/{serial_executor.h,transcription_session.h,transcription_session.cpp}`, `test/transcription_session_tests.cpp`; modify the two CMake files.

**Interfaces:**
- Consumes: Task 1 (`AudioSink`, `VideoProvider::setAudioSink`, `ParticipantModel`), Task 3 (`PhraseWriter`), engine API, `Database` transcript methods (`add_transcript(event_id, consent_scope, model_id, consent_given_at_ms)`, `set_transcript_status`, `revoke_transcript_consent`, `add_transcript_phrase`).
- Produces:
```cpp
namespace pcm::calltranscription {
// One worker thread running queued tasks in order. Destructor drains then joins.
class SerialExecutor { public: SerialExecutor(); ~SerialExecutor(); void post(std::function<void()> task); void waitIdle(); };

using EngineFactory = std::function<std::shared_ptr<pcm::transcription::TranscriptionEngine>(
    pcm::transcription::EngineCallbacks callbacks, QString *error)>;  // runs on the executor thread; null + *error on failure

enum class SessionState { Idle, Loading, Recording, Stopping, Finished, Failed };

class TranscriptionSession final : public QObject, public pcm::video::AudioSink {
  Q_OBJECT
 public:
  TranscriptionSession(std::shared_ptr<pcm::database::Database> db,
                       pcm::video::VideoProvider *provider, EngineFactory factory,
                       QObject *parent = nullptr);
  ~TranscriptionSession() override;   // stop + join, never blocks on decoding longer than 2 s
  [[nodiscard]] SessionState state() const;
  [[nodiscard]] int64_t transcriptId() const;       // 0 before start()
  // GUI thread. Requires state()==Idle, eventId>0, non-empty consentScope.
  // Creates the transcript row (status "recording"), installs the sink, loads
  // the engine on the executor thread. Returns false (state unchanged) on bad input/DB failure.
  bool start(int64_t eventId, const QString &consentScope);
  void stop();     // graceful: drain engine, join writer, status "draft"; emits finished
  void revoke();   // no drain: abort engine+writer, revoke_transcript_consent (status stays as DB defines); emits revoked
  void onAudio(const QString &participantId, const int16_t *samples, std::size_t count, int sampleRate) override;
 signals:
  void stateChanged(pcm::calltranscription::SessionState state);
  void phraseAdded(pcm::database::DuckTranscriptPhrase phrase);   // GUI thread, row has id
  void delayedChanged(bool delayed);
  void failed(QString reason);          // model missing/load failure, writer/DB failure
  void finished(qint64 transcriptId);
  void revoked(qint64 transcriptId);
};
}
```
Behaviour contract:
1. `start`: validate, `db->add_transcript(eventId, consentScope, modelId?, nowMs)` on the GUI thread (cheap), then `provider->setAudioSink(shared_from_this-like non-owning wrapper)`. Because `AudioSink` is held by `shared_ptr` in the provider slot, give the session an inner `struct SinkProxy : AudioSink { std::atomic<TranscriptionSession*> target; }` owned by `shared_ptr`; `stop/revoke/dtor` null the target and call `provider->setAudioSink(nullptr)` first, so no callback can reach a dead session. `onAudio` → loads `std::shared_ptr<TranscriptionEngine>` from a mutex-guarded member (set by the executor after load) and calls `pushAudio(id.toStdString(), …)`; if the engine is not yet ready, audio is dropped.
2. Executor task 1 (Loading): record call-clock start `steady_clock::time_point mClockStart`, call the factory (model + VAD load happen here), on failure post to GUI: state Failed, `failed(reason)`, `set_transcript_status(id,"draft")`, remove sink. On success store engine, post to GUI: state Recording, then add a track for every participant already in `provider->participants()` — the participant list is read on the GUI thread and each `addTrack` is queued on the executor.
3. Participants: connect `participantJoined(id)` → look up `Participant`, queue `engine->addTrack({id, isLocal?Practitioner:Participant, displayName}, offsetMs)` where `offsetMs = ms since mClockStart` computed at the moment of the signal on the GUI thread; `participantLeft(id)` → queue `removeTrack`. Ignore signals unless state is Recording; tracks queued during Loading are added right after load (keep a pending list).
4. Callbacks: `on_phrase` (decode thread) → map to `DuckTranscriptPhrase{transcript_id, track_role "practitioner"/"participant", speaker_name, start_ms, end_ms, text}` → `writer->submit`. Writer `on_written` (writer thread) → `QMetaObject::invokeMethod(this, …, Qt::QueuedConnection)` emitting `phraseAdded` (guard with the same alive-flag/QPointer so a destroyed session is safe). `on_delayed(bool)` → queued `delayedChanged`. A store failure (`id<=0`) is counted; after 3 consecutive failures post `failed("Could not save the transcript")` once.
5. `stop()` (state Recording or Loading): state Stopping; sink removed immediately; executor task: `engine->stop(5 s)` (drains phrases through callbacks), then `writer->stop(5 s)`, then post to GUI: `set_transcript_status(id,"draft")`, state Finished, `finished(id)`. GUI never waits.
6. `revoke()`: sink removed, executor task: `engine->stop(0 ms)`, `writer->stop(0 ms)`, then GUI: `revoke_transcript_consent(id)`, state Finished, `revoked(id)`.
7. Auto-stop: when the `VideoSession` ends — the session constructor takes only the `VideoProvider`, so connect `provider->left()` and `provider->connectionLost(QString)` to `stop()` (state-guarded).
8. Destructor: if not Finished/Idle, behave like `revoke`-less hard stop: remove sink, stop engine/writer with 0 ms timeout, join executor, finalise status to "draft" if the transcript row exists. Never leave status "recording" (startup recovery covers crashes only).
9. Single phrase-writer invariant: exactly one `PhraseWriter` per session.

- [ ] **Step 1: Failing tests** (`transcription_session_tests.cpp`, `QApplication` main like `call_page_tests.cpp`, real temp `Database` via the fixture pattern from `database_transcript_tests.cpp`, `FakeVideoProvider`, fake `ISpeechRecognizer`/`IVoiceActivityDetector` from `test/transcription/transcription_test_support.h` — include via `${CMAKE_SOURCE_DIR}/test/transcription`; the engine factory in tests returns an engine built from those fakes). Use `QSignalSpy` + `QTRY_VERIFY`. Cases:
  `StartCreatesRecordingTranscriptAndInstallsSink`; `StartRejectsEmptyConsentScopeAndNonPositiveEvent` (no row, no sink); `NoSinkInstalledBeforeStartOrAfterStop` (fake provider `audioSink()` null) — "no sink without session"; `ModelLoadRunsOffTheGuiThread` (factory records `std::this_thread::get_id()` ≠ GUI thread id; `addTrack` VAD factory thread ≠ GUI thread); `ReachesRecordingAfterLoad`; `LoadFailureEntersFailedStateAndFinalisesTranscript` (factory returns null + error → `failed` emitted, DB status `draft`, sink removed); `PhrasesFromRemoteAndLocalTracksAreStoredWithRoles` (feed audio via `provider.simulateAudio` for the local and a remote participant; local row `practitioner`, remote `participant`; times non-decreasing per track; speaker names stored); `LateParticipantGetsTrackWithCallClockOffset` (join after 1 s of call clock → its phrase start_ms ≥ 1000); `LeavingParticipantFlushesOpenPhrase`; `MutedMicrophoneProducesNoPractitionerPhrases` is covered in Task 1's tap test, here assert `FakeVideoProvider` mic disabled leaves tap untouched only if the fake simulates it — skip if not applicable; `StopFinalisesDraftAndEmitsFinished` (status `draft`, writer joined, later `simulateAudio` ignored); `RevokeAbortsAndMarksConsentRevoked` (`consent_revoked_at` set, `revoked` emitted, no phrases written after); `ProviderLeftAutoStops`; `ProviderConnectionLostAutoStops`; `DestroyingSessionWhileRecordingFinalisesAndDoesNotCrash`; `GuiThreadIsNotBlockedByStop` (engine whose recogniser sleeps 500 ms: `stop()` returns in < 100 ms).
- [ ] **Step 2:** Build/run → FAIL.
- [ ] **Step 3: Implement** per the behaviour contract above. Use `std::atomic<SessionState>` for `state()` (written on the GUI thread; read anywhere). Queue GUI posts with `QMetaObject::invokeMethod(this, lambda, Qt::QueuedConnection)` guarded by a `std::shared_ptr<std::atomic<bool>> alive_`. Never call `engine->stop()` from a callback or from the GUI thread.
- [ ] **Step 4:** Build/run `Sessio_transcription_session_tests` (loop `--gtest_repeat=20` once to flush races) → PASS.
- [ ] **Step 5: Commit** `feat(transcription): add call transcription session (#118)`.

---

### Task 5: Production engine factory

**Files:** Create `src/call_transcription/engine_factory.{h,cpp}`, `test/engine_factory_tests.cpp`; modify CMake (library links `Sessio_transcription_sherpa`).

**Interfaces:**
- Consumes: `locateModels(app_dir, platform, env_override)` / `LocateResult` (`paths`, plus its error text — read `model_locator.h` for the exact field names), `makeSherpaRecognizer`, `makeSherpaVadFactory`.
- Produces: `EngineFactory makeProductionEngineFactory(std::filesystem::path appDir, std::string modelsEnvOverride = {});` and `bool transcriptionModelsAvailable(const std::filesystem::path &appDir, const std::string &envOverride = {}, QString *reason = nullptr);`

- [ ] **Step 1: Failing tests** (no model files needed): `FactoryReportsMissingModelsWithoutThrowing` (empty temp app dir → returns null and a non-empty `*error`); `AvailabilityFalseWithoutModels`; `AvailabilityTrueWithFakeModelTree` (create the file layout from `ModelPaths` with empty files in a temp dir via the env override — `transcriptionModelsAvailable` only checks existence, it does not load).
- [ ] **Step 2:** FAIL. **Step 3:** Implement: the factory lambda locates models, builds `makeSherpaRecognizer(paths)` and the VAD factory, constructs `TranscriptionEngine` with the callbacks it is given, catches every exception from the sherpa load and turns it into `*error` (no model paths with user names in the message: use only the file's base name). **Step 4:** PASS. **Step 5: Commit** `feat(transcription): production engine factory (#118)`.

---

### Task 6: CallEventResolver (materialise a recurring occurrence)

**Files:** Create `src/call_transcription/call_event_resolver.{h,cpp}`, `test/call_event_resolver_tests.cpp`; modify `src/event_view/recurrence_utils.{h,cpp}`, CMake files (the resolver library links `Sessio_event_view` — find the real event_view target name via `grep -n "recurrence_utils" src/event_view/CMakeLists.txt`).

**Interfaces:**
- Produces:
```cpp
// recurrence_utils.h
// Decodes a virtual occurrence id (-(series.id*1'000'000 + julianDay)) back to its occurrence.
std::optional<DuckEvent> virtualOccurrenceForId(pcm::database::Database &db, int64_t virtualId);
// call_event_resolver.h
namespace pcm::calltranscription {
class CallEventResolver {
 public:
  // materialise persists a virtual occurrence as a real event (production: QTimelineModel::addEvent
  // so published series get their schedule bookkeeping) and returns the new id or <=0.
  using Materialise = std::function<int64_t(const pcm::database::DuckEvent &)>;
  CallEventResolver(std::shared_ptr<pcm::database::Database> db, Materialise materialise);
  // id > 0: returned as is. id < 0: decode, reuse an already materialised occurrence
  // (get_event_by_series_occurrence), otherwise materialise, then link the series' client
  // (add_event_client) when the series has client_id. Returns std::nullopt if the series or
  // occurrence no longer exists or materialisation fails. Idempotent: two calls for the same
  // virtual id create exactly one event.
  std::optional<int64_t> resolve(int64_t eventId);
};
}
```

- [ ] **Step 1: Failing tests:** `PositiveIdIsReturnedUnchanged`; `VirtualIdMaterialisesOneEventWithSeriesFields` (create a weekly series with client, compute a virtual id the same way `recurrence_utils.cpp:221` does, resolve → event exists, `series_id` and `original_occurrence_start` set, name/duration copied, `is_virtual_occurrence` false on the stored row); `ResolveTwiceCreatesSingleEvent`; `ClientIsLinkedToMaterialisedEvent` (`get_client_by_event` matches series client); `AlreadyMaterialisedOccurrenceIsReused` (materialised by hand first); `UnknownSeriesReturnsNullopt`; `OccurrenceThatNoLongerExistsReturnsNullopt` (recurrence rule changed so that day is not an occurrence, or the occurrence is an exception); `MaterialiseFailureReturnsNullopt` (callback returns 0 → no `add_event_client`); and `VirtualOccurrenceForIdRoundTrip` in the same file for the helper. Use the temp-DB fixture pattern.
- [ ] **Step 2:** FAIL. **Step 3:** Implement. `virtualOccurrenceForId`: `series = id/1'000'000` (positive of `-id`), `julian = (-id) % 1'000'000`; load the series, expand its occurrences for that local calendar day (`QDate::fromJulianDay`, same helpers used at `recurrence_utils.cpp:213-223`: `seriesOccurrences(db, series, rangeStart, rangeEnd)`) and `buildVirtualOccurrence` for the one whose recomputed virtual id equals the input; skip occurrences present in `get_event_series_exceptions_for_range`. In `resolve`, call `materialise` with the virtual event after setting `eventDetails.id = -1` (mirror `event_info.cpp:~815`).
- [ ] **Step 4:** PASS. **Step 5: Commit** `feat(transcription): materialise recurring occurrence for a call (#118)`.

---

### Task 7: Application wiring (startup recovery + service plumbing)

**Files:** Modify `src/app/application.{h,cpp}`, `src/CMakeLists.txt`/`src/app/CMakeLists.txt` (link `Sessio_call_transcription`). No UI yet (phase 4).

- [ ] **Step 1:** After `mDb = std::make_shared<Database>(mConf)` (application.cpp ~295) and before any call page/session exists, call and log the counts (no PII):
```cpp
const auto interrupted = mDb->finalize_interrupted_transcripts();
const auto orphans = mDb->purge_orphan_transcripts();
if (interrupted > 0 || orphans > 0)
  qCInfo(logApplication) << "Transcript recovery: interrupted=" << interrupted << "orphans=" << orphans;
```
(use the existing logging category/macros in that file).
- [ ] **Step 2:** Keep `timelineModel` reachable: promote the local at application.cpp:326 to a member (`QPointer<QTimelineModel> mTimelineModel`), and add a member `std::unique_ptr<pcm::calltranscription::CallEventResolver> mCallEventResolver` constructed after the model with `materialise = [this](const DuckEvent &e){ return mTimelineModel ? mTimelineModel->addEvent(e, true) : 0; }`. Expose a private helper `std::optional<int64_t> Application::resolveCallEvent(int64_t eventId)` returning `mCallEventResolver->resolve(eventId)`; phase 4 calls it when the user starts transcription.
- [ ] **Step 3:** Build target `Sessio` (and `Sessio_app`); run the whole fast suite: `ctest --test-dir build-tr -LE models --output-on-failure`. Fix regressions. No new test file; behaviour is covered by Tasks 2, 4, 6 and DB tests (`finalize_interrupted_transcripts`/`purge_orphan_transcripts` have tests from phase 2).
- [ ] **Step 4:** Update `docs/superpowers/specs/2026-10-06-call-transcription-design.md` section on the session layer only if the implemented signatures differ from it (keep the doc truthful). **Step 5: Commit** `feat(app): recover interrupted transcripts and wire call event resolver (#118)`.

---

## Self-Review

- **Spec coverage:** audio taps (T1), no sink without session (T1/T4 tests), mic mute respected (T1), model off GUI thread (T4 `ModelLoadRunsOffTheGuiThread`, executor design), dedicated writer (T3) with private connection (T2), consent gate + revoke (T4), call-clock offsets (T4), auto-stop (T4), occurrence materialisation in the first full version (T6), startup recovery (T7). UI, consent dialog, settings, transcript page, translations, version/CHANGELOG are phases 4–5.
- **Placeholder scan:** exact field names of `LocateResult` and the schedule-transaction test hook are looked up by the implementer from named files; everything else is specified.
- **Type consistency:** `AudioSink::onAudio(QString, const int16_t*, size_t, int)`, `EngineFactory`, `SessionState`, `PhraseWriter::Store/Written`, `CallEventResolver::resolve(int64_t)` are used identically across tasks.
