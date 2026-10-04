#include "call_entry_widget.h"
#include "accent_color.h"
#include "app_settings.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
// Group card: a softly tinted, bordered panel (alpha washes, so it works on the app's themes).
constexpr auto kGroupStyle =
    "QFrame#joinGroup, QFrame#callsGroup, QFrame#nameCard {"
    " background-color: rgba(255, 255, 255, 0.04);"
    " border: 1px solid rgba(255, 255, 255, 0.10);"
    " border-radius: 12px; }"
    "QFrame#joinGroup QLabel, QFrame#callsGroup QLabel, QFrame#nameCard QLabel {"
    " border: none; background: transparent; }";

QLabel *makeCaption(const QString &text, QWidget *parent) {
  auto *label = new QLabel(text, parent);
  label->setStyleSheet("color: rgba(255, 255, 255, 0.60);");
  return label;
}

QLabel *makeGroupTitle(const QString &text, QWidget *parent) {
  auto *label = new QLabel(text, parent);
  QFont font = label->font();
  font.setBold(true);
  label->setFont(font);
  return label;
}
} // namespace

CallEntryWidget::CallEntryWidget(const bool showOwnMeetings, QWidget *parent)
    : QWidget(parent), mShowOwnMeetings(showOwnMeetings) {
  setStyleSheet(kGroupStyle);
  auto *layout = new QVBoxLayout(this);
  layout->setSpacing(12);

  // Top: who you are in calls. Applies to both groups below.
  auto *nameCard = new QFrame(this);
  nameCard->setObjectName("nameCard");
  auto *nameLayout = new QHBoxLayout(nameCard);
  nameLayout->setContentsMargins(16, 12, 16, 12);
  nameLayout->setSpacing(12);
  mAvatarLabel = new QLabel(nameCard);
  mAvatarLabel->setObjectName("callAvatarLabel");
  mAvatarLabel->setFixedSize(36, 36);
  mAvatarLabel->setAlignment(Qt::AlignCenter);
  mAvatarLabel->setStyleSheet(
      QStringLiteral("background-color: %1; border-radius: 18px; font-weight: bold;")
          .arg(pcm::widgets::cssRgba(pcm::widgets::accentColor(), 0.35)));
  nameLayout->addWidget(mAvatarLabel);
  auto *nameColumn = new QVBoxLayout();
  nameColumn->setSpacing(2);
  nameColumn->addWidget(makeCaption(tr("Name in calls"), nameCard));
  mDisplayNameEdit = new QLineEdit(nameCard);
  mDisplayNameEdit->setObjectName("callDisplayNameEdit");
  mDisplayNameEdit->setMaxLength(64);
  mDisplayNameEdit->setPlaceholderText(tr("Your name"));
  mDisplayNameEdit->setText(pcm::app_settings::callDisplayName());
  mDisplayNameEdit->setAccessibleName(tr("Name in calls"));
  nameColumn->addWidget(mDisplayNameEdit);
  nameLayout->addLayout(nameColumn, 1);
  nameLayout->addWidget(makeCaption(tr("Shown to other participants"), nameCard), 0, Qt::AlignVCenter);
  layout->addWidget(nameCard);
  const auto refreshAvatar = [this]() {
    const QString name = mDisplayNameEdit->text().trimmed();
    mAvatarLabel->setText(name.isEmpty() ? QStringLiteral("?") : name.left(1).toUpper());
  };
  connect(mDisplayNameEdit, &QLineEdit::textChanged, this, refreshAvatar);
  refreshAvatar();
  connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *focused) {
    // Re-read a changed settings default when returning from its dialog,
    // while keeping an explicitly edited name for the pending call.
    if (focused && focused->window() == window() && !mDisplayNameEdit->isModified())
      mDisplayNameEdit->setText(pcm::app_settings::callDisplayName());
  });

  auto *groups = new QHBoxLayout();
  groups->setSpacing(12);
  layout->addLayout(groups);

  // Left: join someone else's meeting by invitation.
  auto *joinGroup = new QFrame(this);
  joinGroup->setObjectName("joinGroup");
  auto *joinLayout = new QVBoxLayout(joinGroup);
  joinLayout->setContentsMargins(16, 14, 16, 14);
  joinLayout->setSpacing(6);
  joinLayout->addWidget(makeGroupTitle(tr("Join a meeting"), joinGroup));
  joinLayout->addSpacing(4);
  joinLayout->addWidget(makeCaption(tr("Invitation code or link"), joinGroup));
  mCodeEdit = new QLineEdit(joinGroup);
  mCodeEdit->setObjectName("joinCodeEdit");
  mCodeEdit->setPlaceholderText(tr("Invitation code or link"));
  joinLayout->addWidget(mCodeEdit);
  joinLayout->addWidget(makeCaption(tr("Passcode"), joinGroup));
  mPasscodeEdit = new QLineEdit(joinGroup);
  mPasscodeEdit->setObjectName("joinPasscodeEdit");
  mPasscodeEdit->setPlaceholderText(tr("Passcode (6 digits)"));
  joinLayout->addWidget(mPasscodeEdit);
  joinLayout->addSpacing(4);
  auto *connectButton = new QPushButton(tr("Connect"), joinGroup);
  connectButton->setObjectName("joinByCodeButton");
  joinLayout->addWidget(connectButton);

  // Same error-text colour as AppLockDialog's error label.
  mErrorLabel = new QLabel(joinGroup);
  mErrorLabel->setObjectName("joinErrorLabel");
  mErrorLabel->setStyleSheet("color: #ef7777;");
  mErrorLabel->setWordWrap(true);
  mErrorLabel->setVisible(false);
  joinLayout->addWidget(mErrorLabel);
  joinLayout->addStretch();
  groups->addWidget(joinGroup, 4, Qt::AlignTop);

  // Right: the practitioner's own calls for today (specialist mode only).
  if (mShowOwnMeetings) {
    auto *callsGroup = new QFrame(this);
    callsGroup->setObjectName("callsGroup");
    auto *callsLayout = new QVBoxLayout(callsGroup);
    callsLayout->setContentsMargins(16, 14, 16, 14);
    callsLayout->setSpacing(6);
    callsLayout->addWidget(makeGroupTitle(tr("Your calls today"), callsGroup));
    mOwnMeetingsList = new QWidget(callsGroup);
    mOwnMeetingsList->setObjectName("ownMeetingsList");
    mOwnMeetingsLayout = new QVBoxLayout(mOwnMeetingsList);
    mOwnMeetingsLayout->setContentsMargins(0, 0, 0, 0);
    mOwnMeetingsLayout->setSpacing(0);
    callsLayout->addWidget(mOwnMeetingsList);
    mNoCallsLabel = makeCaption(tr("No calls today"), callsGroup);
    mNoCallsLabel->setObjectName("noCallsLabel");
    callsLayout->addWidget(mNoCallsLabel);
    callsLayout->addStretch();
    groups->addWidget(callsGroup, 5, Qt::AlignTop);
  }
  layout->addStretch();

  connect(connectButton, &QPushButton::clicked, this, [this]() {
    emit joinByCodeRequested(mCodeEdit->text(), mPasscodeEdit->text());
  });
}

