# Native Call UI and Client Mode — Design

> Design for issue [#80](https://github.com/Artem535/Sessio/issues/80), [P1-B.3] Native Qt 1:1 LiveKit video call UI. Builds on the already-merged `MeetingProvider` (#79/PR #90), `VideoProvider`/`VideoSession` (#92/PR #93), and the LiveKit token backend (#78/PR #89).

**Goal:** Give the practitioner a native Qt screen to join a 1:1 LiveKit call from within Sessio, and give a client without any therapist account a way to join the same call by installing Sessio and entering an invitation — without either audience ever touching the other's data or navigation.

**Architecture:** Two top-level window classes chosen by a single branch in `main.cpp` before any heavy subsystem is constructed: the existing `MainWindow` (practitioner) gains a new "Звонки" (Calls) tab, and a new, minimal `ClientModeWindow` (no `Database`/`QClientModel`/notes dependencies at all) hosts the same call-entry widget for a pure client. Both converge on the same `VideoSession`/`VideoProvider` domain layer and the same token-backend endpoints.

**Tech Stack:** C++20, Qt Widgets, the existing `VideoProvider`/`VideoSession`/`MeetingProvider` domain layer, the LiveKit token backend's `specialist-token`/`client-token` endpoints, a new OS-level URL protocol handler (`sessio://`) on Windows/macOS/Linux.

## Global Constraints

- Must not give `ClientModeWindow` any code path that constructs `Database`, `QClientModel`, or any notes/client-record widget — the privacy boundary is structural, not a hidden UI element.
- `VideoSession` records nothing on `Event`; the calling page writes the technical outcome (`call_joined`/`call_failed`) itself, per `CONTEXT.md`'s `VideoSession` entry.
- No group calls, chat, file transfer, screen share, waiting room, recording, live captions, virtual backgrounds, reactions, or automatic event-status changes (roadmap §5.1/§5.5, unchanged by this design).
- Entering a call must not close the client card or expose notes on any shared/remote surface — satisfied here by keeping notes in a local side panel the call page itself owns, never rendered to the remote track.
- Single Sessio binary/installer serves both audiences; there is no separate "client build."

---

## 1. Application shells and role selection

On first launch (no role stored yet), Sessio shows a two-option picker: **"Я специалист"** (full `MainWindow`) or **"Я клиент"** (`ClientModeWindow`). The choice is persisted in `Config` (a new `AppRole` field, alongside the existing config fields in `src/config`). Every subsequent launch reads this field and constructs only the corresponding window — `main.cpp`'s only role-related branch.

`ClientModeWindow` is a new, small `QMainWindow` (or `QWidget`) that:
- Has no sidebar, no `TabButton`s, no `Database`/`QClientModel` member, and no dependency on any client/event/notes class.
- Hosts exactly one thing: the shared call-entry widget (§2), pre-scoped to "join by code" only (no "today's meetings" list, since a pure client has none).

**Role transition (client → specialist only):** a specialist never needs client mode — the "Звонки" tab in their own `MainWindow` already covers joining someone else's meeting (§2). The only real transition is a former pure-client later becoming a practitioner. `ClientModeWindow` gets a low-visibility "Я специалист" action that clears the stored `AppRole` and prompts a restart; there is no live, in-session switch. This is deliberately minimal for v1.

## 2. Shared call-entry widget ("Звонки")

One widget, reused as `MainWindow`'s new "📹 Звонки" tab and as the entire content of `ClientModeWindow`:

- **In `MainWindow`:** shows a "Ваши встречи сегодня" list — the practitioner's own `Event`s with a `LiveKit` `Provider Kind` and a known `meeting_ref`, each with a "Войти" button enabled once its scheduled window opens. Below it, a "Присоединиться к чужой встрече / войти как клиент" form (invitation code + Room Passcode).
- **In `ClientModeWindow`:** shows only the code+passcode form (no "today's meetings" section — a pure client has no `Event`s).
- Clicking "Open Meeting" on a `LiveKit` `Event` in `QEventDetailsWidget` (currently `pcm::meeting::openMeetingUrl(...)`, an `ExternalUrl`-only path today) switches `MainWindow` to the Звонки tab with that specific meeting pre-selected, instead of opening any URL — it is the same entry point as clicking "Войти" on that meeting in the list, not a separate screen.
- A `sessio://join?code=...&passcode=...` link, when the OS hands it to an already-installed Sessio, opens directly to this widget (in whichever shell matches the stored role) with both fields pre-filled; manual entry remains available as a fallback (unregistered handler, dictated code, etc.).

**Two distinct join paths, same UI, same eventual `VideoSession`:**

| Path | Trigger | Token endpoint | Grants |
|---|---|---|---|
| Own scheduled meeting | "Войти" on a listed `Event`, or "Open Meeting" | `POST /v1/meetings/{meeting_ref}/specialist-token` | specialist identity |
| Guest / client | Code+passcode form (typed, or pre-filled from a link) | `POST /v1/invitations/{code}/client-token` | client identity |

Both resolve to a JWT and an endpoint URL, then construct/reuse a `VideoSession` the same way from that point on.

## 3. Call screens

A new page (swapped in the same way as `MainWindow`'s existing pages — Calendar, Clients, Analytics, Notes) driven by `VideoSession::stateChanged`. `ClientModeWindow` hosts the equivalent page directly, no tab-switching chrome around it.

- **`PrejoinCheck`:** local camera preview, camera/microphone/speaker `DeviceManager` selectors, a privacy reminder ("заметки и данные клиента не видны собеседнику"), "Присоединиться" button.
- **`Joining`/`WaitingForClient`:** progress indicator plus a safe cancel action (same page shell as `PrejoinCheck`, swapped content — no new layout risk, not separately mocked).
- **`Connected`:** one remote tile filling the content area, local picture-in-picture in a corner, and a control bar: mute, camera toggle, a device-switch control (opens a small popover listing `DeviceManager`'s devices — standard pattern, not separately mocked), a "Заметки" toggle, and "Завершить" (leave).
- **Notes side panel:** opened by the "Заметки" toggle, slides in from the right; the remote tile shrinks to make room but is never hidden. Reuses `ClientNotesPage`'s existing notes-editing widget as-is (no new, narrower editor) — bound to the client attached to the current `Event`. Only reachable from `MainWindow`'s call page; `ClientModeWindow`'s equivalent screen has no notes toggle at all (a client has no notes to see).
- **`Reconnecting`:** the `Connected` layout unchanged, with a banner across the top ("Переподключение... (N из 30 сек)").
- **`Ended`/`Failed`:** a simple terminal message (call ended / call failed with reason) and a way back to the Звонки tab or `ClientModeWindow`'s idle screen — standard pattern, not separately mocked.

On entering `Ended` or `Failed`, the call page (not `VideoSession` itself) records the technical outcome — `call_joined` or `call_failed` — on the `Event`, when running in `MainWindow` (a client's `ClientModeWindow` has no `Event` to annotate).

## 4. `sessio://` URL scheme

Ships in this same version, not deferred. Registered per platform as part of packaging/installer work:
- **Windows:** registry protocol handler entry, added by the Inno Setup installer script.
- **macOS:** `CFBundleURLTypes` in the app's `Info.plist`.
- **Linux:** a `.desktop` file `MimeType=x-scheme-handler/sessio;` entry plus `xdg-mime default` registration, wired into the AppImage/RPM packaging steps.

The exact platform-specific mechanics (installer script changes, `.desktop` file changes, CMake packaging steps) are implementation-plan detail, not a further design decision — the shared call-entry widget (§2) is the single landing point regardless of which platform delivered the click.

## 5. Error handling

- A JWT request (either token endpoint) failing at the network/HTTP level, or the token backend returning a 4xx (expired invitation, wrong passcode, unknown meeting), surfaces as an inline error on the code+passcode form or the "Войти" action — never a silent no-op, never a modal dialog that could block on a stuck call.
- `VideoSession`'s existing `joinFailed`/`reconnectFailed` signals (already implemented in #92) drive the `Failed` screen and the `Reconnecting` banner's timeout path respectively — no new error-signal surface is needed from the domain layer.
- `mediaError` (also already on `VideoProvider` from #92's final-review fix-wave) surfaces as a non-blocking inline warning on `PrejoinCheck`/`Connected` (e.g. "camera unavailable") — it must not be treated as a network failure or drive the call to `Failed`.

## 6. Testing

- `ClientModeWindow` construction: a unit/smoke test asserting it never touches `Database` (e.g. no `Database` instance reachable from it, or a compile-time check that its translation unit doesn't include `database.h`).
- The shared call-entry widget: unit tests for both list/no-list configurations, and for the "own meeting" vs "code" path choosing the correct token endpoint.
- Call screens: reuse the existing `FakeVideoProvider` (from #92) to drive `VideoSession` through every state and assert the corresponding screen is shown, exactly like #92's own `VideoSessionTest` suite.
- `sessio://` handling: a platform-specific manual verification step (roadmap §5.7 already requires a real two-machine staging call; this is the natural place to also exercise the link).
- Cross-platform build verification (Linux/Windows/macOS) and the real two-machine LiveKit Cloud staging call are carried over from #80's existing acceptance criteria, unchanged by this design.

## Explicitly out of scope

- The previously-discussed standalone web/bot **participant page** (roadmap §5.1, ADR-11) is not replaced by `ClientModeWindow` — they are independent, non-exclusive channels sharing the same `client-token` contract. `ClientModeWindow` is simply the first channel actually built; a web page can still be added later for clients unwilling to install Sessio.
- A client using Sessio to create or manage their own `Event`s — raised and explicitly rejected during design: that would make them a second practitioner, not a client, and is out of scope entirely.
- Live, in-session role switching (client ↔ specialist) — the minimal "clear stored role and restart" transition is enough for v1.
