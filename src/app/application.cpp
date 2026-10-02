#include "application.h"
#include "app_lock_dialog.h"
#include "role_selection_dialog.h"
#include "provider_kind.h"
#include "schedule_zoneinfo.h"
#include "../backup/encrypted_container.h"
#include "../backup/restore_service.h"
#include "../event_view/recurrence_utils.h"
#include "../widgets/app_settings.h"

#include <Poco/Path.h>
#include <QDate>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QGuiApplication>
#include <QLocale>
#include <QStandardPaths>
#include <QMessageBox>
#include <QLibraryInfo>
#include <QMenu>
#include <QStringList>
#include <QTimeZone>
#include <QTranslator>
#include <QWidget>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QDateTime>
#include <oclero/qlementine.hpp>

#include <sodium.h>

#include <algorithm>
#include <optional>
#include <string>

Q_LOGGING_CATEGORY(logApplication, "pcm.Application")

namespace pcm {

namespace {
constexpr int kNotificationPollIntervalMs = 30 * 1000;
// Upper bound on how long specialist startup waits for the keychain to hand
// back the token-backend bearer credential (see loadBearerCredential()).
constexpr int kBearerCredentialReadTimeoutMs = 3 * 1000;

bool sameUpcomingMeetings(const QList<UpcomingMeeting> &lhs,
                          const QList<UpcomingMeeting> &rhs) {
  return std::equal(lhs.cbegin(), lhs.cend(), rhs.cbegin(), rhs.cend(),
                    [](const UpcomingMeeting &a, const UpcomingMeeting &b) {
                      return a.meetingRef == b.meetingRef && a.title == b.title &&
                             a.startTime == b.startTime &&
                             a.joinEnabled == b.joinEnabled && a.eventId == b.eventId;
                    });
}

// Renamed from PsyClientManager to Sessio. Installs that still have their
// Qt-managed settings/backups directory under the old org/app name get it
// moved to the new one before anything reads or writes there.
void migrateLegacyAppConfigDirectory(const QString &legacyName,
                                     const QString &newName) {
  const QString originalOrg = QCoreApplication::organizationName();
  const QString originalApp = QCoreApplication::applicationName();

  QCoreApplication::setOrganizationName(legacyName);
  QCoreApplication::setApplicationName(legacyName);
  const QString legacyDir =
      QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);

  QCoreApplication::setOrganizationName(newName);
  QCoreApplication::setApplicationName(newName);
  const QString newDir =
      QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);

  QCoreApplication::setOrganizationName(originalOrg);
  QCoreApplication::setApplicationName(originalApp);

  if (newDir.isEmpty() || legacyDir.isEmpty() || newDir == legacyDir) {
    return;
  }
  if (QDir(newDir).exists() || !QDir(legacyDir).exists()) {
    return;
  }

  QDir().mkpath(QFileInfo(newDir).absolutePath());
  QDir().rename(legacyDir, newDir);
}

void clearSensitiveText(QString *text) {
  text->fill(QChar{});
  text->clear();
}

void clearSensitiveString(std::string *text) {
  sodium_memzero(text->data(), text->size());
  text->clear();
}
}

Application::Application() = default;

