# QML meetings and transcripts

Approved base: sessio-app-v5.html meetings/transcript views; full available width,
palette-derived colors and read-first transcript navigation.

- [x] Port call entry to QML while retaining token/device-check/session gates.
- [x] Add persisted material history supplied by the application, with notes/transcript links.
- [x] Port transcript library and reader to QML; preserve legacy mutation tools.
- [x] Test loading, data/action contracts and existing call/transcript regressions.
- [x] Refresh translations and verify system-Qt build without vcpkg.

Verification: Sessio linked in build-ui-preview-source/build with system Qt and
VCPKG_MANIFEST_INSTALL=OFF, no toolchain. Tests: transcript reader 26, transcript library 6,
call entry/settings 13, call flow 20, existing QML notes 22 (87 passed).
Call-flow tests needed execution outside the sandbox because PulseAudio initialization
blocked inside it; their final run used a local fake token server/video provider and passed.
Both catalogs contain 1029 finished entries, no unfinished translations. QML reader and
meeting surfaces rendered with synthetic data. No real call, production database mutation,
desktop restart or push was performed.

Independent review found calendar preselection/deep-link and source-focus regressions.
Signals now select the requested QML meeting/open the prefilled form, and source links
route through focusPhrase; tests cover preselection and invitation opening. Re-review clear.

Header follow-up: Notes, Meetings, transcript library and reader now expose headerControls()
registered with MainWindow::setPageCustomWidget, exactly like Clients/Calendar. QML keeps
its standalone header when embedded elsewhere and hides it in the application shell.
Participant selection, search, transcript actions and history toggle stay connected to their
page state; Calls controls disable during device-check/active call. Native transcript actions
respect unsaved phrase edits. Header/property synchronization checks and 65 affected tests
passed; system-Qt Sessio linked and catalogs have 1037 finished entries, none unfinished.

No real Jitsi/SberJazz or cloud integration is implied by this UI port. Reuse current
backend capabilities. No commit/push requested; continue current feature branch.
