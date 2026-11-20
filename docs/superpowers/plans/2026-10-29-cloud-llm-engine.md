# Cloud LLM Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run the post-call summary, live key points and agreement extraction/matching on a user-configured OpenAI-compatible cloud model, chosen per feature, with consent, pseudonymization and a keychain-held key.

**Architecture:** A `CloudEngine` (QNetworkAccessManager, `response_format: json_schema` strict) implements the existing `PrompterEngine` seam. `PrompterWorker` holds a local and a cloud engine and routes each request by its task to the engine configured for that feature. A `Pseudonymizer` wraps the cloud engine's `generate()` (replace names/phones/emails outbound, restore inbound). Results carry an `engine` label shown in the UI. Summary leaf calls get an opt-in parallelism seam.

**Tech Stack:** Qt 6 (Network, Widgets), reflect-cpp (schemas, already used), qtkeychain, GoogleTest, a `QTcpServer`-based fake HTTP server in tests.

**Spec:** `docs/superpowers/specs/2026-10-27-web-calls-kb-hints-cloud-design.md` Part 4 (binding). Decisions made in the brainstorm with the user: parallel small chunks for cloud summary; end-to-end check on a real provider is done by the user, not by tests or by me.

## Global Constraints

- Cloud is OFF by default; usable only after consent accepted for the current base URL; URL change invalidates consent; revoking consent disables the cloud engine for every feature.
- Structured output ONLY via `response_format: {type:"json_schema", json_schema:{name, strict:true, schema}}`. No `json_object` fallback. "Test connection" uses a test schema and rejects a provider that does not support `json_schema` or answers off-schema.
- Pseudonymization ON by default; labels `[Клиент]`, `[Специалист]`, `[Телефон N]`, `[Email N]`; mapping lives only in memory for one request; labels restored in the reply before display/saving.
- API key only in the system keychain (service `Sessio`, key `cloud-llm/api-key`), never in `Config.yaml`/QSettings.
- Timeout; one retry on network error, 429 and 5xx; clear errors for 401, 404/unknown model. NO silent fallback to local; failure shows "Cloud unavailable"/the reason.
- Never log prompts, payloads, replies, or the key. Tests never call a real API.
- Do not edit `prompts/*.txt` or change `summary_pipeline.cpp` logic beyond the parallelism seam (Codex worktree `meeting-followup-fixes` is editing them).
- New UI strings translated EN/RU; update CHANGELOG and app version per the spec's acceptance list.
- Build: vcpkg prebuilt tree (`-DVCPKG_INSTALLED_DIR=/home/a.durynin/Projects/C++/PsyClientManager/build-release/vcpkg_installed`, `-DVCPKG_MANIFEST_INSTALL=OFF`), inside distrobox, `-j4`, never rebuild DuckDB. GUI only via `scripts/run-dev-isolated.sh`; never kill the user's running Sessio.
- Commit trailer: `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Commit on this branch, do not push.

## Review Focus

- Provider returns 200 with `choices[0].message.content` empty/null or `finish_reason: "length"`: must throw `PipelineError("output truncated")`, not parse garbage.
- Provider returns a JSON reply wrapped in a code fence or with extra fields: fenced is tolerated (`parseModelReply`), extra fields rejected by the pipeline validation as today.
- Name in a declined case form (`Ивану`, `Иванова`) and inside a longer word: replaced; unrelated words sharing a 2-letter prefix are not mangled.
- Label text the model echoes in a different spacing/case (`[клиент]`): restored; an unknown label is left untouched.
- Base URL with/without trailing slash or `/v1`: requests go to exactly one `/chat/completions`.
- Cancel while a retry back-off is pending: no further request is sent.
- Key missing from keychain while cloud is enabled: clear "No API key" error, no request.
- Parallel leaf calls: worker's `notesCache` and cancellation stay thread-safe; one failed leaf behaves as in the sequential path.

---

## File Structure

| File | Responsibility |
|---|---|
| `src/call_prompter/cloud_engine.h/.cpp` (new) | `CloudConfig`, `CloudEngine : PrompterEngine`, `createCloudEngine(config)`; HTTP, retry, errors, schema wrapping |
| `src/call_prompter/pseudonymizer.h/.cpp` (new) | `Pseudonymizer`: build map from names/phones/emails, `apply(QString)`, `restore(QJsonValue)` |
| `src/call_prompter/engine_choice.h` (new) | `Feature` enum (Agreements, LivePoints, Summary), `EngineKind` (Local, Cloud), `featureForTask(task)` |
| `src/call_prompter/summary_pipeline.*` | add `SummaryContext::parallelism` (seam only) |
| `src/widgets/app_settings.*` | cloud settings getters/setters (URL, model, ctx, consent URL, pseudonymize, per-feature engine) — no key |
| `src/app/cloud_key_store.h/.cpp` (new) | keychain read/write/delete of the API key |
| `src/pages/transcript_page/prompter_worker.*` | second engine, routing by task, thread-safe cache, `engine` label in results, cloud error reasons |
| `src/app/cloud_settings_panel.h/.cpp` (new), `settings_dialog.cpp` | Settings page + consent dialog + Test connection |
| `src/pages/transcript_page/call_notes_controller.cpp`, `ai_summary_widget.cpp`, `qml/NotesWorkspace.qml`, `prompter_panel.cpp` | show engine label; cloud error texts |
| `test/cloud_engine_tests.cpp`, `test/pseudonymizer_tests.cpp`, `test/cloud_settings_tests.cpp` (new); `test/prompter_worker_tests.cpp`, `test/summary_pipeline_tests.cpp` | tests |
| `translations/*` , `CHANGELOG.md` | EN/RU, release notes |

---

### Task 1: CloudEngine (HTTP, schema, retry, errors)

**Files:** Create `src/call_prompter/cloud_engine.h/.cpp`, `test/cloud_engine_tests.cpp`, `test/fake_http_server.h`; Modify `src/call_prompter/CMakeLists.txt` (add `Qt6::Network`, new sources, new target `Sessio_call_prompter_cloud` or put into `Sessio_call_prompter`), `test/CMakeLists.txt` (`Sessio_cloud_engine_tests`).

**Interfaces:**
- Produces:
  ```cpp
  struct CloudConfig { QString baseUrl; QString model; QString apiKey; int timeoutMs = 120000; int maxOutputTokens = 2048; };
  std::unique_ptr<PrompterEngine> createCloudEngine(const CloudConfig &);
  struct CloudProbe { bool ok; QString reason; };
  CloudProbe probeCloud(const CloudConfig &);   // blocking, test-schema request, used by Test connection (call off the GUI thread)
  ```
  Errors thrown from `generate` as `PipelineError` with fixed codes: `"cloud auth"` (401/403), `"cloud model"` (404/unknown model), `"cloud rate limit"`, `"cloud server"`, `"cloud network"`, `"cloud timeout"`, `"cancelled"`, `"output truncated"`, `"cloud unsupported"` (no json_schema). `failureReason()` in `prompter_pipeline.cpp` maps `cloud *` → reason `"cloud"` (new bucket) and `cloud timeout` → `"timeout"`.
- `FakeHttpServer` (test): listens on 127.0.0.1, queued scripted responses `{status, body, delayMs}`, records requests (path, headers, body).

- [ ] **Step 1: tests** in `cloud_engine_tests.cpp` (QCoreApplication, event loop pump inside the blocking call is NOT needed: engine owns a private `QNetworkAccessManager` on a private `QEventLoop`, so `generate()` is callable from any thread):
  `SendsStrictJsonSchemaRequest` (path `/chat/completions`, `Authorization: Bearer k`, body has `model`, `messages[0].role=="system"`, `messages[1].content==payload`, `response_format.type=="json_schema"`, `json_schema.strict==true`, `json_schema.schema` equals the passed schema, `max_tokens` set); `ParsesContentJson`; `BaseUrlSlashNormalised` (`http://h/v1/` and `http://h/v1` both hit `/v1/chat/completions`); `Retries429ThenSucceeds`; `Retries5xxOnceThenThrowsCloudServer`; `Http401ThrowsCloudAuth`; `Http404ThrowsCloudModel`; `FinishReasonLengthThrowsTruncated`; `EmptyContentThrowsTruncated`; `TimeoutThrowsCloudTimeout` (timeoutMs=200, server delay 1000); `CancelDuringRequestThrowsCancelled` (cancel from another thread after 50 ms); `CancelDuringBackoffSendsNoSecondRequest`; `ConnectionRefusedThrowsCloudNetwork` after exactly one retry; `ProbeRejectsServerWithoutJsonSchema` (server replies 400 `"response_format ... not supported"`) and `ProbeRejectsOffSchemaReply`; `ProbeAcceptsConformingReply`; `LogsNeverContainPayload` (install message handler, assert no payload/key substrings).
- [ ] **Step 2:** Run `ctest -R CloudEngine` → expect FAIL (build error: header missing).
- [ ] **Step 3:** Implement `CloudEngine`: sync `generate()` posting via `QNetworkAccessManager` + `QEventLoop` + `QTimer` timeout on the calling thread; `cancel()` thread-safe via `std::atomic_bool` + `QMetaObject::invokeMethod` abort of the current reply; retry once (≥429/5xx/network) after 500 ms back-off using an interruptible wait; output text from `choices[0].message.content` → `parseModelReply`. Empty schema ⇒ plain request without `response_format`. `probeCloud` sends system "Reply with the JSON object", payload `{"ping":"ok"}`, schema `{"type":"object","properties":{"ok":{"type":"boolean"}},"required":["ok"],"additionalProperties":false}` and requires `ok` field.
- [ ] **Step 4:** Run tests → PASS.
- [ ] **Step 5:** Commit `feat(prompter): OpenAI-compatible cloud engine`.

### Task 2: Pseudonymizer

**Files:** Create `src/call_prompter/pseudonymizer.h/.cpp`, `test/pseudonymizer_tests.cpp`; CMake entries.

**Interfaces:**
- Produces:
  ```cpp
  struct PseudonymSubjects { QStringList clientNames; QStringList specialistNames; QStringList phones; QStringList emails; }; // clientNames: name, last name, patronymic parts
  class Pseudonymizer {
  public:
    explicit Pseudonymizer(const PseudonymSubjects &);
    QString apply(const QString &text);        // names (case forms), phones, emails → labels; also picks up regex phones/emails not in subjects
    QByteArray apply(const QByteArray &jsonPayload); // applies to every string value
    QJsonValue restore(const QJsonValue &reply) const;  // labels → originals, recursive
  };
  ```
- Consumes: nothing.

- [ ] **Step 1: tests:** `ReplacesNameInCases` (`Иван`→`[Клиент]`, `Ивану`, `Ивана`, `Иваном`, `Иванова`), `ReplacesSpecialist`, `DoesNotMangleUnrelatedWords` (name `Ян` leaves `Янтарь`), `PhoneNumbersFromTextAndSubjects` (`+7 (999) 123-45-67`, `89991234567` → same `[Телефон 1]` label for same number, `[Телефон 2]` for another), `EmailsReplaced`, `RestoreIsRecursiveAndRestoresOriginalForm` (restored form is the original surface form seen in text, so `Ивану` returns `Ивану`), `RestoreToleratesCaseAndSpaceInLabel` (`[клиент]`), `UnknownLabelUntouched`, `NoSubjectsNoRegexHitsIsIdentity`, `ApplyJsonPayloadKeepsKeysAndStructure`.
- [ ] **Step 2:** run → FAIL. **Step 3:** Implement: for each name token ≥2 chars build a stem (drop up to 2 trailing letters for tokens ≥4 chars, whole token otherwise) and match whole words starting with the stem and at most 3 extra letters, case-insensitively; record surface form per label occurrence in a `QHash<QString,QString>` (label→first surface form) plus per-occurrence list so restoring a label that appeared in several forms is best-effort (first form; document). Regexes for phone (`\+?\d[\d\s\-()]{8,}\d`) and email. **Step 4:** PASS. **Step 5:** Commit `feat(prompter): pseudonymizer for cloud requests`.

### Task 3: Settings storage, key store, engine choice

**Files:** Create `src/call_prompter/engine_choice.h`, `src/app/cloud_key_store.h/.cpp`, `test/cloud_settings_tests.cpp`; Modify `src/widgets/app_settings.h/.cpp`, `src/app/CMakeLists.txt`.

**Interfaces:**
- Produces in `pcm::app_settings`: `QString cloudBaseUrl()/setCloudBaseUrl`, `cloudModel()/set…`, `int cloudContextTokens()` (default 32768), `QString cloudConsentUrl()` (the URL accepted), `void acceptCloudConsent()` (stores current base URL), `void revokeCloudConsent()`, `bool cloudConsentValid()` (consent URL == normalized current URL, non-empty), `bool cloudPseudonymize()` (default true), `QString featureEngine(Feature)` returning `"local"`/`"cloud"` (default local; returns `"local"` when `!cloudConsentValid()`), `setFeatureEngine`. Changing `setCloudBaseUrl` to a different normalized URL clears consent (revoke).
- `pcm::prompter::Feature { Agreements, LivePoints, Summary }`, `Feature featureForTask(const QString &task)` (`"summary"`→Summary, `"live_points"`→LivePoints, else Agreements).
- `CloudKeyStore : QObject` with `read()`, `write(QString)`, `remove()`; signals `readFinished(bool ok, QString key, QString error)`, `writeFinished(bool, QString)`. Service `Sessio`, key `cloud-llm/api-key`, `setInsecureFallback(false)` (same as `token_backend_credential_store.cpp`).
- [ ] **Step 1: tests:** `DefaultsAreOffAndLocal`; `FeatureEngineIgnoredWithoutConsent` (set cloud, no consent → local); `ConsentTiedToUrl` (accept, change URL → consent invalid, feature engine local); `TrailingSlashDoesNotInvalidateConsent`; `RevokeDisablesAllFeatures`; `ApiKeyNeverWrittenToSettings` (after setters, scan `QSettings` keys/values for the secret — key store is a separate interface so this asserts the settings API has no key field and the file lacks a sentinel). Use an isolated `QSettings` scope (`QCoreApplication::setOrganizationName` test + `QSettings::setPath` to a temp dir).
- [ ] **Step 2-4:** run FAIL → implement → PASS. **Step 5:** Commit `feat(settings): cloud LLM settings, consent and key store`.

### Task 4: Worker routing, cloud engine in `PrompterWorker`

**Files:** Modify `src/pages/transcript_page/prompter_worker.h/.cpp`, `test/prompter_worker_tests.cpp`.

**Interfaces:**
- Consumes: Tasks 1–3.
- Produces on `PrompterWorker`: `void setCloudFactory(std::function<std::unique_ptr<pcm::prompter::PrompterEngine>()> factory)` (default factory reads settings + key; tests inject a scripted engine); `bool startCloud()` ; routing rule inside `send()`: engine kind = `featureEngine(featureForTask(task))`; `start(model)` is only needed for local; a cloud-routed request does not need the local model ready. Results gain `"engine"`: `"cloud:<model>"` or `"local"`; cloud failures produce `{"error":"inference_failed","reason":"cloud"|"timeout"|"cancelled"|…, "cloud_detail": "auth"|"model"|"rate_limit"|"server"|"network"|"no_key"}`.
- Cloud path wraps `engine->generate` with `Pseudonymizer` when `cloudPseudonymize()`; subjects come from the request's new optional `"subjects"` object `{client:[…], specialist:[…], phones:[…], emails:[…]}` (filled by the controllers in Task 6) plus regexes. Cloud call timeout: 120 s per call (`CloudConfig.timeoutMs`), worker request timer for cloud tasks = `kNotesCallTimeoutMs`.
- `notesCache` becomes mutex-guarded (needed by parallel leaves); cache key includes engine kind.

- [ ] **Step 1: tests** (inject a scripted engine; follow existing worker test style): `CloudRoutedSummaryUsesCloudEngineAndLabelsResult` (Summary=cloud, local never started; result `engine` starts with `cloud:`); `LocalRoutedStillUsesLocal`; `AgreementsLocalWhileSummaryCloud` (per-feature split); `CloudErrorReportsReasonAndDoesNotFallBackToLocal` (cloud throws `cloud auth`; local engine scripted to record calls: zero calls); `PseudonymsReachEngineAndAreRestoredInResult` (subjects client "Иван", engine asserts payload lacks "Иван", echoes `[Клиент]`, result contains "Иван"); `PseudonymizationOffSendsRawText`; `NoApiKeyFailsWithoutRequest`; `CancelNotesAbortsCloudRequest`; `CacheKeyedByEngine`.
- [ ] **Step 2-4:** FAIL → implement → PASS (existing worker tests stay green). **Step 5:** Commit `feat(prompter): route requests to local or cloud engine per feature`.

### Task 5: Parallel summary leaves (seam)

**Files:** Modify `src/call_prompter/summary_pipeline.h/.cpp` (leaf loop only), `src/pages/transcript_page/prompter_worker.cpp` (pass `parallelism = 4` for cloud), `test/summary_pipeline_tests.cpp`.

**Interfaces:** `SummaryContext::parallelism = 1` (int). With `>1`, leaf calls of `chunks` run on a bounded pool (`std::async`/`QtConcurrent` with limit), results are merged in chunk order exactly as today; the retry-as-halves logic runs inside each leaf task; `calls` counter becomes `std::atomic_int`; `generate` must be thread-safe (documented).

- [ ] **Step 1: tests:** `ParallelLeavesProduceSameSummaryAsSequential` (scripted deterministic generate, compare `QJsonObject`s for parallelism 1 vs 4); `ParallelLeavesRunConcurrently` (generate blocks on a barrier of 3 until 3 callers arrive, times out otherwise); `ParallelLeafFailureHandledLikeSequential` (one leaf invalid → half-retry, then dropped); `ParallelismNeverExceedsLimit`; `BudgetErrorStillThrowsInParallel`.
- [ ] **Step 2-4:** FAIL → implement the seam minimally → PASS, existing 18 summary tests unchanged. **Step 5:** Commit `feat(summary): optional parallel leaf calls`.

### Task 6: Controllers pass subjects; engine label and errors in UI

**Files:** Modify `src/pages/transcript_page/call_notes_controller.cpp`, `call_notes_finalizer.cpp`, `call_prompter_controller.cpp`, `ai_summary_widget.cpp`, `notes_workspace_bridge.cpp/.h`, `qml/NotesWorkspace.qml`, `prompter_panel.cpp`, `failure_text.cpp`, relevant tests.

- Requests built by the controllers/finalizer add `"subjects"` from the DB: client name/last name/patronymic parts, client phone/email, specialist display name (`callDisplayName()`) — only when the feature is routed to cloud (avoid DB work otherwise).
- On `handleResult`, store `engine` into the saved summary/live JSON (`"engine"` key next to `format`) and expose it through the bridge as `engineLabel` ("Cloud · <model>" / "Local · Gemma"); shown as a small caption on the summary card and the live points header; prompter panel shows the engine for agreements.
- Error texts: `reason=="cloud"` with `cloud_detail` → "Cloud unavailable: invalid API key / model not found / rate limit / server error / no connection / no API key"; `timeout` keeps the existing text. No "retry locally" automation.

- [ ] **Step 1: tests** (extend existing suites): `SummaryRequestCarriesSubjectsOnlyForCloud`; `ResultEngineLabelStoredAndExposed`; `CloudFailureShowsCloudUnavailableNotLocalError`; `AiSummaryWidgetShowsEngineCaption`; legacy summaries without `engine` show no caption.
- [ ] **Step 2-4:** FAIL → implement → PASS (run `call_notes_controller`, `notes_widgets`, `call_notes_page`, `client_notes_ui`, `prompter_panel`). **Step 5:** Commit `feat(notes): show producing engine and cloud errors`.

### Task 7: Settings page, consent dialog, Test connection

**Files:** Create `src/app/cloud_settings_panel.h/.cpp`, `src/app/cloud_consent_dialog.h/.cpp`; Modify `src/app/settings_dialog.cpp/.h`, `src/app/application.cpp` (wire worker cloud factory: reads settings + `CloudKeyStore`, reconfigure on settings change; start paths for cloud-routed features do not require a local model — `generationRequested` handlers enable generation when the feature is cloud), `test/cloud_settings_tests.cpp` (panel tests, offscreen).

- Panel fields: base URL, model, context size, API key (password field; write-only: shows "stored in keychain" placeholder, "Remove key" button), pseudonymization checkbox (on), per-feature combos (Local/Cloud) disabled until consent, "Enable cloud…" button → consent dialog (host, what is sent, personal-data and cross-border warning, user's responsibility, pseudonymization limits sentence), "Revoke consent", "Test connection" (runs `probeCloud` on a worker thread; shows ok/reason; records nothing).
- [ ] **Step 1: tests:** `FeatureCombosDisabledWithoutConsent`; `AcceptingConsentEnablesCombos`; `ChangingUrlRevokesConsentAndResetsCombos`; `KeyFieldNeverPrefilled`; `TestConnectionShowsRejectionForUnsupportedProvider` (fake server from Task 1); `ConsentDialogNamesHost`.
- [ ] **Step 2-4:** FAIL → implement → PASS. **Step 5:** Commit `feat(settings): cloud LLM settings page and consent`.

### Task 8: Translations, docs, version, full run

**Files:** `translations/*.ts` (EN/RU for all new `tr()` strings via `lupdate`), `CHANGELOG.md`, app version per project convention, short user doc (provider setup incl. GigaChat-via-proxy note, privacy limits).

- [ ] **Step 1:** Run `lupdate`, fill RU, ensure no unfinished entries for new strings (`grep -c 'type="unfinished"'` unchanged or lower).
- [ ] **Step 2:** Configure build in the distrobox with the prebuilt vcpkg flags, `git submodule update --init third_party/qlementine`, build `-j4`, `ctest -j4`. Expected: only the known pre-existing failures (MainWindowCallsTabTest ×2 SEGFAULT; parallel flakes DatabaseTest.AddClientAndEvent, RecurrenceUtilsTest.SeriesWithoutPinnedTimezoneKeepsLegacyCalendar).
- [ ] **Step 3:** Run the dev app only via `scripts/run-dev-isolated.sh` against a local fake server (script a tiny Python `http.server` returning a schema-conformant summary) and confirm: summary produced, engine caption "Cloud · fake-model", unreachable server shows "Cloud unavailable" with no fallback. Give the user steps to try their real provider.
- [ ] **Step 4:** Save a short hand-over note to the shared notes folder; commit `docs/i18n: cloud LLM engine`. Do not push.

---

## Self-review

- **Spec coverage:** client/OpenAI-compat + retry/errors (T1), strict json_schema + test-connection (T1,T7), reflect-cpp schemas reused as-is (pipelines already build them; T1 passes them through), per-task engine + context-size budgets (T3 stores context; budgets: cloud path keeps the existing Gemma-sized chunks by the brainstorm decision, parallelism T5 — context-size field is stored for future use/documented), consent bound to URL + revoke (T3,T7), no silent fallback (T4,T6), pseudonymization (T2,T4), keychain (T3), settings page (T7), engine shown (T6), EN/RU + CHANGELOG (T8).
- **Gap noted:** spec says budgets scale with the engine's context; the user chose parallel small chunks instead, so `cloudContextTokens` is validated/stored but not yet consumed. Say so in the settings help text (T7) and the hand-over note.
- **Type consistency:** `Feature`/`featureForTask` (T3) used in T4; `CloudConfig`/`createCloudEngine`/`probeCloud` (T1) used in T4/T7; `Pseudonymizer`/`PseudonymSubjects` (T2) used in T4; `subjects` request key produced in T6, consumed in T4; `engine` result key produced in T4, consumed in T6.
