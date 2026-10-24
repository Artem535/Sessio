#pragma once

#include "meeting_history.h"
#include "notes_widget_frame.h"
#include <QHash>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <optional>
#include <vector>

class CallNotesController;

// «Last time» card: date, summary preview and up to three agreements of the
// client's latest completed meeting. Hidden while there is no such meeting.
class PreviousMeetingWidget final : public NotesWidgetFrame {
  Q_OBJECT
public:
  explicit PreviousMeetingWidget(QWidget *parent = nullptr);
  void setMeeting(const std::optional<MeetingRecord> &meeting);  // nullopt hides
signals:
  void openRequested(qint64 materialId);
};

// Factory for a controller of a past meeting (host wires the finalizer queue).
using MeetingControllerFactory = std::function<CallNotesController *(qint64 materialId, QObject *parent)>;

// Past meetings of the client; each row expands lazily into its summary,
// key points, own notes and agreements.
class MeetingHistoryList final : public QWidget {
  Q_OBJECT
public:
  explicit MeetingHistoryList(QWidget *parent = nullptr);
  void setControllerFactory(MeetingControllerFactory factory);
  void setMeetings(const std::vector<MeetingRecord> &meetings, const QStringList &notesWithoutMeeting);
  void expand(qint64 materialId);  // collapses others
signals:
  void sourceRequested(qint64 phraseId);

private:
  void populate(QWidget *details, const MeetingRecord &meeting);

  MeetingControllerFactory mFactory;
  QHash<qint64, MeetingRecord> mMeetings;
};