int Application::run(int argc, char *argv[], const QString &launchUrl) {
  QApplication app(argc, argv);

  // Installed on &app as early as possible, before the role dialog or any
  // other nested event loop below can run: on macOS, a cold start via a
  // sessio:// link delivers a QFileOpenEvent to the QApplication instance
  // itself, and it can arrive before mMainWindow/mClientModeWindow exists.
  // eventFilter() queues that URL into mPendingJoinUrl when neither window
  // exists yet; runClientFlow()/runSpecialistFlow() replay it via
  // handleJoinLink() right after constructing their window.
  app.installEventFilter(this);

  // Single-instance guard: must run before any other setup below (style,
  // translations, config read, role dialog, window construction) so a second
  // launch exits as fast as possible. It can't run before QApplication
  // itself is constructed — QLocalServer/QLocalSocket need a
  // QCoreApplication-derived event-loop instance to already exist (the
  // guard's own tests construct QCoreApplication first for the same reason)
  // — but everything else in this function is far heavier than constructing
  // QApplication, so a secondary instance still exits well before any of it
  // runs.
  mSingleInstanceGuard = std::make_unique<SingleInstanceGuard>();
  if (!mSingleInstanceGuard->isPrimaryInstance()) {
    // Forward unconditionally, even when launchUrl is empty: a plain
    // relaunch (e.g. double-clicking the icon again with no sessio:// link
    // involved) should still make the primary instance raise its window.
    // SingleInstanceGuard::forwardToPrimaryInstance() substitutes a
    // non-empty activation sentinel for an empty url, since writing zero
    // bytes over the local socket would never reach the primary at all.
    mSingleInstanceGuard->forwardToPrimaryInstance(launchUrl);
    return 0;
  }

  app.setQuitOnLastWindowClosed(false);
  migrateLegacyAppConfigDirectory(QStringLiteral("PsyClientManager"),
                                  QStringLiteral("Sessio"));
  app.setOrganizationName("Sessio");
  app.setApplicationName("Sessio");
  app.setApplicationDisplayName("Sessio");
  app.setApplicationVersion("0.2.8");
  // Installed builds ship libical's timezone data next to the executable; the
  // path compiled into the schedule engine only exists in development trees.
  pcm::meeting::configureScheduleZoneinfo(QCoreApplication::applicationDirPath());
  // Wayland panels match a window to its .desktop entry (and icon) by app_id,
  // which Qt derives from the desktop file name; without this an RPM-installed
  // Sessio's window can end up with a foreign icon.
  QGuiApplication::setDesktopFileName(QStringLiteral("Sessio"));
  app.setWindowIcon(QIcon::fromTheme(QStringLiteral("Sessio"),
                                     QIcon(":/icons/brain-solid-full.svg")));
  auto *style = new oclero::qlementine::QlementineStyle(&app);
  app.setStyle(style);

  auto *themeManager = new oclero::qlementine::ThemeManager(style, &app);
  themeManager->loadDirectory(":/themes");
  themeManager->setCurrentTheme("Dark");
  qCInfo(logApplication) << "Qlementine themes loaded:" << themeManager->themeCount()
                         << "current:" << themeManager->currentTheme();

  QTranslator appTranslator;
  QTranslator qtTranslator;
  const QString localeName = QLocale::system().name().toLower();
  const QString preferredLanguage = pcm::app_settings::languageCode();
  const bool preferRu = preferredLanguage == "ru" ||
                        (preferredLanguage == "system" && localeName.startsWith("ru"));
  const auto qtLocale = preferRu ? QLocale(QLocale::Russian) : QLocale(QLocale::English);
  if (qtTranslator.load(qtLocale, QStringLiteral("qtbase"), QStringLiteral("_"),
                        QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
    app.installTranslator(&qtTranslator);
    qCInfo(logApplication) << "Loaded Qt translation for locale:" << qtLocale.name();
  } else if (preferRu) {
    qCWarning(logApplication) << "Failed to load Qt base translation for locale:"
                              << qtLocale.name();
  }

  const QStringList translationOrder =
      preferRu ? QStringList{"app_ru", "app_en"}
               : QStringList{"app_en", "app_ru"};

  auto tryLoadTranslation = [&](const QString &baseName) -> bool {
    const QStringList resourceCandidates = {
        QString(":/i18n/%1.qm").arg(baseName),
        QString(":/i18n/i18n/%1.qm").arg(baseName),
    };
    for (const auto &resourcePath : resourceCandidates) {
      if (appTranslator.load(resourcePath)) {
        app.installTranslator(&appTranslator);
        qCInfo(logApplication) << "Loaded translation:" << resourcePath;
        return true;
      }
    }
    return false;
  };

  bool translationLoaded = false;
  for (const auto &baseName : translationOrder) {
    if (tryLoadTranslation(baseName)) {
      translationLoaded = true;
      break;
    }
  }

  if (!translationLoaded) {
    qCWarning(logApplication) << "Failed to load translations. Locale:"
                              << localeName;
  }

  config::Config conf;
  try {
    conf = config::Config::read_config();
  } catch (const std::exception &error) {
    qCWarning(logApplication) << "Failed to read config, falling back to defaults:"
                              << error.what();
  }

  auto role = config::appRoleFromString(QString::fromStdString(conf.app_role));
  if (!role.has_value() || *role == config::AppRole::Unset) {
    RoleSelectionDialog roleDialog;
    if (roleDialog.exec() != QDialog::Accepted || !roleDialog.selectedRole().has_value()) {
      return 0; // user closed the first-launch prompt without choosing
    }
    role = roleDialog.selectedRole();
    conf.app_role = config::appRoleToString(*role).toStdString();
    try {
      config::Config::save_config(conf);
    } catch (const std::exception &error) {
      // The chosen role still applies to this session; the prompt just
      // reappears on the next launch.
      qCWarning(logApplication) << "Failed to save the selected role:" << error.what();
    }
  }
  qCInfo(logApplication) << "Starting with role:" << config::appRoleToString(*role);

  mDeviceManager = std::make_unique<pcm::video::DeviceManager>();
  mTokenBackendBaseUrl = QString::fromStdString(conf.token_backend_base_url);
  mTokenClient = std::make_unique<pcm::tokenclient::TokenBackendClient>(mTokenBackendBaseUrl);

  // A later launch forwards its sessio:// URL here once this instance's
  // window exists; handleJoinLink() itself checks which of mMainWindow /
  // mClientModeWindow got constructed, so this connection is safe to make
  // before the role branch below actually builds either window.
  connect(mSingleInstanceGuard.get(), &SingleInstanceGuard::urlReceivedFromSecondaryInstance,
          this, &Application::handleJoinLink);

  if (*role == config::AppRole::Client) {
    return runClientFlow(app, launchUrl);
  }
  return runSpecialistFlow(app, launchUrl);
}

// Client mode: a join-by-code window and nothing else. Deliberately never
// constructs Database, QClientModel, MeetingCoordinator, AutoBackupScheduler,
// the tray/notifications or app-lock, and never runs a pending backup restore
// or reads the specialist bearer credential from the keychain.
int Application::runClientFlow(QApplication &app, const QString &launchUrl) {
  // run() turns this off because the specialist flow keeps running in the
  // system tray; Client mode has no tray icon, so closing its only window
  // must end the process.
  app.setQuitOnLastWindowClosed(true);
  mClientModeWindow =
      std::make_unique<ClientModeWindow>(mDeviceManager.get(), mTokenClient.get());
  mClientModeWindow->show();
  if (!launchUrl.isEmpty()) {
    handleJoinLink(launchUrl);
  }
  if (!mPendingJoinUrl.isEmpty()) {
    handleJoinLink(mPendingJoinUrl);
    mPendingJoinUrl.clear();
  }
  return app.exec();
}

// Specialist mode: the full application.
int Application::runSpecialistFlow(QApplication &app, const QString &launchUrl) {
  restorePendingBackup();

  mDb = std::make_shared<database::Database>(mConf);

  mAutoBackupScheduler =
      std::make_unique<pcm::backup::AutoBackupScheduler>(mDb);
  mAutoBackupScheduler->start();

  loadBearerCredential();
  mMeetingCoordinator = std::make_unique<pcm::meeting::MeetingCoordinator>(
      mTokenBackendBaseUrl, mBearerCredential, this);

  mMainWindow = std::make_unique<MainWindow>();
  mClientModel = std::make_shared<QClientModel>(mDb);

  // Recurring-call publishing: schedule outbox sync, permanent invitation and
  // the facade the event editor / timeline use. The sync starts once the
  // window is up (see below).
  mScheduleCredentialReader =
      std::make_unique<StoreCredentialReader>(*mTokenCredentialStore);
  mInvitationSecretStore = std::make_unique<QtKeychainSeriesInvitationStore>();
  mScheduleSync = std::make_unique<pcm::meeting::ScheduleSync>(
      *mDb, *mTokenClient, mScheduleCredentialReader->reader());
  mSeriesInvitations = std::make_unique<pcm::meeting::SeriesInvitationService>(
      *mDb, *mScheduleSync, *mTokenClient, mScheduleCredentialReader->reader(),
      *mInvitationSecretStore);
  mScheduleCommitter = std::make_unique<pcm::meeting::SeriesScheduleCommitter>(*mDb);
  mScheduleCommitter->setSync(mScheduleSync.get());
  mSeriesCalls = std::make_unique<pcm::meeting::SeriesCallService>(
      *mDb, *mScheduleSync, *mSeriesInvitations, *mScheduleCommitter,
      mMeetingCoordinator.get());

  auto *timelineModel = new QTimelineModel(mDb, mMeetingCoordinator.get(), this);
  timelineModel->setScheduleCommitter(mScheduleCommitter.get());
  mMainWindow->addEventInfoPage(timelineModel, mMeetingCoordinator.get());
  if (auto *eventPage = dynamic_cast<QEventInfoPage *>(
          mMainWindow->getPage(MainWindow::Pages::eventInfo))) {
    eventPage->setSeriesCallService(mSeriesCalls.get());
  }
  mMainWindow->addClientInfoPage(mClientModel);
  mMainWindow->addAnalyticsPage(mDb);
  mMainWindow->addClientCardPage(mDb);
  mMainWindow->addClientNotesPage(mDb);
  mMainWindow->addCallsPage(mDeviceManager.get(), mTokenClient.get(),
                            [this]() { return mBearerCredential; });
  refreshUpcomingMeetings();
  mMainWindow->setDatabase(mDb);
  mMainWindow->connectSignals();
  mMainWindow->installEventFilter(this);
  // app.installEventFilter(this) already happened at the top of run(), before
  // mMainWindow existed, so a QFileOpenEvent arriving during earlier startup
  // (e.g. the first-launch role dialog) is queued in mPendingJoinUrl rather
  // than lost; it is replayed below alongside the launchUrl parameter.
  connectSignals();
  initializeAppLock();
  initializeNotifications();
  // Keep the Calls tab's "today's meetings" list current (meetings created or
  // canceled while the app sits in the tray) on the notification poll cadence.
  connect(&mNotificationTimer, &QTimer::timeout, this, &Application::refreshUpcomingMeetings);

  mMainWindow->show();
  // Recover the persisted schedule queue (and probe the backend once).
  mScheduleSync->start();
  // Restart resume. A series published before the app was closed may lack its
  // permanent invitation (ensureInvitation is a no-op for series that have one
  // and waits for the schedule ACK otherwise), or may hold the idempotency key
  // of a reissue that was interrupted before its secret was stored.
  for (const auto &series : mDb->get_event_series_for_range(0, 253402300799000)) {
    const auto identity = mDb->get_schedule_identity(series.id);
    if (!identity.has_value()) {
      continue;
    }
    if (identity->invitation_generation <= 0) {
      mSeriesInvitations->ensureInvitation(series.id);
    } else if (identity->invitation_key.has_value() && !identity->invitation_key->empty()) {
      mSeriesInvitations->resumeInterruptedReissue(series.id);
    }
  }
  connect(mScheduleSync.get(), &pcm::meeting::ScheduleSync::statusChanged, this,
          [this](const QString &) { refreshUpcomingMeetings(); });
  if (!launchUrl.isEmpty()) {
    handleJoinLink(launchUrl);
  }
  if (!mPendingJoinUrl.isEmpty()) {
    handleJoinLink(mPendingJoinUrl);
    mPendingJoinUrl.clear();
  }

  return app.exec();
}

// Reads the token-backend bearer credential from the keychain. The read is
// asynchronous (QtKeychain completes it from the event loop), and
// MeetingCoordinator takes the credential by value at construction, before
// app.exec() is running, so without a wait it would always be constructed
// with an empty credential. Wait for the result for at most
// kBearerCredentialReadTimeoutMs; a slower keychain (e.g. a locked keyring
// prompting for its password) still updates mBearerCredential when it
// finishes, which the Calls tab picks up through its provider lambda.
void Application::loadBearerCredential() {
  mTokenCredentialStore = std::make_unique<QtKeychainTokenBackendCredentialStore>();
  connect(mTokenCredentialStore.get(), &TokenBackendCredentialStore::readFinished, this,
          [this](const bool ok, const QString &credential, const QString &error) {
            mBearerCredentialReadDone = true;
            if (ok) {
              mBearerCredential = credential;
              // Bug 3 (fixwave group 5): this lambda fires both for the very
              // first startup read (here mMeetingCoordinator is still null —
              // runSpecialistFlow() constructs it only after
              // loadBearerCredential() returns, see that function's body)
              // and for every later re-read triggered by onSettingsSaved()
              // (where it already exists). The null check makes both cases
              // correct: nothing to propagate to yet on the first read
              // (MeetingCoordinator's constructor takes the current
              // mBearerCredential by value instead), and live propagation on
              // every later read.
              if (mMeetingCoordinator) {
                mMeetingCoordinator->setBearerCredential(mBearerCredential);
              }
            } else {
              qCWarning(logApplication)
                  << "Token-backend bearer credential unavailable:" << error;
            }
          });

  QEventLoop waitLoop;
  // Context object &waitLoop: both connections die with the loop, so a read
  // finishing after the timeout never touches the destroyed local.
  connect(mTokenCredentialStore.get(), &TokenBackendCredentialStore::readFinished, &waitLoop,
          &QEventLoop::quit);
  QTimer::singleShot(kBearerCredentialReadTimeoutMs, &waitLoop, &QEventLoop::quit);
  mTokenCredentialStore->readBearerCredential();
  if (!mBearerCredentialReadDone) {
    waitLoop.exec();
  }
  if (!mBearerCredentialReadDone) {
    qCWarning(logApplication) << "Timed out waiting for the keychain; LiveKit meeting "
                                 "creation will run without a bearer credential";
  }
}

// Builds the Calls tab's "today's meetings" list: today's LiveKit events that
// have a meeting reference, are scheduled/completed/confirmed (the same status
// set the reminder notifications use) and have not ended yet. Only pushes the
// list to the page when it actually changed, so the periodic refresh does not
// rebuild the rows (and drop keyboard focus) every tick.
void Application::refreshUpcomingMeetings() {
  if (!mMainWindow || !mDb) {
    return;
  }
  auto *callsPage = dynamic_cast<CallsPage *>(mMainWindow->getPage(MainWindow::Pages::calls));
  if (callsPage == nullptr) {
    return;
  }

  const auto today = QDate::currentDate();
  const auto localTz = QTimeZone::systemTimeZone();
  const auto dayStartMs = QDateTime(today, QTime(0, 0), localTz).toMSecsSinceEpoch();
  const auto dayEndMs =
      QDateTime(today.addDays(1), QTime(0, 0), localTz).toMSecsSinceEpoch() - 1;
  const auto nowMs = QDateTime::currentMSecsSinceEpoch();

  const auto isEligibleStatus = [](const int64_t statusId) {
    return statusId == 1 || statusId == 2 || statusId == 4;
  };

  auto events = mDb->get_day_events(dayStartMs, dayEndMs);
  std::sort(events.begin(), events.end(), [](const DuckEvent &a, const DuckEvent &b) {
    return a.start_date.value_or(0) < b.start_date.value_or(0);
  });

  // Occurrences of published series have no Event row until edited, and their
  // call is the series' permanent invitation, joined by occurrence.
  if (mSeriesCalls) {
    const auto dayStart = QDateTime::fromMSecsSinceEpoch(dayStartMs);
    const auto dayEnd = QDateTime::fromMSecsSinceEpoch(dayEndMs);
    auto virtualEvents = pcm::recurrence::virtualOccurrencesInRange(*mDb, dayStart, dayEnd);
    events.insert(events.end(), virtualEvents.cbegin(), virtualEvents.cend());
    std::sort(events.begin(), events.end(), [](const DuckEvent &a, const DuckEvent &b) {
      return a.start_date.value_or(0) < b.start_date.value_or(0);
    });
  }

  QList<UpcomingMeeting> meetings;
  for (auto &event : events) {
    if (mSeriesCalls && event.series_id.has_value()) {
      if (const auto target = mSeriesCalls->joinTargetFor(event)) {
        event.provider_kind = "livekit";
        event.meeting_ref = target->toStdString();
      }
    }
    const auto kind = pcm::meeting::providerKindFromString(event.provider_kind.value_or(""));
    if (kind != pcm::meeting::ProviderKind::LiveKit) {
      continue;
    }
    if (!event.meeting_ref.has_value() || event.meeting_ref->empty() ||
        !isEligibleStatus(event.event_stat_id)) {
      continue;
    }
    if (event.end_date.has_value() && *event.end_date < nowMs) {
      continue;
    }

    UpcomingMeeting meeting;
    meeting.meetingRef = QString::fromStdString(*event.meeting_ref);
    meeting.title = QString::fromStdString(event.name.value_or(""));
    meeting.startTime = QDateTime::fromMSecsSinceEpoch(event.start_date.value_or(0));
    meeting.joinEnabled = true;
    meeting.eventId = event.id;
    meetings.append(meeting);
  }

  if (sameUpcomingMeetings(meetings, mUpcomingMeetings)) {
    return;
  }
  mUpcomingMeetings = meetings;
  callsPage->setUpcomingMeetings(mUpcomingMeetings);
}

// Handles a sessio://join?code=...&passcode=... URL, whether it came from
// this instance's own launch argument or was forwarded by a second instance
// via mSingleInstanceGuard. The forwarded payload is not always a real join
// link — a plain relaunch with no sessio:// url (e.g. double-clicking the
// icon again) forwards a non-empty activation sentinel instead (see
// SingleInstanceGuard::forwardToPrimaryInstance()) so raising the window
// below always runs regardless of whether url parses as a join link.
// Exactly one of mMainWindow / mClientModeWindow is constructed by the time
// this can run (after the role branch in run()), so this only ever touches
// the window for the role this process is actually running as.
void Application::handleJoinLink(const QString &url) {
  // Unconditional: raising/showing the existing window must happen whether
  // or not url turns out to be a parseable join link.
  if (mMainWindow) {
    mMainWindow->show();
    mMainWindow->raise();
  } else if (mClientModeWindow) {
    mClientModeWindow->show();
    mClientModeWindow->raise();
  }

  const auto link = parseSessioJoinUrl(url);
  if (!link.has_value()) {
    return;
  }
  // Client mode: an invitation that names its token backend retargets the
  // token client before the code is prefilled, so the join that follows goes
  // to the backend that issued the invitation. Persisted so a later manual
  // code entry or relaunch keeps using it.
  //
  // Specialist mode deliberately ignores it: the specialist's token client
  // also sends the keychain bearer credential (specialist-token requests from
  // the Calls tab), so letting any clicked link retarget it would hand that
  // credential to whatever backend the link names. Specialists configure
  // their backend in Settings instead.
  const bool linkNamesBackend = link->backendUrl.has_value() && !link->backendUrl->isEmpty();
  if (linkNamesBackend && mClientModeWindow && !mMainWindow) {
    applyTokenBackendBaseUrl(*link->backendUrl);
    persistTokenBackendBaseUrl(*link->backendUrl);
  } else if (linkNamesBackend) {
    qCInfo(logApplication) << "Ignoring the join link's backend in specialist mode";
  }
  if (mMainWindow) {
    if (auto *callsPage = dynamic_cast<CallsPage *>(mMainWindow->getPage(MainWindow::Pages::calls))) {
      callsPage->prefillJoinCode(link->code, link->passcode);
    }
  } else if (mClientModeWindow) {
    // ClientModeWindow's CallsPage is its central widget — reuse the same
    // dynamic_cast pattern via centralWidget() rather than a page lookup.
    if (auto *callsPage = dynamic_cast<CallsPage *>(mClientModeWindow->centralWidget())) {
      callsPage->prefillJoinCode(link->code, link->passcode);
    }
  }
}

void Application::applyTokenBackendBaseUrl(const QString &baseUrl) {
  mTokenBackendBaseUrl = baseUrl;
  if (mTokenClient) {
    mTokenClient->setBaseUrl(mTokenBackendBaseUrl);
    if (mScheduleSync) {
      mScheduleSync->wake();
    }
  }
  // Bug 3 (fixwave group 5): mMeetingCoordinator is null in Client mode
  // (never constructed there) and also null here if this runs before
  // runSpecialistFlow() constructs it — but applyTokenBackendBaseUrl() is
  // only ever called from handleJoinLink() (Client mode only, guarded by
  // mClientModeWindow && !mMainWindow there) and onSettingsSaved() (only
  // reachable once mMainWindow/mMeetingCoordinator already exist), so this
  // guard is a defensive no-op in both current call sites, not a real gap.
  if (mMeetingCoordinator) {
    mMeetingCoordinator->setTokenBackendBaseUrl(mTokenBackendBaseUrl);
  }
}

void Application::persistTokenBackendBaseUrl(const QString &baseUrl) {
  config::Config conf;
  try {
    conf = config::Config::read_config();
  } catch (const std::exception &error) {
    // Saving a default-constructed Config here would overwrite the unreadable
    // file's role and database path, so leave it alone; the new URL still
    // applies for this session.
    qCWarning(logApplication) << "Failed to read config, not persisting the token backend URL:"
                              << error.what();
    return;
  }
  if (conf.token_backend_base_url == baseUrl.toStdString()) {
    return;
  }
  conf.token_backend_base_url = baseUrl.toStdString();
  try {
    config::Config::save_config(conf);
  } catch (const std::exception &error) {
    qCWarning(logApplication) << "Failed to save the token backend URL:" << error.what();
  }
}

// MainWindow emits settingsSaved after its SettingsDialog closes; the LiveKit
// section may have written a new token backend URL to Config, which the
// already-constructed token client would otherwise ignore until a restart.
void Application::onSettingsSaved() {
  // The same section may also have written a new bearer credential to the
  // keychain; re-read it so the Calls tab's credential provider (which reads
  // mBearerCredential) picks it up. The readFinished connection made in
  // loadBearerCredential() updates mBearerCredential asynchronously.
  if (mTokenCredentialStore) {
    mTokenCredentialStore->readBearerCredential();
  }
  if (mScheduleSync) {
    mScheduleSync->wake();
  }

  config::Config conf;
  try {
    conf = config::Config::read_config();
  } catch (const std::exception &error) {
    qCWarning(logApplication) << "Failed to read config after settings changed:" << error.what();
    return;
  }
  applyTokenBackendBaseUrl(QString::fromStdString(conf.token_backend_base_url));
}

void Application::restorePendingBackup() {
  const auto markerPath = Poco::Path(mConf.config_pth.value())
                              .makeParent()
                              .append("pending-restore.json")
                              .toString();
  if (const auto marker = pcm::backup::read_pending_restore_marker(markerPath)) {
    std::optional<std::string> recoveryPassword;
    bool restoreCancelled = false;
    if (pcm::backup::detect_backup_container(marker->backup_path) ==
        pcm::backup::BackupContainerKind::Encrypted) {
      bool accepted = false;
      QString password = QInputDialog::getText(
          nullptr, tr("Encrypted Backup"),
          tr("The backup is encrypted. Enter its recovery password to continue "
             "the restore."),
          QLineEdit::Password, {}, &accepted);
      if (!accepted) {
        restoreCancelled = true;
      } else {
        auto passwordBytes = password.toUtf8();
        recoveryPassword = std::string(
            passwordBytes.constData(), static_cast<std::size_t>(passwordBytes.size()));
        std::fill(passwordBytes.begin(), passwordBytes.end(), '\0');
      }
      clearSensitiveText(&password);
    }

    pcm::backup::RestoreResult result;
    if (!restoreCancelled) {
      pcm::backup::RestoreService service;
      pcm::backup::RestoreOptions options;
      options.attachments_root =
          pcm::app_settings::attachmentsStorageRoot().toStdString();
      options.recovery_password = std::move(recoveryPassword);
      result = service.restore_backup(
          marker->backup_path, mConf.db_conf.value_.db_pth.toString(), options);
      if (options.recovery_password.has_value()) {
        clearSensitiveString(&*options.recovery_password);
      }
    }
    pcm::backup::remove_pending_restore_marker(markerPath);
    if (restoreCancelled) {
      QMessageBox::information(nullptr, tr("Restore Cancelled"),
                               tr("The restore was cancelled. Your current data was not changed."));
    } else if (result.ok) {
      QMessageBox::information(nullptr, tr("Restore Complete"),
                               tr("The backup was restored successfully."));
    } else {
      QMessageBox::warning(
          nullptr, tr("Restore Failed"),
          tr("The restore could not be completed:\n%1\n\nYour previous "
             "data was kept.").arg(QString::fromStdString(result.error)));
    }
  }

}

bool Application::eventFilter(QObject *watched, QEvent *event) {
  // macOS delivers a sessio:// link (or any registered URL scheme/file open
  // request) as a QFileOpenEvent sent to the QApplication instance itself,
  // not to mMainWindow/mClientModeWindow — hence the filter installed on
  // &app at the top of run(). If neither window has been constructed yet
  // (a cold start), queue the URL in mPendingJoinUrl instead of dropping it;
  // runClientFlow()/runSpecialistFlow() replay it once their window exists.
  if (event != nullptr && event->type() == QEvent::FileOpen) {
    const auto *openEvent = static_cast<QFileOpenEvent *>(event);
    const QString url = openEvent->url().toString();
    if (mMainWindow || mClientModeWindow) {
      handleJoinLink(url);
    } else {
      mPendingJoinUrl = url;
    }
    return true;
  }

  if (watched == mMainWindow.get() && event != nullptr &&
      event->type() == QEvent::Resize && mAppLockOverlay != nullptr) {
    mAppLockOverlay->setGeometry(mMainWindow->rect());
  }

  if (watched == mMainWindow.get() && event != nullptr &&
      event->type() == QEvent::Close && !mIsQuitting) {
    if (mTrayIcon) {
      event->ignore();
      mMainWindow->hide();
      if (!mTrayCloseHintShown && QSystemTrayIcon::supportsMessages()) {
        mTrayIcon->showMessage(tr("Sessio"),
                               tr("The app is still running in the system tray."),
                               QSystemTrayIcon::Information, 5000);
        mTrayCloseHintShown = true;
      }
      return true;
    }
  }

  if (event != nullptr && mAppLockController && mAppLockService &&
      mAppLockService->isConfigured() && !mAppLockController->isLocked()) {
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::TouchBegin:
      mAppLockController->recordActivity(QDateTime::currentMSecsSinceEpoch());
      break;
    default:
      break;
    }
  }

  return QObject::eventFilter(watched, event);
}

