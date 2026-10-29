#include "call_notes_page.h"
#include "agreements_widget.h"
#include "ai_summary_widget.h"
#include "call_notes_controller.h"
#include "client_notes_page.h"
#include "key_points_widget.h"
#include "meeting_history.h"
#include "meeting_history_widgets.h"
#include "notes_widget_frame.h"
#include "prompter_panel.h"
#include "notes_workspace_bridge.h"
#include <QButtonGroup>
#include <QDateTime>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QQuickWidget>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QMessageBox>
#include <QScreen>

static void initializeNotesResources() { Q_INIT_RESOURCE(notes_workspace_qml); }
static void initializeNotesGraphics() {
  static const bool initialized = [] {
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
      QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    return true;
  }();
  Q_UNUSED(initialized);
}

CallNotesPage::CallNotesPage(std::shared_ptr<pcm::database::Database> db, CallNotesController *controller,
                             QWidget *parent)
    : QWidget(parent), mDb(std::move(db)), mController(controller) {
  auto *legacy = new QWidget(this);
  legacy->setObjectName("legacyNotesWorkspace");
  auto *outer = new QVBoxLayout(legacy);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(8);
  mSwitch = new QWidget(this);
  mSwitch->setObjectName("notesModeSwitch");
  auto *switchLayout = new QVBoxLayout(mSwitch);
  switchLayout->setContentsMargins(12, 12, 12, 12);
  switchLayout->setSpacing(10);
  mSwitch->setMinimumWidth(240);
  mSwitch->setMaximumWidth(300);
  auto *current = new QPushButton(tr("This meeting"), mSwitch);
  current->setObjectName("notesModeCurrent");
  auto *history = new QPushButton(tr("History"), mSwitch);
  history->setObjectName("notesModeHistory");
  auto *group = new QButtonGroup(this);
  group->setExclusive(true);
  for (auto *button : {current, history}) {
    button->setCheckable(true);
    group->addButton(button);
    switchLayout->addWidget(button);
  }
  auto *historyList = new QListWidget(mSwitch);
  historyList->setObjectName("callMeetingHistory");
  historyList->setFrameShape(QFrame::NoFrame);
  historyList->setWordWrap(true);
  historyList->setSpacing(6);
  historyList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  switchLayout->addWidget(historyList, 1);
  connect(historyList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
    showHistory(item->data(Qt::UserRole).toLongLong());
  });
  current->setChecked(true);
  mSwitch->hide();

  auto *scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *content = new QWidget(scroll);
  auto *contentLayout = new QVBoxLayout(content);
  contentLayout->setContentsMargins(12, 8, 12, 12);
  contentLayout->setSpacing(10);
  contentLayout->setAlignment(Qt::AlignTop);

  mCurrent = new QWidget(content);
  mCurrent->setObjectName("meetingDocument");
  mCurrent->setAutoFillBackground(true);
  mCurrent->setBackgroundRole(QPalette::Base);
  mCurrent->setMaximumWidth(1040);
  mCurrent->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
  auto *column = mColumn = new QVBoxLayout(mCurrent);
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(0);
  mPrevious = new PreviousMeetingWidget(mCurrent);
  mMeeting = new NotesWidgetFrame(tr("Meeting"), mCurrent);
  mMeeting->setObjectName("meetingInfoCard");
  mMeetingDetails = new QLabel(mMeeting);
  mMeetingDetails->setObjectName("meetingInfoDetails");
  mMeetingDetails->setWordWrap(true);
  mMeeting->body()->addWidget(mMeetingDetails);
  column->addWidget(mMeeting);
  auto *notesCard = mNotesCard = new NotesWidgetFrame(tr("My notes"), mCurrent);
  notesCard->setObjectName("myNotesCard");
  mEditor = new ClientNotesPage(mDb, notesCard);
  mEditor->setEmbedded(true);
  notesCard->body()->addWidget(mEditor);
  // The editor supplies its own heading and toolbar inside the document.
  if (auto *title = notesCard->findChild<QLabel *>("notesWidgetTitle")) title->hide();
  column->addWidget(notesCard);
  mSummary = new AiSummaryWidget(controller, mCurrent);
  mSummary->setAutoFillBackground(true);
  mSummary->setBackgroundRole(QPalette::AlternateBase);
  column->addWidget(mSummary);
  mPoints = new KeyPointsWidget(controller, mCurrent);
  mAgreements = new AgreementsWidget(mCurrent);
  column->addWidget(mAgreements);
  column->addWidget(mPoints);
  column->addWidget(mPrevious);
  column->addStretch();

  mHistory = new MeetingHistoryList(content);
  auto *documentRow = new QHBoxLayout;
  documentRow->setContentsMargins(0, 0, 0, 0);
  documentRow->addWidget(mCurrent, 100);
  documentRow->addStretch(1);
  contentLayout->addLayout(documentRow);
  contentLayout->addWidget(mHistory);
  mHistory->hide();
  scroll->setWidget(content);
  auto *workspace = new QHBoxLayout;
  workspace->setContentsMargins(0, 0, 0, 0);
  workspace->setSpacing(12);
  workspace->addWidget(mSwitch);
  workspace->addWidget(scroll, 1);
  outer->addLayout(workspace, 1);

  connect(current, &QPushButton::clicked, this, [this] { setHistoryMode(false); });
  connect(history, &QPushButton::clicked, this, [this] { setHistoryMode(true); });
  connect(mPrevious, &PreviousMeetingWidget::openRequested, this, &CallNotesPage::showHistory);
  connect(mSummary, &AiSummaryWidget::contentChanged, this, &CallNotesPage::updateVisibility);
  connect(mPoints, &KeyPointsWidget::contentChanged, this, &CallNotesPage::updateVisibility);
  connect(mSummary, &AiSummaryWidget::sourceRequested, this, &CallNotesPage::sourceRequested);
  connect(mPoints, &KeyPointsWidget::sourceRequested, this, &CallNotesPage::sourceRequested);
  connect(mHistory, &MeetingHistoryList::sourceRequested, this, &CallNotesPage::sourceRequested);
  connect(mSummary, &AiSummaryWidget::retryRequested, this, &CallNotesPage::retryRequested);
  updateVisibility();

  // Keep the complete Widgets editor available while the QML workspace is
  // introduced. Application's storage facade and its existing bindings stay alive.
  auto *backToDocument = new QPushButton(tr("Back to document"), legacy);
  outer->insertWidget(0, backToDocument, 0, Qt::AlignLeft);
  auto *rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->addWidget(legacy);
  legacy->hide();
  initializeNotesResources();
  mWorkspace = new NotesWorkspaceBridge(mDb, controller, this, this);
  // The specialist window also hosts QOpenGLWidget video surfaces. Keep the
  // two renderers on the same graphics API; software remains available for tests.
  initializeNotesGraphics();
  mQuick = new QQuickWidget(this);
  mQuick->setObjectName("notesQuickWorkspace");
  mQuick->setResizeMode(QQuickWidget::SizeRootObjectToView);
  mQuick->rootContext()->setContextProperty("workspaceBridge", mWorkspace);
  mQuick->setSource(QUrl("qrc:/sessio/notes/NotesWorkspace.qml"));
  rootLayout->addWidget(mQuick, 1);
  if (auto *root = mQuick->rootObject()) {
    root->setProperty("dark", palette().color(QPalette::Window).lightness() < 128);
    connect(root, SIGNAL(advancedRequested()), this, SLOT(showAdvanced()));
  }
  connect(backToDocument, &QPushButton::clicked, this, [legacy, this] { legacy->hide(); mQuick->show(); });
  connect(mWorkspace, &NotesWorkspaceBridge::sourceRequested, this, &CallNotesPage::sourceRequested);
  connect(mWorkspace, &NotesWorkspaceBridge::retryRequested, this, &CallNotesPage::retryRequested);
}

