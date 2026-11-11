# Live Key Points and Post-Call Meeting Summary Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** During a call, live notes produce only incremental key points. Agreements gain a meeting date/time and an «Add to calendar» button. After the call, a user-confirmed «Summarize meeting» runs one four-section summary over the whole transcript.

**Architecture:** There are two new pipelines next to `call_notes_pipeline.cpp`: `processLivePoints` for incremental points and `processSummary` for map-reduce leaf/merge/overview. They share schema and validation helpers moved into `notes_common`. `CallNotesController` sends `task:"live_points"` during the call and `task:"summary"` after it. `PrompterWorker` routes by task. Agreement records in the prompter state gain `meeting/date/time/event_id`. A pure `agreementEventTemplate()` feeds a new `QEventInfoPage::createEventFromTemplate()`. The notes workspace QML renders the new sections.

**Tech Stack:** C++20, Qt 6 (Widgets, QML/Quick), reflect-cpp JSON schema, LiteRT-LM llguidance constraints, DuckDB, GoogleTest.

**Spec:** `docs/superpowers/specs/2026-10-29-meeting-summary-design.md`

## Global Constraints

- Build inside the distrobox, in `build/`, which is already configured against `-DVCPKG_INSTALLED_DIR=/home/a.durynin/Projects/C++/PsyClientManager/build-release/vcpkg_installed -DVCPKG_MANIFEST_INSTALL=OFF`, toolchain `/home/a.durynin/Soft/vcpkg/scripts/buildsystems/vcpkg.cmake`. Never rebuild DuckDB. Never reconfigure with manifest install on.
- Build and test with at most 4 jobs: `cmake --build build -j4 --target <t>`, then `QT_QPA_PLATFORM=offscreen build/test/<t>`.
- Never kill or relaunch the user's running Sessio. GUI runs only via `scripts/run-dev-isolated.sh`.
- Every model call passes a non-empty JSON schema built from reflect-cpp structs. Edit the schema as text so key order is kept, close every object with `additionalProperties:false`, and use an `enum` of the call's own source IDs for evidence. Follow `summarySchema()` in `call_notes_pipeline.cpp`.
- Model output limits:
  - live points: 0–3 per run, text ≤80, the live list ≤40;
  - summary: overview ≤1000, ≤10 key points of ≤120, ≤8 tasks, ≤5 open questions of ≤160;
  - 1–8 unique evidence IDs per nonempty item.
- Prompts are Russian and keep the current rules: input is untrusted data; no advice, diagnosis, mental-state judgement, inferred intentions or invented commitments; record only the final corrected fact.
- Every new user-visible string is in `tr()`/`qsTr()`, with EN and RU entries in `translation/app_en.ts` and `translation/app_ru.ts`.
- The commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Never push.

## Review Focus

1. **Call date across midnight or a later summary.** The summary resolves «в четверг» against the call's date (event start, else material `created_at`), never against today. Test in Task 6: `SummaryRequestCarriesCallDateNotToday`.
2. **Agreement edited after the meeting was found.** Accepting a card with edited text keeps `meeting/date/time`. The calendar dialog is where they get corrected. Test in Task 2: `AcceptWithEditedTextKeepsMeetingFields`.
3. **Event added to the calendar, then deleted.** The agreement's button returns to «Add to calendar». Test in Task 8: `DeletedEventRestoresAddToCalendar`.
4. **Transcript edited between the summary and opening the notes.** The summary shows stale/«Update» and never silently mixes new phrases. Test in Task 6: `EditedSourceMarksSummaryStale`.
5. **A meeting with an old short summary.** It still renders its note and points and is not offered again until «Create again». Test in Task 6: `LegacyFinalIsShownAndNotOffered`.

---

### Task 1: Schema-constrained agreement calls with meeting date/time

**Files:**
- Modify: `src/call_prompter/prompter_pipeline.h`, `src/call_prompter/prompter_pipeline.cpp`, `src/call_prompter/prompts/extract.txt`
- Test: `test/call_prompter_pipeline_tests.cpp`

