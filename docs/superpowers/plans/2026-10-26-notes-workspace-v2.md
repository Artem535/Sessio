# Notes workspace v2 implementation plan

**Goal:** Transfer the approved document variant from `docs/prototypes/notes-workspace-v2.html` into Qt Widgets.

**Architecture:** Keep existing controllers and storage. Put history tools in the left rail, render selected meeting as one document, and put notes before the optional transcript source panel. Keep palette based colors for both application themes.

**Constraints:** No vcpkg configure/install/build. Preserve working database and unrelated changes. No commit or push.

## Screenshot corrections

The real-data screenshots exposed repeated empty note sections: the journal created a section and action pair for every call material of an appointment. Consolidate all saved notes into one section, skip empty content, and move per-recording navigation into a dated menu while preserving every material/transcript. Replace the call page's unbounded date buttons with a scrollable preview list. Anchor the document beside history, remove the empty embedded feed allocation, and hide the empty prompter in ended calls. Verify using synthetic appointments containing eight empty recordings and a call page with 24 history entries, including dark-theme screenshots.

- [x] Recompose client journal into history rail and unified document; retain attachment, export, print and edit actions.
- [x] Give the note composer a document appearance and compact save action.
- [x] Put meeting notes first and make transcript an optional right panel; source links reveal it.
- [x] Build using the existing dependency installation, run affected UI tests and inspect the rendered result.
- [ ] Restart with the normal application configuration. Automatic approval review rejected stopping the two previous application processes because unsaved user data may be lost; await explicit confirmation.

Verification: native application build succeeded in `/tmp/sessio-ui-source/build`; 68 affected UI tests passed (25 transcript, 18 client notes, 4 call notes page, 11 notes widgets, 10 history). The final call-document sizing and source-scroll changes were rebuilt and their 29 tests rerun. Wide/narrow synthetic snapshots are `/tmp/sessio-v2-workspace.png` and `/tmp/sessio-v2-journal.png` (plus `-narrow.png`). No vcpkg build/install/configure was invoked. Both translation catalogs contain 897 finished translations and zero unfinished entries. No commit or push was performed.
