# Call Transcription — Interface (Phase 4) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the practitioner the user interface for live call transcription: a "Transcribe" control in the call, a consent dialog, a "Notes | Transcript" switcher in the call side panel, a transcript review/edit window opened from the event, and a settings section. No new engine or storage work: everything sits on phases 1–3.

**Architecture:** A new static library `Sessio_transcription_ui` (`src/pages/transcript_page/`) holds the widgets (`TranscriptionConsentDialog`, `TranscriptPanel`, `CallSidePanel`, `TranscriptDialog`, `TranscriptionSettingsPanel`) and the glue object `CallTranscriptionController`. Widgets are passive: they get data through setters and report intent through signals, so they are tested without a database or a call. The controller (QObject, GUI thread) owns the call-time flow: resolve the event, ask for consent, create/stop/revoke a `TranscriptionSession`, feed the panel, and ask the user what to do after a revoke; every dialog and lookup is an injected `std::function`, so tests drive it with fakes. `CallsPage` stays free of database dependencies: it only gains signals that expose the current `VideoSession`, and `CallPage` gains a generic "Transcribe" control-bar button.

**Tech Stack:** C++20, Qt 6 Widgets, Qlementine (`SegmentedControl`), GoogleTest + Qt Test (`QTest`, `QSignalSpy`) with `QT_QPA_PLATFORM=offscreen`, phase 1–3 libraries.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-10-06-call-transcription-design.md`, sections *Lifecycle* and *UI* are binding. Phase 3 code (`src/call_transcription/`) is the API: `TranscriptionSession` (`start(eventId, consentScope)`, `stop()`, `revoke()`, signals `stateChanged`, `phraseAdded(DuckTranscriptPhrase)`, `delayedChanged(bool)`, `failed(QString)`, `trackFailed(QString)`, `finished(qint64)`, `revoked(qint64)`; states `Idle, Loading, Recording, Stopping, Finished, Failed`), `makeProductionEngineFactory`, `transcriptionModelsAvailable`, `CallEventResolver`, `Application::resolveCallEvent`.
- Consent scope string stored with every transcript: `live_local_v1`. Model id: `gigaam-v3-rnnt`.
- Consent is per session and explicit: the start button of the consent dialog stays disabled until the box "The client has given consent to transcribing this session" is ticked. No session is ever started without that dialog having been accepted in the same flow.
- The speech model is never loaded or run on the GUI thread (already guaranteed by `TranscriptionSession`; never call engine or model code from widgets or the controller).
- Database calls `set_transcript_status`, `revoke_transcript_consent`, `delete_transcript`, `delete_all_transcripts` require that no session is writing to that transcript: the UI must disable or reject these while the transcript's status is `recording` / a session is active.
- Call-UI module boundary: `src/pages/calls_page` must not depend on `Database`, `ClientNotesPage`, or any transcription library. Client mode (no specialist) has no transcription UI at all.
- All user-visible strings use `tr()`; Russian and English translations are produced in Task 8 (`translation/app_ru.ts`, `translation/app_en.ts`, no `type="unfinished"` entries). Do not edit `.ts` files in other tasks.
- Raw failure reasons from `TranscriptionSession` (`failed(QString)`) are English diagnostics and must never be shown to the user as is: the controller maps them to translated texts.
- Logs must not contain phrase text, speaker names or meeting URLs.
- Style: C++20, two-space indent, `PascalCase` classes, `camelCase` Qt methods, `public:`/`private:` at column 0, `} // namespace` with one space. Widgets follow the surrounding code (QSS only where neighbours use it; icons are self-drawn in `src/widgets/call_control_icons.cpp`). Run `git diff --check` before committing.
- Build (host, prebuilt vcpkg — never reconfigure with different settings, never let vcpkg rebuild): `distrobox-host-exec cmake --build build-tr --target <target> --parallel 3`; tests `distrobox-host-exec ctest --test-dir build-tr -R <regex> --output-on-failure` (gtest binaries need `QT_QPA_PLATFORM=offscreen`). Building *all* targets fails at link for six unrelated livekit/libcurl test targets; build only what you need. Never `pkill -f`, never `git stash`.
- GUI smoke runs (if any) only through `scripts/run-dev-isolated.sh`, never the raw binary.
- TDD is mandatory: write the test, run it and see it fail for the expected reason, then implement.

## File Structure

- Create `src/pages/transcript_page/` (library `${PROJECT_NAME}_transcription_ui`, added from the root `CMakeLists.txt` inside the `SESSIO_TRANSCRIPTION_ENABLED` block, next to `src/call_transcription`):
  - `transcription_consent_dialog.{h,cpp}`
  - `transcript_panel.{h,cpp}`
  - `call_side_panel.{h,cpp}`
  - `transcript_dialog.{h,cpp}`
  - `transcription_settings_panel.{h,cpp}`
  - `call_transcription_controller.{h,cpp}`
  - `failure_text.{h,cpp}` (maps raw reasons to translated text)
  - `CMakeLists.txt`
