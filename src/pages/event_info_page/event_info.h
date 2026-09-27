#pragma once

#include "database.h"
#include "day_summary_widget.h"
#include "meeting_coordinator.h"
#include "qevent_details_widget.h"
#include "timeline_widget.h"
#include "../../widgets/quick_slots_widget.h"
#include "../../widgets/rounded_calendar_widget.h"
#include <QDate>
#include <QDateTime>
#include <QPair>
#include <QPointer>
#include <QPushButton>
#include <QTime>
#include <QWidget>
#include <memory>
#include <optional>

namespace Ui {
class EventInfo;
}

class QEventInfoPage final : public QWidget {
  Q_OBJECT

public:
  QEventInfoPage(QTimelineModel *model, pcm::meeting::MeetingCoordinator *meetingCoordinator,
                QWidget *parent);
  ~QEventInfoPage() override;

signals:
  void provideClientEventPairSave(int64_t clientId, int64_t eventId);
  void provideFillClientComboBox(QComboBox *comboBox);
  void provideClientByEventId(int64_t eventId);
  void clientResolved(int64_t clientId);

  /**
   * @brief Forwarded from the active QEventDetailsWidget when the user
   * requests to open a LiveKit meeting; the owner (Application) routes this
   * to MainWindow's Calls tab instead of opening a URL.
   */
  void openLiveKitMeetingRequested(QString meetingRef);

public slots:
  void onClientResolved(int64_t clientId);
  void refreshAppearance();
  void openEventOnDay(int64_t eventId, qint64 dayMs);

private slots:
  void onCalendarClicked(const QDate &date);
  void onCreateEventClicked();
  void onTimelineEventSelected(int64_t eventId);
  void onTimelineEventEditRequested(int64_t eventId);
  void onTimelineEventDeleteRequested(int64_t eventId);
  void onEventSaved(QEventItem *event);
  void onEditingCanceled();
  void onDaySummaryEventHighlightRequested(int64_t eventId);

private:
  void connectSignals();
  void initDefaultStates();
  void updateCalendarHighlights() const;
  void openEventDialog(const std::optional<DuckEvent> &event = std::nullopt,
                       std::optional<int64_t> clientId = std::nullopt);
  void openQuickEventDialog(const QTime &startTime, int durationMinutes);
  void editEventWithDialog(int64_t eventId);
  void refreshQuickSlots() const;
  void refreshDaySummary() const;
  [[nodiscard]] QVector<QPair<QDateTime, QDateTime>> currentBusyIntervals() const;
  [[nodiscard]] std::optional<DuckEvent> checkEventConflict(const DuckEvent &event) const;

  std::unique_ptr<Ui::EventInfo> mUi;
  RoundedCalendarWidget *mCalendarWidget = nullptr;
  QTimelineWidget *mTimelineWidget = nullptr;
  QPointer<pcm::meeting::MeetingCoordinator> mMeetingCoordinator;
  QPushButton *mCreateEventButton = nullptr;
  QuickSlotsWidget *mQuickSlotsWidget = nullptr;
  DaySummaryWidget *mDaySummaryWidget = nullptr;
  QPointer<QEventDetailsWidget> mActiveEventDetailsWidget;

  int64_t mClientId = 0;
  QDate mSelectedDate;
};
