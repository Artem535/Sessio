# Live Call Transcription — Design

Issue: #118. Engine selection and measurements: #116
(`docs/asciidoc/15-realtime-asr-spike.adoc`, spike code in `spikes/asr-model-bench/`
on branch `spike/116-asr-model-bench`).

## Goal

During a LiveKit call the practitioner can turn on a running transcript of both
participants. Recognition happens on the practitioner's machine, audio is never
stored or sent anywhere, and the text is saved as an editable draft tied to the
event. This is the P2 transcription slice of `docs/video-roadmap.en.md` §7,
limited to local processing.

## Scope

In scope: transcription engine, audio taps in the call, per-session consent,
`Transcript`/`TranscriptPhrase` storage with backup and restore, call side-panel
switcher with a transcript panel, a transcript page for review and editing,
settings section, build and packaging of the engine and the model on Linux, Windows and macOS.
Windows and macOS are built in CI from the start; their runtime behaviour is
verified by hand by the practitioner/developer.

Out of scope: AI summaries and agents (P2.3+), cloud recognition, languages other
than Russian, speaker diarization (roles come from the audio track), search,
word-by-word live text (the T-One draft layer from the spike ADR), automatic
retention expiry, a client-side consent flow (the Participant Page does not exist).

## Decisions

| Topic | Decision | Why |
|---|---|---|
| Engine | GigaAM v3 RNN-T behind Silero VAD, via sherpa-onnx 1.13.8, CPU | Best Russian accuracy on spontaneous speech, 340 MB RAM, RTF 0.03; see the #116 ADR |
| Process model | In-process library, `src/transcription/` | The measured design; a helper process adds IPC with no gain while there is one model |
| Sharing | One recogniser per call, one VAD per track, one decode worker | Ten tracks cost about 19 MB each, not a model each (load test in the ADR) |
| Text granularity | Whole phrases, available about 0.65 s after the speaker stops | Consequence of VAD plus offline model; accepted |
| Consent | The practitioner confirms the client's consent before every session; revocable | Roadmap requires per-session, revocable consent; client has no app to confirm in |
| Storage | New tables `Transcript`, `TranscriptPhrase`; own page | Chosen over a `ClientNote`; keeps time and role per phrase |
| Notes | Existing `ClientNotesPage` stays as is, next to the transcript in a switcher | It is already embedded in the call side panel |
| Model delivery | Bundled with the installer as plain data files | Chosen over download-on-first-use; costs about 195 MB of package size |
| Base branch | `worktree-videoprovider-domain-layer` (calls UI, PR #94) | The calls UI is not on `main` yet |

## Architecture

New module `src/transcription/`, no dependency on widgets. Every unit has an
interface so it can be tested with a fake.

| Unit | Responsibility | Depends on |
|---|---|---|
| `Resampler` | Convert 48 kHz mono int16 (call audio) to 16 kHz float | nothing |
| `PhraseSegmenter` | Per track: feed 512-sample windows to a VAD, emit closed phrases with start and end sample offsets | `IVoiceActivityDetector` |
| `ISpeechRecognizer` | `std::string transcribe(span<const float>)`; the sherpa-onnx implementation wraps one `OfflineRecognizer` | sherpa-onnx |
| `TranscriptionEngine` | `addTrack(TrackInfo)`, `pushAudio(TrackId, span<const int16_t>, sampleRate)`, `removeTrack`, `stop`; signal `phraseReady(TranscribedPhrase)`; thread-safe | the three above |
| `ModelLocator` | Resolve the GigaAM and Silero VAD files relative to the application directory per platform | nothing |
| `TranscriptionSession` | GUI-thread QObject for one call: consent gate, owns the engine, installs and removes the audio taps, writes phrases through `TranscriptRepository` | engine, repository, `VideoSession` |
| `TranscriptRepository` | CRUD for `Transcript` and `TranscriptPhrase` over `pcm::database::Database` | database |

`TrackInfo` carries a stable track id (the LiveKit participant identity), a role
(`Practitioner` or `Participant`), and a display name.

### Audio taps and threading

The adapters stay free of transcription code. A small `AudioSink` interface
(`pushAudio(TrackId, const int16_t*, size_t, int sampleRate)`) lives in
`src/video/`. `AudioCaptureAdapter` (local microphone, 48 kHz mono) and
`RemoteAudioPlayer` (remote track, read on its own thread) call the sink when one
is set. Setting the sink is the only way audio reaches the engine, and it is set
only after a `Transcript` row with consent exists (see Consent).

Engine threads, independent of the number of participants:

1. Capture and reader threads call `pushAudio`, which only copies samples into a
   per-track queue under a short lock and returns.
2. One segmenter thread resamples, runs each track's VAD, and puts closed phrases
   on a decode queue.
3. One decode worker takes phrases in order, calls `ISpeechRecognizer`, and emits
   `phraseReady`. One worker is deliberate: the safe use of a shared recogniser
   from several threads is not established, and it keeps phrases in order.

Phrase times are `start_ms`/`end_ms` from the start of the call, computed from the
track's VAD sample offsets plus the moment the track was added.

VAD parameters taken from the spike: threshold 0.5, minimum silence 0.4 s,
minimum speech 0.25 s, maximum phrase 20 s (forces a split; GigaAM accepts up to
25 s).

### Error handling

- Missing or unreadable model files: the engine does not start, the session
  reports `Failed` with a user-readable reason, the call is not affected.
- Decode failure for one phrase: that phrase is dropped and counted; the session
  continues. The count is shown in the panel footer.
- Engine falls behind (decode queue older than 10 s): the panel shows a
  "recognition is delayed" notice; phrases are never discarded silently.
- `VideoSession` enters `Leaving`, `Ended`, or `Failed`: the session stops, drains
  the queue (at most 5 s), and the transcript becomes a `draft`.
- No text, names, or audio in logs: only counts, durations, and state changes.

## Data model and consent

```sql
CREATE TABLE IF NOT EXISTS Transcript (
    id INTEGER PRIMARY KEY,
    event_id INTEGER NOT NULL REFERENCES Event(id),
    status TEXT NOT NULL,              -- recording | draft | reviewed
    consent_scope TEXT NOT NULL,       -- 'live_local_v1': local, audio not stored
    consent_given_at TIMESTAMP NOT NULL,
    consent_revoked_at TIMESTAMP,
    model_id TEXT,                     -- 'gigaam-v3-rnnt'
    created_at TIMESTAMP NOT NULL,
    updated_at TIMESTAMP NOT NULL
);
CREATE TABLE IF NOT EXISTS TranscriptPhrase (
    id INTEGER PRIMARY KEY,
    transcript_id INTEGER NOT NULL REFERENCES Transcript(id),
    track_role TEXT NOT NULL,          -- practitioner | participant
    speaker_name TEXT,
    start_ms BIGINT NOT NULL,          -- from the start of the call
    end_ms BIGINT NOT NULL,
    text TEXT NOT NULL,
    edited BOOLEAN DEFAULT FALSE
);
```

The client is found through the existing `EventClient` link. One `Transcript` is
created per join of a call. The schema change is added to `kCreateTables` and
`kSchemaMigrations`, raises `schema_version`, and is covered by a restore
round-trip test, as AGENTS.md requires. Backups (including encrypted ones) include
both tables.

### Lifecycle

1. *Start.* The practitioner presses "Transcribe" in the call. A consent dialog
   explains what is processed, that it is local, that audio is not stored, that
   consent can be revoked, and that the text can be edited and deleted. The start
   button is enabled only with the box "The client has given consent to
   transcribing this session". On confirmation `TranscriptionSession` creates the
   `Transcript` row (`recording`, `consent_given_at` set) and only then installs
   the audio taps and starts the engine.
2. *During the call.* Each phrase is written to `TranscriptPhrase` as soon as it
   is final (a crash does not lose text) and shown in the panel.
3. *Stop.* The same as the end of a call: taps are removed, the engine drains and
   stops, the transcript becomes a `draft`; consent stays recorded for this session.
4. *Revoke.* Taps are removed and the engine stops immediately,
   `consent_revoked_at` is set, and a dialog offers "Delete what was recorded" or
   "Keep as draft".
5. *End of call.* The session stops automatically; status becomes `draft`. The
   transcript page lets the practitioner edit or delete phrases and mark the
   transcript `reviewed`.
6. *Deletion.* Manual on the transcript page and from Settings ("Delete all
   transcripts"). Deleting an event or a client deletes its transcripts and
   phrases. There is no automatic expiry in this version.

Consent is enforced structurally: with no active session there is no audio sink
set, so no audio can reach the engine. A test asserts this.

## UI

Mockups were reviewed in the design discussion.

- *Call side panel.* A new `CallSidePanel` replaces the panel widget passed to
  `CallPage::setSidePanelWidget`. It holds a Qlementine `SegmentedControl`
  ("Notes | Transcript") and a `QStackedWidget`: page 0 is the existing
  `ClientNotesPage` unchanged, page 1 is the new `TranscriptPanel`. In client mode
  there is no panel, as today.
- *`TranscriptPanel`.* Phrases with speaker name and time, a "listening…" row for
  an open phrase, a "local" badge, "Аудио не сохраняется" footer text, and "Stop"
  and "Revoke consent" buttons. A "Transcribe" button in the call control bar
  starts the flow; it shows "Transcription running" while active.
- *Consent dialog.* As described in Lifecycle.
- *Transcript page.* Header with event, client, phrase count, model; status chip
  (`Draft`/`Reviewed`); buttons "Mark reviewed" and delete; per-phrase edit, save,
  cancel, delete; edited phrases carry an "edited" chip. Opened from the event info
  page.
- *Settings.* A "Call transcription" section: enable switch, model information,
  where data is stored, "Delete all transcripts". The switch disabled hides the
  call button.

All new strings go through `tr()`; `translation/app_ru.ts` and `app_en.ts` are
updated with no `unfinished` entries (CI requirement).

## Build and packaging

- sherpa-onnx is added with `FetchContent` at tag `v1.13.8` (shallow), shared
  libraries, with Python, tests, binaries, TTS, speaker diarization, WebSocket,
  PortAudio, and GPU switched off (the configuration verified in the spike). The
  ONNX Runtime archive that sherpa downloads at configure time is pinned by hash.
  The libraries are installed beside the application, following
  `cmake/BundleLiveKitLinuxDeps.cmake`.
- The prebuilt Linux sherpa-onnx tarballs use the pre-C++11 `std::string` ABI and
  do not work with the C++ wrapper (`cxx-api.h`); building from source avoids it.
- New CMake option `SESSIO_ENABLE_TRANSCRIPTION`, default ON on every platform.
  OFF compiles the application without the module and without the settings
  section (escape hatch only).
- Model files (about 170 MB) are installed as plain data files, not as Qt
  resources: sherpa-onnx loads models by file path, and an embedded resource would
  have to be extracted at every start. Locations: Linux `share/sessio/models/` (lowercase, as `sessio/zoneinfo`),
  macOS `Sessio.app/Contents/Resources/models`, Windows beside the executable via
  `packaging/Sessio.iss`. At build time the files are downloaded from the
  sherpa-onnx release page with pinned SHA-256 values and cached.
- Package size grows by about 195 MB (model plus libraries). This works against
  PR #71 (smaller RPM) and is an accepted trade-off.
- Licences added to the third-party notices: GigaAM (MIT), Silero VAD (MIT),
  sherpa-onnx (Apache-2.0), ONNX Runtime (MIT).

## Testing

GoogleTest, existing conventions (no `QApplication` harness where avoidable).

- Pure units with fakes: `Resampler`; `PhraseSegmenter` with a fake VAD (phrase
  boundaries, forced split at 20 s, flush); `TranscriptionEngine` with a fake
  recogniser (order, several tracks, add and remove while running, stop with a
  non-empty queue, delayed notice); consent state machine; `ModelLocator`.
- Database: create, append, edit, delete, cascade from event and client delete,
  migration from the previous schema, restore round-trip, backup contents.
- Privacy: log capture contains no phrase text or names; no audio file is created;
  with no active session the audio sink is unset.
- UI: `CallSidePanel` switching and default page; `TranscriptPanel` rendering from
  fake phrases; consent dialog start button gated by the checkbox; transcript page
  editing and deletion.
- Real model (label `models`, skipped when the model is absent): GigaAM through
  the real recogniser on a short sample; the engine with ten tracks reproducing the
  spike's load test: two staggered speakers (4 s apart) must keep every phrase lag
  within 0.4 s + 0.15 x phrase length + 0.2 s, with an empty queue and nothing
  dropped after stop. The simultaneous worst case (two identical long phrases ending
  together are decoded sequentially) is documented and bounded at 3 s.

## Phases

Each phase ends with something testable and gets its own tasks in the plan.

0. *Build gate (all platforms).* sherpa-onnx and the model through CMake and
   packaging, built in the Linux, Windows and macOS CI jobs; model tests and the
   install check run on Linux in CI. Runtime behaviour on Windows and macOS is
   tested by hand.
1. *Engine.* `src/transcription/` and its tests, including the ten-track load test.
2. *Data.* Schema, repository, migration, backup and restore, cascade deletes.
3. *Session and consent.* `TranscriptionSession`, audio sink and taps,
   `VideoSession` integration, consent logic.
4. *Interface.* `CallSidePanel`, `TranscriptPanel`, consent dialog, transcript page,
   settings.
5. *Release.* Version bump, `CHANGELOG.md`, translations, documentation (update the
   #116 ADR status, add a privacy note), manual test checklist.

## Risks

- Windows and macOS builds of sherpa-onnx with FetchContent are unverified until
  their first CI run; fixing them is part of phase 0. Runtime behaviour (ONNX
  Runtime loading, model paths, installer contents) there is verified by hand.
- The accuracy numbers come from a studio podcast, not from a real two-person
  call; a recording of a consented test call should be re-run before the engine is
  frozen (follow-up from #116).
- One recogniser used from the decode worker only: if ten people speaking at once
  turns out to matter, a second recogniser and worker is the remedy (about 300 MB).
- The call UI and the multi-participant work are not on `main`; this branch must be
  rebased or merged with PR #94 before it can be merged.
- Package size growth conflicts with the open PR #71.