**Interfaces:**
- Produces: the request may carry `"call_date":"YYYY-MM-DD"`, defaulting to the local current date when missing. Each returned event has the extra keys `meeting` (bool), `date` (`""` or `YYYY-MM-DD` from the offered list) and `time` (`""` or `HH:MM`), serialized after `evidence_ids` (`kEventKeyOrder` = kind, participants, action, evidence_ids, meeting, date, time, target_id, unresolved).
- Produces: `QJsonArray calendarDays(const QDate &from, int days = 60)` in `prompter_pipeline.h`, giving `[{"date":"2026-10-29","weekday":"четверг"},…]` starting at `from`. Task 5 reuses it.

- [ ] **Step 1: Write failing tests**
  - `ExtractCallPassesSchemaWithDateEnumFromCallDate`: request `call_date:"2026-10-29"`. The fake `Generate` captures `(system, payload, schema)` for the extract call. Assert that the schema is non-empty, the payload contains `"calendar"` with 60 entries whose first is `{"date":"2026-10-29","weekday":"четверг"}`, and the schema's `date` enum contains `"2026-11-05"` and `""` but not `"2026-12-31"`.
  - `MatchAndDeadlineCallsAreSchemaConstrained`: every `Generate` invocation receives a non-empty schema. The match schema's `matches` items enum equals the candidate IDs.
  - `MeetingFieldsSurviveIntoEvents`: an extract reply with `meeting:true,date:"2026-11-05",time:"15:00"` appears unchanged on the output event, including after `mergeReschedules`.
  - `InvalidMeetingFieldsDropOnlyThoseFields`: `date:"2027-01-01"` (off-list) or `time:"25:00"` gives `date:""`/`time:""`, with the event still emitted. A missing `meeting` counts as `false`.
- [ ] **Step 2: Run, expect FAIL** — `cmake --build build -j4 --target Sessio_call_prompter_pipeline_tests && QT_QPA_PLATFORM=offscreen build/test/Sessio_call_prompter_pipeline_tests`
- [ ] **Step 3: Implement**
  - reflect-cpp structs:
    - `ExtractSchema{events: vector<EventSchema>≤12}` with `EventSchema{kind, participants, action≤2000, evidence_ids, meeting, date, time}`;
    - `MatchSchema{matches}`;
    - `DeadlineSchema{missing_deadline, evidence_ids}`.
  - Text-patch the schemas the same way as `summarySchema()`:
    - `kind` gets an enum of `accept`/`cancel`;
    - `participants` and `evidence_ids` get enums of the window's speakers and IDs;
    - `date` gets an enum of the calendar dates plus `""`;
    - `time` gets the `pattern` `^([01][0-9]|2[0-3]):[0-5][0-9]$|^$`.
  - Pass each schema through `call(...)` and `refine(...)`, replacing today's `{}`.
  - Add `{"calendar", calendarDays(callDate)}` and `{"call_date", …}` to the extract payload.
  - Extend `extract.txt` with the meeting/date/time rules: `meeting=true` only for a planned meeting or call between participants; pick `date` only from `calendar`; otherwise use `""`. Update examples A and B with `"meeting":true,"date":…,"time":…`.
  - Validate the fields after parsing, as the last test describes.
  - **Token budget (review note):** the schema enum costs no prompt tokens, but the payload list does. The payload carries `call_date` with its weekday and a compact list of the next 21 days only (`"2026-10-29 чт"` strings via `calendarDays(from, 21)`). The schema `date` enum keeps 60 days. In the test, measure the extract payload's byte size with a 24-utterance window and assert that it stays within the existing input budget. Record the result in the commit message.
- [ ] **Step 4: Run, expect PASS** (whole suite)
- [ ] **Step 5: Commit** `feat(prompter): schema-constrained agreement calls with meeting date/time`

### Task 2: Agreement records carry meeting fields; ledger helpers

**Files:**
- Modify: `src/pages/transcript_page/call_prompter_controller.{h,cpp}`, `src/pages/transcript_page/meeting_history.{h,cpp}`
- Test: `test/call_prompter_controller_tests.cpp`

