#pragma once

#include <QDateTime>
#include <QList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QVBoxLayout;

struct UpcomingMeeting {
  QString meetingRef;
  QString title;
  QDateTime startTime;
  bool joinEnabled = false;
  int64_t eventId = 0;
};

class CallEntryWidget final : public QWidget {
  Q_OBJECT

public:
  explicit CallEntryWidget(bool showOwnMeetings, QWidget *parent = nullptr);

  void setUpcomingMeetings(const QList<UpcomingMeeting> &meetings);
  void preselectOwnMeeting(const QString &meetingRef);
  void prefillJoinCode(const QString &code, const QString &passcode);
  // Shows a join failure (token request or call failure) below the form;
  // clearError() hides it again, e.g. before each new join attempt.
  void showError(const QString &message);
  void clearError();
  QString displayName() const;

protected:
  void showEvent(QShowEvent *event) override;

signals:
  void ownMeetingJoinRequested(QString meetingRef);
  void joinByCodeRequested(QString code, QString passcode);

private:
  bool mShowOwnMeetings;
  QWidget *mOwnMeetingsList{nullptr};
  QVBoxLayout *mOwnMeetingsLayout{nullptr};
  QLineEdit *mCodeEdit{nullptr};
  QLineEdit *mPasscodeEdit{nullptr};
  QLineEdit *mDisplayNameEdit{nullptr};
  QLabel *mErrorLabel{nullptr};
};
