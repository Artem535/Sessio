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
| Distinct rooms per occurrence on real devices | Not performed; staging check compares the join route's `subject=<room>` log value between occurrences (needs a time-adjustable backend or a one-week wait) |
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
- Desktop packages ship libical's timezone data (addressed in the final review
  wave): `cmake --install` puts it in `<prefix>/share/sessio/zoneinfo` (RPM,
  AppImage), the macOS bundle in `Contents/Resources/zoneinfo`, and the Windows
  workflow copies it to `zoneinfo` next to `Sessio.exe`. At startup
  `configureScheduleZoneinfo()` resolves it relative to the executable
  (`SESSIO_ZONEINFO_DIR` overrides); without data every named timezone is
  rejected and recurring series can not be published (fails closed, logged).
  Residual risk: desktop and backend each carry their own copy of the zone
  data, so after a tzdata/libical update on only one side they can disagree
  about an occurrence time around a rule change; update both together.
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
   Edits made in 0.2.7 are never published, so the permanent link keeps
   admitting clients under the last schedule published by 0.2.8 (including
   occurrences canceled afterwards in 0.2.7). Before downgrading, revoke the
   series invitations with `POST /v1/schedule-series/{series_uid}/revoke`
   (a reissue would only replace the link), or accept that exposure.
3. After a migration the old meeting is invalidated once, without retry. If that
   request fails the app logs a warning (no meeting reference or link) and emits
   `legacyInvalidationFailed`; the old shared link then stays valid until its
   window ends. Invalidate it manually from the backend if that matters.
4. A series migrated from a legacy meeting has its old meeting invalidated only
   after the new invitation is stored; to undo, create a fresh legacy meeting.
5. Invitations can be retired at any time with reissue or the series revoke
   route.

## Not verified

- Real audio/video between two devices, TURN/TLS networks.
- A backend binary built from the Dockerfile (Docker not run here).
- RPM, AppImage, Windows installer and macOS bundle builds were not made here
  (no rpmbuild/linuxdeploy/Inno Setup/macdeployqt run). Verified only:
  `cmake --install` on Linux lays the data out at `share/sessio/zoneinfo`, and
  the resolver unit tests cover the Linux, Windows and macOS relative layouts.
  The Windows workflow step and the macOS install rule are untested.
- The thread-confinement check in `Database::write_connection` is an `assert`
  (compiled out in the Release builds the tests use) plus a release-mode
  fallback to a private connection; no test exercises the cross-thread path.
- Rollback and revoke claims above come from reading the code.
