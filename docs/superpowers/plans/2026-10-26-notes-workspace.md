# Notes workspace implementation plan

**Goal:** Port both screens of `docs/prototypes/notes-workspace.html` to native Qt Widgets.

**Architecture:** Reuse TranscriptPage and CallNotesPage for the split workspace, and ClientNotesPage for the client journal. Keep the existing controllers, storage, attachment handling and review rules. Use the application palette for both themes rather than hard-coded HTML dark colors.

**Spec:** The HTML prototype and the user's approval to transfer both screens.

**Constraints:** Preserve existing uncommitted work. Continue on issue #151's feature branch because the UI depends on its controllers, which are absent from main. Do not commit or push without a separate request. Shared memory tools are unavailable in this session.

**Build constraint:** The user explicitly prohibited vcpkg builds. The initial automatic build chain was stopped. Verify in `build-ui-port` with no vcpkg toolchain, preinstalled libraries in `build-release/vcpkg_installed/x64-linux`, and existing FetchContent sources. Never invoke vcpkg install/build.

## Tasks

- [x] Transcript workspace: test panel toggle, transcript activation and phrase search; finish the partially introduced split-view API, and attach a real CallNotesPage in Application with source navigation and existing finalization wiring.
- [x] Client journal: test selection and combined search/filter behavior; add a timeline beside the detail view, group session materials, preserve attachment and composer actions, and add journal export and selected-protocol printing.
- [x] Match card spacing, typography and ordering to the prototype using native palette-aware surfaces. Preserve compact embedded call views.
- [x] Update version and changelog; regenerate and complete English/Russian translations.
- [x] Build application and affected UI tests, run tests and diff checks; capture representative native screenshots with synthetic data.

## Review focus

- Empty/deleted transcripts must clear the attached notes rather than show another call.
- Transcript rerenders must preserve edits and avoid rebinding the note composer.
- Search and category filters must compose, with no stale selected detail.
- Compact call layouts must remain usable without horizontal clipping.
- Existing unsaved notes and attachment selections must survive journal navigation.

## Verification and decisions

- Native Qt 6.11.2 / GCC 16.2.1 build of Sessio and five UI test executables succeeded in `/tmp/sessio-ui-source/build`; 67 affected CTest cases passed. Full repository suite and a real media call were not run.
- The verification source root symlinks the actual sources. Its temporary root CMake skips the existing Qlementine patch checker: the user's pre-existing submodule changes make neither forward nor reverse patch checking succeed. The submodule itself was preserved. No vcpkg toolchain/install is used in this verification tree; preinstalled libraries are referenced directly.
- The translation update target found 898 existing strings and zero new strings on its final pass; both TS files have no unfinished entries. The lupdate scanner emits existing diagnostics for bundled CTRE headers.
- Synthetic wide and narrow snapshots: `/tmp/sessio-workspace.png`, `/tmp/sessio-workspace.png-narrow.png`, `/tmp/sessio-journal.png`, `/tmp/sessio-journal.png-narrow.png`.
- Independent review found and fixed cross-transcript source navigation, buffered-editor loss, attachment actions, and controller drift between calls. Cached review widgets own fixed controllers; recording review is read-only until transcriptReady, while the live call editor remains available.
- Journal filters use a native combo box for compact layouts. The journal shows completed final AI summaries, preserving the former feed contract. Saved call notes are edited through the existing materials editor; print/export use native Qt printing and Markdown.
- No commit, push, PR or merge was performed.
