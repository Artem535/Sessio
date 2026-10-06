#include "main_window.h"
#include "../widgets/app_settings.h"
#include "ui/app/ui_mainwindow.h"

#include <oclero/qlementine/widgets/AboutDialog.hpp>
#include <oclero/qlementine/widgets/Switch.hpp>
#include <oclero/qlementine/widgets/LineEdit.hpp>

#include <QApplication>
#include <QIcon>
#include <QPixmap>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), mUi(std::make_unique<Ui::MainWindow>()) {
  mUi->setupUi(this);
  menuBar()->hide();
  mUi->gridLayout->setColumnStretch(0, 0);
  mUi->gridLayout->setColumnStretch(1, 1);
  mUi->gridLayout->setRowStretch(0, 0);
  mUi->gridLayout->setRowStretch(1, 1);
  mPageCustomWidgetLayout = new QHBoxLayout(mUi->pageCustomWidgetHost);
  mPageCustomWidgetLayout->setContentsMargins(
      pcm::widgets::constants::kPageContentMargin +
          pcm::widgets::constants::kPanelPadding,
      0,
      pcm::widgets::constants::kPageContentMargin +
          pcm::widgets::constants::kPanelPadding,
      0);
  mPageCustomWidgetLayout->setSpacing(pcm::widgets::constants::kPanelPadding);

  auto *titleLayout = new QHBoxLayout();
  titleLayout->setContentsMargins(0, 0, 0, 0);
  titleLayout->setSpacing(10);
  auto *titleIconLabel = new QLabel(this);
  titleIconLabel->setPixmap(QIcon(":/icons/brain-solid-full.svg").pixmap(24, 24));
  auto *titleTextLabel = new QLabel(mUi->label->text(), this);
  titleTextLabel->setFont(mUi->label->font());
  auto *titleWidget = new QWidget(this);
  titleWidget->setLayout(titleLayout);
  titleLayout->addWidget(titleIconLabel);
  titleLayout->addWidget(titleTextLabel);
  titleLayout->addStretch();
  mTitleWidget = titleWidget;
  mUi->gridLayout->replaceWidget(mUi->label, titleWidget);
  mUi->label->hide();
  mUi->label->deleteLater();

  // Create navigation buttons
  mBtnCalendar =
      new TabButton(QIcon(":/icons/calendar-solid-full.svg"), tr(": NAV_CALENDAR"), this);
  mBtnClients =
      new TabButton(QIcon(":/icons/users-line-solid-full.svg"), tr(": NAV_CLIENTS"), this);
  mBtnAnalytics =
      new TabButton(QIcon(":/icons/chart-area-solid-full.svg"), tr("Analytics"), this);
  mBtnProfile =
      new TabButton(QIcon(":/icons/users-gear-solid-full.svg"), tr(": NAV_DETAILS"), this);
  mBtnNotes =
      new TabButton(QIcon(":/icons/notes.svg"), tr("Notes"), this);
  mBtnCalls =
      new TabButton(QIcon(":/icons/video-solid-full.svg"), tr("Calls"), this);

  // Add buttons to the vertical layout
  mUi->verticalLayout->addWidget(mBtnCalendar);
  mUi->verticalLayout->addWidget(mBtnClients);
  mUi->verticalLayout->addWidget(mBtnAnalytics);
  mUi->verticalLayout->addWidget(mBtnProfile);
  mUi->verticalLayout->addWidget(mBtnNotes);
  mUi->verticalLayout->addWidget(mBtnCalls);
  mBtnProfile->hide();
  mBtnNotes->hide();

  mBtnBackToClients = new QPushButton(tr("Back to clients"), this);
  mBtnBackToClients->setFlat(true);
  mBtnBackToClients->setCursor(Qt::PointingHandCursor);
  connect(mBtnBackToClients, &QPushButton::clicked, this, [this]() {
    setClientNavigationVisible(Pages::clientCard, false);
    setClientNavigationVisible(Pages::clientNotes, false);
    showPage(Pages::clientInfo, mBtnClients);
  });

  // Add stretch to push buttons to the top
  mUi->verticalLayout->addStretch();
  setupUtilityButtons();

  // Initialize default UI style
  initDefaultStyle();
}


