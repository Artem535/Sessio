# Call Prompter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Встроить локальные проверяемые карточки суфлёра в панель заметок и транскрипции и добавить настройки автозапуска.

**Architecture:** Qt-контроллер хранит очередь и решения пользователя, отдельный постоянный процесс выполняет CPU-инференс Gemma. Новые завершённые реплики поступают из контроллера транскрипции. Принятие карточки использует клиентскую ленту заметок.

**Tech Stack:** C++20, Qt Widgets/QProcess, существующая DuckDB, Python worker и установленный LiteRT-LM CPU.

**Spec:** `docs/superpowers/specs/2026-10-13-call-prompter-design.md`.

## Global Constraints

- База `worktree-videoprovider-domain-layer`; #151, отдельная `codex/151-call-prompter`.
- Только локальный CPU; никаких Radeon-прогонов или автоматического скачивания модели.
- По умолчанию оба автозапуска false; согласие обязательно; никакого автоповтора после stop/revoke/error.
- Все изменения договорённостей проверяются пользователем; содержимое разговора не выводить в логи.
- Сохранить пользовательские заметки и непрерывность звонка при любой ошибке worker.
- Версия, CHANGELOG, EN/RU, migration/restore tests обязательны перед PR.

## Review Focus

- Отказ в согласии, модальное окно и завершение звонка: автозапуск не создаёт сессию после detach.
- Повторный connect того же звонка: ручная остановка сохраняется.
- Поздний ответ после ручной правки: не перезаписывает решение, реплики остаются для повторного анализа.
- Standalone без клиента: карточка сохраняется до привязки, в чужую ленту ничего не записывается.
- Delete/revoke/restore: карточки следуют жизненному циклу транскрипции, сохранённая заметка не дублируется.

### Task 1: Persistent auto-start and consent-safe lifecycle

Files: `src/widgets/app_settings.{h,cpp}`, `src/pages/transcript_page/transcription_settings_panel.{h,cpp}`, `call_transcription_controller.{h,cpp}`, `src/app/application.cpp`; tests: `test/transcription_settings_panel_tests.cpp`, `test/call_transcription_controller_tests.cpp`.

Interfaces: add `app_settings::transcriptionAutoStart()`, `setTranscriptionAutoStart(bool)`, `prompterAutoStart()`, `setPrompterAutoStart(bool)` in the existing settings namespace. Add hook `std::function<bool()> autoStartEnabled` to `ControllerHooks`. Auto-start uses `onTranscribeRequested()` after connected state, with a per-attachment attempt marker.

- [ ] Add failing controller cases: enabled auto-start + connected -> one consent and one start; rejected consent -> no start; stop + repeated connected -> no restart; detach during consent -> no start; next call -> new attempt. Add settings persistence and dependency cases.
- [ ] Run the two existing test targets with the new cases before production edits; record actual failures.
- [ ] Add settings keys and checkbox; connect hooks in application; implement one-attempt state transitions without opening/changing tabs on automatic start.
- [ ] Run settings/controller suites; preserve existing manual-start and consent cases.

### Task 2: Agreement model, migration and notes handoff

Files: create `src/call_prompter/prompter_types.h`, `agreement_store.{h,cpp}`; modify existing DB migration registration, backup queries, `src/pages/client_notes_page/client_notes_page.{h,cpp}`. Tests: create `test/prompter_store_tests.cpp`, `test/prompter_notes_tests.cpp` and register in `test/CMakeLists.txt`.

Interfaces: `AgreementCard` carries card/transcript IDs, kind, action, evidence IDs, optional target ID, decision, revision and optional saved note ID. Store methods return explicit success/error; a notes handoff receives card ID, expected revision and client ID and persists note + saved-note association in one transaction. Resolve the current migration and note API names from this branch before editing their call sites.

