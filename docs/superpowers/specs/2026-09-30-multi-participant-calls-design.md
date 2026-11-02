# Sessio #95: participant model and 1:2 calls

Status: implementation authorized; layout B approved on 2026-09-30.
Issue: https://github.com/Artem535/Sessio/issues/95.

## Intent and scope

The user selected the whole of #95: native calls must grow from one practitioner
and one client to one practitioner and two clients. The media/model interfaces
must support lists of participants so later 5–10-person layouts do not require
another transport/model rewrite. Keep 1:1 as the primary experience and preserve
the existing private notes/assistant side panel and client-mode separation.

The user explicitly requested implementation after the benchmark. Work proceeds
in a separate dependent branch above PR #94, without merging that PR. The final
PR stays dependent on #94's merge and manual device verification. The rendering
benchmark is recorded in issue #95 and commit 761be66; it supports QOpenGLWidget
tiles with painter scaling, not a claim that real group calls are already tested.

Recording, transcription, assistant inference, participant consent, screen
sharing, active-speaker selection, quality/simulcast tuning, and new invitation
management UI are outside this change.

## Decisions

1. A Qt `ParticipantModel : QAbstractListModel` represents participants. Each
   entry contains stable `id`, `displayName`, a string `role`, `isLocal`, and
   microphone/camera state. Unknown names/roles have honest UI fallbacks. Roles
   describe the participant and never confer permissions.
2. `VideoProvider` owns this model and exposes per-participant `VideoFrameSource`
   objects instead of ready-made video widgets. `VideoSession` exposes the same
   model to `CallPage`. Provider and frame-source code has no QWidget ownership.
3. The UI owns one `RemoteVideoRenderer` per displayed participant. The existing
   renderer becomes a Qt-only consumer of a frame source; it preserves black
   letterboxing and scales with QPainter's OpenGL paint engine. LiveKit reading
   moves into a provider-owned frame source.
4. Each remote participant has independent video-track and audio-player state.
   Switching the output device updates all current audio players. Qt's audio
   output backend mixes the individual streams; verify simultaneous audible
   playback in the live smoke. A track's unsubscribe affects only that track.
5. `VideoStream::Options::capacity = 2`. Keep one latest copied image and at most
   one pending frame-available notification per source. Track replacement,
   unsubscribe, and leave close blocked SDK readers before joining their threads
   and clear their displayed frame. Local preview uses the same frame-source
   abstraction and the already-active camera capture, without a second capture.
6. Participant updates and media callbacks reach the Qt owner thread as copied
   values, never raw pointers to callback events. Discard callbacks from prior
   room generations or replaced track SIDs. The model upserts by identity;
   duplicate events do not create duplicate rows.

## Identity prerequisite in the token backend

Today every client of a meeting gets identity `client-<meetingRef>`, so a second
client replaces the first in LiveKit. Generate a cryptographically random suffix
for each token issuance, for both client and practitioner identities. The
identity remains stable for the life of that token, including SDK reconnect.
Renewing/reissuing a token creates a new connection identity.

Add signed participant role metadata (`client` or `practitioner`) to the JWT.
No name or contact information is added. The desktop reads role metadata as
display data and handles older tokens with absent metadata. Existing HTTP
request/response schemas, token grants, invitation validation, status/window
checks and rate limits remain compatible. Test repeated issuance for one meeting
and decode the claims to prove unique identities, unchanged room/grants and role.

## Presence and lifecycle

Rename `WaitingForClient` to `WaitingForParticipants`: the room is joined but
has zero remote participants. `Connected` means at least one remote participant
is present, including an audio-only participant with no video track.

Initial room snapshots and participant events may arrive before `joined`;
collect them in the model and resolve presence when joining completes. Leaving
one of two remote participants keeps the session Connected. Only the last
remote participant leaving enters WaitingForParticipants. Duplicate joins,
duplicate/unknown leaves, and media unsubscribe do not alter room presence.

Reconnect preserves model identities and resolves the destination from current
presence; a successful reconnect with nobody remaining returns to waiting.
Terminal failures and explicit leave tear down all streams/players and clear
the model. Device failures remain non-fatal media errors. No network callback
from a previous room may populate a newly attached session.

## Layout and UI ownership

Keep the stage's floating controls, fullscreen/device controls and side panel.
Layout selection is separate from participant storage and rendering:

- Zero remote participants: visible waiting state with the local preview.
- One remote participant: large remote video plus local picture-in-picture.
- Two or more remote participants: equally sized tiles for all participants,
  including the local preview, in a balanced grid. Use QGridLayout for the
  tile host and a separate strategy selecting rows/columns from available
  stage size and 16:9 tile proportions. Center an incomplete final row.
  This is a
  geometry fallback; active-speaker selection, pinning, pagination and other
  advanced group layouts remain deferred. The live acceptance scenario is 1:2.

Participants with no camera display their name and camera-off placeholder.
Names are escaped/rendered as plain text. The side panel reduces the video stage
size; tile geometry is computed from that size. Local PiP in 1:1 stays inside
the stage on narrow windows and above video but below floating controls.

On attaching another session, disconnect model/source observers and discard
the old UI-owned tiles. Source deletion must be safe while the UI is visible.
The provider never reparents or deletes UI widgets. Widget/model tests assert
observable ownership and participant identity rather than borrowed-widget internals.

## Verification and delivery

- Establish the PR #94 baseline in the isolated checkout before attributing
  failures to #95. Use temporary HOME/XDG_CONFIG_HOME for filesystem-affecting
  settings tests.
- Model tests: unique identity upsert, role/name updates, row ordering, duplicate
  events, unknown removal, local-vs-remote count and clear.
- State tests: presence-before-joined, 0→1→2→1→0 remote participants, audio-only
  presence, unsubscribe without departure, reconnect with changing presence,
  stale callbacks and teardown.
- Source/media tests: independent sources, frame coalescing/latest frame,
  replacement clearing, close/destroy without blocked readers and no cross-talk.
- UI tests: 1:1 and 1:2 geometry, identity preservation when the first participant
  leaves, placeholder state, portrait/narrow resize, side panel, session swap
  and deletion ordering.
- Backend tests: concurrent/repeated join token issuance preserves authorization
  and produces distinct identities in the same room.
- Live smoke: a disposable room with three synthetic participants, independent
  camera patterns and audio tones; join/leave/rejoin, mute/unmute and output
  switching. Run a populated Qt UI on hardware OpenGL and retain screenshots.
- Build/test relevant desktop and backend targets, run the relevant suites,
  update translations and resolve unfinished ru/en entries. Bump both desktop
  version declarations from 0.2.4 to 0.2.5 and add the changelog entry.
- Run `git diff --check`, push focused commits and open a draft PR against the
  current #94 branch, linked to #95. Retarget to main once #94 is merged.
  Report live verification, platform limitations and deployment state explicitly.

## Alternatives considered

Provider-owned widget lists would be a smaller change but would keep transport
and presentation coupled, contrary to #95. A custom shader renderer or Qt Quick
rewrite would expand the scope without evidence from the benchmark. The chosen
frame-source model retains the existing Widgets UI and provides the participant
boundary needed by later layouts.