- Modify `src/widgets/app_settings.{h,cpp}` (flag), `src/widgets/call_control_icons.{h,cpp}` (icon), `src/pages/calls_page/{call_page,calls_page}.{h,cpp}` (button, session signals), `src/pages/event_info_page/{qevent_details_widget,event_info}.{h,cpp}` (Transcript button + lookup + reload slot), `src/app/{settings_dialog,application}.{h,cpp}`, `src/app/CMakeLists.txt`.
- Tests (all under `test/`, targets guarded by `if(SESSIO_TRANSCRIPTION_ENABLED)` except the two that do not need the module): `transcription_consent_dialog_tests.cpp`, `transcript_panel_tests.cpp`, `call_side_panel_tests.cpp`, `transcript_dialog_tests.cpp`, `transcription_settings_panel_tests.cpp`, `call_transcription_controller_tests.cpp`, `failure_text_tests.cpp`; extend `test/call_page_tests.cpp` and the app-settings tests (find them with `grep -n app_settings test/CMakeLists.txt`).

---

### Task 1: Setting, control-bar button and call-session signals

**Files:**
- Modify: `src/widgets/app_settings.{h,cpp}`, `src/widgets/call_control_icons.{h,cpp}`, `src/pages/calls_page/call_page.{h,cpp}`, `src/pages/calls_page/calls_page.{h,cpp}`, `test/call_page_tests.cpp`, the app-settings test file, `test/CMakeLists.txt` if a target must be extended.

**Interfaces:**
- Produces:
```cpp
// app_settings.h
bool transcriptionEnabled();               // default true
void setTranscriptionEnabled(bool enabled);

// call_control_icons.h
[[nodiscard]] QIcon transcriptIcon(bool active);   // self-drawn (speech-lines glyph), accent when active

// CallPage (no dependency on transcription code)
void setTranscribeButtonVisible(bool visible);     // false by default; no button at all when false
void setTranscribeButtonState(bool active, const QString &tooltip);  // active = session running
void openSidePanel();                              // expands the side panel (no-op without toggle)
signals: void transcribeRequested();               // button clicked (also when active: controller decides)

// CallsPage
signals:
  void callSessionStarted(pcm::video::VideoSession *session);  // after the session object exists, before join
  void callSessionEnded();                                      // session reset / call ended or failed
void setTranscribeButtonVisible(bool);   // forwards to CallPage
void setTranscribeButtonState(bool active, const QString &tooltip);
void openSidePanel();
signals: void transcribeRequested();     // relayed from CallPage
```

- [ ] **Step 1: Failing tests.** (a) app-settings test: default true, round trip, survives `QSettings` re-read (follow the neighbouring tests for the in-memory/temp settings pattern). (b) `call_page_tests.cpp`: `TranscribeButtonHiddenByDefault`; `TranscribeButtonShownWhenEnabledAndEmitsSignalOnClick` (find it with `findChild<QToolButton*>("transcribeButton")`, `QSignalSpy` on `transcribeRequested`); `TranscribeButtonStateChangesTooltipAndCheckedLook`; `OpenSidePanelExpandsToggle` (after `setSidePanelToggleVisible(true)` and a panel widget, `openSidePanel()` makes the panel host visible; without a toggle it does nothing). (c) a `CallsPage` test (extend the existing calls-page tests, find with `grep -ln CallsPage test/*.cpp`; use `setVideoProviderFactoryForTesting` with `FakeVideoProvider`): `CallSessionStartedEmittedOnJoinAndEndedOnLeave` and `TranscribeRequestedIsRelayed`.
- [ ] **Step 2:** Build and run → FAIL.
- [ ] **Step 3: Implement.** `transcriptionEnabled` uses the same `QSettings` mechanism and key naming as the neighbours (key `transcription/enabled`). Icon: draw three horizontal rounded lines of different length inside a speech-bubble outline with `QPainter`, colour from palette like the other icons (read `call_control_icons.cpp` first and mimic `notesIcon()`/`microphoneIcon`). Button: `QToolButton` named `transcribeButton` in the control bar next to the devices button, same QSS family as `devicesButton` (opaque fills, `:checked` look for active); tooltip/accessible name `tr("Transcribe")` when idle and the text passed to `setTranscribeButtonState` when active. `CallsPage::startJoin` emits `callSessionStarted(mSession.get())` right after the session is constructed and attached; wherever `mSession.reset()` runs (and when the call ends/fails) emit `callSessionEnded()` exactly once per started session.
- [ ] **Step 4:** Run the extended tests and the existing `CallPage|CallsPage|AppSettings` tests → PASS (the call page tests need `QT_QPA_PLATFORM=offscreen`).
- [ ] **Step 5: Commit** `feat(calls): transcribe button, call session signals and setting (#118)`.