void Application::saveClient(const DuckClient &client) {
  qCDebug(logApplication) << "Application::saveClient| Client id:" << client.id;
  if (client.id > 0) {
    mDb->update_client(client);
  } else {
    mDb->add_client(client);
  }
  if (mClientModel) {
    mClientModel->reload();
  }
}

void Application::removeClient(const int64_t clientId) {
  qCDebug(logApplication) << "Application::removeClient| Client id:" << clientId;
  if (clientId <= 0) {
    return;
  }

  if (!mDb->remove_client(clientId)) {
    qCWarning(logApplication) << "Application::removeClient| Failed to remove client:"
                              << clientId;
    return;
  }

  if (mClientModel) {
    mClientModel->reload();
  }
}

void Application::fillClientComboBox(QComboBox *box) {
  box->clear();
  const auto clients = mDb->get_clients();
  for (const auto &client : clients) {
    if (!client || !client->client_active) {
      continue;
    }

    QString title{"%1 %2"};
    const auto name = client->name != std::nullopt
                          ? QString::fromStdString(client->name.value())
                          : tr(": VALUE_UNDEFINED");
    const auto lastname = client->last_name != std::nullopt
                              ? QString::fromStdString(client->last_name.value())
                              : tr(": VALUE_UNDEFINED");
    title = title.arg(name, lastname);
    const auto var = QVariant::fromValue(client->id);
    box->addItem(title, var);
  }
}