**Interfaces:**
- Consumes: Task 1 event fields.
- Produces:
  - `void CallPrompterController::setCallDate(const QDate &date)`. The request carries `call_date` (default `QDate::currentDate()`).
  - Cards and records keep `meeting`, `date`, `time`. A record may also carry `event_id` (int64).
  - `bool CallPrompterController::refreshFromStorage()` reloads cards/records/suggestions/`last_processed`/revision when the stored revision differs and nothing is in flight, then emits `cardsChanged`.
  - In `meeting_history.h`:
    - `bool appendMaterialAgreement(pcm::database::Database &db, int64_t materialId, const QJsonObject &record)` appends to the `records` of the material's **last** linked transcript's prompter state, saving with that state's revision. It returns false when there is no transcript or the save fails.
    - `bool setMaterialAgreementEvent(pcm::database::Database &db, int64_t materialId, const QString &recordId, int64_t eventId)` finds the record in any linked transcript and sets `event_id`. An `eventId` ≤0 removes the key.

- [ ] **Step 1: Write failing tests**
  - `RequestCarriesCallDate`: after `setCallDate(QDate(2026,10,29))` the emitted request has `call_date:"2026-10-29"`.
  - `MeetingFieldsReachCardAndRecord`: a result event with `meeting:true,date,time` produces a card with those fields, and `acceptCard` copies them into the record.
  - `AcceptWithEditedTextKeepsMeetingFields` (Review Focus 2).
  - `InvalidMeetingFieldTypesRejectBatch`: `meeting:"yes"` → `batchFailed` path (same as other schema violations).
  - `AppendMaterialAgreementAndSetEventRoundTrip`: through a real test DB with a material and transcript, assert `loadMaterialAgreements` returns the appended record. `setMaterialAgreementEvent(…, 42)` then `(…, 0)` adds and removes `event_id`.
  - `RefreshFromStorageSeesExternalAppend`: a live controller, then `appendMaterialAgreement` on its transcript's material, then `refreshFromStorage()` → `records()` contains the new record, and a following `acceptCard` persists without a revision conflict.
- [ ] **Step 2: Run, expect FAIL** — `Sessio_call_prompter_controller_tests`
- [ ] **Step 3: Implement** the interfaces above. Validate the new fields in `handleResult` with the same rules as Task 1.
- [ ] **Step 4: Run, expect PASS**
- [ ] **Step 5: Commit** `feat(prompter): keep meeting date/time on agreements; ledger helpers`

### Task 3: Shared notes helpers and the live key-points pipeline

**Files:**
- Create: `src/call_prompter/notes_common.{h,cpp}`, `src/call_prompter/live_points_pipeline.{h,cpp}`, `src/call_prompter/prompts/live_points.txt`
- Modify: `src/call_prompter/call_notes_pipeline.cpp` (use `notes_common`), `src/call_prompter/CMakeLists.txt`, the prompts `.qrc`/resource list next to `notes.txt`, `test/CMakeLists.txt`
- Test: create `test/live_points_pipeline_tests.cpp` (target `Sessio_live_points_pipeline_tests`, linked like `Sessio_call_notes_pipeline_tests`)

**Interfaces:**
- Produces, in namespace `pcm::prompter::notes`, all moved unchanged from `call_notes_pipeline.cpp`:
  - `QByteArray serialize(const QJsonObject&)`
  - `QString prompt(const char *name)`
  - `QJsonObject checkedItem(const QJsonValue&, qsizetype limit, bool allowEmpty, const QSet<QString>& allowed)`
  - `QList<QJsonArray> chunks(const QJsonArray& utterances, qsizetype payloadBudget)`
  - `QByteArray restrictEvidence(QByteArray schema, const QSet<QString>& allowed)`: the enum patch
  - `QByteArray closeObjects(QByteArray schema)`
  - `constexpr int kMaxEvidenceIds = 8`