---

### Task 2: Consent dialog and failure text

**Files:** Create `src/pages/transcript_page/{CMakeLists.txt,transcription_consent_dialog.h,transcription_consent_dialog.cpp,failure_text.h,failure_text.cpp}`, `test/transcription_consent_dialog_tests.cpp`, `test/failure_text_tests.cpp`; modify root `CMakeLists.txt` (add the subdirectory inside the existing `SESSIO_TRANSCRIPTION_ENABLED` block) and `test/CMakeLists.txt`.

**Interfaces:**
- Produces:
```cpp
// transcription_consent_dialog.h
class TranscriptionConsentDialog final : public QDialog {
  Q_OBJECT
 public:
  explicit TranscriptionConsentDialog(QWidget *parent = nullptr);
  [[nodiscard]] bool consentChecked() const;
  [[nodiscard]] QPushButton *startButton() const;   // object name "consentStartButton"
};
// failure_text.h
namespace pcm::transcriptionui {
// Maps a raw diagnostic from TranscriptionSession::failed()/trackFailed to a translated,
// user-facing sentence. Unknown input maps to a generic sentence. Never returns the raw text.
QString userFacingFailure(const QString &raw);
}
```
Library target `${PROJECT_NAME}_transcription_ui` (STATIC, AUTOMOC ON): links `Qt6::Widgets`, `qlementine` (include dir `${qlementine_SOURCE_DIR}/lib/include`, same as `client_notes_page`), `${PROJECT_NAME}_database`, `${PROJECT_NAME}_widgets`, `${PROJECT_NAME}_call_transcription`; `target_include_directories(... INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})`. Later tasks append sources.

- [ ] **Step 1: Failing tests.** Dialog: `StartButtonDisabledUntilConsentBoxChecked`; `UncheckingDisablesStartAgain`; `AcceptingWithoutConsentIsNotPossible` (calling `startButton()->click()` while disabled does not accept; `accept()` is only reachable through the enabled button); `TextMentionsLocalProcessingNoAudioStorageRevocationAndEditing` (the dialog's label text contains four distinct statements — assert on four labels' object names/`accessibleName`, not on translated wording, e.g. object names `consentLocalNote`, `consentNoAudioNote`, `consentRevokeNote`, `consentEditNote` all exist and are non-empty); `CancelRejects`. Failure text: known raw reasons from phase 3 (`"Could not save the transcript"`, the engine-factory errors that start with `"Model file not found"`, `"Voice activity model could not be created"`, anything else) map to distinct non-empty strings, never contain the raw input, and the generic fallback is used for unknown text. Read `src/call_transcription/engine_factory.cpp` and `transcription_session.cpp` for the exact raw strings.
- [ ] **Step 2:** FAIL. **Step 3: Implement.** Dialog body (all `tr()`): title "Transcribe this session"; explanatory paragraphs split across the four labelled notes: (1) the session is converted to text on this computer; (2) audio is not stored and not sent anywhere; (3) the client can withdraw consent at any moment, and transcription stops immediately; (4) the text is saved as a draft, can be edited and deleted. Check box `tr("The client has given consent to transcribing this session")`; buttons `tr("Start transcription")` (object name `consentStartButton`, disabled initially) and `tr("Cancel")`. **Step 4:** PASS. **Step 5: Commit** `feat(transcription-ui): consent dialog and failure texts (#118)`.

---

### Task 3: TranscriptPanel

**Files:** Create `src/pages/transcript_page/transcript_panel.{h,cpp}`, `test/transcript_panel_tests.cpp`; modify the library CMake and `test/CMakeLists.txt`.

