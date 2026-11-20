# Meeting follow-up fixes implementation plan

**Goal:** Preserve separate announced meetings, make renaming dependable, and review call results in a maximized window.

**Architecture:** Keep existing CallNotesPage/NotesWorkspaceBridge and finalizer ownership. Extend extraction instructions to explicit personal plans while retaining user review. Open an independent saved-material workspace after the call ends; never reparent the live call page.

**Constraints:** No commits, push, paid inference, or production data changes. Reuse the existing vcpkg install. Run GUI only with scripts/run-dev-isolated.sh.

1. Add a real-model regression for two consecutive announcements at 17:00 and 12:00 tomorrow, including separate source IDs. Adjust extraction and matching instructions so an additional meeting never implicitly reschedules the first. Run pipeline tests and local-model smoke when available.
2. Reproduce event/title editing and the My Day task editor using actual UI interaction. Fix the failing path and cover persisted renamed text.
3. Make the notes header grow with wrapped text; stack actions in narrow cards. Add geometry regression at 340px.
4. Add payment state/action to the notes bridge for linked work events, with database persistence tests. Add a maximized post-call review using an independent controller bound to the completed material, shared finalizer, existing source navigation, and existing calendar callbacks.
5. Build the app and focused tests, update translations, version and changelog; inspect isolated GUI and review the final diff. Report local versus model/device evidence separately.

Additional reported bugs: live key points must omit greetings, audio checks and incomplete ASR fragments, deduplicate repeated points across updates/chunks, and retain concrete substantive facts. Explain the existing Jitsi invitation flow in the join dialog without introducing a second connection protocol.

## Execution record

- Base: 2ecf572f on codex/151-call-prompter. Current local main lacks these features; use a dependent fix branch for issue #151.
- Original checkout has unrelated dirty/untracked files; native worktree keeps them untouched.