void MainWindow::addClientInfoPage(std::shared_ptr<QClientModel> model) {
  const auto page = new ClientInfo(std::move(model), this);
  mPages.insertOrAssign(Pages::clientInfo, page);

  const int index = mUi->stackedWidget->addWidget(page);
  mPagesIndex.insertOrAssign(Pages::clientInfo, index);

  mClientPageActions = new QWidget(this);
  auto *actionsLayout = new QHBoxLayout(mClientPageActions);
  actionsLayout->setContentsMargins(0, 0, 0, 0);
  actionsLayout->setSpacing(pcm::widgets::constants::kPanelPadding);

  mClientSearchInput = new oclero::qlementine::LineEdit(mClientPageActions);
  mClientSearchInput->setPlaceholderText(tr("Search clients"));
  mClientSearchInput->setClearButtonEnabled(true);
  mClientSearchInput->setIcon(QIcon(":/icons/user-solid-full.svg"));
  mClientSearchInput->setMinimumWidth(260);

  mShowInactiveClientsSwitch =
      new oclero::qlementine::Switch(mClientPageActions);
  mShowInactiveClientsSwitch->setText(tr("Show inactive"));

  mAddClientButton = new QPushButton(QIcon(":/icons/user-plus-solid-full.svg"),
                                     tr("Add client"), mClientPageActions);
  mAddClientButton->setIconSize(QSize(16, 16));
  mAddClientButton->setCursor(Qt::PointingHandCursor);

  actionsLayout->addWidget(mClientSearchInput, 1);
  actionsLayout->addWidget(mShowInactiveClientsSwitch, 0);
  actionsLayout->addWidget(mAddClientButton, 0);
  setPageCustomWidget(Pages::clientInfo, mClientPageActions);
}


void MainWindow::addEventInfoPage(QTimelineModel *model,
                                  pcm::meeting::MeetingCoordinator *meetingCoordinator) {
  const auto page = new QEventInfoPage(model, meetingCoordinator, this);
  mPages.insertOrAssign(Pages::eventInfo, page);

  const int index = mUi->stackedWidget->addWidget(page);
  mPagesIndex.insertOrAssign(Pages::eventInfo, index);
  // Day|Month switch and "New meeting" live in the top row next to the title.
  setPageCustomWidget(Pages::eventInfo, page->headerControls());
}

void MainWindow::addAnalyticsPage(std::shared_ptr<pcm::database::Database> db) {
  const auto page = new AnalyticsPage(std::move(db), this);
  mPages.insertOrAssign(Pages::analytics, page);

  const int index = mUi->stackedWidget->addWidget(page);
  mPagesIndex.insertOrAssign(Pages::analytics, index);
}


void MainWindow::addClientCardPage(std::shared_ptr<pcm::database::Database> db) {
  const auto page = new QClientInfoCardPage(std::move(db), this);
  mPages.insertOrAssign(Pages::clientCard, page);

  const int index = mUi->stackedWidget->addWidget(page);
  mPagesIndex.insertOrAssign(Pages::clientCard, index);
  setPageCustomWidget(Pages::clientCard, mBtnBackToClients);
}

void MainWindow::addClientNotesPage(std::shared_ptr<pcm::database::Database> db) {
  const auto page = new ClientNotesPage(std::move(db), this);
  mPages.insertOrAssign(Pages::clientNotes, page);

  const int index = mUi->stackedWidget->addWidget(page);
  mPagesIndex.insertOrAssign(Pages::clientNotes, index);
  setPageCustomWidget(Pages::clientNotes, mBtnBackToClients);
}