**Interfaces:**
- Consumes: `pcm::calltranscription::SessionState`, `DuckTranscriptPhrase` (global namespace, `src/database/schema.hpp`).
- Produces:
```cpp
class TranscriptPanel final : public QWidget {
  Q_OBJECT
 public:
  explicit TranscriptPanel(QWidget *parent = nullptr);
  void setState(pcm::calltranscription::SessionState state);   // drives status line + button availability
  void setStartAvailable(bool available, const QString &reason);  // false: Start disabled, reason shown (no event / models missing / disabled)
  void addPhrase(const DuckTranscriptPhrase &phrase);          // appends a row, keeps scrolled to the end unless the user scrolled up
  void setDelayed(bool delayed);                               // shows/hides the "falling behind" hint
  void setNotice(const QString &text);                         // non-fatal notice line (e.g. trackFailed), empty hides
  void setError(const QString &userFacingText);                // error banner, empty hides
  void clearPhrases();
  [[nodiscard]] int phraseCount() const;
 signals:
  void startRequested();
  void stopRequested();
  void revokeRequested();
};
```
Behaviour: a header with a "Local" badge and a status line (Idle: "Transcription is off"; Loading: "Loading speech model…"; Recording: "Transcription running"; Stopping: "Finishing…"; Finished: "Transcript saved as a draft"; Failed: "Transcription stopped"). Phrases show `mm:ss` (from `start_ms`, `h:mm:ss` over an hour), speaker (`speaker_name`, falling back to "You"/"Participant" by `track_role`) and text; the practitioner role is visually distinct. While Recording, a "listening…" row is shown at the end. Footer text "Audio is not stored". Buttons: **Start** (visible in Idle/Finished/Failed, enabled only if `setStartAvailable(true,…)`), **Stop** (Loading/Recording), **Revoke consent** (Loading/Recording/Stopping). Buttons emit signals only; they never change state themselves.

- [ ] **Step 1: Failing tests:** `ButtonsFollowState` (table over every `SessionState`: which of start/stop/revoke are visible and enabled); `StartDisabledWithReasonShown`; `AddPhraseShowsTimeSpeakerAndText` (find row widgets by object names `phraseTime`, `phraseSpeaker`, `phraseText`; check `65000 ms → "01:05"`, `3'725'000 ms → "1:02:05"`); `SpeakerFallbackByRole`; `PhraseCountAndClear`; `ListeningRowOnlyWhileRecording`; `DelayedHintToggles`; `ErrorBannerAndNoticeShowAndHide`; `ButtonsEmitOnlySignals` (state unchanged after clicks); `KeepsBottomWhenAtBottomButNotWhenScrolledUp` (scrollbar value assertions, offscreen platform; send 200 phrases).
- [ ] **Step 2:** FAIL. **Step 3:** Implement with a `QScrollArea` of row widgets inside the panel (a `QListWidget` is acceptable if it keeps per-row styling simple; pick one and keep object names above). Accessible names on every button. **Step 4:** PASS. **Step 5: Commit** `feat(transcription-ui): transcript panel (#118)`.

---

### Task 4: CallSidePanel (Notes | Transcript switcher)

**Files:** Create `src/pages/transcript_page/call_side_panel.{h,cpp}`, `test/call_side_panel_tests.cpp`; modify library CMake, `test/CMakeLists.txt`.

**Interfaces:**
- Produces:
```cpp
class CallSidePanel final : public QWidget {
  Q_OBJECT
 public:
  // Takes ownership of neither widget's lifetime beyond reparenting into its stack.
  CallSidePanel(QWidget *notesPage, QWidget *transcriptPanel, QWidget *parent = nullptr);
  void setTranscriptionAvailable(bool available);   // false: switcher hidden, only Notes shown
  void showNotes();
  void showTranscript();
  [[nodiscard]] bool transcriptShown() const;
 signals:
  void pageChanged(bool transcriptShown);
};
```
Uses `oclero::qlementine::SegmentedControl` with items `tr("Notes")` and `tr("Transcript")` (same API as `settings_dialog.cpp:259`; `setItemsShouldExpand(true)`) above a `QStackedWidget` (page 0 notes, page 1 transcript). Default page is Notes.

- [ ] **Step 1: Failing tests:** `DefaultsToNotes`; `SwitcherSwitchesPages` (click via `setCurrentIndex` on the control, assert stack index and `pageChanged`); `ShowTranscriptSelectsThePage`; `HidingTranscriptionHidesSwitcherAndForcesNotes` (even if the transcript page was selected); `ReenablingKeepsNotesSelected`; `BothWidgetsAreReparentedIntoThePanel`.
- [ ] **Step 2:** FAIL. **Step 3:** Implement. **Step 4:** PASS (needs `QApplication` main like `call_page_tests.cpp`, Qlementine include dir in the test target as in `test/CMakeLists.txt:194`). **Step 5: Commit** `feat(transcription-ui): call side panel switcher (#118)`.

---

### Task 5: CallTranscriptionController

**Files:** Create `src/pages/transcript_page/call_transcription_controller.{h,cpp}`, `test/call_transcription_controller_tests.cpp`; modify library CMake, `test/CMakeLists.txt`.

