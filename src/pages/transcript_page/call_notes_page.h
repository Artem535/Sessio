#pragma once

#include "../event_info_page/event_template.h"
#include "meeting_history_widgets.h"
#include <QJsonArray>
#include <QPointer>
#include <QWidget>
#include <QDialog>
#include <functional>
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
class QVBoxLayout;
class PrompterPanel;
class NotesWorkspaceBridge;
class QQuickWidget;

// Call document with history navigation on the left and a scrollable column
// of notes, summary, agreements and supporting meeting details.
class CallNotesPage final : public QWidget {
  Q_OBJECT
public:
  CallNotesPage(std::shared_ptr<pcm::database::Database> db, CallNotesController *controller,
                QWidget *parent = nullptr);
  ~CallNotesPage() override;
  ClientNotesPage *editor() const;
  void setClient(const std::optional<DuckClient> &client);
  void setMeeting(const QString &details, const QString &status);
  void setAgreements(const QJsonArray &records);
  void setControllerFactory(MeetingControllerFactory factory);
  // «Add to calendar» on an agreement opens the pre-filled new-event dialog; «In calendar» opens the event.
  void setCalendarOpener(std::function<std::optional<int64_t>(const EventTemplate &)> opener);
  void setEventOpener(std::function<void(int64_t)> opener);
  // Shows the live prompter as a card after «My notes»; the page owns it.
  void setPrompterPanel(PrompterPanel *panel);
  void showHistory(qint64 materialId);
  bool historyMode() const;
  void setEditorMode();
  bool editorDirty() const;
signals:
  void sourceRequested(qint64 phraseId);
  void retryRequested();
  void editorCloseRequested();
  void editorSaved();

protected:
  void changeEvent(QEvent *event) override;

private slots:
  void showAdvanced();

private:
  void reloadHistory();
  void setHistoryMode(bool history);
  void updateVisibility();

  std::shared_ptr<pcm::database::Database> mDb;
  QPointer<CallNotesController> mController;
  qint64 mClientId = 0;
  QWidget *mSwitch = nullptr;
  QWidget *mCurrent = nullptr;
  QVBoxLayout *mColumn = nullptr;
  NotesWidgetFrame *mNotesCard = nullptr;
  NotesWidgetFrame *mPrompter = nullptr;
  PreviousMeetingWidget *mPrevious = nullptr;
  NotesWidgetFrame *mMeeting = nullptr;
  QLabel *mMeetingDetails = nullptr;
  ClientNotesPage *mEditor = nullptr;
  AiSummaryWidget *mSummary = nullptr;
  KeyPointsWidget *mPoints = nullptr;
  AgreementsWidget *mAgreements = nullptr;
  MeetingHistoryList *mHistory = nullptr;
  NotesWorkspaceBridge *mWorkspace = nullptr;
  QQuickWidget *mQuick = nullptr;
};

// One native dialog hosts the QML editor; all dismissal paths share the guard.
class NotesEditorDialog final : public QDialog {
  Q_OBJECT
public:
  explicit NotesEditorDialog(QWidget *parent = nullptr);
  void setPage(CallNotesPage *page);
  void setDiscardConfirmation(std::function<bool()> confirm) { mConfirmDiscard = std::move(confirm); }
public slots:
  void reject() override;
private:
  QPointer<CallNotesPage> mPage;
  std::function<bool()> mConfirmDiscard;
};
