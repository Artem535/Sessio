#include "call_entry_widget.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

CallEntryWidget::CallEntryWidget(const bool showOwnMeetings, QWidget *parent)
    : QWidget(parent), mShowOwnMeetings(showOwnMeetings) {
  auto *layout = new QVBoxLayout(this);

  if (mShowOwnMeetings) {
    layout->addWidget(new QLabel(tr("Your meetings today"), this));
    mOwnMeetingsList = new QWidget(this);
    mOwnMeetingsList->setObjectName("ownMeetingsList");
    mOwnMeetingsLayout = new QVBoxLayout(mOwnMeetingsList);
    layout->addWidget(mOwnMeetingsList);
  }

  layout->addWidget(new QLabel(tr("Join another meeting"), this));
  mCodeEdit = new QLineEdit(this);
  mCodeEdit->setObjectName("joinCodeEdit");
  mCodeEdit->setPlaceholderText(tr("Invitation code or link"));
  mPasscodeEdit = new QLineEdit(this);
  mPasscodeEdit->setObjectName("joinPasscodeEdit");
  mPasscodeEdit->setPlaceholderText(tr("Passcode (6 digits)"));
  auto *connectButton = new QPushButton(tr("Connect"), this);
  connectButton->setObjectName("joinByCodeButton");

  layout->addWidget(mCodeEdit);
  layout->addWidget(mPasscodeEdit);
  layout->addWidget(connectButton);

  // Same error-text colour as AppLockDialog's error label.
  mErrorLabel = new QLabel(this);
  mErrorLabel->setObjectName("joinErrorLabel");
  mErrorLabel->setStyleSheet("color: #ef7777;");
  mErrorLabel->setWordWrap(true);
  mErrorLabel->setVisible(false);
  layout->addWidget(mErrorLabel);
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
  for (const auto &meeting : meetings) {
    auto *row = new QWidget(mOwnMeetingsList);
    auto *rowLayout = new QVBoxLayout(row);
    rowLayout->addWidget(new QLabel(meeting.title, row));
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
