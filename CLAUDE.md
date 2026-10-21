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
