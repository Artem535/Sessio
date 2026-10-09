#pragma once

#include "database.h"
#include "day_summary_widget.h"
#include "meeting_coordinator.h"
#include "series_call_service.h"
#include "qevent_details_widget.h"
#include "timeline_widget.h"
#include "../../widgets/quick_slots_widget.h"
#include "month_calendar_widget.h"
#include "month_picker_widget.h"
#include "../../widgets/rounded_calendar_widget.h"
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
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

  // The recurring-call facade. Without it recurring LiveKit events keep the
  // legacy local-only behavior.
  void setSeriesCallService(pcm::meeting::SeriesCallService *service);
  void setMonthView(bool enabled);
  void setTranscriptCountProvider(std::function<int(int64_t)> provider);
  // Day|Month switch and the "New meeting" button. MainWindow hosts them in the
  // top row; standalone they stay in a row above the page body.
  [[nodiscard]] QWidget *headerControls() const;
  [[nodiscard]] bool isMonthView() const;
  // Whether "This and future events" may be offered for this series (it may
  // not for a published series: the permanent invitation cannot be split).
  [[nodiscard]] bool canDeleteFutureOccurrences(int64_t seriesId) const;

signals:
  void openTranscriptRequested(int64_t eventId);
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
  void reloadSelectedDay();
  void onClientResolved(int64_t clientId);
  void refreshAppearance();
  void openEventOnDay(int64_t eventId, qint64 dayMs);
  // Selects the event in the inspector without opening the editor dialog.
  void showEventOnDay(int64_t eventId, qint64 dayMs);

private slots:
  void onCalendarClicked(const QDate &date);
  void onCreateEventClicked();
  void onTimelineEventSelected(int64_t eventId);
  void onTimelineEventEditRequested(int64_t eventId);
  void onTimelineEventDeleteRequested(int64_t eventId);
  void onEventSaved(QEventItem *event);
  void onEditingCanceled();
  void onDaySummaryEventHighlightRequested(int64_t eventId);

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  QWidget *mLeftColumn = nullptr;
  void connectSignals();
  void setupSeriesCall(QEventDetailsWidget *widget, const DuckEvent &event);
  // IANA timezone to pin a series to: the machine's, or one the specialist
  // picks when the machine's is unusable. Empty when they decline.
  [[nodiscard]] QString confirmNewSeriesTimezone();
  void initDefaultStates();
  void refreshCalendar();
  void updateCalendarHighlights() const;
  void selectMonthEvent(DuckEvent event);
  // Series data the inspector shows besides the occurrence itself.
  struct ShownSeriesRule {
    std::string recurrence_rule;
    std::optional<int64_t> recurrence_until;
    bool operator==(const ShownSeriesRule &) const = default;
  };
  [[nodiscard]] std::optional<ShownSeriesRule>
  shownSeriesRule(const std::optional<DuckEvent> &event) const;
  void showInspector(const std::optional<DuckEvent> &event, bool force = false);
  void syncInspector(const QVector<DuckEvent> &events);
  void editSelectedEvent();
  void openEventDialog(const std::optional<DuckEvent> &event = std::nullopt,
                       std::optional<int64_t> clientId = std::nullopt);
  void openQuickEventDialog(const QTime &startTime, int durationMinutes);
  void editEventWithDialog(int64_t eventId);
  void refreshQuickSlots() const;
  void refreshDaySummary() const;
  [[nodiscard]] QVector<QPair<QDateTime, QDateTime>> currentBusyIntervals() const;
  [[nodiscard]] std::optional<DuckEvent> checkEventConflict(const DuckEvent &event) const;

  std::unique_ptr<Ui::EventInfo> mUi;
  MonthCalendarWidget *mMonthCalendar = nullptr;
  oclero::qlementine::Switch *mViewSwitch = nullptr;
  QStackedWidget *mCalendarStack = nullptr;
  QWidget *mHeaderControls = nullptr;
  RoundedCalendarWidget *mCalendarWidget = nullptr;
  MonthPickerWidget *mMonthPicker = nullptr;
  QStackedWidget *mCalendarCardStack = nullptr;
  QStackedWidget *mInfoStack = nullptr;
  QScrollArea *mInspectorScroll = nullptr;
  QVBoxLayout *mInspectorLayout = nullptr;
  QPointer<QEventDetailsWidget> mInspector;
  std::optional<DuckEvent> mSelectedEvent;
  std::optional<ShownSeriesRule> mShownSeriesRule;
  bool mNavigating = false;
  QTimelineWidget *mTimelineWidget = nullptr;
  QPointer<pcm::meeting::MeetingCoordinator> mMeetingCoordinator;
  QPointer<QTimelineModel> mModel;
  QPointer<pcm::meeting::SeriesCallService> mSeriesCallService;
  QPushButton *mCreateEventButton = nullptr;
  QuickSlotsWidget *mQuickSlotsWidget = nullptr;
  DaySummaryWidget *mDaySummaryWidget = nullptr;
  QPointer<QEventDetailsWidget> mActiveEventDetailsWidget;

  int64_t mClientId = 0;
  QDate mSelectedDate;
  std::function<int(int64_t)> mTranscriptCountProvider;
};
