# Handoff — live call transcription (#118)

Written 2026-10-07 for the next agent. Read this first, then the plan for the phase you execute. Everything needed to continue is in this file or in the files it points to; nothing depends on the previous conversation.

## 1. Where things stand

| Item | Value |
|---|---|
| Issue | #118 "Live call transcription: local GigaAM + VAD, consent, editable transcript (P2)" (open) |
| Branch / worktree | `feat/118-call-transcription` in `/home/a.durynin/Projects/C++/PsyClientManager/.worktrees/transcription` (base is the video branch, PR #94; no PR for this branch yet) |
| Pushed HEAD | `e5e7e46` (phase 4 plan); last code commit `56d7cc3` |
| Done | Phase 0 build gate, 1 engine, 2 data layer, 3 session layer (all reviewed, fix waves applied, pushed) |
| Next | **Phase 4 (interface), plan written, not started:** `docs/superpowers/plans/2026-10-07-call-transcription-ui.md` (8 tasks) |
| After that | Phase 5 release (see section 7) |

Specs and plans (all committed):
- Spec: `docs/superpowers/specs/2026-10-06-call-transcription-design.md` (kept truthful to the code; *Lifecycle* and *UI* are binding for phase 4)
- Plans: `docs/superpowers/plans/2026-10-06-call-transcription-engine.md` (phases 0–1), `…-data.md` (phase 2), `2026-10-07-call-transcription-session.md` (phase 3), `2026-10-07-call-transcription-ui.md` (phase 4)
- Engine measurements/ADR: `docs/asciidoc/15-realtime-asr-spike.adoc` and spike code on branch `spike/116-asr-model-bench`

## 2. What the code does (phases 0–3)

- `src/transcription/` (Qt-free, `pcm::transcription`): `Resampler`, `PhraseSegmenter` over `IVoiceActivityDetector`, `ISpeechRecognizer`, `TranscriptionEngine` (one shared recogniser, per-track VAD, one segmenter thread, one decode worker), `ModelLocator`, sherpa-onnx backend lib `Sessio_transcription_sherpa` (GigaAM v3 RNN-T + Silero VAD, Russian only).
- `src/database/`: tables `Transcript`/`TranscriptPhrase`, methods `add_transcript`, `get_transcripts_for_event`, `set_transcript_status`, `revoke_transcript_consent`, `delete_transcript`, `delete_all_transcripts`, phrase CRUD, `finalize_interrupted_transcripts`, `purge_orphan_transcripts`, `rename_transcript_speaker`. Both FKs were dropped (DuckDB limitation), integrity is in code. All transcript methods that may run on a worker thread use a private connection.
- `src/video/audio_sink.h` + taps in `AudioCaptureAdapter` / `RemoteAudioPlayer` + `VideoProvider::setAudioSink` (mic tap respects mute; no sink without a session).
- `src/call_transcription/` (library `Sessio_call_transcription`): `PhraseWriter` (own thread, private DB connection), `SerialExecutor`, `TranscriptionSession` (consent-gated, model load/addTrack/stop on its own thread, revoke upgrades a graceful stop, destructor never blocks), `engine_factory` (`makeProductionEngineFactory`, `transcriptionModelsAvailable`, `sanitizeLoadError`), `CallEventResolver` (materialises a recurring-series occurrence; virtual id decode `julian = (enc % 1e6) + 2e6`).
- `src/app/application.cpp`: startup recovery (`finalize_interrupted_transcripts` + `purge_orphan_transcripts`), `mTimelineModel`, `mCallEventResolver`, private `resolveCallEvent(int64_t)` (currently `[[maybe_unused]]`, phase 4 uses it).
- Hard requirements from the user (do not regress): the speech model is never loaded or run on the GUI thread; consent is per session and explicit; no audio sink without a session; module `SESSIO_ENABLE_TRANSCRIPTION` is ON on all platforms (CI builds Linux/Windows/macOS; the user tests Windows/macOS runtime by hand).

## 3. Environment (read before building anything)

- Shell is fish on a Fedora host; the Ubuntu distrobox container **cannot build**. Run builds/tests on the host: `distrobox-host-exec cmake --build build-tr --target <target> --parallel 3` and `distrobox-host-exec ctest --test-dir build-tr -R <regex> --output-on-failure`. gtest binaries that need widgets: `QT_QPA_PLATFORM=offscreen`.
- Build dir `build-tr` (git-excluded) is configured against the **prebuilt vcpkg tree** `.claude/worktrees/videoprovider-domain-layer/build/vcpkg_installed`. NEVER let vcpkg rebuild dependencies (DuckDB takes ages; the user was angry about this). Re-configure only if you must, with exactly: `distrobox-host-exec cmake -S . -B build-tr -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_TOOLCHAIN_FILE=/home/a.durynin/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_MANIFEST_INSTALL=OFF -DVCPKG_INSTALLED_DIR=/home/a.durynin/Projects/C++/PsyClientManager/.claude/worktrees/videoprovider-domain-layer/build/vcpkg_installed -DPCM_BUILD_TESTS=ON -DLIVEKIT_CURL_LIBRARY=$HOME/.local/share/sessio-dev/curl/libcurl.so.4`. A fresh worktree also needs `git submodule update --init third_party/qlementine` (the submodule shows ` m third_party/qlementine` in `git status`: ignore it, never commit it).
- `LIVEKIT_CURL_LIBRARY` points at a libcurl copied from the Ubuntu container; it makes the `Sessio` executable link locally. Six unrelated test targets still fail to link (livekit needs `curl_*@CURL_OPENSSL_4`): `Sessio_frame_convert_tests`, `…audio_capture_adapter_smoke_test`, `…video_capture_adapter_smoke_test`, `…livekit_video_frame_source_smoke_test`, `…remote_audio_player_lifecycle_test`, `…livekit_video_provider_smoke_test` (+ `Sessio_fake_client`). **Never run a bare full build** (it ends in "subcommand failed"); build named targets. A full `ctest -LE models` shows 12 pre-existing failures: those 6 (Not Run) plus six calendar pixel/geometry tests (`MonthCalendarTest.DenseDayOverflowSelectsEveryHiddenEventAtSmallHeight`, `CalendarLayoutTest.*` ×5) — not caused by this branch. `BackupValidatorTest.DetectsCorruptedEntry` was flaky once.
- Models for model tests are in `build-tr/transcription-models` (fetched by `cmake/FetchTranscriptionModels.cmake`, pinned SHA-256). Label `models` tests skip themselves when absent.
- GUI runs: only through `scripts/run-dev-isolated.sh <binary>` (isolated HOME/XDG so the user's real data is never touched). A real LiveKit call cannot be exercised in the sandbox.
- Never `pkill -f` (it kills your own shell); kill by PID. Never bare `git stash`. Do not push to `main`; do not develop on `main`.
- Git trailer for commits: `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` (use the model that you are). Run `git diff --check` before each commit. Push through plain `git push` (rtk optional).
- Repo rules to keep in mind (AGENTS.md): C++20, two-space indent, `public:` at column 0, `} // namespace`; every MR needs a version bump in BOTH `CMakeLists.txt` and `src/app/application.cpp` plus a `CHANGELOG.md` entry; new/changed `tr()` strings need `cmake --build build-tr --target update_translations` and no `type="unfinished"` entries in `translation/app_ru.ts` / `app_en.ts` (CI fails otherwise); schema changes need a migration + restore round-trip test (none planned).

## 4. How the work was run (repeat this workflow)

The user chose: one written plan per phase, executed with `superpowers:subagent-driven-development`:
1. One **fresh Sonnet implementer** per task (prompt = task text from the plan + environment block from section 3 + "TDD: show red then green" + "commit once with the task's message + trailer, do not push" + "report files, red/green evidence, SHA, deviations"). Agents sometimes skip the red step: demand it explicitly.
2. One **Sonnet task reviewer** (read-only: no build/edit/commit) per task or per pair of small tasks, with a focused checklist (thread-safety, lifecycle, test quality, style). Ranked findings → **one fix wave** (a single implementer commit `fix(...): … per review`), then scoped re-review only if the fix was risky.
3. After the last task: one **Opus whole-phase review** (read-only) of `git diff <phase-base>..HEAD`; fix wave for "must fix before PR"; defer the rest into the ledger.
4. Run at most one build-capable agent at a time in the worktree (one ninja build dir); read-only reviewers can run in parallel with an implementer.
5. Push after each phase's final fix wave; keep the git-ignored ledger `.superpowers/sdd/<plan-name>/progress.md` updated (it is worktree-local and not in git; this handoff plus the plan files are the durable record).
Prefer Sonnet for dispatches in this project (Opus once hit a rate limit mid-task); Opus only for the final phase review.

Review lessons worth reusing: tests must assert behaviour (no `SUCCEED()` tautologies, no sleeps as synchronisation); thread tests need `--gtest_repeat=20`+; prove a regression test goes red on the old code; every shutdown path must finalise the transcript row (never leave `recording`).

## 5. Phase 4 — what to do now

Execute `docs/superpowers/plans/2026-10-07-call-transcription-ui.md` task by task (1 → 8; Task 8, translations, last). Points that the plan states but are easy to miss:
- `CallsPage`/`CallPage` must not gain dependencies on `Database`, `ClientNotesPage` or any transcription library (call-UI module boundary).
- Controller flow is binding: consent dialog accepted in the same flow before any session; resolve the event id (negative = recurring occurrence that must be materialised via `Application::resolveCallEvent`); after materialising, reload the timeline day; delete-after-revoke only after the session emitted `revoked`.
- DB calls `set_transcript_status`/`revoke_transcript_consent`/`delete_*` need no live writer: disable the UI for transcripts in status `recording`.
- Failure reasons from `TranscriptionSession::failed()` are raw English diagnostics: map them (Task 2 `userFacingFailure`), never show them.
- Open design choices the user has not explicitly confirmed (listed in the plan summary): the transcript review UI is a dialog, not a page; Start is available from both the control-bar button and the panel; a call joined by code (no calendar event) cannot be transcribed.
- Client mode must have no transcription UI.

## 6. Deferred items from the phase 3 review (still open)

- Phase 4: warn before deleting events that have transcripts ("this and following" series deletion purges them at next start); speaker names are fixed when the track is added (remote participants present when the session starts may have an empty name); the engine locks a track's sample rate on the first push; stop sessions on `aboutToQuit` with a bounded wait (Task 7 does this).
- Phase 5: **packaging check** — `Sessio` now links `Sessio_transcription_sherpa` (sherpa-onnx + onnxruntime shared libs): build/launch the installed app for RPM, AppImage, macOS, Windows; **phase-3/4 tests are not run in CI** (CI configures `-DSESSIO_BUILD_TRANSCRIPTION_TESTS=ON`, which builds only `test/transcription`; the new targets are only guarded by `SESSIO_TRANSCRIPTION_ENABLED`): add them to a CI step; document that materialising a recurring occurrence of a published series creates a schedule revision.
- Minor engine/data deferrals (from phases 1–2 ledgers, low priority): the install smoke test accepts a missing RUNPATH, macOS bundle assertions are thin, `purge_orphan_transcripts` does no CHECKPOINT, CI does not run the fake-based unit tests, `model_id` is stored as NULL by the session (the production factory could supply `gigaam-v3-rnnt`).
- Engine fact: lag is about 0.34 s VAD wait plus decode; measured RTF 0.083 here vs 0.03 in the spike (unexplained); one decode worker serialises simultaneous long phrases by design.
- Revoke while the engine drains: an in-flight `engine->stop()` cannot be cancelled; its output is discarded and the writer gets a zero budget.

## 7. Phase 5 — release checklist

1. Version bump in both `CMakeLists.txt` and `src/app/application.cpp`; `CHANGELOG.md` entry for the user-visible feature.
2. Third-party licence notices for sherpa-onnx, onnxruntime, GigaAM v3, Silero VAD (check each licence; the package ships ~195 MB of model data — note the conflict with PR #71 about package size).
3. ADR/docs: update the #116 ADR status, add a privacy note (local processing, audio not stored, consent, revocation, deletion), a manual test checklist for Windows/macOS.
4. CI: run phase-3/4 tests (`ctest -LE models`) in a job; the model tests job stays Linux only.
5. Open the PR (title/body in repo convention, `Closes #118` only after merge; the branch is based on the video branch: PR #94 must be merged first, or rebase onto `main` afterwards). Use the `ccd_pr` tools after opening a PR (bind it, read CI, offer Auto-fix; never poll CI yourself).
6. Manual verification the user does themselves: Windows and macOS runtime, a real consented two-person call (re-run accuracy on a real call, follow-up from #116).

## 8. Open questions to raise with the user when relevant

- Is a transcript *dialog* acceptable instead of a page? (spec says page)
- Should a call without a calendar event be transcribable (would need a standalone event or an "unlinked transcript")?
- Retention: no automatic expiry exists; the spec explicitly excludes it for this version.

## 9. Ready-to-paste starting prompt

> Continue issue #118 (live call transcription) in the worktree `/home/a.durynin/Projects/C++/PsyClientManager/.worktrees/transcription` on branch `feat/118-call-transcription`. Read `docs/superpowers/handoff/2026-10-07-call-transcription-handoff.md` first. Execute phase 4 from `docs/superpowers/plans/2026-10-07-call-transcription-ui.md` with `superpowers:subagent-driven-development` (Sonnet implementer + Sonnet reviewer per task, Opus for the final phase review, one fix wave), following the environment rules in the handoff (host builds via `distrobox-host-exec`, prebuilt vcpkg, build only named targets). Push when phase 4 passes its final review, then plan phase 5.
