# PsyClientManager token backend

Mints short-lived, room-scoped LiveKit JWTs for the self-hosted LiveKit
deployment. Never holds anything the desktop app needs to trust beyond a
signed token — the LiveKit API secret and the bearer credential live only
here. See `docs/asciidoc/11-token-backend-account-model-adr.adoc` and
`docs/asciidoc/12-invitation-security-model-adr.adoc` for the design this
implements.

## Build

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build
ctest --test-dir build --output-on-failure
```

## Configuration (environment variables)

| Variable | Required | Default | Purpose |
|---|---|---|---|
| `PORT` | no | `8080` | HTTP listen port |
| `DB_PATH` | no | `token-backend.sqlite3` | SQLite database file |
| `LIVEKIT_API_KEY` | yes | — | Must match the self-hosted LiveKit server's key |
| `LIVEKIT_API_SECRET` | yes | — | Must match the self-hosted LiveKit server's secret |
| `LIVEKIT_WS_ENDPOINT` | **yes** | — | Returned to clients as the connection URL |
| `INVITATION_BASE_URL` | **yes** | — | Prefix the invitation code is appended to |
| `TOKEN_TTL_SECONDS` | no | `600` | LiveKit JWT lifetime |

All of these are read once at startup by `Config::fromEnv()`. A missing or
empty **required** variable aborts the process with an error naming it —
`INVITATION_BASE_URL` is required precisely because its old placeholder
default (`https://example.invalid/join/`) let a misconfigured deploy come up
healthy while handing out invitation links that go nowhere. `LIVEKIT_WS_ENDPOINT`
is required for the same reason: its old default was a real production
server address, reached over unencrypted `ws://`, so a deploy that forgot to
set it would silently point every call at that server in plaintext.

`--seed-account` is the exception: it touches only the database, so it needs
`DB_PATH` alone and none of the required variables.

## API

All request and response bodies are JSON (`Content-Type: application/json`).
Field names below match the DTOs in `src/controller/dto.h` exactly.

Endpoints marked **auth** require the account bearer credential:

```
Authorization: Bearer <credential>
```

The `Bearer ` scheme prefix is optional and case-insensitive — a bare
`Authorization: <credential>` is also accepted — but sending the RFC 7235
form is recommended.

Exactly one endpoint is public: `POST /v1/invitations/{code}/client-token`.
That is deliberate (ADR-12): the client has no account, and the invitation
code plus the passcode are the two secrets that stand in for one.

Every error response has the shape:

```json
{ "error": "passcode_required" }
```

### `GET /healthz` — public

Liveness probe. Returns `200` with the plain-text body `ok`.

### `POST /v1/meetings` — auth

Creates a meeting, its LiveKit room, and its first invitation.

Request:

```json
{ "scheduledStart": "2026-10-01T10:00:00Z", "scheduledEnd": "2026-10-01T10:50:00Z" }
```

Response `200`:

```json
{
  "meetingRef": "mtg_AbC123...",
  "invitationUrl": "https://<INVITATION_BASE_URL>/<invitation code>",
  "passcode": "048213",
  "scheduledStart": "2026-10-01T10:00:00Z",
  "scheduledEnd": "2026-10-01T10:50:00Z"
}
```

`passcode` is returned in plaintext here and nowhere else — it is stored only
as an Argon2id hash. Show it to the practitioner **next to, never inside**,
the invitation link (ADR-12: the two secrets travel by separate channels).

| Status | Meaning |
|---|---|
| `200` | Created |
| `401` | Missing or unknown credential |

### `POST /v1/meetings/{meetingRef}/invitation` — auth

Re-issues the invitation for an existing meeting: every invitation currently
on the meeting is invalidated and a fresh code and passcode are minted.
`meetingRef`, the room, and the scheduled window are unchanged, so nothing
client-side that keys off the meeting is orphaned.

Use it when the invitation is locked out after 5 wrong passcode attempts, or
when the link is believed to have leaked. No request body.

Response `200` — same shape as `POST /v1/meetings`.

| Status | Meaning |
|---|---|
| `200` | New invitation issued; the previous one is dead |
| `401` | Missing or unknown credential |
| `404` | No such meeting for this account |
| `410` | The meeting was explicitly invalidated |

The scheduled window is deliberately **not** required to be open here — a
practitioner can prepare a replacement invitation ahead of the session.

### `POST /v1/meetings/{meetingRef}/specialist-token` — auth

Mints the practitioner's LiveKit JWT. Identity is `practitioner-{meetingRef}`.
No request body.

Response `200`:

```json
{
  "endpointUrl": "ws://46.173.25.218:7880",
  "roomName": "rm_XyZ789...",
  "token": "<LiveKit JWT>",
  "expiresAt": 1790000000
}
```

`expiresAt` is a Unix timestamp in seconds.

| Status | Meaning |
|---|---|
| `200` | Token issued |
| `401` | Missing or unknown credential |
| `404` | No such meeting for this account |
| `410` | Outside the scheduled window, or the meeting was invalidated |

### `POST /v1/invitations/{code}/client-token` — **public**

Exchanges an invitation code plus the room passcode for the client's LiveKit
JWT. Identity is `client-{meetingRef}`, fixed per meeting, so a duplicate
redemption visibly displaces the existing session rather than joining
silently alongside it (ADR-12).

Request:

```json
{ "passcode": "048213" }
```

Response `200` — same shape as `specialist-token`.

| Status | Meaning |
|---|---|
| `200` | Token issued |
| `400` | `passcode` missing or empty — does **not** count as a failed attempt |
| `401` | Wrong passcode (counts against the 5-attempt budget) |
| `404` | Unknown invitation code |
| `410` | Outside the scheduled window, the meeting was invalidated, or the invitation was superseded by a re-issue |
| `429` | 5 failed passcode attempts — this invitation is permanently dead; re-issue it |

The code stays redeemable for repeated exchanges within the scheduled window
so a client who drops can rejoin; it is not consumed by first use.

### `POST /v1/meetings/{meetingRef}/invalidate` — auth

Kills the meeting: no further token of either role is issued for it. No
request body, and no response body.

| Status | Meaning |
|---|---|
| `204` | Invalidated (idempotent) |
| `401` | Missing or unknown credential |
| `404` | No such meeting for this account |

## Invitation lifetime: the meeting's scheduled window

An invitation is **not** valid indefinitely. Per
`docs/asciidoc/12-invitation-security-model-adr.adoc`, the meeting's scheduled
window is the invitation's lifetime boundary: tokens are issued only while

```
scheduledStart - 5 minutes  ≤  now  ≤  scheduledEnd + 15 minutes
```

The 5-minute pre-join buffer lets a client connect slightly early; the
15-minute grace period covers sessions that run over and clients who need to
reconnect right after the scheduled end. Outside that range both
`specialist-token` and `client-token` return `410 Gone`, even though the
meeting's status is still `active`.

The invitation code stays reusable *within* that window — it is not consumed
by the first redemption, so a client who drops can rejoin. Requests outside
the window are rejected before the passcode is checked, so they do not count
against the 5-attempt limit.

`scheduledStart`/`scheduledEnd` are stored exactly as supplied at creation and
parsed as UTC in the `YYYY-MM-DDTHH:MM:SSZ` form. A value that cannot be
parsed fails closed (the window is treated as shut).

## First deploy: seed the one account

```bash
pcm_token_backend --seed-account
```

Copy the printed bearer credential into PsyClientManager's Settings once —
it is never shown again. Losing it means re-seeding, which invalidates the
previous credential (see `AccountsRepository::seedAccount`).
