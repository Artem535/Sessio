#include "application.h"
#include "app_lock_dialog.h"
#include "../backup/encrypted_container.h"
#include "../backup/restore_service.h"
#include "../event_view/recurrence_utils.h"
#include "../widgets/app_settings.h"

#include <Poco/Path.h>
#include <QDir>
#include <QFileInfo>
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

int Application::run(int argc, char *argv[]) {
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  migrateLegacyAppConfigDirectory(QStringLiteral("PsyClientManager"),
                                  QStringLiteral("Sessio"));
  app.setOrganizationName("Sessio");
  app.setApplicationName("Sessio");
  app.setApplicationDisplayName("Sessio");
  app.setApplicationVersion("0.1.34");
  app.setWindowIcon(QIcon(":/icons/brain-solid-full.svg"));
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

  restorePendingBackup();

  mDb = std::make_shared<database::Database>(mConf);

  mAutoBackupScheduler =
      std::make_unique<pcm::backup::AutoBackupScheduler>(mDb);
  mAutoBackupScheduler->start();

  // TODO(Task 17): real token-backend base URL/bearer credential wiring.
  // MeetingCoordinator's constructor gained a token-backend base URL and
  // bearer credential in Task 7; this call site is deliberately left as a
  // minimal placeholder until Task 17's role-branch rewrite of
  // Application::run() supplies the real config/keychain-driven values.
  mMeetingCoordinator = std::make_unique<pcm::meeting::MeetingCoordinator>(
      QString(), QString(), this);

  mMainWindow = std::make_unique<MainWindow>();
  mClientModel = std::make_shared<QClientModel>(mDb);

  mMainWindow->addEventInfoPage(new QTimelineModel(mDb, mMeetingCoordinator.get(), this),
                                mMeetingCoordinator.get());
  mMainWindow->addClientInfoPage(mClientModel);
  mMainWindow->addAnalyticsPage(mDb);
  mMainWindow->addClientCardPage(mDb);
  mMainWindow->addClientNotesPage(mDb);
  mMainWindow->setDatabase(mDb);
  mMainWindow->connectSignals();
  mMainWindow->installEventFilter(this);
  app.installEventFilter(this);
  connectSignals();
  initializeAppLock();
  initializeNotifications();

  mMainWindow->show();

  return app.exec();
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
    const auto occurrences = pcm::recurrence::occurrences(series, rangeStart, rangeEnd);
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

  {
    const auto widget = mMainWindow->getPage(MainWindow::Pages::eventInfo);
    const auto page = dynamic_cast<QEventInfoPage *>(widget);
    connect(page, &QEventInfoPage::provideFillClientComboBox, this,
            &Application::fillClientComboBox);
    connect(page, &QEventInfoPage::provideClientByEventId, [page, this](int64_t eventId) {
      const auto client = mDb->get_client_by_event(eventId);
      emit page->clientResolved(client.id);
    });
    connect(page, &QEventInfoPage::openLiveKitMeetingRequested, mMainWindow.get(),
            &MainWindow::preselectLiveKitMeeting);
  }
}

} // namespace pcm
