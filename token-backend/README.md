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

### Timezone data

Recurring-schedule resolution uses libical's **own** builtin timezone files
(`share/libical/zoneinfo`, `*.ics`), not the operating system's tzdata, so that
the backend and the desktop app resolve occurrences identically. The directory
is compiled in as `PCM_SCHEDULE_ZONEINFO_DIR` and read at run time, therefore it
must exist at that same path on the machine that runs the binary:

- `PCM_SCHEDULE_ZONEINFO_SOURCE_DIR` (default
  `<libical cmake dir>/../libical/zoneinfo`, i.e. the vcpkg copy) is the data
  available at **build** time. Configure fails if it has no timezone data.
- `PCM_SCHEDULE_ZONEINFO_DIR` (defaults to the source dir) is the **runtime**
  path compiled into the binary. It may differ from the source dir; if it does
  not exist at configure time CMake only warns, because it can legitimately be
  created later (installed or copied) on the machine that runs the binary.
  `scripts/check_zoneinfo_configure.sh` verifies all three cases.
- `Dockerfile` passes `-DPCM_SCHEDULE_ZONEINFO_DIR=/usr/share/pcm-schedule/zoneinfo`
  in the build stage (configure prints the warning, which is expected there)
  and copies the vcpkg `libical/zoneinfo` directory to that path in the runtime
  stage; the `COPY` fails the image build if the source is missing. No OS tzdata
  package is needed. Not verified: no docker/podman is available here, so the
  image was never built.
- For any other packaging, pass an explicit `-DPCM_SCHEDULE_ZONEINFO_DIR=<install path>`
  and install the same directory there. This repository has no RPM/AppImage
  packaging for the backend. The desktop app links the same schedule library
  but does not use this compiled-in path in packaged builds: it ships its own
  copy of the data under `share/sessio/zoneinfo` and resolves it at start-up
  (`src/meeting/schedule_zoneinfo.h`). Backend and desktop zone data can drift
  if only one side is updated; update both together.

## Configuration (environment variables)

| Variable | Required | Default | Purpose |
|---|---|---|---|
| `PORT` | no | `8080` | HTTP listen port |
| `DB_PATH` | no | `token-backend.sqlite3` | SQLite database file |
| `LIVEKIT_API_KEY` | yes | — | Must match the self-hosted LiveKit server's key |
| `LIVEKIT_API_SECRET` | yes | — | Must match the self-hosted LiveKit server's secret |
| `LIVEKIT_WS_ENDPOINT` | **yes** | — | Returned to clients as the connection URL |
| `INVITATION_BASE_URL` | **yes** | — | Legacy code prefix or a `std::format` URL template |
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

`INVITATION_BASE_URL` remains backward-compatible with a prefix such as
`https://join.example.test/j/`, to which the invitation code is appended. For
the Sessio desktop deep link, configure a positional `std::format` template
instead (the first field is the code, the second is the passcode):

```sh
INVITATION_BASE_URL='sessio://join?code={}&passcode={}&backend=https%3A%2F%2Flivekit.sessio-pcm.ru'
```

Named fields such as `{code}` are not supported by the C++ standard formatter;
the service rejects an invalid template at startup rather than silently issuing
broken invitations.

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
  "invitationUrl": "<configured invitation URL for this code and passcode>",
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

## Persistent recurring invitations

Publish a schema-v1 schedule snapshot first through
`PUT /v1/schedule-series/{series_uid}` (see the repository's recurring-call
service specification). Then authenticated
`POST /v1/schedule-series/{series_uid}/invitation` accepts exactly
`{"reissue":false}` with a non-nil UUID `Idempotency-Key` header and returns
`{"invitation_url":"...","passcode":"...","generation":1}`.
To replace an invitation explicitly, use `{"reissue":true}` and a new key.
The previous generation is revoked atomically. A new key with `reissue=false`
when any generation already exists returns 409 `invitation_exists`.
`POST /v1/schedule-series/{series_uid}/revoke` (empty body or `{}`) returns 204.
All practitioner routes enforce account ownership; foreign resources return 404.

The existing `POST /v1/invitations/{code}/client-token` also accepts series codes.
It checks the passcode before resolving the latest server schedule. The first
five wrong passcodes return 401; the sixth permanently revokes that generation
and returns 410 `invitation_revoked`. This count survives weeks and restarts.
The single-meeting invitation policy remains unchanged.
Practitioners join via authenticated
`POST /v1/schedule-series/{series_uid}/occurrences/{original_start_utc}/specialist-token`,
with no body, `{}`, or `{"displayName":"..."}`. The original start is strict
UTC `YYYY-MM-DDTHH:MM:SSZ`; it stays unchanged after a move. Both token routes
return existing `endpointUrl`, `roomName`, `token`, `expiresAt` fields.