void CallEntryWidget::setUpcomingMeetings(const QList<UpcomingMeeting> &meetings) {
  if (!mShowOwnMeetings) {
    return;
  }
  QLayoutItem *item;
  while ((item = mOwnMeetingsLayout->takeAt(0)) != nullptr) {
    delete item->widget();
    delete item;
  }
  mNoCallsLabel->setVisible(meetings.isEmpty());
  for (const auto &meeting : meetings) {
    auto *row = new QWidget(mOwnMeetingsList);
    row->setStyleSheet(
        "QWidget#callRow { background: transparent; border-top: 1px solid rgba(255, 255, 255, 0.08); }");
    row->setObjectName("callRow");
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 8, 0, 8);
    rowLayout->setSpacing(12);
    auto *timeLabel = makeCaption(meeting.startTime.toString(QStringLiteral("HH:mm")), row);
    timeLabel->setMinimumWidth(44);
    rowLayout->addWidget(timeLabel);
    auto *titleLabel = new QLabel(meeting.title, row);
    titleLabel->setWordWrap(true);
    rowLayout->addWidget(titleLabel, 1);
    auto *joinButton = new QPushButton(tr("Join"), row);
    joinButton->setObjectName("joinOwnMeetingButton_" + meeting.meetingRef);
    joinButton->setEnabled(meeting.joinEnabled);
    connect(joinButton, &QPushButton::clicked, this,
            [this, ref = meeting.meetingRef]() { emit ownMeetingJoinRequested(ref); });
    rowLayout->addWidget(joinButton);
    mOwnMeetingsLayout->addWidget(row);
  }
}

void CallEntryWidget::preselectOwnMeeting(const QString &meetingRef) {
  if (!mShowOwnMeetings) {
    return;
  }
  if (auto *button = findChild<QPushButton *>("joinOwnMeetingButton_" + meetingRef)) {
    button->setFocus();
  }
}

void CallEntryWidget::prefillJoinCode(const QString &code, const QString &passcode) {
  mCodeEdit->setText(code);
  mPasscodeEdit->setText(passcode);
}

void CallEntryWidget::showError(const QString &message) {
  mErrorLabel->setText(message);
  mErrorLabel->setVisible(true);
}

void CallEntryWidget::clearError() {
  mErrorLabel->clear();
  mErrorLabel->setVisible(false);
}

QString CallEntryWidget::displayName() const {
  return mDisplayNameEdit->text().trimmed();
}

void CallEntryWidget::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  mDisplayNameEdit->setText(pcm::app_settings::callDisplayName());
}
