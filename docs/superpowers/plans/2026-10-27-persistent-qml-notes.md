# Persistent QML notes workspace

Goal: port the approved notes layout with participant selection and a permanent composer.

Architecture: QQuickWidget hosts the new journal surface. ClientNotesPage retains database,
attachments, recurrence linking and navigation; a narrow property/invokable facade serves QML.
Embedded meeting editors retain their existing lifecycle. No demo records enter production.

- [x] Add QML facade for participants, filtered journal, selected document and composer.
- [x] Add full-width QML journal and persistent editor; keep drafts across list navigation.
- [x] Make Notes permanently accessible in main navigation.
- [x] Verify participant isolation, saving and QML loading with the existing system-Qt build.
- [x] Refresh translations and report remaining runtime limitations. No vcpkg build.

Verification: system-Qt Sessio linked successfully; 20 client-note UI, 22 call-note page,
11 note widget and 25 transcript page tests passed (78 total). Both translation catalogs
contain 985 finished entries and no unfinished entries. Dark-theme QML rendering checked
with a synthetic database. The normal-profile desktop session was not restarted.
Independent review found missing attachment access, stale filters and collapsed recording
navigation; all three were fixed and the reviewer confirmed no remaining blocking issue.

Existing work continues on codex/151-call-prompter; GitHub issue lookup currently fails
because api.github.com is unreachable. No commit or push is requested.
