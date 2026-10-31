# Client App (Diary + Calls) Design

## Scope

A second, client-facing application next to the specialist desktop app
(Sessio). It is a personal mood/emotion diary that works on its own and can
optionally be linked to a specialist who uses Sessio, giving the client a
diary the specialist can partly see and a way to join calls.

This document records the product and architecture decisions from the
2026-10-21..24 design interview. It does not yet break work into issues; the
first step is the LiveKit-on-Android spike (see "First steps").

Prior art: `~/Projects/C++/PsyNotes/PsyNotes` (PsyNote, 2023-09..2024-06):
Qt 6.6 Quick + Material, Android build (`android/`), diary list/note page,
`EmotionSelector` with 5 emotions, Couchbase Lite C storage, a stub AI chat.
Only the QML diary prototype and the Android packaging setup carry over; the
storage layer and AI chat do not.

## Product decisions

| Topic | Decision |
|---|---|
| Audience | Standalone diary for anyone; a Sessio specialist can be linked optionally. |
| MVP features | Diary entries; emotion diary + charts; local reminders; link a specialist; upcoming sessions; call; "Need help now" screen with crisis-line numbers. |
| Later | v1.1 entry templates (e.g. CBT situation/thoughts/emotions/reaction); v2 questionnaires (PHQ-9, GAD-7), homework from specialist. |
| Not planned | Client↔specialist chat (creates 24/7-availability expectations); AI chat companion (crisis/liability/store-review risk). |
| What the specialist sees | Emotion ratings are shared by default; entry text is private by default and shared per entry. |
| Unlinking | Already-shared data stays with the specialist (a copy was sent); new data stops flowing. The first "share" action states "the specialist receives a copy". |
| Linking | Both directions: specialist invites from Sessio (link/QR), or client enters the specialist's code. Both sides must confirm. |
| Specialist view in Sessio | "Client diary" tab in the client card (emotion chart + shared entries) and a simple pre-session summary (chart + new entries since last session). AI summary via the prompter later. No alerts on worrying trends (or only explicit opt-in). |
| AI | None in MVP. Later: on-device insights over the user's own diary (Gemma via LiteRT, descriptive only, no advice/diagnosis). |
| Emotion model | Six base emotions with intensity — anger, sadness, fear, joy, calm, shame — each expandable to specific words (wheel style, e.g. fear → anxiety, insecurity, panic, unease; joy → relief, pride, gratitude, love/tenderness). Fixed Russian vocabulary in MVP; validate with practising psychologists. Specialist-configurable vocabularies later. |
| Monetisation | Everything free in MVP; architecture keeps feature flags so paid features (backup, analytics, templates, AI) can be enabled later. Clients of Sessio specialists would get them via the specialist's subscription. |
| Branding | Separate warm name with a "by Sessio" byline; name TBD. Avoid "Psy" in the name. |
| Language | Russian first, through Qt translations like Sessio. |

## Architecture decisions

| Topic | Decision |
|---|---|
| Platforms | Android first, then iOS, plus desktop (Windows/macOS/Linux). Cross-platform code from day one. |
| Stack | Qt Quick (QML + C++). |
| Calls | LiveKit C++ SDK built for Android/iOS ourselves (no fallback planned). Devices stay on Qt Multimedia exactly as in Sessio (`QCamera`/`QAudioSource` → `livekit::VideoSource`/`AudioSource`, `QAudioSink`/`QVideoSink` for remote), so WebRTC's Java device modules are not needed. Background calls (foreground service / iOS background mode) and native incoming-call UI are native code regardless. |
| Data model | No client accounts. Identity is a keypair generated on the device. Linking exchanges public keys (QR/link/code). |
| Server | A thin relay added to `token-backend`: mailboxes with random IDs holding encrypted envelopes, deleted on delivery or after a TTL (~30 days). The server sees no names, contacts or content. |
| Specialist side | Sessio has its own keypair with a recovery code; clients encrypt shared items to the specialist's public key. |
| Backup | Encrypted file written to the user's own cloud (Google Drive / iCloud / Yandex Disk via the system file picker). Key from a backup password plus a one-time recovery code. Our server is not involved. |
| Multi-device | The user's devices are paired the same way (QR between own devices). |
| Notifications | Local only: diary reminders and session reminders computed from the schedule received in envelopes. No server push in MVP (so no "incoming call" push; the client joins at the scheduled time). |
| Local storage | SQLite + SQLCipher, key in Android Keystore / iOS Keychain. Record format aligned with the relay envelope format. |
| Repository | Monorepo: `apps/desktop` (current Sessio), `apps/client`, `libs/call`, `libs/crypto`, `libs/relay-protocol`. Mobile CI jobs run only on changes to `apps/client` and `libs/`. |
| Regions | Russia first, servers in RF; the protocol carries a region field so an EU relay can be added later. A specialist and their clients share a region. |

## Legal notes (not legal advice)

- No accounts and content-blind relay keep us close to not processing
  personal data; IP addresses and random IDs remain a grey zone.
- For linked clients the specialist is already the data operator of their
  own client records.
- Before public store release: one paid consultation with a 152-FZ lawyer
  (incl. whether emotion data counts as health data), privacy policy, and an
  ИП/legal entity for store publishing.

## Distribution

Android: RuStore + Google Play. iOS: App Store (TestFlight beta first).
Desktop: website.

## First steps (run in parallel with current work)

1. **LiveKit on Android spike (1–2 weeks, may take up to 4).** Goal: two-way
   audio + video on a real arm64 phone from a Qt Quick app reusing Sessio's
   capture/render adapters. Work items: cross-compile the Rust core
   (`cargo-ndk`, `aarch64-linux-android`) against LiveKit's prebuilt
   `webrtc-android-arm64` (`LK_CUSTOM_WEBRTC`); NDK ≤ 27 or the fix for
   rust-sdks#992; add an NDK path to the C++ SDK build; JNI init via
   `QJniEnvironment` if WebRTC needs a `JavaVM`; check echo cancellation on
   speakerphone with custom audio sources. Facts as of 2026-10-24: the C++ SDK
   (v1.12.4) ships no Android/iOS builds and documents none; the Rust core and
   WebRTC prebuilts do support Android and iOS.
2. Legal minimum: ИП if missing; privacy-policy template.
3. Pick the app name.

## Rough estimate (Android + desktop MVP, one developer, full-time weeks)

| Block | Weeks |
|---|---|
| LiveKit Android spike | 1–2 |
| Monorepo restructure, extract `libs/call`, `libs/crypto` | 1–2 |
| Relay + protocol + crypto | 2–3 |
| Client app: diary, emotion wheel, reminders, help screen, SQLCipher | 3–4 |
| Linking, sharing, Sessio tab + pre-session summary | 2–3 |
| Calls in the client app, background mode | 2–3 |
| Backup to user cloud, recovery code | 1–2 |
| Packaging, CI, RuStore/Google Play | 1–2 |
| **Total** | **≈ 13–21** |

iOS adds roughly 4–6 weeks. Running in parallel with the call assistant work
roughly doubles calendar time.
