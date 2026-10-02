# Issue 98 verification (recurring LiveKit calls) - 2026-10-02

Desktop version **0.2.8** (CMakeLists.txt and src/app/application.cpp). Base
0.2.7. No production deployment, no real participant data, no real credentials.

## Status

| Area | Status |
|---|---|
| Backend schedule/series/invitation/token routes | Implemented, tested: token backend ctest 170/170 |
| Desktop sync, outbox, invitation, event editor, timeline, join | Implemented, tested: app ctest 483/483 (1 opt-in skipped, run separately) |
| Client-to-real-backend wire interop (publish + CAS second revision + fetch) | Tested against a locally started backend with throwaway credentials: passed |
| Two occurrences, shared invitation, distinct rooms | Tested in-process (HttpIntegrationTest.SeriesInvitationJoinsTwoWeeklyWindowsAndUsesOneConcurrentRoom, SeriesServiceTest with injected clock) |
| Transfer (backup restore conflict), revoke/reissue, offline retry | Tested (ScheduleSyncTest, SeriesInvitationTest, SeriesServiceTest.ReissueAndExplicitRevokeRetireOldCodes, SeriesCallTest) |
| Legacy regressions (single events, legacy series, opt-in migration) | Tested (SeriesCallTest, TimelineSeriesTest, SeriesCallUiTest) |
| Translations | Both catalogs in sync with tests ON and OFF, no unfinished entries |
| Populated GUI via scripts/run-dev-isolated.sh | Blocked/partial: see below |
| Distinct rooms per occurrence on real devices | Not observable from clients; backend tests only |
| Real two-device call acceptance on staging | **Not performed - blocked** (no staging endpoint or credential available; see docs/asciidoc/16-recurring-calls-staging-smoke.adoc) |

## Populated GUI

The isolated launcher was run under Xvfb and the application started and ran
12 s without crashing on an empty profile. A populated weekly series with an
invitation was not created through the live GUI (it needs a reachable backend
and an interactive session); populated UI behaviour is covered by the offscreen
suites SeriesCallUiTest, TimelineSeriesTest and SeriesTimezoneDialogTest.

## Known environment-dependent failure

SettingsDialogLayoutTest.TallPagesScrollInsteadOfOverflowingTheDialog fails
when the test binary is run on the host's real display (backupPage scroll bar
maximum 0) and passes with QT_QPA_PLATFORM=offscreen (and under ctest). The
branch changes no settings code relative to base e3cb0e5, so it is not caused
by this work; not re-run on a base build.

## Configuration requirements

- Backend: LIVEKIT_API_KEY, LIVEKIT_API_SECRET, LIVEKIT_WS_ENDPOINT,
  INVITATION_BASE_URL (all required). LIVEKIT_API_SECRET also derives the
  invitation replay-encryption key; rotating it makes pending replays fail
  closed (410 invitation_replay_expired). Fallback: explicit reissue.
- Backend build: PCM_SCHEDULE_ZONEINFO_SOURCE_DIR (vcpkg libical data, validated at
  configure; fails if missing) and PCM_SCHEDULE_ZONEINFO_DIR (runtime path
  compiled in; only a configure warning if absent at build time). The Dockerfile
  copies the data to /usr/share/pcm-schedule/zoneinfo in the runtime stage;
  no OS tzdata is needed. Checked by token-backend/scripts/check_zoneinfo_configure.sh
  (passed locally); the Docker image build itself is unverified (no docker/podman).
- Desktop and backend must both be this version: older backends report
  schedules as unsupported and the app never falls back to a single meeting
  for a recurring event.

## Rollback

Expected behaviour below was derived from the code and migrations, not exercised
by a downgrade test.

1. Roll the backend image back to the previous tag. Schedule tables are
   additive; the old binary ignores them. Existing legacy single-meeting
   invitations keep working.
2. Desktop: reinstall 0.2.7. Published series data stays in the local database
   and the server; 0.2.7 treats those events as ordinary recurring events.
3. A series migrated from a legacy meeting has its old meeting invalidated only
   after the new invitation is stored; to undo, create a fresh legacy meeting.
4. Invitations can be retired at any time with reissue or the series revoke
   route.

## Not verified

- Real audio/video between two devices, TURN/TLS networks.
- A backend binary built from the Dockerfile (Docker not run here).
- RPM/AppImage do not ship the backend; nothing about backend packaging
  outside the Dockerfile was verified.