Join windows include start minus 5 minutes through end plus 15 minutes. No
eligible occurrence returns 409 `occurrence_unavailable`; multiple eligible
occurrences return 409 `ambiguous_occurrence` for both participant paths.
Resolver failure returns 503 `schedule_resolution_unavailable`. A moved
occurrence keeps its room and client identity. All tokens for a mapped legacy
meeting also check the latest schedule; stale stored meeting dates are not used.
Legacy invitation reissue on a mapped meeting is rejected with 400; use the
series invitation endpoint. Legacy invalidate permanently closes that mapped
room to new tokens and preserves its mapping; it does not cancel the entire
series or create a replacement room. Revocation does not disconnect participants
or invalidate an already-issued JWT before expiry.

Invitation mutation/replay/revoke share a rolling 10 requests/minute/account
limit. Series practitioner token routes (including mapped legacy routes) share
30/minute/account; series client exchange is 30/minute/invitation. Limits are
process-local, reset on restart, and return 429 with `Retry-After: 60`.
Secret responses have `Cache-Control: no-store`. New series request bodies and
the client-token body are bounded to 4 KiB; oversized bodies return 413. Invalid
invitation fields, UUID keys and specialist request shapes return 400
`invalid_request` without echoing input. Snapshot bodies retain their own limits.

Idempotency keys are scoped to the account and bind the series and normalized
`reissue` value. Reusing a key for a different series or payload returns 409
`idempotency_conflict`. For 24 hours retries return the original URL, passcode
and generation, even after a URL configuration change or a later reissue/revoke;
such an old generation remains revoked. The response is encrypted at rest with
XChaCha20-Poly1305 and a fresh random nonce. The key is HMAC-SHA256 of the fixed
domain `sessio:series-invitation-replay:v1`, keyed by the existing mandatory
`LIVEKIT_API_SECRET`. Account/key/series/payload/generation/expiry are authenticated
as associated data. No additional secret or ephemeral startup key is used.
Existing required configuration validation remains the startup readiness gate.

Rotating `LIVEKIT_API_SECRET` makes existing replay ciphertext unreadable; those
retries fail closed with 410 `invitation_replay_expired`, as do expired retries.
An explicit reissue with a new idempotency key is then required to recover a lost
invitation. Passcode hashes and existing invitation codes remain valid across
rotation; new JWTs use the new key. Keep the configured secret with database
backups if replay restoration is needed. Never include it in the database.
Expired encrypted payloads are cleared on service startup and invitation
requests; expiry is checked before every replay. Tombstones remain so an old
key never silently creates new secrets. Cleanup of history/tombstones and
multi-process rate limiting remain production lifecycle work.

Tests inject a constructor clock into `MeetingService`/`SeriesService`, including
real HTTP tests covering two weekly windows. Production defaults to system UTC;
there is no environment clock override or public test-clock endpoint. JWT `nbf`,
`exp` and the response `expiresAt` use the same selected server instant.

## Account management: one credential per specialist

Each specialist gets their own bearer credential; every meeting and
invitation is scoped to the account that created it, so credentials are never
shared between specialists.

### First deploy: bootstrap the first account

```bash
pcm_token_backend --seed-account
```

Copy the printed bearer credential into PsyClientManager's Settings once —
it is never shown again. This only works when the database has **no**
accounts yet (genuine first-run bootstrap); once at least one account exists,
it refuses and points you at `--add-account` / `--revoke-account` instead of
silently destroying an existing specialist's credential (see
`AccountsRepository::seedAccount`).

### Adding another specialist

```bash
pcm_token_backend --add-account
```

Creates a new account and prints its bearer credential to stdout (shown once,
same as `--seed-account`). Does not touch any existing account.

### Listing accounts

```bash
pcm_token_backend --list-accounts
```

Prints `id` and `created_at` for every account, one per line — never the
credential itself, since only its hash is stored. Prints `No accounts.` if
none exist yet.

### Revoking a specialist's account

```bash
pcm_token_backend --revoke-account <id>
```

Removes the account with the given id, immediately invalidating its bearer
credential. Refuses (exit 1) if the account still has meetings or invitations
referencing it — re-issue or invalidate those first, or delete them, before
revoking the account.