void Application::saveClientEventPair(const int64_t clientId,
                                      const int64_t eventId) {
  qCDebug(logApplication) << "Application::saveClientEventPair| Client id:"
                          << clientId << " Event id:" << eventId;

  mDb->add_event_client(eventId, clientId);
}

QString Application::notificationKey(const DuckEvent &event) const {
  return QStringLiteral("%1:%2")
      .arg(event.id)
      .arg(event.start_date.value_or(0));
}

QString Application::notificationTitleForEvent(const DuckEvent &event) const {
  Q_UNUSED(event);
  return pcm::notificationTitle(pcm::app_settings::notificationPrivacyMode());
}

QString Application::notificationBodyForEvent(const DuckEvent &event) const {
  const auto startTime = QDateTime::fromMSecsSinceEpoch(
                             event.start_date.value_or(0), QTimeZone::UTC)
                             .toLocalTime();

  pcm::NotificationEventInfo info;
  info.title = QString::fromStdString(event.name.value_or(""));
  info.startTime = startTime;
  info.isWorkEvent = event.is_work_event;

  if (event.is_work_event) {
    if (event.client_name.has_value()) {
      info.clientName = QString::fromStdString(*event.client_name).trimmed();
    } else {
      try {
        const auto client = mDb->get_client_by_event(event.id);
        info.clientName = pcm::recurrence::fullClientName(client);
      } catch (const std::exception &) {
      }
    }
  }

  return pcm::notificationBody(pcm::app_settings::notificationPrivacyMode(), info);
}