**Interfaces:**
- Consumes: Tasks 2–4 (`TranscriptPanel`, `CallSidePanel`, `userFacingFailure`), phase 3 (`TranscriptionSession`, `EngineFactory`, `SessionState`), `pcm::video::VideoSession`/`VideoProvider`, `Database`.
- Produces:
```cpp
namespace pcm::transcriptionui {
struct ControllerHooks {
  // All run on the GUI thread; injected so tests need no dialogs or application.
  std::function<std::optional<int64_t>(int64_t callEventId)> resolveEvent;  // Application::resolveCallEvent
  std::function<bool()> askConsent;                                         // shows TranscriptionConsentDialog
  enum class RevokeChoice { DeleteRecorded, KeepAsDraft };
  std::function<RevokeChoice()> askRevokeChoice;
  std::function<pcm::calltranscription::EngineFactory()> engineFactory;     // production: makeProductionEngineFactory
  std::function<bool()> modelsAvailable;
  std::function<bool()> transcriptionEnabled;                               // app setting
  std::function<void(int64_t eventId)> eventMaterialised;                   // timeline reload hook
};
class CallTranscriptionController final : public QObject {
  Q_OBJECT
 public:
  CallTranscriptionController(std::shared_ptr<pcm::database::Database> db, TranscriptPanel *panel,
                              CallSidePanel *sidePanel, ControllerHooks hooks, QObject *parent = nullptr);
  ~CallTranscriptionController() override;     // never blocks the GUI (the session destructor is already bounded)
  // Called when CallsPage reports a call. `callEventId` is the calendar entry id (maybe negative/virtual)
  // or nullopt for a call without a known event (join by code).
  void attachCall(pcm::video::VideoSession *session, std::optional<int64_t> callEventId);
  void detachCall();                           // call ended: session auto-stops itself; panel keeps showing the result
  void refreshAvailability();                  // re-evaluates enabled/models/event and updates panel + side panel
  void shutdown();                             // application quit: hard-stop, bounded
 public slots:
  void onTranscribeRequested();                // control-bar button or panel Start
 signals:
  void transcribeButtonState(bool active, QString tooltip);   // for CallsPage::setTranscribeButtonState
  void requestOpenTranscriptTab();             // CallsPage::openSidePanel + CallSidePanel::showTranscript
  void callTranscriptReady(int64_t eventId, int64_t transcriptId);  // after finish: transcript is available for the page
};
}
```
Flow (binding):
1. `onTranscribeRequested` while no session or a finished one: require `transcriptionEnabled()`, `modelsAvailable()`, an attached call with a `callEventId`; otherwise update the panel notice and return. Ask `askConsent()`; on reject do nothing (no row created). `resolveEvent(callEventId)`; if null show a translated error. If the id was negative and a real id came back, call `eventMaterialised(realId)`. Create a `TranscriptionSession(db, session->provider(), hooks.engineFactory())`, connect its signals to the panel (`stateChanged→setState`, `phraseAdded→addPhrase`, `delayedChanged→setDelayed`, `failed→setError(userFacingFailure(raw))`, `trackFailed→setNotice`), `start(realId, "live_local_v1")`; `false` → error text. Emit `requestOpenTranscriptTab()` and `transcribeButtonState(true, …)`.
2. Panel `stopRequested` → `session->stop()`. `finished(id)` → state text, `transcribeButtonState(false, …)`, `callTranscriptReady(eventId, id)`.
3. Panel `revokeRequested` → `session->revoke()`; on `revoked(id)` call `askRevokeChoice()`: `DeleteRecorded` → `db->delete_transcript(id)` (the session already joined engine+writer; this runs only after `revoked`) and `panel->clearPhrases()`; `KeepAsDraft` → leave the row (consent already marked revoked) and keep the phrases shown.
4. `detachCall` (call ended): the session stops itself; the controller keeps the session object until `finished`/`revoked`/`failed` arrives, then drops it; `shutdown()` hard-stops via destroying the session.
5. A new start while the previous session object is still finishing is rejected with a notice.
6. `refreshAvailability` computes `setStartAvailable(false, reason)` with translated reasons: transcription disabled in settings ("Transcription is turned off in Settings"), models missing ("Speech models are not installed"), no known event ("Open the call from a calendar event to transcribe it"), otherwise available; and hides the transcription switcher (`CallSidePanel::setTranscriptionAvailable`) when disabled or models missing. The control-bar button visibility is decided by the owner (Task 7) from the same result.