- Produces: `QJsonObject processLivePoints(const QJsonArray &newUtterances, const QStringList &knownPoints, const Generate &generate)` returns `{"key_points":[{text,evidence_ids}]}`, with 0–3 points per chunk.
  - The payload is `{"known_points":[…≤10 most recent, texts only], "utterances":[…]}`.
  - Evidence may cite only that chunk's IDs.
  - An unusable chunk reply is skipped. The pipeline throws `PipelineError("invalid notes output")` only when every chunk failed.
  - Empty input returns `{"key_points":[]}` without calling the model.

- [ ] **Step 1: Write failing tests**
  - `EmptyInputDoesNotInvokeModel`
  - `PayloadHasOnlyNewUtterancesAndKnownPointTexts`: `known_points` is a string array, never cited.
  - `SchemaEnumCoversOnlyChunkIds`
  - `CitingKnownOrForeignIdRejectsThatChunk`
  - `AtMostThreePointsPerChunkAndEightyChars`
  - `LongWindowIsChunkedAndPointsConcatenate`: 3 chunks → points from all, in order.
  - `OneFailedChunkIsSkippedAllFailedThrows`
- [ ] **Step 2: Run, expect FAIL**
- [ ] **Step 3: Implement**
  - Move the helpers into `notes_common`. `call_notes_pipeline.cpp` must still pass its own suite unchanged.
  - Write `processLivePoints` with the reflect-cpp struct `LivePointsSchema{key_points: Validator<vector<PointSchema>, Size<Maximum<3>>>}`.
  - Write `live_points.txt`: new key facts from `utterances` only, not repeating `known_points`; 0–3 points; schema line; the same rules as `notes.txt`.
  - Use the leaf budget from `kContextInputBytes - prompt bytes`, as in `processNotes`.
- [ ] **Step 4: Run, expect PASS** for both `Sessio_live_points_pipeline_tests` and `Sessio_call_notes_pipeline_tests`
- [ ] **Step 5: Commit** `feat(notes): incremental live key-points pipeline`

### Task 4: Live notes send only new phrases and append points

**Files:**
- Modify: `src/pages/transcript_page/call_notes_controller.{h,cpp}`, `src/pages/transcript_page/prompter_worker.cpp`
- Test: `test/call_notes_controller_tests.cpp`, `test/prompter_worker_tests.cpp`

**Interfaces:**
- Consumes: Task 3 `processLivePoints`.
- Produces:
  - The live request is `{task:"live_points", final:false, utterances:[phrases with id > live_through_id], known_points:[…]}` plus the existing identity keys.
  - The state gains `live.key_points` (appended, ≤40), `live.sources` (the union of the phrases cited so far) and `live_through_id` (int64, the last phrase ID of a successful run).
  - `live.note` is no longer written.
  - `QJsonArray CallNotesController::livePoints() const` returns `live.key_points`.
  - The worker accepts `task` in `{"live_points","summary"}` as background tasks. They are enqueued, not prepended, use `kNotesCallTimeoutMs` and use the notes cache. The result is `{"notes": <pipeline output>}` with the same identity keys copied.
  - `"summary"` reaches `processSummary` from Task 5. Until Task 5 lands, the worker answers `summary` with the error reply.

- [ ] **Step 1: Write failing tests**
  - `LiveRequestContainsOnlyPhrasesAfterLastSuccess`: add 3 phrases, a successful result, 2 more phrases; the second request has exactly 2 utterances.
  - `LiveResultAppendsWithoutRewritingEarlierPoints`
  - `FailedLiveRunKeepsPhrasesForTheNextRun`: transient error, then retry; the request still has those phrases.
  - `LivePointsCapStopsLiveRuns`: with 40 points, new phrases dispatch nothing.
  - `LiveResultCitingUnsentIdIsRejected`
  - `LegacyLiveNoteIsIgnoredForDraft`: a stored `live.note` is not returned by `draft()` while not ended.
  - In the worker tests: `LivePointsAndSummaryAreBackgroundTasks` (queued behind agreement requests).
