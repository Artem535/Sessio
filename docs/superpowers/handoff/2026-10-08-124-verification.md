# Issue #124 — independent transcripts verification

Date: 2026-10-08. Local branch `codex/124-standalone-transcripts`.

## Implementation and review boundaries

Transcripts can be created with SQL NULL event context and no client card, retain
their own identity and consent lifecycle, and appear in the general Transcripts
page. Review, edit, text export and explicit existing-client assignment operate by
transcript ID. Deleting a calendar event, recurring override or client removes
the corresponding association and preserves transcript phrases. Recording rows
remain protected from editing/deletion. Unsaved review edits block navigation to
another transcript.

Schema 1 migrates to schema 2 once: nullable event ID and independent
TranscriptClient links backfilled from EventClient. Manual unlink survives
reopening; migration failure rolls back its DDL/backfill/version update. Future
schema versions are rejected before mutation. Backup format remains 1; restore
migrates legacy schema in staging before replacing the target. Both current
standalone and legacy transcript round trips preserve data.

Version is 0.2.18 in CMake and Application; CHANGELOG contains both 0.2.18 (#124)
and 0.2.17 (#123). The existing unrelated qlementine dirty submodule was preserved.
No push, PR, deployment, branch merge or issue closure was performed.

Local commit order:

1. `1e3047c`: owned #124 implementation checkpoint, parent `77bbf5e`.
2. `a4a5f13`, `5bbc606`, `b145752`, `92b8fe4`: cherry-picked #123 commits
   from original `0a3c9f0`, `0549049`, `d1d3835`, `ceb0863`, respectively.
   Only release version/changelog conflicts required manual resolution.
3. Follow-up #124 correction: Application button availability no longer requires
   an event; review header resolves independent TranscriptClient links; actual
   MainWindow standalone navigation and combined nullable-event/audio-gap/clock
   regression were added.

Review the checkpoint `77bbf5e..1e3047c` and the #124 follow-up after `92b8fe4`.
Review the combined final session/controller against both contracts: nullable
event plus #123 call clock, resetTrack and audioGap behavior. Do not interpret all
of `77bbf5e..HEAD` as newly authored #124 code.

## Fresh verification

Fedora host, Qt 6.11.2; build-release uses the read-only shared vcpkg installation
with manifest installation disabled, and the configured LiveKit curl library.
Runtime commands below use:

```bash
export QT_QPA_PLATFORM=offscreen
export LD_LIBRARY_PATH=/home/a.durynin/.local/share/sessio-dev/curl
```

All final checks exited 0:

| Check | Evidence |
| --- | --- |
| Focused build | Database, backup, controller, session, workflow model, transcript review/list/client dialog targets built |
| `rtk cmake --build build-release --target Sessio --parallel 4` | Full application linked successfully after final production changes and translation regeneration |
| `build-release/test/Sessio_database_transcript_tests --gtest_brief=1` | 42/42 passed |
| `build-release/test/Sessio_backup_tests --gtest_brief=1` | 55/55 passed, including current standalone and legacy schema 1 restore, future schema rejection and invalid restore preservation |
| `build-release/test/Sessio_call_transcription_controller_tests --gtest_brief=1` | 29/29 passed, including no-event consent/start/stop, rejected consent, attachment guards, failure and leave |
| `ctest --test-dir build-release --output-on-failure -R '^SessionTest\.|^SerialExecutorTest\.'` | 39/39 passed before adding the combined regression; retained all #123 tests |
| `Sessio_transcription_session_tests --gtest_filter=SessionTest.StandaloneLateStartAndAudioGapPreserveClockAndTranscript --gtest_brief=1` | New regression 1/1 passed: NULL event, late start at 20 seconds, local gap drops PCM, remote continues, reset splits local phrases, unchanged transcript ID and draft finalization |
| Workflow model tests | 2/2 passed with actual local ASR and prerecorded speech; includes standalone without event/client |
| `ctest --test-dir build-release --output-on-failure -R '^TranscriptPageTest\.|^TranscriptListTest\.|^TranscriptClientTest\.|^MainWindowCallsTabTest\.|^CallPageTest.TranscribeButton'` | 29/29 passed after final correction |
| `rtk cmake --build build-release --target update_translations --parallel 4` | Both catalogs regenerated; 713 finished strings, zero unfinished; two obsolete event-client header strings removed |
| `rtk git diff --check` | Passed |

The initial 126-test filtered run found one old review-header expectation based on
EventClient. The final regression explicitly verifies that later EventClient
changes do not silently attach a transcript; explicit TranscriptClient assignment
and unlink update its header. This corrected UI suite passed in full.

## Runtime QA boundaries

Qt offscreen tests operate real CallPage/MainWindow/TranscriptPage/list/dialog
widgets and a real temporary DuckDB. The list scenario closes/reopens Database,
opens the standalone ID, edits a persisted phrase, exports text, drives the modal
client checkbox dialog to attach/unlink, reloads the list and deletes the record.
The MainWindow scenario displays the standalone list and review without creating
an event or client. An offscreen screenshot was saved locally at
`/tmp/sessio-124-standalone-review.png`; it is a fixture screenshot, not a styled
production acceptance image.

Controller/session tests use a fake video provider; the real-ASR workflow feeds
prerecorded PCM into the local engine. These prove storage/UI/lifecycle contracts
and model integration, not real-call media QA. No two-device call, hardware
microphone switch, group/30-minute call, packaged runtime or Windows acceptance
was performed here. The inherited #123 hardware/device gates remain open. No
production database, client PII or invitation credentials were used.

## Review correction wave 1

Base: `d54aaca`. Formal review requested one P2 correction: Delete client hides
event-linked cards, but its early return left their TranscriptClient links intact.
`remove_client` now detaches TranscriptClient for both successful deactivation and
physical deletion, within the same transaction as the card mutation. An error
rolls back the detach. Event history, transcript identity/status/phrases and other
client associations remain intact.

Three new real-DuckDB regressions cover event-linked deactivation, standalone
physical deletion and failed physical deletion. The failure is forced with a
temporary FK reference to Client, proving that a rejected deletion preserves the
previous transcript links. Before production correction, the deactivation and
rollback tests failed; the standalone successful deletion test passed. After the
correction:

```bash
rtk cmake --build build-release --target Sessio_database_transcript_tests --parallel 4
rtk proxy build-release/test/Sessio_database_transcript_tests --gtest_brief=1
rtk git diff --check
```

All commands exited 0; final focused DB target passed **45/45**. Other suites were
not rerun for this bounded database-only change; their earlier evidence and
runtime acceptance boundaries above remain unchanged. No translation strings,
schema version or release version changed in this correction.