- [ ] **Step 1: Failing tests** (real temp `Database` like `test/database_transcript_tests.cpp`, `FakeVideoProvider` + `VideoSession` as in `test/video_session_tests.cpp`, the fake-engine factory pattern from `test/transcription_session_tests.cpp` — copy its fake recogniser/VAD helpers from `test/transcription/transcription_test_support.h`, hooks as lambdas recording calls): `ConsentRejectedCreatesNoTranscriptAndNoSink`; `ConsentAcceptedCreatesRecordingTranscriptWithScopeAndEvent`; `VirtualEventIdIsResolvedAndTimelineHookCalled`; `UnresolvableEventShowsErrorAndStartsNothing`; `DisabledSettingBlocksStartWithReason`; `MissingModelsBlockStartWithReason`; `CallWithoutEventBlocksStart`; `PhrasesReachThePanelAndTheDatabase`; `StopFinishesAsDraftAndEmitsReady`; `RevokeAskedChoiceDelete` (transcript and phrases gone from the DB, panel cleared); `RevokeAskedChoiceKeep` (row stays with `consent_revoked_at` set); `FailureIsShownAsUserFacingText` (the raw reason never appears in any panel label); `CallEndedStopsSessionAndKeepsResultVisible`; `SecondStartWhileFinishingIsRejected`; `ShutdownDoesNotBlock` (slow recogniser, returns < 200 ms).
- [ ] **Step 2:** FAIL. **Step 3:** Implement. **Step 4:** PASS; run `--gtest_repeat=20` once. **Step 5: Commit** `feat(transcription-ui): call transcription controller (#118)`.

---

### Task 6: TranscriptDialog (review and edit) and the event entry point

**Files:** Create `src/pages/transcript_page/transcript_dialog.{h,cpp}`, `test/transcript_dialog_tests.cpp`; modify `src/pages/event_info_page/{qevent_details_widget,event_info}.{h,cpp}`, library CMake, `test/CMakeLists.txt`, `src/pages/event_info_page/CMakeLists.txt` only if a dependency is needed (the event page must NOT depend on the transcription library: it gets a count through an injected function and re-emits a signal).

**Interfaces:**
- Produces:
```cpp
// transcript_dialog.h
class TranscriptDialog final : public QDialog {
  Q_OBJECT
 public:
  TranscriptDialog(std::shared_ptr<pcm::database::Database> db, int64_t eventId,
                   const QString &title, QWidget *parent = nullptr);
  [[nodiscard]] int transcriptCount() const;     // transcripts of the event; the newest is shown, others via a combo
 signals:
  void transcriptsChanged();                     // status change or deletion (the event page refreshes its button)
};
// QEventInfoPage
void setTranscriptCountProvider(std::function<int(int64_t eventId)> provider);  // 0 hides the button
void reloadSelectedDay();                                                      // public slot: onSelectedDayChanged(mSelectedDate)
signals: void openTranscriptRequested(int64_t eventId);
// QEventDetailsWidget
void setTranscriptCount(int count);              // shows a "Transcript" button in inspector mode when > 0
signals: void openTranscriptRequested();
```
Dialog behaviour: header with event title, phrase count, model id, a status chip (`Draft` / `Reviewed` / `Recording`); buttons **Mark reviewed** (calls `set_transcript_status(id,"reviewed")`; hidden when already reviewed; disabled when `recording`), **Delete transcript** (confirm via `QMessageBox`, `delete_transcript`, closes; disabled when `recording`). Phrase list: time, speaker, text; per-phrase **Edit** (inline `QPlainTextEdit` with Save/Cancel; `update_transcript_phrase_text`; an "edited" chip appears; empty text is refused), **Delete** (`delete_transcript_phrase`, no confirm needed). While a transcript is `recording`, editing and deleting are disabled and a note says it is still being recorded; the dialog does not refresh live (user reopens). Several transcripts for one event (several joins): a combo above the header, newest first, labelled by start time.

- [ ] **Step 1: Failing tests** (temp DB with an event + transcript + phrases built through `Database` methods): `ShowsNewestTranscriptWithPhrasesInOrder`; `ComboListsEveryTranscriptNewestFirst`; `MarkReviewedUpdatesStatusAndChip`; `RecordingTranscriptDisablesEditDeleteAndReview`; `EditingAPhraseSavesTextAndMarksEdited` (DB row has the new text and `edited`); `EmptyEditIsRefused`; `DeletingAPhraseRemovesItFromDbAndList`; `DeleteTranscriptAskesConfirmationAndRemovesEverything` (use an injectable confirmation hook `std::function<bool(const QString&)> setConfirmHook(...)` defaulting to `QMessageBox`; tests set it to accept/reject); `TranscriptsChangedEmittedOnReviewAndDelete`. Event page tests (extend the existing event-info tests, find with `grep -ln EventInfoPage test/*.cpp`): `TranscriptButtonHiddenWhenCountIsZero`, `TranscriptButtonShownAndRelaysEventId`, `ReloadSelectedDayReloadsFromModel` (smallest observable effect available in the existing fixture).
- [ ] **Step 2:** FAIL. **Step 3:** Implement. In `showInspector` (event_info.cpp ~425) set `widget->setTranscriptCount(provider ? provider(event.id) : 0)` for real events (positive id) and 0 otherwise; connect the details widget's `openTranscriptRequested` to the page's signal with the event id. **Step 4:** PASS. **Step 5: Commit** `feat(transcription-ui): transcript review dialog and event entry point (#118)`.

