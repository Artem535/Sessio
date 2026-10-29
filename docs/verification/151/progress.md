# Implementation ledger

Base: 5b9923c, branch codex/151-call-prompter. User approved spec/plan and native execution on 2026-10-13.

Preflight: existing transcription controller publishes persisted phrases internally; existing client notes are client-scoped; switcher uses pageChanged(bool), so keep compatibility and add separate prompter navigation.

Task 1: RED test added for separate initially-off auto-start controls and dependency behavior.

Build correction: first configure used host GCC 16 against the shared Ubuntu/GCC 15 vcpkg tree and attempted a heavy rebuild. Stopped only the new configure process tree; rerunning in the existing Ubuntu distrobox to restore cached dependencies. Future commands use Ubuntu and at most two build jobs. No GPU inference.
