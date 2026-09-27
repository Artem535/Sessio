# PsyClientManager

A local-first desktop application for private psychological practice: scheduling, client records, notes, and backups, with an optional native video-call feature (LiveKit) that must not become a dependency of the core offline workflow.

## Language

**Practitioner**:
The single psychologist/therapist who owns a PsyClientManager install and its data. Canonical term across code, issues, and `CONTEXT.md`; "специалист" is only the RU UI translation of this same concept, not a separate one.
_Avoid_: Specialist (English code/docs), user, therapist (in English-facing text).

**Account**:
A billable/authorizable identity on the LiveKit token backend, distinct from a `Practitioner`'s in-app profile. For the MVP exactly one `Account` row exists, seeded manually at deploy time; the model exists from day one so a future paid/multi-account model needs no schema migration. See `docs/asciidoc/11-token-backend-account-model-adr.adoc`.
_Avoid_: User, tenant (no self-service signup exists yet).

**Authorizer**:
The token backend's single seam that turns a bearer credential into an `AccountId` (or rejects it). The MVP implementation is a static-token comparison against the one seeded `Account`; only this implementation changes when a real billing/subscription model is chosen.
_Avoid_: Auth middleware, login check (implies more machinery than exists today).

**Meeting**:
A logical LiveKit video call belonging to exactly one `Event`, identified by an opaque `meeting_ref` the desktop app holds. Distinct from the LiveKit `room_name`: the room name is generated once at meeting creation and stored against the `meeting_ref`, but is only disclosed to a participant at token-issuance time, not before.
_Avoid_: Call, session, room (room specifically means the LiveKit-level `room_name`, a different value).

**Invitation**:
The client's one-time-issued, repeatedly-redeemable link into a `Meeting`. "One-time" means one invitation is created per meeting, not that redeeming it twice fails — it stays valid for repeated token exchange until the meeting's scheduled window closes or it is invalidated, so a client can reconnect after a dropped connection or a closed tab. See `docs/asciidoc/12-invitation-security-model-adr.adoc`.
_Avoid_: Single-use link, invite code alone (it is not sufficient by itself — see Room Passcode).

**Room Passcode**:
A 6-digit numeric secret generated alongside an `Invitation`, required in addition to the invitation link to redeem a client JWT. Delivered to the client through a channel separate from the link itself (spoken, or a separate message), so a leaked link alone is not sufficient to join. Rate-limited to 5 failed attempts before the invitation is invalidated.
_Avoid_: Meeting password, PIN (passcode is the project's chosen term).