void MainWindow::addCallsPage(pcm::video::DeviceManager *deviceManager,
                              pcm::tokenclient::TokenBackendClient *tokenClient,
                              std::function<QString()> bearerCredentialProvider) {
  const auto page = new CallsPage(/*specialistMode=*/true, deviceManager, tokenClient, this);
  page->setBearerCredentialProvider(std::move(bearerCredentialProvider));
  mPages.insertOrAssign(Pages::calls, page);
  connect(page, &CallsPage::fullscreenChanged, this, &MainWindow::setCallFullscreen);

  const int index = mUi->stackedWidget->addWidget(page);
  mPagesIndex.insertOrAssign(Pages::calls, index);
}

void MainWindow::setCallFullscreen(bool fullscreen) {
  if (fullscreen == mCallFullscreen) {
    return;
  }
  mCallFullscreen = fullscreen;
  if (fullscreen) {
    mChromeVisibility.clear();
    const auto hide = [this](QWidget *widget) {
      if (!widget) return;
      mChromeVisibility.insert(widget, widget->isVisibleTo(this));
      widget->hide();
    };
    hide(mTitleWidget);
    hide(mUi->pageCustomWidgetHost);
    hide(statusBar());
    for (int i = 0; i < mUi->verticalLayout->count(); ++i) {
      hide(mUi->verticalLayout->itemAt(i)->widget());
    }
    mGridMargins = mUi->gridLayout->contentsMargins();
    mUi->gridLayout->setContentsMargins(0, 0, 0, 0);
  } else {
    for (auto it = mChromeVisibility.cbegin(); it != mChromeVisibility.cend(); ++it) {
      it.key()->setVisible(it.value());
    }
    mChromeVisibility.clear();
    mUi->gridLayout->setContentsMargins(mGridMargins);
  }
}

void MainWindow::setDatabase(std::shared_ptr<pcm::database::Database> db) {
  mDb = std::move(db);
}


void MainWindow::connectSignals() {
  const auto clientInfoPage =
      dynamic_cast<ClientInfo *>(mPages[Pages::clientInfo]);
  const auto clientCardPage =
      dynamic_cast<QClientInfoCardPage *>(mPages[Pages::clientCard]);
  const auto clientNotesPage =
      dynamic_cast<ClientNotesPage *>(mPages[Pages::clientNotes]);
  const auto eventInfoPage =
      dynamic_cast<QEventInfoPage *>(mPages[Pages::eventInfo]);

  // Connect navigation buttons to switch pages
  connect(mBtnCalendar, &QPushButton::clicked,
          [this]() { showPage(Pages::eventInfo, mBtnCalendar); });

  connect(mBtnClients, &QPushButton::clicked,
          [this]() { showPage(Pages::clientInfo, mBtnClients); });

  connect(mBtnAnalytics, &QPushButton::clicked,
          [this]() { showPage(Pages::analytics, mBtnAnalytics); });

  connect(mBtnProfile, &QPushButton::clicked,
          [this]() { showPage(Pages::clientCard, mBtnProfile); });
  connect(mBtnNotes, &QPushButton::clicked,
          [this]() { showPage(Pages::clientNotes, mBtnNotes); });
  connect(mBtnCalls, &QPushButton::clicked,
          [this]() { showPage(Pages::calls, mBtnCalls); });

  // When a client is selected in the list, show its info in the client card page
  connect(clientInfoPage, &ClientInfo::displayButtonClicked, clientCardPage,
          &QClientInfoCardPage::setClientInfo);

  // Switch to the client card page after selecting a client
  connect(clientInfoPage, &ClientInfo::displayButtonClicked,
          [this]() {
            setClientNavigationVisible(Pages::clientCard, true);
            showPage(Pages::clientCard, mBtnProfile);
          });

  connect(clientInfoPage, &ClientInfo::notesButtonClicked, clientNotesPage,
          &ClientNotesPage::setClientInfo);
  connect(clientInfoPage, &ClientInfo::notesButtonClicked, [this]() {
    setClientNavigationVisible(Pages::clientNotes, true);
    showPage(Pages::clientNotes, mBtnNotes);
  });

  connect(clientNotesPage, &ClientNotesPage::openClientCardRequested,
          clientCardPage, &QClientInfoCardPage::setClientInfo);
  connect(clientNotesPage, &ClientNotesPage::openClientCardRequested,
          [this]() {
            setClientNavigationVisible(Pages::clientCard, true);
            showPage(Pages::clientCard, mBtnProfile);
          });

  connect(clientNotesPage, &ClientNotesPage::openEventRequested,
          [this, eventInfoPage](const int64_t eventId, const qint64 dayMs) {
            eventInfoPage->openEventOnDay(eventId, dayMs);
            showPage(Pages::eventInfo, mBtnCalendar);
          });

  connect(clientInfoPage, &ClientInfo::removeButtonClicked, this,
          [this](const int64_t clientId) { emit provideRemoveClient(clientId); });

  connect(mClientSearchInput, &QLineEdit::textChanged, clientInfoPage,
          &ClientInfo::setSearchQuery);
  connect(mShowInactiveClientsSwitch, &QAbstractButton::toggled, clientInfoPage,
          &ClientInfo::setShowInactiveClients);

  connect(mAddClientButton, &QPushButton::clicked, this, [this, clientCardPage]() {
    clientCardPage->setClientInfo(std::nullopt);
    clientCardPage->enterInEditMode();
    setClientNavigationVisible(Pages::clientCard, true);
    showPage(Pages::clientCard, mBtnProfile);
  });

  // Forward the save client signal from the client card to the main window
  connect(clientCardPage, &QClientInfoCardPage::provideSaveClient,
          [&](const auto &client) { emit provideSaveClient(client); });

  // Forward the client-event pair save signal from event info to main window
  connect(eventInfoPage, &QEventInfoPage::provideClientEventPairSave,
          [this](const int64_t clientId, const int64_t eventId) {
            emit provideClientEventPairSave(clientId, eventId);
          });

  showPage(Pages::eventInfo, mBtnCalendar);
}