void Application::initializeNotifications() {
  mTrayIcon =
      std::make_unique<QSystemTrayIcon>(QIcon(":/icons/brain-solid-full.svg"));
  mTrayIcon->setToolTip(QStringLiteral("Sessio"));

  auto *trayMenu = new QMenu();
  auto *openAction = trayMenu->addAction(tr("Open"));
  mLockAppAction = trayMenu->addAction(tr("Lock app"));
  auto *quitAction = trayMenu->addAction(tr("Quit"));
  connect(openAction, &QAction::triggered, this, &Application::restoreMainWindow);
  mLockAppAction->setEnabled(mAppLockService && mAppLockService->isConfigured());
  connect(mLockAppAction, &QAction::triggered, this, [this]() {
    if (mAppLockController && mAppLockService && mAppLockService->isConfigured()) {
      lockApplication();
    }
  });
  connect(quitAction, &QAction::triggered, this, &Application::quitApplication);
  connect(trayMenu, &QMenu::aboutToShow, this, [this]() {
    mLockAppAction->setEnabled(mAppLockService && mAppLockService->isConfigured());
  });
  mTrayIcon->setContextMenu(trayMenu);
  connect(mTrayIcon.get(), &QSystemTrayIcon::activated, this,
          [this](const QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger ||
                reason == QSystemTrayIcon::DoubleClick) {
              restoreMainWindow();
            }
          });

  mTrayIcon->show();

  mNotificationTimer.setInterval(kNotificationPollIntervalMs);
  connect(&mNotificationTimer, &QTimer::timeout, this,
          &Application::checkUpcomingEventNotifications);
  mNotificationTimer.start();

  checkUpcomingEventNotifications();
}