---

### Task 7: Settings section and application wiring

**Files:** Create `src/pages/transcript_page/transcription_settings_panel.{h,cpp}`, `test/transcription_settings_panel_tests.cpp`; modify `src/app/{settings_dialog,application,main_window}.{h,cpp}` (as needed), `src/app/CMakeLists.txt`, library CMake, `test/CMakeLists.txt`.

**Interfaces:**
- Produces:
```cpp
class TranscriptionSettingsPanel final : public QWidget {
  Q_OBJECT
 public:
  explicit TranscriptionSettingsPanel(QWidget *parent = nullptr);
  void setEnabledState(bool enabled);                      // switch position (no signal)
  void setModelInfo(const QString &modelId, bool installed);
  void setTranscriptCount(int count);                      // label "N transcripts stored"; delete button disabled at 0
  void setDeleteAllAllowed(bool allowed, const QString &reason);  // false while a session is recording
 signals:
  void enabledToggled(bool enabled);
  void deleteAllRequested();                               // the owner confirms and deletes
};
```
Panel text: switch "Enable call transcription"; model line "Model: gigaam-v3-rnnt — installed / not installed"; note "Transcription runs on this computer. Audio is never stored. Text is saved in the local database next to your other data and included in backups."; count label; button "Delete all transcripts…".

Application wiring (binding, no new test file beyond the panel; verify by building `Sessio`, `ctest -LE models`, plus the controller and widget tests):
1. **Settings dialog**: add a section item `tr("Transcription")` (key `transcription`) after "LiveKit", with a scroll page hosting `TranscriptionSettingsPanel`; wire `enabledToggled → app_settings::setTranscriptionEnabled`, `deleteAllRequested → QMessageBox::question` then `mDb->delete_all_transcripts()` and refresh the count; the dialog gets `std::function<bool()> transcriptionActive` (disable delete-all while a session records) and the database pointer through its existing constructor/setters (follow how other sections get the database; do not make the dialog depend on the call page).
2. **Application** (`#ifdef SESSIO_CALL_TRANSCRIPTION`): replace the current `eventKnownForCurrentCall` lambda's panel creation so the notes page is wrapped: create `TranscriptPanel`, `CallSidePanel(mCallNotesPanel, transcriptPanel)`, and `CallTranscriptionController` once, lazily (same lifetime rules as `mCallNotesPanel` today; `callsPage->setSidePanelWidget(sidePanel)`). Hooks: `resolveEvent = resolveCallEvent`, `askConsent` shows `TranscriptionConsentDialog` (parent main window) and returns `exec()==Accepted`, `askRevokeChoice` shows a `QMessageBox` with "Delete what was recorded" / "Keep as draft", `engineFactory = makeProductionEngineFactory(QCoreApplication::applicationDirPath().toStdString(), qEnvironmentVariable("SESSIO_MODELS_DIR").toStdString())` (check `model_locator.h` for the exact app-dir convention and the env name), `modelsAvailable = transcriptionModelsAvailable(...)`, `transcriptionEnabled = app_settings::transcriptionEnabled`, `eventMaterialised = [page](int64_t){ page->reloadSelectedDay(); }`.
3. Connect `CallsPage::callSessionStarted(session)` → `controller->attachCall(session, <the event id the page last announced>)` — the id comes from `eventKnownForCurrentCall`, so store it in an `std::optional<int64_t> mCurrentCallEventId` set by that signal and cleared (reset to nullopt) when a call starts without one: connect order matters, `callSessionStarted` must fire with the id already known; if `CallsPage` emits `eventKnownForCurrentCall` before `callSessionStarted` keep that, otherwise fix the order in `CallsPage` and keep the existing calls-page tests green. `callSessionEnded` → `controller->detachCall()`. `CallsPage::transcribeRequested` and the panel's `startRequested` → `controller->onTranscribeRequested()`. Controller signals → `CallsPage::setTranscribeButtonState`, `CallsPage::openSidePanel` + `CallSidePanel::showTranscript`.
4. The control-bar button is shown (`CallsPage::setTranscribeButtonVisible(true)`) only in specialist mode, when `transcriptionEnabled()` and models are available, and a real call event is known; refresh it on `callSessionStarted`, on settings change and when the Settings dialog closes (`controller->refreshAvailability()` decides; expose its result via a `bool startAvailable()` accessor, add it to the controller if missing, with a test).
5. Event page: `page->setTranscriptCountProvider([db](int64_t id){ return int(db->get_transcripts_for_event(id).size()); })`; `openTranscriptRequested(eventId)` → `TranscriptDialog(mDb, eventId, title, mMainWindow)`.exec-less `show()` with `WA_DeleteOnClose`; refresh the inspector count after the dialog's `transcriptsChanged`.
6. `aboutToQuit` → `controller->shutdown()` (bounded; the phase-3 destructor never blocks long).
7. Client mode: none of this is created.