- [ ] **Step 2: Run, expect FAIL** — `Sessio_call_notes_controller_tests`, `Sessio_prompter_worker_tests`
- [ ] **Step 3: Implement**
  - In `handleResult`, branch on `mRequest["task"]`.
  - The stale check for live runs only covers the sent phrases.
  - `processed_ms` keeps meaning "last processed end_ms", for `statusText`.
- [ ] **Step 4: Run, expect PASS**
- [ ] **Step 5: Commit** `feat(notes): live notes produce incremental key points only`

### Task 5: Summary pipeline

**Files:**
- Create: `src/call_prompter/summary_pipeline.{h,cpp}`, `src/call_prompter/prompts/summary_leaf.txt`, `summary_merge.txt`, `summary_overview.txt`
- Modify: `src/call_prompter/CMakeLists.txt`, the prompts resource list, `test/CMakeLists.txt`, `src/pages/transcript_page/prompter_worker.cpp` (route `summary`)
- Test: create `test/summary_pipeline_tests.cpp` (target `Sessio_summary_pipeline_tests`)

**Interfaces:**
- Consumes: `notes_common` (Task 3), `calendarDays` (Task 1).
- Produces:
  - `struct SummaryContext { QDate callDate; QStringList agreements; };`
  - `QJsonObject processSummary(const QJsonArray &utterances, const SummaryContext &ctx, const Generate &generate)` returns format 2:
    ```json
    {"format":2,"overview":{"text","evidence_ids"},"key_points":[…],
     "tasks":[{"text","evidence_ids","meeting","date","time"}],"open_questions":[…]}
    ```
    The limits are in Global Constraints.
  - The worker passes `request["call_date"]` and `request["agreements"]` (a string array) into `SummaryContext`.
- Algorithm (fixed by the spec):
  1. **Leaf per chunk.** The input is `{utterances, calendar, agreements}`. The output is `{note≤300, key_points≤10, tasks≤8, open_questions≤5}`, citing that chunk's IDs. A rejected leaf is retried once as two halves. A half that fails again is skipped. If every leaf fails, throw `invalid notes output`.
  2. **Pairwise merge** of the lists only (`key_points`, `tasks`, `open_questions`). Evidence is limited to the IDs of the two inputs. A rejected merge becomes the alternating concatenation cut to the limits, as `concatenated()` does.
  3. **Overview.** The input is `{notes:[chunk notes in order]}`. When the notes exceed the budget, first reduce adjacent pairs with the same overview prompt (cap 300 per intermediate), then make the final call with a 1000 cap. A rejected final overview call falls back to the joined chunk notes cut to 1000 at a word boundary, with the union of their evidence capped at 8.
  - After the pipeline, drop any task whose normalized text (case-folded, simplified, trailing `.!?` removed) equals one of `ctx.agreements`.
  - Size the output reserve per call from its schema maximum. Measure the real reply size on the smoke test (Task 9). If the 4096 context cannot fit the overview, lower the overview cap and record it in the spec.

- [ ] **Step 1: Write failing tests**
  - `EmptyTranscriptReturnsEmptyFormat2WithoutModel`
  - `EveryCallHasNonEmptySchemaWithLocalEvidenceEnum`
  - `LeafPayloadCarriesCalendarAndAgreements`: the calendar starts at `ctx.callDate`.
  - `LateFactsSurviveMergeAndMergeFallbackConcatenates`: 4 chunks; the last chunk's point is in the output; one merge reply invalid → fallback.
  - `OverviewBuiltFromChunkNotesInOrder`
  - `OverviewFallbackJoinsNotes`
  - `TasksDuplicatingAcceptedAgreementsAreDropped`
  - `LimitsEnforced`: 11 points, an 1001-character overview or a 9th evidence ID → that reply rejected.
  - `MeetingTaskKeepsDateFromCalendarOnly`
- [ ] **Step 2: Run, expect FAIL**
- [ ] **Step 3: Implement** `processSummary` and the three prompts. The prompts follow the four sections in the spec: overview «с чем пришёл клиент, что обсуждали, к чему пришли».
- [ ] **Step 4: Run, expect PASS** for `Sessio_summary_pipeline_tests` and `Sessio_prompter_worker_tests`
- [ ] **Step 5: Commit** `feat(notes): post-call meeting summary pipeline`