void Application::initializeAppLock() {
  mAppLockService = std::make_unique<AppLockService>();
  mAppLockController = std::make_unique<AppLockController>(
      pcm::app_settings::appLockTimeoutMinutes());
  mAppLockController->recordActivity(QDateTime::currentMSecsSinceEpoch());
  mAppLockTimer.setInterval(1000);
  connect(&mAppLockTimer, &QTimer::timeout, this, &Application::checkAppLock);
  mAppLockTimer.start();
}

void Application::checkAppLock() {
  if (!mAppLockService || !mAppLockController || !mAppLockService->isConfigured()) {
    return;
  }
  mAppLockController->setTimeoutMinutes(pcm::app_settings::appLockTimeoutMinutes());
  if (mAppLockController->shouldLock(QDateTime::currentMSecsSinceEpoch())) {
    lockApplication();
  }
}

void Application::lockApplication() {
  if (!mAppLockService || !mAppLockController || mAppLockDialogVisible) {
    return;
  }
  mAppLockController->lockNow();
  mAppLockDialogVisible = true;

  mAppLockOverlay = new QWidget(mMainWindow.get());
  mAppLockOverlay->setObjectName(QStringLiteral("appLockOverlay"));
  mAppLockOverlay->setAttribute(Qt::WA_StyledBackground);
  mAppLockOverlay->setGeometry(mMainWindow->rect());
  mAppLockOverlay->setStyleSheet("background-color: #20242e;");
  mAppLockOverlay->show();
  mAppLockOverlay->raise();
  restoreMainWindow();

  AppLockDialog dialog{*mAppLockService, mMainWindow.get()};
  if (dialog.exec() == QDialog::Accepted) {
    mAppLockController->unlock(QDateTime::currentMSecsSinceEpoch());
  }
  delete mAppLockOverlay;
  mAppLockOverlay = nullptr;
  mAppLockDialogVisible = false;
}