- [ ] **Step 1: Failing tests** for `TranscriptionSettingsPanel`: `SwitchReflectsStateWithoutSignal`; `TogglingEmitsEnabledToggled`; `ModelInfoShowsInstalledAndMissing`; `DeleteAllDisabledWithoutTranscripts`; `DeleteAllDisabledWhileRecordingWithReason`; `DeleteAllEmitsRequest`. Controller addition test `StartAvailableReflectsSettingModelsAndEvent`.
- [ ] **Step 2:** FAIL. **Step 3:** Implement the panel, then the wiring. **Step 4:** Build `Sessio_app` and `Sessio`; run `ctest -LE models` and compare with the known baseline (six livekit/libcurl link failures and six calendar pixel tests are pre-existing; nothing else may fail). **Step 5:** Optional smoke: `scripts/run-dev-isolated.sh` with the built binary and take no actions that touch real data; report what was and was not exercised (a real call is not possible in the sandbox). **Step 6: Commit** `feat(app): wire transcription UI, settings and transcript entry (#118)`.

---

### Task 8: Translations and string audit

**Files:** Modify `translation/app_ru.ts`, `translation/app_en.ts`.

- [ ] **Step 1:** `distrobox-host-exec cmake --build build-tr --target update_translations` (per AGENTS.md; the translation scan covers every CMake target including the new `Sessio_transcription_ui`; check that the new source files appear in the `.ts` context list). If the target is missing in `build-tr`, use the configured release build tree per AGENTS.md; do not reconfigure vcpkg.
- [ ] **Step 2:** Translate every new `type="unfinished"` entry in both files: Russian text for `app_ru.ts`, English (same as source) for `app_en.ts`. Use consistent terminology: транскрипция/расшифровка → «транскрипция»; «consent» → «согласие»; «draft» → «черновик»; «reviewed» → «проверено». Plural forms (`%n phrases`) need `numerusform` entries in Russian (one/few/many).
- [ ] **Step 3: Failing check first:** before translating, `grep -c 'type="unfinished"' translation/app_ru.ts translation/app_en.ts` must be > 0 (proves the audit is meaningful); after translating both must be 0.
- [ ] **Step 4:** Add a test or script check that fails on unfinished entries if one does not exist yet (CI already fails on them; confirm and do not duplicate).
- [ ] **Step 5:** Build `Sessio` (translations are compiled into resources) and verify no `lupdate`/`lrelease` warnings about the new strings. **Step 6: Commit** `feat(transcription-ui): Russian and English translations (#118)`.

---

## Self-Review

- **Spec coverage:** control-bar "Transcribe" button and its active state (T1), consent dialog with the gating checkbox and the four statements (T2), `TranscriptPanel` with phrases/speaker/time, "listening", local badge, no-audio footer, Stop and Revoke (T3), `CallSidePanel` Notes|Transcript with default Notes and client mode untouched (T4, T7 step 7), consent → start → stop → revoke (with the delete/keep choice) → auto-stop at call end flow (T5), transcript page with status chip, mark reviewed, per-phrase edit/save/cancel/delete, "edited" chip, deletion, opened from the event info page (T6), settings section with enable switch, model information, data location, "Delete all transcripts" and the switch hiding the call button (T7), translations with no unfinished entries (T8). Occurrence materialisation, startup recovery and the stop-on-quit hook from the phase-3 deferred list are consumed in T5/T7 (`eventMaterialised`, `reloadSelectedDay`, `shutdown`, failure text mapping, no raw reasons). Warning before deleting events that have transcripts, speaker-name staleness for participants present at join, packaging, CI tests and the version bump are phase 5 (see `.superpowers/sdd/2026-10-07-call-transcription-session/progress.md`).
- **Placeholder scan:** exact wording of translated texts beyond the listed English strings, widget geometry and QSS colours follow neighbouring widgets by instruction (named files); raw failure strings are looked up in named source files; no step says "later".
- **Type consistency:** `SessionState`, `DuckTranscriptPhrase`, `EngineFactory`, `ControllerHooks`, `userFacingFailure`, `transcribeButtonState(bool, QString)`, `setTranscribeButtonState(bool, const QString&)`, `callSessionStarted/Ended`, `openTranscriptRequested(int64_t)`, `reloadSelectedDay()` are named identically across tasks.