### Task 6: Summary is offered after the call; format 2 in the controller and readers

**Files:**
- Modify:
  - `src/pages/transcript_page/call_notes_controller.{h,cpp}`
  - `src/pages/transcript_page/call_notes_finalizer.cpp` (only if `requestFinal` semantics require it)
  - `src/app/application.cpp` (`callSessionEnded`: remove `finals->enqueue(...)`; previews at ~586)
  - `src/pages/transcript_page/meeting_history.{h,cpp}`
  - `src/pages/client_notes_page/client_notes_journal.cpp`, `src/pages/client_notes_page/client_notes_page.cpp`
- Delete: `processNotes`, `notes.txt`, `notes_merge.txt` and `test/call_notes_pipeline_tests.cpp`, once nothing references them. Move any test there that still covers `notes_common` behaviour into `live_points_pipeline_tests.cpp`.
- Test: `test/call_notes_controller_tests.cpp`, `test/client_notes_page_ui_tests.cpp`

**Interfaces:**
- Consumes: Task 5 format 2, Task 2 `loadMaterialAgreements`.
- Produces:
  - `finishCall()` never sets `final_status:"queued"`. When the material has a linked transcript and no `final`, it sets `final_status:"offered"`.
  - `openMaterial`/`openTranscript` of an ended meeting that has a transcript, no `final`, and `final_status` in `{idle, cancelled, interrupted}` report `finalStatus()=="offered"`. This is computed, not persisted.
  - The final request is `{task:"summary", final:true, utterances, call_date, agreements:[active record texts]}`. `call_date` is the local date of the material's event `start_date`, else of `created_at`.
  - `QJsonObject CallNotesController::summary() const`:
    - format-2 `final` → itself;
    - legacy `final` → `{"format":1,"overview":note,"key_points":…,"tasks":[],"open_questions":[]}`;
    - dismissed or no `final` → `{}`.
  - `draft()` returns `summary()["overview"]`. `keyPoints()` returns `summary()["key_points"]`. `editDraft`/`dismissDraft` act on `final.overview` for format 2 and on `final.note` for format 1.
  - `source(id)`/`sourcesCurrent()` look in `final.sources` after the call and in `live.sources` during it.
  - `statusText()` for `"offered"`: tr("Summarize the whole meeting when you are ready.")
  - `QString materialSummaryText(const QJsonObject &state)` in `meeting_history.h` returns the overview text of format 2, else `final.note.text`, else `""`. It never falls back to `live`. All readers of `state["final"|"live"]["note"]` (meeting_history, client_notes_journal, client_notes_page, application.cpp previews) use it.

- [ ] **Step 1: Write failing tests**
  - `FinishCallOffersInsteadOfQueuing`: after `finishCall()` the status is `"offered"` and no `requestReady` is emitted.
  - `RequestFinalSendsSummaryTaskWithAgreements`
  - `SummaryRequestCarriesCallDateNotToday` (Review Focus 1): a material whose event started on 2026-10-01 has request `call_date:"2026-10-01"`.
  - `Format2ResultValidatedAndStored`: invalid evidence in `tasks` → `invalid_output`.
  - `EditedSourceMarksSummaryStale` (Review Focus 4).
  - `LegacyFinalIsShownAndNotOffered` (Review Focus 5): legacy `final` → `summary()["format"]==1`, `draft()` is the note, status is not `"offered"`, and `requestFinal()` produces format 2.
  - `EndedMeetingWithoutSummaryIsOffered`
  - Journal test: `JournalPreviewUsesOverviewText`.
- [ ] **Step 2: Run, expect FAIL**
- [ ] **Step 3: Implement**. Remove the call-end auto-enqueue in `application.cpp`; `requestFinal()` already emits `generationRequested` → the finalizer.
- [ ] **Step 4: Run, expect PASS** for the controller, client notes, notes widgets and call notes page suites
- [ ] **Step 5: Commit** `feat(notes): offer the meeting summary after the call; format 2 summary`