QWidget *MainWindow::getPage(const Pages page) { return mPages[page]; }

void MainWindow::setPageCustomWidget(const Pages page, QWidget *widget) {
  if (widget != nullptr) {
    widget->setParent(mUi->pageCustomWidgetHost);
    widget->hide();
  }

  mPageCustomWidgets.insertOrAssign(page, widget);

  if (mCurrentPage == page) {
    applyPageCustomWidget(page);
  }
}

void MainWindow::preselectLiveKitMeeting(const QString &meetingRef) {
  showPage(Pages::calls, mBtnCalls);
  if (auto *callsPage = dynamic_cast<CallsPage *>(mPages.value(Pages::calls, nullptr))) {
    callsPage->preselectOwnMeeting(meetingRef);
  }
}

void MainWindow::initDefaultStyle() const {
  checkButton(mBtnCalendar);
}

void MainWindow::checkButton(QPushButton *btn) const {
  mBtnCalendar->setChecked(false);
  mBtnClients->setChecked(false);
  mBtnAnalytics->setChecked(false);
  mBtnProfile->setChecked(false);
  mBtnNotes->setChecked(false);
  mBtnCalls->setChecked(false);
  btn->setChecked(true);
}

void MainWindow::showPage(const Pages page, QPushButton *btn) {
  if (!mPagesIndex.contains(page)) {
    return;
  }

  mUi->stackedWidget->setCurrentIndex(mPagesIndex[page]);
  mCurrentPage = page;
  applyPageCustomWidget(page);
  checkButton(btn);
  refreshPageAppearance();

  if (pcm::app_settings::showStatusBarMessages()) {
    statusBar()->showMessage(tr("Opened %1").arg(pageTitle(page)), 2000);
  } else {
    statusBar()->clearMessage();
  }
}

void MainWindow::applyPageCustomWidget(const Pages page) {
  while (const auto item = mPageCustomWidgetLayout->takeAt(0)) {
    if (const auto widget = item->widget()) {
      widget->hide();
    }
    delete item;
  }

  if (const auto widget = mPageCustomWidgets.value(page, nullptr)) {
    widget->setParent(mUi->pageCustomWidgetHost);
    widget->show();
    mPageCustomWidgetLayout->addWidget(widget);
  }
}

