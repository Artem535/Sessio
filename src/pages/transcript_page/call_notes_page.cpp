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
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

CallNotesPage::CallNotesPage(std::shared_ptr<pcm::database::Database> db, CallNotesController *controller,
                             QWidget *parent)
    : QWidget(parent), mDb(std::move(db)), mController(controller) {
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(8);
  mSwitch = new QWidget(this);
  mSwitch->setObjectName("notesModeSwitch");
  auto *switchLayout = new QHBoxLayout(mSwitch);
  switchLayout->setContentsMargins(12, 8, 12, 0);
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
  switchLayout->addStretch();
  current->setChecked(true);
  outer->addWidget(mSwitch);
  mSwitch->hide();

  auto *scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *content = new QWidget(scroll);
  auto *contentLayout = new QVBoxLayout(content);
  contentLayout->setContentsMargins(12, 8, 12, 12);
  contentLayout->setSpacing(10);

  mCurrent = new QWidget(content);
  auto *column = mColumn = new QVBoxLayout(mCurrent);
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(10);
  mPrevious = new PreviousMeetingWidget(mCurrent);
  column->addWidget(mPrevious);
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
  column->addWidget(notesCard);
  mSummary = new AiSummaryWidget(controller, mCurrent);
  column->addWidget(mSummary);
  mPoints = new KeyPointsWidget(controller, mCurrent);
  column->addWidget(mPoints);
  mAgreements = new AgreementsWidget(mCurrent);
  column->addWidget(mAgreements);
  column->addStretch();

  mHistory = new MeetingHistoryList(content);
  contentLayout->addWidget(mCurrent);
  contentLayout->addWidget(mHistory);
  mHistory->hide();
  scroll->setWidget(content);
  outer->addWidget(scroll, 1);

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
}

ClientNotesPage *CallNotesPage::editor() const { return mEditor; }

void CallNotesPage::setClient(const std::optional<DuckClient> &client) {
  mClientId = client ? client->id : 0;
  mEditor->setClientInfo(client);
  reloadHistory();
}

void CallNotesPage::reloadHistory() {
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
  mPrevious->setMeeting(meetings.empty() ? std::nullopt : std::optional<MeetingRecord>(meetings.front()));
  mHistory->setMeetings(meetings, loadNotesWithoutMeeting(*mDb, mClientId));
}

void CallNotesPage::setMeeting(const QString &details, const QString &status) {
  mMeetingDetails->setText(details);
  mMeeting->setStatus(status);
}

void CallNotesPage::setAgreements(const QJsonArray &records) {
  mAgreements->setRecords(records);
  updateVisibility();
}

void CallNotesPage::setControllerFactory(MeetingControllerFactory factory) {
  mHistory->setControllerFactory(std::move(factory));
}

void CallNotesPage::setPrompterPanel(PrompterPanel *panel) {
  if (mPrompter || !panel) return;
  mPrompter = new NotesWidgetFrame(tr("Prompter"), mCurrent);
  mPrompter->setObjectName("prompterNotesCard");
  panel->setEmbedded(true);
  mPrompter->body()->addWidget(panel);
  mColumn->insertWidget(mColumn->indexOf(mNotesCard) + 1, mPrompter);
  connect(panel, &PrompterPanel::pendingCountChanged, mPrompter, [frame = mPrompter, this](int count) {
    frame->setStatus(count > 0 ? tr("%n new", nullptr, count) : QString());
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
