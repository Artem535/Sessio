# Approved modal editor implementation

Reference: `docs/prototypes/notes-editor-v3.html`; user approved transfer to QML.

Scope: dedicated QML modal editor, wide responsive native dialog, separate local edit buffer, explicit save/cancel, unsaved-close guard, context summary/agreements, attachments, note selection for multi-note meetings. Retain current workspace outside the modal. Journal primary action Edit, secondary Transcript, overflow Print/recordings. Existing storage and conflict checks remain authoritative.

Execution: reuse the dirty checkout and existing system-Qt validation build; no vcpkg operations, commits, push or unrelated edits.

- [x] Storage-backed modal session and regression tests for cancel/save/failure/conflicts.
- [x] Dedicated QML editor and guarded host dialog; replace old 500px modal and redundant export footer.
- [x] Journal actions match the approved mockup.
- [x] Translation scan, affected tests, wide/narrow visual checks, fresh review.

Decision: open a persisted draft first when present, otherwise latest saved note, otherwise a new note. Provide a saved-note selector and explicit New note choice, so older entries stay reachable. Preserve saved-entry attachments and independent new drafts. New-note save refuses a concurrently changed draft rather than clearing someone else's edits.

## Result and evidence

`NotesEditor.qml` is a dedicated modal surface with 17px editor text, local rich-text buffer, attachment controls, read-only preview, summary/agreements/key points in the context rail and a fixed save/cancel footer. `NotesEditorDialog` provides a screen-bounded initial 1100×800 native window and guards close, Escape and QML cancel through one reject handler. The old nested workspace and extra export footer are removed from Application's material dialog. Saving refreshes the client journal. The rest of the full workspace remains available outside the dialog; journal rendering remains Widgets, with Edit/Transcript/More on its own bounded toolbar, native popup overflow and a compact client-card action.

Reviewed live-call retargeting was reproduced RED: old modal text saved to the next call material. Fixed by keeping modal selection stable while the adapter rehomes the old controller. Regression GREEN. Also reproduced and fixed popup reparenting losing `Qt::Popup`; the menu remains hidden until requested. A fresh read-only review found no remaining important modal issues. Journal toolbar visual adjustments followed that review and are covered by journal tests and screenshots.

Validation: system-Qt application build without vcpkg, 87 affected tests (22 meeting/editor, 19 journal/client-notes, 25 transcript, 11 notes widgets, 10 history), QML lint, diff whitespace check. Translation scan found 956 strings in each catalog, all finished. Existing third-party lupdate parser warnings remain. Synthetic screenshots: `/tmp/sessio-modal-v3-{dark,light}.png` and `-narrow.png`, modal 1100×800 and 640×700; journal `/tmp/sessio-journal-actions-v3.png` at 2360×1400 and `-narrow.png` at 560×860. Manual native file chooser, IME, installed packages and cross-platform CI were not exercised. The running desktop instance still uses the prior executable until restart.

## 2026-10-27 viewport and theme revision

User explicitly requested full available-window layouts and corrected colors. This supersedes the HTML prototypes' 1040–1200px document/modal width limits: removed caps from the workspace and client journal, made the workspace editor take remaining vertical space, and sized the native editor to available screen geometry with 16px margins. Moved Mark reviewed into the existing transcript navigation row so hiding its neighboring title no longer stretches the button across the page.

Both QML surfaces now use SystemPalette from the application for paper, background, text, border, accent and selected text. Secondary text is a 65% text/paper blend rather than the theme's 20%-alpha disabled placeholder role. The summary surface derives from the shared accent; removed independent blue/green/violet literals. Native journal context caption has matching readable opacity. Workspace QTextDocuments use 17px default font.

Validation: application built; all 87 affected tests pass after the final changes; QML lint and diff check pass. Wide geometry assertions now require the document and composer to fill available space, and QML colors to equal application palette roles. Reviewed synthetic wide and narrow captures `/tmp/sessio-full-workspace.png`, `-sources.png`, `-narrow.png`, `/tmp/sessio-full-journal.png` and `/tmp/sessio-full-modal.png`.

The temporary build disappeared before the launch request. Restored a persistent preview build under `build-ui-preview-source/build`, with copied root CMake skipping the already-dirty Qlementine patch checker and reusing existing same-workspace speech shared libraries. Production CMake and dependencies were not rebuilt through vcpkg. Tests are enabled in that persistent tree. Normal-profile desktop instance was started from this path before the viewport/theme follow-up; it needs restart for these latest changes.
