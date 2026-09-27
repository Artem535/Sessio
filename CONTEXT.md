# Sessio

A local-first desktop application for private psychological practice: scheduling, client records, notes, and backups, with an optional native video-call feature (LiveKit) that must not become a dependency of the core offline workflow.

## Language

**Practitioner**:
The single psychologist/therapist who owns a Sessio install and its data. Canonical term across code, issues, and `CONTEXT.md`; "специалист" is only the RU UI translation of this same concept, not a separate one.
_Avoid_: Specialist (English code/docs), user, therapist (in English-facing text).

**Account**:
A billable/authorizable identity on the LiveKit token backend, distinct from a `Practitioner`'s in-app profile. For the MVP exactly one `Account` row exists, seeded manually at deploy time; the model exists from day one so a future paid/multi-account model needs no schema migration. See `docs/asciidoc/11-token-backend-account-model-adr.adoc`.
_Avoid_: User, tenant (no self-service signup exists yet).

**Authorizer**:
The token backend's single seam that turns a bearer credential into an `AccountId` (or rejects it). The MVP implementation is a static-token comparison against the one seeded `Account`; only this implementation changes when a real billing/subscription model is chosen.
_Avoid_: Auth middleware, login check (implies more machinery than exists today).

**Meeting**:
A logical online meeting belonging to exactly one `Event`, identified by an opaque `meeting_ref` and a `Provider Kind` (`ExternalUrl` or `LiveKit`). For the `LiveKit` provider specifically, the `meeting_ref` is distinct from the LiveKit `room_name`: the room name is generated once at meeting creation and stored against the `meeting_ref`, but is only disclosed to a participant at token-issuance time, not before. For `ExternalUrl`, the `meeting_ref` and the practitioner-entered link are the same value.
_Avoid_: Call, session, room (room specifically means the LiveKit-level `room_name`, a different value, only meaningful for the `LiveKit` provider).

**Provider Kind**:
Which implementation of `MeetingProvider` an `Event`'s `Meeting` uses: `ExternalUrl` (existing behavior — the practitioner types/pastes an arbitrary http(s) link) or `LiveKit` (mints a real LiveKit room via the token backend). Persisted on `Event`/`EventSeries` alongside `meeting_ref`. `LiveKit` is not yet selectable in the UI — that lands with the native call UI.
_Avoid_: Provider type, meeting type.

**MeetingProvider**:
The desktop app's polymorphic interface for creating and cancelling a `Meeting`, one implementation per `Provider Kind`. Asynchronous (signal-based, like `CredentialStore`) so `LiveKitMeetingProvider` can call the token backend without blocking the UI thread; `ExternalUrlMeetingProvider` does no I/O and emits its result synchronously within the call. The `LiveKit` implementation starts as a non-functional stub — it satisfies the interface but is never invoked, since the UI does not yet expose `LiveKit` as a selectable `Provider Kind`.
_Avoid_: `VideoProvider` (a distinct concept for in-call runtime state, not for creating/cancelling the logical `Meeting` — see `VideoProvider`).

**MeetingDescriptor**:
The result of `MeetingProvider::create`: `Provider Kind`, `meeting_ref`, an optional `meeting_url` fallback, and an optional invitation state. Only the `LiveKit` provider populates invitation state, and only once it becomes functional; until then both providers leave it unset.

**MeetingCoordinator**:
The desktop app's non-Qt-UI class that selects the right `MeetingProvider` by `Provider Kind` and drives `create`/`cancel` on behalf of the event-editing UI, keeping provider selection and error handling unit-testable independent of `QEventDetailsWidget`.

**VideoProvider**:
The desktop app's abstraction over the actual in-call media session — device capture, connecting to the LiveKit SDK, rendering remote video — used by `VideoSession`. Unlike `MeetingProvider`, it is not polymorphic across multiple real backends: `LiveKitVideoProvider` is the only production implementation. The interface exists solely so `VideoSession` and the native call UI can be tested against a `FakeVideoProvider` without a real SDK, network, or camera/microphone.
_Avoid_: `MeetingProvider` (creates/cancels the logical `Meeting` record; does not touch media or devices).

**VideoSession**:
Owns the call lifecycle for one `Meeting`, encapsulating a `QStateMachine` over `VideoSessionState` and driving a `VideoProvider`. Exposes `join()`/`leave()` and a `stateChanged` signal; knows nothing about `Event`, `QTimelineModel`, or persistence — recording a technical outcome (`call_joined`/`call_failed`) on the `Event` is the caller's responsibility, not `VideoSession`'s. The JWT it needs to join is passed in by the caller (via `MeetingProvider`/token-backend integration), not fetched by `VideoSession` itself.
_Avoid_: Call, video call (ambiguous with `Meeting`, which is the persisted record rather than the live runtime object).

**VideoSessionState**:
The enum driving `VideoSession`'s state machine: `NoMeeting → Provisioned → PrejoinCheck → Joining → WaitingForClient → Connected ↔ Reconnecting → Leaving → Ended`, with a `Failed` state reachable from any in-progress state. See `docs/video-roadmap.md` §5.5.

**DeviceManager**:
A thin wrapper over Qt Multimedia (`QMediaDevices`) that enumerates and selects the camera/microphone/speaker. Has no dependency on `VideoProvider` or the LiveKit SDK, so device selection UI (e.g. `PrejoinCheck`) is testable independent of any real call.
_Avoid_: Device selector (`DeviceManager` is the project's chosen term, matching `MeetingProvider`/`MeetingCoordinator`'s naming style).

**Participant Page**:
The client-facing surface for joining a `Meeting`: device check followed by room join via a one-time `Invitation`. Deliberately not part of the Sessio desktop app or its codebase — a distinct, not-yet-built deliverable whose channel (a minimal web page, a Telegram bot, or otherwise) is intentionally undecided (see `docs/asciidoc/11-token-backend-account-model-adr.adoc`), since any channel need only implement the same invitation-redemption contract.
_Avoid_: Client app, web client (implies more than the minimal, no-PII-access surface this actually is).

**Invitation**:
The client's one-time-issued, repeatedly-redeemable link into a `Meeting`. "One-time" means one invitation is created per meeting, not that redeeming it twice fails — it stays valid for repeated token exchange until the meeting's scheduled window closes or it is invalidated, so a client can reconnect after a dropped connection or a closed tab. See `docs/asciidoc/12-invitation-security-model-adr.adoc`.
_Avoid_: Single-use link, invite code alone (it is not sufficient by itself — see Room Passcode).

**Room Passcode**:
A 6-digit numeric secret generated alongside an `Invitation`, required in addition to the invitation link to redeem a client JWT. Delivered to the client through a channel separate from the link itself (spoken, or a separate message), so a leaked link alone is not sufficient to join. Rate-limited to 5 failed attempts before the invitation is invalidated.
_Avoid_: Meeting password, PIN (passcode is the project's chosen term).