void CallNotesPage::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::PaletteChange && mQuick && mQuick->rootObject())
    mQuick->rootObject()->setProperty("dark", palette().color(QPalette::Window).lightness() < 128);
}

CallNotesPage::~CallNotesPage() {
  // The adapter owns the QTextDocuments used by TextArea. Destroy the view
  // while its context object and documents are still alive.
  delete mQuick;
  mQuick = nullptr;
}

void CallNotesPage::setEditorMode() {
  mWorkspace->beginEditorSession();
  mQuick->setSource(QUrl("qrc:/sessio/notes/NotesEditor.qml"));
  if (auto *root = mQuick->rootObject()) {
    root->setProperty("dark", palette().color(QPalette::Window).lightness() < 128);
    connect(root, SIGNAL(closeRequested()), this, SIGNAL(editorCloseRequested()));
    connect(root, SIGNAL(saved()), this, SIGNAL(editorSaved()));
  }
}

bool CallNotesPage::editorDirty() const { return mWorkspace && mWorkspace->editorDirty(); }

NotesEditorDialog::NotesEditorDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Meeting notes"));
  setModal(true);
  const auto area = screen()->availableGeometry();
  resize(area.width() - 32, area.height() - 32);
  auto *body = new QVBoxLayout(this); body->setContentsMargins(0, 0, 0, 0);
}
void NotesEditorDialog::setPage(CallNotesPage *page) {
  mPage = page; page->setEditorMode(); layout()->addWidget(page);
  connect(page, &CallNotesPage::editorCloseRequested, this, &NotesEditorDialog::reject);
  connect(page, &CallNotesPage::editorSaved, this, &QDialog::accept);
}
void NotesEditorDialog::reject() {
  if (mPage && mPage->editorDirty()) {
    const bool discard = mConfirmDiscard ? mConfirmDiscard() :
        QMessageBox::question(this, tr("Unsaved changes"), tr("Close without saving your changes?"),
            QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Discard;
    if (!discard) return;
  }
  QDialog::reject();
}

void CallNotesPage::showAdvanced() {
  if (mWorkspace && !mWorkspace->state()["current"].toBool())
    showHistory(mWorkspace->state()["materialId"].toString().toLongLong());
  else setHistoryMode(false);
  findChild<QWidget *>("legacyNotesWorkspace")->show();
  mQuick->hide();
}

ClientNotesPage *CallNotesPage::editor() const { return mEditor; }

void CallNotesPage::setClient(const std::optional<DuckClient> &client) {
  mClientId = client ? client->id : 0;
  if (mWorkspace) mWorkspace->setClient(client);
  mEditor->setClientInfo(client);
  reloadHistory();
}

void CallNotesPage::reloadHistory() {
  auto *historyList = mSwitch->findChild<QListWidget *>("callMeetingHistory");
  historyList->clear();
  const bool hasClient = mClientId > 0;
  mSwitch->setVisible(hasClient);
  if (!hasClient) {
    mPrevious->setMeeting(std::nullopt);
    mHistory->setMeetings({}, {});
    setHistoryMode(false);
    return;
  }
  const qint64 exclude = mController ? mController->materialId() : 0;
  const auto meetings = loadMeetingHistory(*mDb, mClientId, exclude);
  for (const auto &meeting : meetings) {
    const auto date = QDateTime::fromMSecsSinceEpoch(meeting.startMs).toLocalTime().toString("dd MMM yyyy · HH:mm");
    auto *entry = new QListWidgetItem(date + "\n\n" + meetingPreview(meeting).simplified().left(100), historyList);
    entry->setData(Qt::UserRole, meeting.materialId);
    entry->setSizeHint(QSize(240, 100));
    entry->setToolTip(meetingPreview(meeting));
  }
  mPrevious->setMeeting(meetings.empty() ? std::nullopt : std::optional<MeetingRecord>(meetings.front()));
  mHistory->setMeetings(meetings, loadNotesWithoutMeeting(*mDb, mClientId));
}

void CallNotesPage::setMeeting(const QString &details, const QString &status) {
  if (mWorkspace) mWorkspace->setMeeting(details, status);
  mMeetingDetails->setText(details);
  mMeeting->setStatus(status);
}

void CallNotesPage::setCalendarOpener(std::function<std::optional<int64_t>(const EventTemplate &)> opener) {
  if (mWorkspace) mWorkspace->setCalendarOpener(std::move(opener));
}

void CallNotesPage::setEventOpener(std::function<void(int64_t)> opener) {
  if (mWorkspace) mWorkspace->setEventOpener(std::move(opener));
}

void CallNotesPage::setAgreements(const QJsonArray &records) {
  if (mWorkspace) mWorkspace->setAgreements(records);
  mAgreements->setRecords(records);
  updateVisibility();
}

void CallNotesPage::setControllerFactory(MeetingControllerFactory factory) {
  if (mWorkspace) mWorkspace->setControllerFactory(factory);
  mHistory->setControllerFactory(std::move(factory));
}

void CallNotesPage::setPrompterPanel(PrompterPanel *panel) {
  if (mPrompter || !panel) return;
  if (mWorkspace) mWorkspace->setPrompterPanel(panel);
  mPrompter = new NotesWidgetFrame(tr("Prompter"), mCurrent);
  mPrompter->setObjectName("prompterNotesCard");
  panel->setEmbedded(true);
  mPrompter->body()->addWidget(panel);
  mColumn->insertWidget(mColumn->indexOf(mPrevious), mPrompter);
  connect(panel, &PrompterPanel::pendingCountChanged, mPrompter, [frame = mPrompter, this](int count) {
    frame->setStatus(count > 0 ? tr("%n new", nullptr, count) : QString());
    frame->setVisible(count > 0 || !mController || !mController->ended());
  });
  panel->refresh();
}

void CallNotesPage::showHistory(qint64 materialId) {
  setHistoryMode(true);
  mHistory->expand(materialId);
}

bool CallNotesPage::historyMode() const { return !mHistory->isHidden(); }

void CallNotesPage::setHistoryMode(bool history) {
  mHistory->setVisible(history);
  mCurrent->setVisible(!history);
  if (auto *button = findChild<QPushButton *>(history ? "notesModeHistory" : "notesModeCurrent"))
    button->setChecked(true);
}

void CallNotesPage::updateVisibility() {
  mSummary->setVisible(mSummary->hasContent());
  mPoints->setVisible(mPoints->hasContent());
  mAgreements->setVisible(mAgreements->hasContent());
}