void Application::restoreMainWindow() {
  if (!mMainWindow) {
    return;
  }

  mMainWindow->show();
  mMainWindow->raise();
  mMainWindow->activateWindow();
}

void Application::quitApplication() {
  if (mIsQuitting) {
    return;
  }
  mIsQuitting = true;
  if (mTrayIcon) {
    mTrayIcon->hide();
  }
  if (mAutoBackupScheduler && mAutoBackupScheduler->isDue()) {
    connect(mAutoBackupScheduler.get(),
            &pcm::backup::AutoBackupScheduler::backupFinished, this,
            [](bool, const QString &) { QApplication::quit(); });
    mAutoBackupScheduler->runAsync();
    return;
  }
  QApplication::quit();
}

void Application::checkUpcomingEventNotifications() {
  const auto now = QDateTime::currentDateTime();
  const auto nowMs = now.toMSecsSinceEpoch();

  if (!pcm::app_settings::notificationsEnabled()) {
    return;
  }

  if (!mTrayIcon || !QSystemTrayIcon::supportsMessages()) {
    return;
  }

  const auto leadMinutes = std::max(1, pcm::app_settings::notificationLeadMinutes());
  const auto windowEndMs = now.addSecs(leadMinutes * 60).toMSecsSinceEpoch();
  const auto events = mDb->get_upcoming_events(nowMs, windowEndMs);
  for (const auto &event : events) {
    mTrayIcon->showMessage(notificationTitleForEvent(event),
                           notificationBodyForEvent(event),
                           QSystemTrayIcon::Information, 15'000);
    mDb->mark_event_reminder_notified(event.id, nowMs);
  }

  notifyUpcomingSeriesOccurrences(nowMs, windowEndMs);
}

void Application::notifyUpcomingSeriesOccurrences(const int64_t nowMs,
                                                   const int64_t windowEndMs) {
  const auto localTz = QTimeZone::systemTimeZone();
  const auto rangeStart =
      QDateTime::fromMSecsSinceEpoch(nowMs, QTimeZone::UTC).toTimeZone(localTz);
  const auto rangeEnd =
      QDateTime::fromMSecsSinceEpoch(windowEndMs, QTimeZone::UTC).toTimeZone(localTz);

  // Mirrors the status filter used by kSelectUpcomingEventsQuery: scheduled(1),
  // completed(2), confirmed(4). Excludes canceled(3), no_show(5), rescheduled(6).
  const auto isEligibleStatus = [](const int64_t statusId) {
    return statusId == 1 || statusId == 2 || statusId == 4;
  };

  const auto exceptions =
      mDb->get_event_series_exceptions_for_range(nowMs, windowEndMs);
  const auto alreadyNotified =
      mDb->get_notified_series_occurrences_for_range(nowMs, windowEndMs);

  auto seriesList = mDb->get_event_series_for_range(nowMs, windowEndMs);
  for (auto &series : seriesList) {
    if (!isEligibleStatus(series.event_stat_id)) {
      continue;
    }
    pcm::recurrence::resolveSeriesClientName(*mDb, series);

    const auto materializedStarts =
        mDb->get_materialized_occurrence_starts_for_series(series.id);
    const auto occurrences =
        pcm::recurrence::seriesOccurrences(*mDb, series, rangeStart, rangeEnd);
    for (const auto &occurrence : occurrences) {
      const auto occurrenceStartMs = occurrence.toUTC().toMSecsSinceEpoch();
      const std::pair<int64_t, int64_t> key{series.id, occurrenceStartMs};
      if (exceptions.contains(key) || materializedStarts.contains(occurrenceStartMs) ||
          alreadyNotified.contains(key)) {
        continue;
      }

      const auto virtualEvent =
          pcm::recurrence::buildVirtualOccurrence(series, occurrence, 0);
      mTrayIcon->showMessage(notificationTitleForEvent(virtualEvent),
                             notificationBodyForEvent(virtualEvent),
                             QSystemTrayIcon::Information, 15'000);
      mDb->mark_series_occurrence_reminder_notified(series.id, occurrenceStartMs,
                                                     nowMs);
    }
  }
}

void Application::connectSignals() {
  connect(mMainWindow.get(), &MainWindow::provideSaveClient, this,
          &Application::saveClient);
  connect(mMainWindow.get(), &MainWindow::provideRemoveClient, this,
          &Application::removeClient);
  connect(mMainWindow.get(), &MainWindow::provideClientEventPairSave, this,
          &Application::saveClientEventPair);
  connect(mMainWindow.get(), &MainWindow::settingsSaved, this, &Application::onSettingsSaved);

  {
    const auto widget = mMainWindow->getPage(MainWindow::Pages::eventInfo);
    const auto page = dynamic_cast<QEventInfoPage *>(widget);
    connect(page, &QEventInfoPage::provideFillClientComboBox, this,
            &Application::fillClientComboBox);
    connect(page, &QEventInfoPage::provideClientByEventId, [page, this](int64_t eventId) {
      const auto client = mDb->get_client_by_event(eventId);
      emit page->clientResolved(client.id);
    });
    // Connected before preselectLiveKitMeeting so a meeting created earlier in
    // this session is already in the Calls tab's list when it gets preselected
    // (slots run in connection order).
    connect(page, &QEventInfoPage::openLiveKitMeetingRequested, this,
            &Application::refreshUpcomingMeetings);
    connect(page, &QEventInfoPage::openLiveKitMeetingRequested, mMainWindow.get(),
            &MainWindow::preselectLiveKitMeeting);
  }

  // Specialist Calls tab: when a call for one of the specialist's own events
  // starts, show that event's client notes in the call's side panel. One
  // panel is created lazily and reused for later calls (CallPage detaches a
  // replaced panel without deleting it).
  {
    auto *callsPage = dynamic_cast<CallsPage *>(mMainWindow->getPage(MainWindow::Pages::calls));
    connect(callsPage, &CallsPage::eventKnownForCurrentCall, this,
            [this, callsPage](const int64_t eventId) {
              std::optional<DuckClient> client;
              try {
                client = mDb->get_client_by_event(eventId);
              } catch (const std::exception &) {
                // Event without a linked client: show an empty notes panel.
              }
              if (mCallNotesPanel.isNull()) {
                mCallNotesPanel = new ClientNotesPage(mDb, callsPage);
                callsPage->setSidePanelWidget(mCallNotesPanel);
              }
              mCallNotesPanel->setClientInfo(client);
              callsPage->setSidePanelExpandedByDefault(client.has_value());
            });
  }
}

} // namespace pcm