void MainWindow::setClientNavigationVisible(const Pages page,
                                            const bool visible) const {
  switch (page) {
    case Pages::clientCard:
      if (mBtnProfile) {
        mBtnProfile->setVisible(visible);
      }
      break;
    case Pages::clientNotes:
      if (mBtnNotes) {
        mBtnNotes->setVisible(visible);
      }
      break;
    default:
      break;
  }
}

void MainWindow::setupUtilityButtons() {
  const auto makeUtilityButton = [this](const QIcon &icon, const QString &text) {
    auto *button = new QPushButton(icon, text, this);
    button->setFlat(true);
    button->setCheckable(false);
    button->setCursor(Qt::PointingHandCursor);
    button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    button->setIconSize(QSize(16, 16));
    // The colour is taken from the live palette (no hard-coded value), so it
    // follows the light and dark Qlementine themes; a style sheet without any
    // colour would leave the text unreadable.
    button->setStyleSheet(
        "QPushButton {"
        " color: palette(window-text);"
        " background: transparent;"
        " border: none;"
        " padding: 6px 10px;"
        " text-align: left;"
        "}");
    return button;
  };

  mBtnSettings =
      makeUtilityButton(QIcon(":/icons/users-gear-solid-full.svg"), tr("Settings"));
  mBtnAbout = makeUtilityButton(QIcon(":/icons/brain-solid-full.svg"), tr("About"));

  mUi->verticalLayout->addWidget(mBtnSettings);
  mUi->verticalLayout->addWidget(mBtnAbout);

  connect(mBtnSettings, &QPushButton::clicked, this, &MainWindow::openSettingsDialog);
  connect(mBtnAbout, &QPushButton::clicked, this, &MainWindow::openAboutDialog);

  if (mUi->stackedWidget != nullptr) {
    mUi->stackedWidget->setFocusPolicy(Qt::StrongFocus);
    mUi->stackedWidget->setFocus();
  }
}

void MainWindow::openSettingsDialog() {
  SettingsDialog dialog(mDb, this);
  dialog.setRoleSwitcher(mRoleSwitcher);
  dialog.exec();
  refreshPageAppearance();
  emit settingsSaved();
}

void MainWindow::openAboutDialog() {
  oclero::qlementine::AboutDialog dialog(this);
  dialog.setWindowTitle(tr("About"));
  dialog.setIcon(QIcon(":/icons/brain-solid-full.svg"));
  dialog.setApplicationName(QApplication::applicationDisplayName());
  dialog.setApplicationVersion(QApplication::applicationVersion());
  dialog.setDescription(
      tr("Desktop workspace for calendar scheduling, client management, and session tracking."));
  dialog.setLicense(tr("Built with Qt, DuckDB, and Qlementine."));
  dialog.setCopyright(QStringLiteral("2026 Sessio"));
  dialog.exec();
}

QString MainWindow::pageTitle(const Pages page) const {
  switch (page) {
    case Pages::clientInfo:
      return tr("Clients");
    case Pages::eventInfo:
      return tr("Calendar");
    case Pages::analytics:
      return tr("Analytics");
    case Pages::clientCard:
      return tr("Details");
    case Pages::clientNotes:
      return tr("Notes");
    case Pages::calls:
      return tr("Calls");
  }

  return tr("Page");
}

void MainWindow::refreshPageAppearance() {
  if (const auto eventPage =
          dynamic_cast<QEventInfoPage *>(mPages.value(Pages::eventInfo, nullptr))) {
    eventPage->refreshAppearance();
  }

  if (const auto analyticsPage =
          dynamic_cast<AnalyticsPage *>(mPages.value(Pages::analytics, nullptr))) {
    analyticsPage->refresh();
  }

  if (const auto notesPage =
          dynamic_cast<ClientNotesPage *>(mPages.value(Pages::clientNotes, nullptr))) {
    notesPage->refresh();
  }
}


MainWindow::~MainWindow() = default;