### Task 7: Pre-filled new-event dialog from an agreement

**Files:**
- Create: `src/pages/transcript_page/agreement_calendar.{h,cpp}`
- Modify: `src/pages/event_info_page/qevent_details_widget.{h,cpp}`, `src/pages/event_info_page/event_info.{h,cpp}`, `src/pages/transcript_page/CMakeLists.txt`
- Test: create `test/agreement_calendar_tests.cpp` (target `Sessio_agreement_calendar_tests`). The details-widget test goes in the existing event page test file, if one exists (`grep -l QEventDetailsWidget test/*.cpp`), else in the new file.

**Interfaces:**
- Produces:
  - `struct EventTemplate { QDate date; std::optional<QTime> time; std::optional<int> durationMinutes; QString title; QString notes; std::optional<int64_t> clientId; };` in `agreement_calendar.h`.
  - `EventTemplate agreementEventTemplate(const QJsonObject &record, const std::optional<DuckEvent> &callEvent, std::optional<int64_t> clientId, const QString &clientName, const QDate &callDate)`:
    - `date` is the record's date, else `callDate.addDays(7)`;
    - `time` is the record's time, else the call event's start time, else nullopt;
    - `durationMinutes` is the call event's duration, else nullopt;
    - `title` is the call event's name, else `clientName`, else the record text;
    - `notes` is the record text.
  - `void QEventDetailsWidget::applyTemplate(const QString &title, const QString &description, std::optional<int64_t> clientId)`, called after `startCreatingNewEvent(date, time, duration)`. It selects the client in the combo box the same way `loadEvent(…, clientId)` does.
  - `std::optional<int64_t> QEventInfoPage::createEventFromTemplate(const EventTemplate &t)` (public) opens the same modal dialog as `openQuickEventDialog`, pre-filled. It returns the new event ID only when the save path (`onEventSaved` create branch) succeeded, and nullopt on cancel or a failed save. It records the ID in a member set by the create branch.

- [ ] **Step 1: Write failing tests**
  - `TemplateUsesAgreementDateAndTime`
  - `TemplateFallsBackToCallEventAndWeekLater`
  - `TemplateWithoutClientOrEventUsesAgreementText`
  - `ApplyTemplateFillsNameDescriptionAndClient`: widget-level, offscreen.
- [ ] **Step 2: Run, expect FAIL**
- [ ] **Step 3: Implement**
- [ ] **Step 4: Run, expect PASS**
- [ ] **Step 5: Commit** `feat(calendar): new-event dialog pre-filled from an agreement`

### Task 8: Notes workspace UI: offer, four sections, live points, calendar and accept

**Files:**
- Modify:
  - `src/pages/transcript_page/notes_workspace_bridge.{h,cpp}`
  - `src/pages/transcript_page/qml/NotesWorkspace.qml`, `src/pages/transcript_page/qml/NotesEditor.qml` (summary block only)
  - `src/pages/transcript_page/prompter_panel.{h,cpp}` (calendar button on accepted meeting cards)
  - `src/app/application.cpp` (wire the openers)
  - legacy widgets `ai_summary_widget.cpp`/`key_points_widget.cpp`: they keep working through `draft()`/`keyPoints()`; only the «Retry» label becomes «Summarize meeting» when `"offered"`. The legacy page gets no new sections; this is the builder's call recorded here.
- Test: `test/call_notes_page_tests.cpp`, `test/prompter_panel_tests.cpp`, `test/notes_widgets_tests.cpp`

