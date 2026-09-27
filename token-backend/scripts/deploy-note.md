# Deploying alongside the existing LiveKit containers

This follows the same `/opt/livekit/docker-compose.yml` pattern already
running on the PSY server for `livekit` and `meet`. Add a sibling service —
do not put this inside the `livekit` container.

## Exposure: internal only, for now

> **This deployment is internal-only until a domain plus the Caddy reverse
> proxy (already fronting the sibling LiveKit deployment) is put in front of
> it.** Do **not** open the service's port in firewalld and do **not** bind
> it to a public interface. Every request to this service carries a secret in
> plaintext — the account bearer credential, the invitation code, the room
> passcode, or a freshly minted LiveKit JWT — so reaching it over plain HTTP
> from outside the host means handing those secrets to anyone on the path.

Until TLS termination exists, the only supported exposure is:

* bound to `127.0.0.1` on the host, reachable from the host itself and from
  whatever the reverse proxy will be, **or**
* published only onto the container network shared with the future proxy,
  with no host port publish at all.

Implementing TLS is out of scope here; this note only makes sure the
pre-TLS deployment is not reachable from the internet in the meantime.

## Compose service

Note the deliberate differences from the `livekit`/`meet` services:
`network_mode: host` is **not** used (it would expose the listener on every
interface), and the published port is bound explicitly to `127.0.0.1`.

```yaml
  token-backend:
    build:
      context: /opt/pcm-token-backend
      dockerfile: Dockerfile
    container_name: pcm-token-backend
    restart: unless-stopped
    # Loopback-only publish. Once Caddy fronts this, drop the `ports:` block
    # entirely and put the proxy on the same compose network instead.
    ports:
      - "127.0.0.1:8080:8080"
    environment:
      LIVEKIT_API_KEY: <same value as the livekit service>
      LIVEKIT_API_SECRET: <same value as the livekit service>
      LIVEKIT_WS_ENDPOINT: ws://46.173.25.218:7880
      # Required — the process refuses to start without it. Point it at the
      # real join-page prefix; a wrong value silently produces dead links.
      INVITATION_BASE_URL: https://<your-domain>/join/
      DB_PATH: /data/token-backend.sqlite3
    volumes:
      - /opt/pcm-token-backend/data:/data
```

Copy `token-backend/` from this repo to `/opt/pcm-token-backend` on the PSY
server (same layout used for the LiveKit Meet build earlier), then:

```bash
podman-compose build token-backend
podman-compose up -d token-backend
```

No firewalld rule is needed or wanted. Verify from the host only:

```bash
curl -sS http://127.0.0.1:8080/healthz    # -> ok
```

## Seed the account — required once, after the first deploy

A fresh database contains **zero** accounts, so every authenticated endpoint
returns `401` until this is run once. Run it against the same volume the
service uses, so the row lands in the same database file:

```bash
podman-compose run --rm \
  -e LIVEKIT_API_KEY=<same value as the service> \
  -e LIVEKIT_API_SECRET=<same value as the service> \
  -e DB_PATH=/data/token-backend.sqlite3 \
  token-backend --seed-account
```

(`--seed-account` is appended to the image's `ENTRYPOINT`, which is the
binary itself.)

It prints the bearer credential exactly once:

```
Seeded account. Bearer credential (copy this now, it will not be shown again):
<credential>
```

Paste it into PsyClientManager's Settings immediately. It is stored only as a
hash, so it cannot be recovered — re-running `--seed-account` mints a new
credential and **invalidates the previous one** (the accounts table holds
exactly one row for the MVP; see `AccountsRepository::seedAccount`).

Sanity-check it, again from the host only:

```bash
curl -sS -X POST http://127.0.0.1:8080/v1/meetings \
  -H "Authorization: Bearer <credential>" \
  -H "Content-Type: application/json" \
  -d '{"scheduledStart":"2026-10-01T10:00:00Z","scheduledEnd":"2026-10-01T10:50:00Z"}'
```

## When the domain lands

1. Put Caddy in front, exactly as for the sibling LiveKit deployment, and let
   it terminate TLS and proxy to this container over the internal network.
2. Drop the `ports:` block above so nothing is published on the host at all.
3. Update `INVITATION_BASE_URL` to the real `https://` join prefix.
