# Call transcription — release (phase 5)

Scope: release preparation for issue #118 after interface integration. Saved transcripts use a separate page inside Sessio, confirmed by the user on 2026-10-07. PR #94 remains the base dependency; re-query its state before choosing a PR base or rebasing.

## Entry checks

- Review the phase-4 diff against `35196c9`, including untracked new sources if not committed yet. Tasks 1–4 were independently reviewed; task-5 lifecycle fix and final integration still need independent review because the account limit stopped reviewers. Do not claim that review passed.
- Verify the worktree ledger `.superpowers/sdd/2026-10-07-call-transcription-ui/progress.md` and current Git state, rather than repeating completed tasks.
- Build named targets using build-tr and the existing prebuilt vcpkg cache. Never reinstall dependencies or run a bare full build with the known LiveKit/libcurl link failures.

## Release tasks

1. Resolve remaining review findings and verify consent, revoke, replacement-call identity, stopping and shutdown with real audio. The current aboutToQuit handler starts nonblocking teardown; process exit can precede detached database finalisation. Crash recovery repairs recording rows on next specialist start. Determine and document whether a bounded shutdown completion hook is needed before release.
2. Warn before deleting an event with transcripts, including recurring overrides. Preserve phase-4 module boundaries and recording mutation guards. Fix or document names for participants already present when recording starts, and provide the production model id when creating transcript rows instead of NULL.
3. Raise the version in both CMakeLists.txt and src/app/application.cpp and write the user-visible CHANGELOG entry. Choose the version from current branch/main state at execution time.
4. Verify sherpa-onnx, ONNX Runtime, GigaAM v3 and Silero VAD licences from primary sources and add third-party notices. Document package-size growth and the relation to PR #71.
5. Update the ASR spike ADR, privacy documentation (local processing, no audio files, per-call consent, revocation, manual deletion, backup inclusion, no automatic retention expiry), and recurring-occurrence schedule revision behavior.
6. Add phase-3/4 fake-based test targets to CI without forcing GUI/model tests into unsupported jobs. Keep real-model tests Linux-only. Translation scan must include every new CMake target and both TS files must have no unfinished entries.
7. Inspect staged package trees and dependency resolution for RPM, AppImage, Windows and macOS. Never install a test RPM on the host. Verify ONNX Runtime/sherpa shared libraries, model location, loader paths and actual installed launch separately from metadata checks.
8. Prepare a manual test checklist: specialist/client mode; setting disabled; models missing; rejection and acceptance of consent; local microphone mute; two consented participants; stop/rejoin/revoke-keep/revoke-delete; editing/deleting/reviewing drafts; recurring occurrence; application shutdown/restart; backup/restore; Windows/macOS runtime. A fake-engine test is not real-call accuracy evidence.
9. Re-query PR #94 and branch state. Rebase only after its integration when appropriate; preserve unrelated worktree/submodule dirt. Open the linked #118 PR after release checks, attach it to the chat, and describe tested behavior and manual/environment limitations accurately. Do not merge or close the issue prematurely.

## Local preview

From the transcription worktree:

```sh
SESSIO_MODELS_DIR="$PWD/build-tr/transcription-models" scripts/run-dev-isolated.sh "$PWD/build-tr/Sessio"
```

The isolated profile has no real client data or configured call backend. Choose specialist mode to see the full interface. A transcript page requires an event with saved phrases; widget/navigation tests use temporary populated data. Configure a consented test call separately for runtime verification.