**Interfaces:**
- Consumes: Tasks 2, 6, 7.
- Produces, on `NotesWorkspaceBridge`:
  - `void setCalendarOpener(std::function<std::optional<int64_t>(const EventTemplate&)>)`
  - `void setEventOpener(std::function<void(int64_t eventId)>)`
  - `Q_INVOKABLE void summarizeMeeting()`, which calls `storage->requestFinal()`
  - `Q_INVOKABLE void addAgreementToCalendar(const QString &recordId)`: builds the template, calls the opener, and on success `setMaterialAgreementEvent` then `prompter->refreshFromStorage()` when live
  - `Q_INVOKABLE void openAgreementEvent(const QString &recordId)`
  - `Q_INVOKABLE bool acceptSummaryTask(int index)`: `appendMaterialAgreement` with `{id: uuid, text, participants: [], active: true, evidence_ids, meeting, date, time}`, then refresh
- Produces, in `info()`:
  - `summaryState` may be `"offered"`;
  - `overview`, which replaces `summary`;
  - `points` (summary), `livePoints`, `tasks` (with `accepted` bool), `openQuestions`;
  - each `agreements` entry gains `canAddToCalendar` (= `active && meeting && !inCalendar`) and `inCalendar` (= `event_id` set **and** `get_event(event_id)` exists).
  - The same two fields go on `prompterCards` entries for accepted cards, matched by record ID.
- QML:
  - With `"offered"`, a «Summarize meeting» primary action replaces the summary body.
  - The sections are «Overview», «Key points», «Agreements and tasks» (accepted agreements first, then tasks with «Accept»), «Open questions» and «From the call» (live points).
  - «Add to calendar» / «In calendar» go on agreements.
  - «Create again» stays in the summary actions.
- `application.cpp`: the calendar opener calls `eventPage->createEventFromTemplate`, and the event opener calls `eventPage->openEventOnDay(id, start)` after switching to the calendar page.

- [ ] **Step 1: Write failing tests**
  - `OfferedStateShowsSummarizeMeetingAndClickRequestsFinal`
  - `FourSectionsAndLivePointsRender`: find the QML items by `objectName` (`summaryOverview`, `summaryPoints`, `summaryTasks`, `summaryQuestions`, `livePoints`).
  - `AddToCalendarCallsOpenerWithTemplateAndStoresEventId`
  - `CancelledCalendarDialogStoresNothing`
  - `DeletedEventRestoresAddToCalendar` (Review Focus 3)
  - `AcceptSummaryTaskMovesItIntoAgreements`
  - `PrompterPanelAcceptedMeetingCardShowsAddToCalendar`
- [ ] **Step 2: Run, expect FAIL**
- [ ] **Step 3: Implement**
- [ ] **Step 4: Run, expect PASS** for all touched UI suites
- [ ] **Step 5: Commit** `feat(notes): summary sections, offer and add-to-calendar in the notes workspace`

### Task 9: Translations, real-model smoke and the full suite

**Files:**
- Modify: `translation/app_en.ts`, `translation/app_ru.ts`, `test/call_prompter_model_smoke_tests.cpp`

- [ ] **Step 1:** Run `lupdate` the way the project does: check `CMakeLists.txt` for `qt_add_translations` / an update target. Fill RU for every new or changed string from Tasks 2–8 and EN where the project keeps explicit EN text. Keep no `unfinished` entries for new strings.
- [ ] **Step 2:** Smoke tests (skipped without `SESSIO_PROMPTER_MODEL`):
  - `LivePointsOnSyntheticLongCall`: points from at least 2 windows, no repeats;
  - `SummaryOnSyntheticLongCall`: a nonempty overview that includes a late fact;
  - `NextMeetingDateResolvedFromWeekday`: call_date 2026-10-29, «встретимся в четверг в 15:00» → `date:"2026-11-05", time:"15:00", meeting:true`.

  Run them against the private CPU shim in `../prompter-recovery/build-litert/install` if `SESSIO_LITERT_DIR` can point there. Otherwise record that they were not run. Tune the prompts until they pass. If the overview does not fit the budget, apply the Task 5 fallback rule.
- [ ] **Step 3:** Run the full ctest: `cd build && QT_QPA_PLATFORM=offscreen ctest -j4 --output-on-failure`. Expected: all pass, or only failures shown to already exist on the base commit.
- [ ] **Step 4: Commit** `i18n(notes): EN/RU strings for live points and meeting summary; smoke tests`