- [ ] Write failing tests for standalone storage, accepted/edit/cancel decisions, double-save idempotency, wrong-client rejection, deletion/revoke and backup/restore round-trip.
- [ ] Run new DB tests and observe missing behavior.
- [ ] Add transcript-owned tables and migration using existing schema conventions; implement transactional handoff, source associations, and existing deletion semantics.
- [ ] Run DB/backup/note suites; check rollback on failed save keeps the card reviewable.

### Task 3: Local worker protocol and bounded inference queue

Files: create `src/call_prompter/prompter_worker.{h,cpp}`, `call_prompter_controller.{h,cpp}`, `CMakeLists.txt`, `scripts/call-prompter/worker.py` and final extract/match prompts; modify transcription controller for source signals. Tests: create `test/call_prompter_controller_tests.cpp`, worker-protocol Python tests with a fake engine.

Interfaces: controller slots `beginTranscript(qint64)`, `addPhrase(DuckTranscriptPhrase)`, `pause()`, `resume()`, `endCall()`; signals `cardsChanged()`, `statusChanged(QString)`. Transcription publishes transcript identity and persisted completed phrases. Process requests/replies carry protocol_version, request_id, session_generation and ledger_revision; only one request in flight.

- [ ] Write failing fake-worker tests for FIFO/coalescing, duplicate IDs, bounded output, timeout/EOF/invalid JSON, invalid evidence/target IDs, stale generation/revision, pause/resume and pending cancellation.
- [ ] Run protocol/controller tests before implementation.
- [ ] Adapt only the final spike v7 extraction/matching logic into persistent worker; load configured model once, use CPU, validate all messages, keep stderr content-free. Implement QProcess lifecycle and generation/revision guards. Retain unprocessed source phrases on stale-result retry.
- [ ] Run fake-engine suites, then a short real Gemma smoke with the configured runtime and synthetic accepted/rescheduled/cancelled agreements. Record actual latency and output; do not run GPU.

### Task 4: Panel, source navigation and clarification suggestions

Files: create `src/pages/transcript_page/prompter_panel.{h,cpp}`; modify `call_side_panel.{h,cpp}`, `transcript_panel.{h,cpp}`, `src/app/application.cpp`, settings runtime selection and CMake registration. Tests: `test/call_side_panel_tests.cpp`, new `test/prompter_panel_tests.cpp`.

Interfaces: side panel `showPrompter()` and unread count; prompter emits `acceptRequested(cardId)`, `editRequested(cardId)`, `rejectRequested(cardId)`, `sourceRequested(phraseId)`. Source navigation selects the transcript page and focuses the identified phrase. Suggestions are evidence-linked and separate from agreement events.

- [ ] Add failing panel tests for three-page navigation, one shared suggestion, unchanged active page on new card, source jump, unavailable runtime, save failure, and counter updates.
- [ ] Run panel tests before edits.
- [ ] Implement cards and actions; connect store/controller/notes; show missing runtime/model and current processed time. Preserve scroll/edit state. Add constrained missing-detail suggestions with an independent synthetic quality set; do not infer guidance quality from agreement evaluation.
- [ ] Run UI/controller tests and render the real panel in light/dark narrow layouts; inspect screenshots and keyboard navigation.

### Task 5: Integrated verification and delivery

Files: `CMakeLists.txt`, `src/app/application.cpp`, `CHANGELOG.md`, `translation/app_{en,ru}.ts`; add `docs/verification/151/README.md`.

- [ ] Test a real call with simultaneous video/audio, ASR and CPU inference, manual notes, source links, stop/restart, call end and reopening persisted cards. Record latency/backlog and any unverified platform paths.
- [ ] Run relevant application/DB/transcription/prompter tests and required build checks. Verify worker failure does not block media/UI.
- [ ] Bump both app versions to the next free patch version based on the implementation branch; update CHANGELOG. Run `cmake --build build-release --target update_translations`, finish EN/RU translations, and check `git diff --check`.
- [ ] Review the complete diff against spec; report implemented, test-passed, runtime-verified and remaining limits separately. Prepare a linked draft PR against the selected integration branch; push/publication only under the session's authorization.
