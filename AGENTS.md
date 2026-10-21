# Repository Guidelines

## Project Structure & Module Organization

Sessio is a C++20 Qt Widgets application.

- `src/`: database, event/timeline, clients, pages, widgets, config, and app code.
- `ui/`: Qt Designer forms; generated headers stay in the build directory.
- `test/`: GoogleTest tests; `resources/`: themes, icons, and Qt resources.
- `translation/`: English/Russian Qt Linguist files.
- `docs/asciidoc/` and `docs/site/`: technical docs and static website.
- `third_party/`: vendored dependencies and submodules; avoid unrelated edits.

## Build, Test, and Development Commands

Use `rtk` when available for compact output: `rtk git`, `rtk gh`, `rtk cmake`,
and `rtk test`. Use native commands when exact output is required.

Configure and build the release tree:

```bash
cmake --preset vcpkg-release
cmake --build build-release --parallel
```

Configure a local test build and run tests:

```bash
cmake -S . -B build -DPCM_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run the application from the generated build directory. Open
`docs/site/index.html` directly to preview the website.

## Issue and Branch Workflow

Every non-trivial task starts as a GitHub issue:

1. Create/select the issue and branch from `main`, for example
   `feat/123-event-statuses` or `fix/124-timeline-overlap`.
2. Add the `in-progress` label when taking the issue.
3. Add progress comments when scope, blockers, or verification status changes.
4. Keep commits focused on the issue and push the branch through `rtk git push`
   or `git push`.
5. Open a linked MR/PR with tests, docs, migrations, and relevant screenshots.
6. Raise the application version for every MR: update both `CMakeLists.txt` and
   `src/app/application.cpp`.
7. Update `CHANGELOG.md` with the user-visible changes for that version.
8. Close the issue through the merged MR/PR using the repository's supported
   closing syntax, such as `Closes #123`.
9. Before committing new or changed `tr()` strings, run
   `cmake --build build-release --target update_translations` and translate
   any resulting `type="unfinished"` entries in `translation/app_ru.ts` and
   `translation/app_en.ts` — CI fails the build on out-of-sync `.ts` files or
   any remaining `type="unfinished"` entry. The translation scan covers
   everything reachable from CMake targets, including `third_party/qlementine`,
   and would silently miss any source file never attached to a CMake target.

Do not develop directly on `main` or close an issue before its MR/PR is merged.

## Coding Style & Naming Conventions

Use C++20 and two-space indentation. Follow existing Qt conventions: `PascalCase`
for classes, `camelCase` for Qt methods and UI handlers, and `snake_case` for
database APIs and schema fields. Keep headers focused and prefer existing module
boundaries over new global helpers. Use `apply_patch` for manual edits. Before
committing, run `git diff --check`.

## Testing Guidelines

Use GoogleTest and name tests after observable behavior, for example
`DashboardExcludesCanceledNoShowAndRescheduledEvents`. Cover migrations,
recurrence, UI data contracts, and status/overlap edge cases.

## Commit & Pull Request Guidelines

Recent commits use short imperative titles such as `Add ...`, `Fix ...`, and
`Bump version ...`. Keep commits focused; describe tests, migrations, translations,
screenshots, and docs in the MR/PR. Version and CHANGELOG updates are required in
every MR, not only at milestone releases.

## Security & Configuration Tips

Do not commit local databases, build directories, credentials, tokens, or client
PII. Keep production logs free of names, contact details, note contents, meeting
URLs, and encryption keys. Database schema changes require a migration and a
restore/round-trip test.

## Shared memory (MCP `memory`)

This memory is shared with other agents (Claude Code and Codex). Always use `group_id` `Sessio` — both when writing and when searching (`group_ids: ["Sessio"]`).

**At the start of a task**, call `search_memory_facts` (and `search_nodes` if useful) about the task: decisions, conventions, known issues, how to build/test.

**Save with `add_memory`** when:
- an architectural or technical decision is made (what was chosen and why);
- a non-obvious bug cause or workaround is found;
- a project convention, build/deploy command, or environment quirk is discovered;
- the user states a preference or rule.

Format: `name` — short title; `episode_body` — 2–6 factual sentences, no filler, with file/service names; `source: "text"`; `source_description: "claude-code"` or `"codex"`.

**Never save** secrets, keys, passwords, personal data, `.env` contents, or anything already recorded in the code or git history.

If a memory fact contradicts the code, trust the code and save the corrected fact.
Do not call `clear_graph`, `delete_episode`, or `delete_entity_edge` unless the user explicitly asks.
