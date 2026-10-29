#pragma once

#include <QDateTime>
#include <QList>
#include <QWidget>
#include <QVariantList>

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
  Q_PROPERTY(QVariantList meetings READ meetings NOTIFY workspaceChanged)
  Q_PROPERTY(QVariantList materials READ materials NOTIFY workspaceChanged)
  Q_PROPERTY(QString error READ error NOTIFY workspaceChanged)
  Q_PROPERTY(QString name READ displayName WRITE setDisplayName NOTIFY workspaceChanged)
  Q_PROPERTY(QString code READ code NOTIFY workspaceChanged)
  Q_PROPERTY(QString passcode READ passcode NOTIFY workspaceChanged)

public:
  explicit CallEntryWidget(bool showOwnMeetings, QWidget *parent = nullptr);
  ~CallEntryWidget() override;

  void setUpcomingMeetings(const QList<UpcomingMeeting> &meetings);
  void preselectOwnMeeting(const QString &meetingRef);
  void prefillJoinCode(const QString &code, const QString &passcode);
  // Shows a join failure (token request or call failure) below the form;
  // clearError() hides it again, e.g. before each new join attempt.
  void showError(const QString &message);
  void clearError();
  QString displayName() const;
  void enableQuickWorkspace();
  QWidget *headerControls();
  void setMaterialHistory(const QVariantList &history);
  QVariantList meetings() const { return mMeetings; }
  QVariantList materials() const { return mMaterials; }
  QString error() const;
  QString code() const;
  QString passcode() const;
  void setDisplayName(const QString &name);
  Q_INVOKABLE void joinOwn(const QString &ref);
  Q_INVOKABLE void joinInvitation(const QString &code, const QString &passcode);
  Q_INVOKABLE void openMaterial(const QString &id);
  Q_INVOKABLE void openTranscript(const QString &id);

protected:
  void showEvent(QShowEvent *event) override;

signals:
  void workspaceChanged();
  void meetingPreselected(QString ref);
  void invitationPrefilled();
  void materialRequested(qint64 id);
  void transcriptRequested(qint64 id);
  void ownMeetingJoinRequested(QString meetingRef);
  void joinByCodeRequested(QString code, QString passcode);
  // The entry screen became visible: its meeting list may be stale.
  void shown();

private:
  bool mShowOwnMeetings;
  QVariantList mMeetings, mMaterials;
  class QQuickWidget *mQuick = nullptr;
  QWidget *mExternalHeader = nullptr;
  QWidget *mOwnMeetingsList{nullptr};
  QVBoxLayout *mOwnMeetingsLayout{nullptr};
  QLineEdit *mCodeEdit{nullptr};
  QLineEdit *mPasscodeEdit{nullptr};
  QLineEdit *mDisplayNameEdit{nullptr};
  QLabel *mErrorLabel{nullptr};
  QLabel *mAvatarLabel{nullptr};
  QLabel *mNoCallsLabel{nullptr};
};
