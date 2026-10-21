#pragma once

#include "meeting_history_widgets.h"
#include <QJsonArray>
#include <QPointer>
#include <QWidget>
#include <memory>
#include <optional>

namespace pcm::database { class Database; }
struct DuckClient;
class CallNotesController;
class ClientNotesPage;
class AiSummaryWidget;
class KeyPointsWidget;
class AgreementsWidget;
class NotesWidgetFrame;
class QLabel;

// Notes page of a call: «This meeting | History» switch over one scroll area
// with a column of cards (last time, meeting, my notes, summary, key points,
// agreements) or the client's meeting history.
class CallNotesPage final : public QWidget {
  Q_OBJECT
public:
  CallNotesPage(std::shared_ptr<pcm::database::Database> db, CallNotesController *controller,
                QWidget *parent = nullptr);
  ClientNotesPage *editor() const;
  void setClient(const std::optional<DuckClient> &client);
  void setMeeting(const QString &details, const QString &status);
  void setAgreements(const QJsonArray &records);
  void setControllerFactory(MeetingControllerFactory factory);
  void showHistory(qint64 materialId);
  bool historyMode() const;
signals:
  void sourceRequested(qint64 phraseId);
  void retryRequested();

private:
  void reloadHistory();
  void setHistoryMode(bool history);
  void updateVisibility();

  std::shared_ptr<pcm::database::Database> mDb;
  QPointer<CallNotesController> mController;
  qint64 mClientId = 0;
  QWidget *mSwitch = nullptr;
  QWidget *mCurrent = nullptr;
  PreviousMeetingWidget *mPrevious = nullptr;
  NotesWidgetFrame *mMeeting = nullptr;
  QLabel *mMeetingDetails = nullptr;
  ClientNotesPage *mEditor = nullptr;
  AiSummaryWidget *mSummary = nullptr;
  KeyPointsWidget *mPoints = nullptr;
  AgreementsWidget *mAgreements = nullptr;
  MeetingHistoryList *mHistory = nullptr;
};
