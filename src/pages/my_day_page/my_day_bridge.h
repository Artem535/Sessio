#pragma once

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <functional>
#include <memory>
#include <optional>

#include "database.h"
#include "my_day_logic.h"

class QTimelineModel;

namespace pcm::meeting {
class SeriesCallService;
}

namespace pcm::my_day {

// Everything the «Мой день» page binds to: shared calendar model + DB to-dos + pure logic.
class MyDayBridge final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString dateTitle READ dateTitle NOTIFY changed)
  Q_PROPERTY(bool isToday READ isToday NOTIFY changed)
  // keys: meetings, meetingsSubtitle, income, currency, unpaidCount, unpaidSum, withoutNotesCount
  Q_PROPERTY(QVariantMap tiles READ tiles NOTIFY changed)
  // per item: id, dayMs, title, start, timeRange, section (morning|afternoon|evening),
  // state (past|current|next|upcoming), canceled, unpaid, withoutNote, online, personal,
  // inPerson, joinable
  Q_PROPERTY(QVariantList timeline READ timeline NOTIFY changed)
  // per group: title, clientId, eventId, dayMs, todos[{id, title, done, clientId, eventId,
  // carriedFrom}]
  Q_PROPERTY(QVariantList todoGroups READ todoGroups NOTIFY changed)
  Q_PROPERTY(int todoDone READ todoDone NOTIFY changed)
  Q_PROPERTY(int todoTotal READ todoTotal NOTIFY changed)
  Q_PROPERTY(bool groupByMeeting READ groupByMeeting WRITE setGroupByMeeting NOTIFY changed)

public:
  using Clock = std::function<QDateTime()>;

  MyDayBridge(std::shared_ptr<pcm::database::Database> db, QTimelineModel *model,
              pcm::meeting::SeriesCallService *seriesCalls, Clock clock = {},
              QObject *parent = nullptr);

  QString dateTitle() const { return mDateTitle; }
  bool isToday() const { return mIsToday; }
  QVariantMap tiles() const { return mTiles; }
  QVariantList timeline() const { return mTimeline; }
  QVariantList todoGroups() const { return mTodoGroups; }
  int todoDone() const { return mTodoDone; }
  int todoTotal() const { return mTodoTotal; }
  bool groupByMeeting() const { return mGroupByMeeting; }
  void setGroupByMeeting(bool value);

  void reload();

  Q_INVOKABLE void previousDay();
  Q_INVOKABLE void nextDay();
  Q_INVOKABLE void goToday();
  Q_INVOKABLE QVariantList unpaidList();
  Q_INVOKABLE QVariantList withoutNotesList();
  Q_INVOKABLE QVariantList clientChoices();
  Q_INVOKABLE QVariantList meetingChoices();
  // 0 = none; eventId may be a virtual (negative) id: stores series + occurrence start.
  Q_INVOKABLE void addTodo(const QString &title, qint64 clientId, qint64 eventId);
  Q_INVOKABLE void setTodoTitle(qint64 id, const QString &title);
  Q_INVOKABLE void setTodoDone(qint64 id, bool done);
  Q_INVOKABLE void removeTodo(qint64 id);
  Q_INVOKABLE void openEvent(qint64 eventId, qint64 dayMs);
  Q_INVOKABLE void joinEvent(qint64 eventId);
  Q_INVOKABLE void openClient(qint64 clientId);

signals:
  void changed();
  void openEventRequested(qint64 eventId, qint64 dayMs);
  void openLiveKitMeetingRequested(QString target);
  void openClientRequested(qint64 clientId);

private:
  QDateTime now() const;
  void setDay(const QDate &day);
  std::optional<int64_t> clientIdFor(const DuckEvent &event);
  QString clientNameFor(int64_t clientId) const;
  QString titleFor(const DuckEvent &event) const;
  QVariantList eventRows(const std::vector<DuckEvent> &events);
  const DuckEvent *findDayEvent(int64_t id) const;
  std::optional<DuckEvent> resolveEvent(int64_t id);

  std::shared_ptr<pcm::database::Database> mDb;
  QPointer<QTimelineModel> mModel;
  QPointer<pcm::meeting::SeriesCallService> mSeriesCalls;
  Clock mClock;
  QTimer mMinuteTimer;
  QDate mDay;
  bool mGroupByMeeting = false;

  QVector<DuckEvent> mDayEvents;
  QVector<DuckEvent> mRecentEvents;
  QHash<QString, bool> mNoteCache;
  QHash<int64_t, QString> mClientNames;
  QHash<int64_t, std::optional<int64_t>> mEventClientCache;

  QString mDateTitle;
  bool mIsToday = true;
  QVariantMap mTiles;
  QVariantList mTimeline;
  QVariantList mTodoGroups;
  int mTodoDone = 0;
  int mTodoTotal = 0;
};

} // namespace pcm::my_day
