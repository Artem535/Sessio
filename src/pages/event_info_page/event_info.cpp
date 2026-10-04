#include "event_info.h"
#include "../../widgets/constants.hpp"
#include "../../widgets/app_settings.h"
#include "recurrence_utils.h"
#include "series_timezone_dialog.h"
#include "ui/pages/ui_eventinfo.h"

#include <QDialog>
#include <algorithm>
#include <QIcon>
#include <QMessageBox>
#include <QPainter>
#include <QSize>
#include <QTextCharFormat>
#include <QTimeZone>
#include <QVBoxLayout>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <oclero/qlementine/widgets/Switch.hpp>

Q_LOGGING_CATEGORY(logEventInfo, "pcm.EventInfo")

namespace {
// Rounded bordered card; colours come from the live palette.
class InfoPanelCard final : public QWidget {
public:
  using QWidget::QWidget;

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    auto border = palette().color(QPalette::Text);
    border.setAlpha(46);
    painter.setPen(QPen(border, 1));
    painter.setBrush(QColor(255, 255, 255, 8));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
  }
};

bool sameOccurrence(const DuckEvent &a, const DuckEvent &b) {
  if (a.series_id && a.original_occurrence_start) {
    return a.series_id == b.series_id &&
           a.original_occurrence_start == b.original_occurrence_start;
  }
  return a.id > 0 && a.id == b.id;
}
bool sameEventData(const DuckEvent &a, const DuckEvent &b) {
  return a.id == b.id && a.name == b.name && a.description == b.description &&
         a.client_name == b.client_name && a.is_work_event == b.is_work_event &&
         a.event_stat_id == b.event_stat_id && a.payment_stat_id == b.payment_stat_id &&
         a.start_date == b.start_date && a.end_date == b.end_date &&
         a.duration == b.duration && a.cost == b.cost && a.is_online == b.is_online &&
         a.meeting_url == b.meeting_url && a.series_id == b.series_id &&
         a.original_occurrence_start == b.original_occurrence_start &&
         a.cancellation_reason == b.cancellation_reason && a.canceled_by == b.canceled_by &&
         a.buffer_before_minutes == b.buffer_before_minutes &&
         a.buffer_after_minutes == b.buffer_after_minutes &&
         a.is_virtual_occurrence == b.is_virtual_occurrence &&
         a.provider_kind == b.provider_kind && a.meeting_ref == b.meeting_ref &&
         a.invitation_state == b.invitation_state;
}
enum class RecurringEditScope {
  Cancel,
  SingleOccurrence,
  WholeSeries,
};

enum class RecurringDeleteScope {
  Cancel,
  SingleOccurrence,
  FutureOccurrences,
  WholeSeries,
};

RecurringEditScope askRecurringEditScope(QWidget *parent) {
  QMessageBox messageBox(parent);
  messageBox.setWindowTitle(QObject::tr("Recurring event"));
  messageBox.setText(QObject::tr("What do you want to update?"));
  const auto singleButton =
      messageBox.addButton(QObject::tr("Only this event"), QMessageBox::AcceptRole);
  const auto seriesButton =
      messageBox.addButton(QObject::tr("Whole series"), QMessageBox::ActionRole);
  messageBox.addButton(QMessageBox::Cancel);
  messageBox.setDefaultButton(singleButton);
  messageBox.exec();

  if (messageBox.clickedButton() == singleButton) {
    return RecurringEditScope::SingleOccurrence;
  }
  if (messageBox.clickedButton() == seriesButton) {
    return RecurringEditScope::WholeSeries;
  }
  return RecurringEditScope::Cancel;
}

RecurringDeleteScope askRecurringDeleteScope(QWidget *parent, bool allowFuture) {
  QMessageBox messageBox(parent);
  messageBox.setWindowTitle(QObject::tr("Recurring event"));
  messageBox.setText(QObject::tr("What do you want to delete?"));
  const auto singleButton =
      messageBox.addButton(QObject::tr("Only this event"), QMessageBox::AcceptRole);
  QAbstractButton *futureButton = nullptr;
  if (allowFuture) {
    futureButton =
        messageBox.addButton(QObject::tr("This and future events"), QMessageBox::ActionRole);
  } else {
    messageBox.setInformativeText(
        QObject::tr("\"This and future events\" is not available for a series with a "
                    "permanent call link. Delete only this event or the whole series."));
  }
  const auto seriesButton =
      messageBox.addButton(QObject::tr("Whole series"), QMessageBox::DestructiveRole);
  messageBox.addButton(QMessageBox::Cancel);
  messageBox.setDefaultButton(singleButton);
  messageBox.exec();

  if (messageBox.clickedButton() == singleButton) {
    return RecurringDeleteScope::SingleOccurrence;
  }
  if (futureButton && messageBox.clickedButton() == futureButton) {
    return RecurringDeleteScope::FutureOccurrences;
  }
  if (messageBox.clickedButton() == seriesButton) {
    return RecurringDeleteScope::WholeSeries;
  }
  return RecurringDeleteScope::Cancel;
}

} // namespace

QEventInfoPage::QEventInfoPage(QTimelineModel *model,
                               pcm::meeting::MeetingCoordinator *meetingCoordinator,
                               QWidget *parent)
    : QWidget(parent), mUi(std::make_unique<Ui::EventInfo>()),
      mMeetingCoordinator(meetingCoordinator), mModel(model) {
  mUi->setupUi(this);
  if (mModel) {
    // A schedule change the server could not be offered is rolled back as a
    // whole; never let that pass silently.
    connect(mModel, &QTimelineModel::scheduleCommitFailed, this,
            [this](const QString &error, const QString &detail) {
              QString text = tr("The change was not saved.");
              if (error == QLatin1String("split_unsupported")) {
                text = tr("This series has a permanent call link, so it cannot be split into "
                          "\"this and future\" events. Nothing was changed.");
              } else if (error == QLatin1String("invalid_schedule")) {
                text = tr("The schedule cannot be published: %1. Nothing was changed.")
                           .arg(detail);
              } else if (!detail.isEmpty()) {
                text = tr("The change was not saved: %1").arg(detail);
              }
              QMessageBox::warning(this, tr("Recurring event"), text);
            });
  }
  mSelectedDate = QDate::currentDate();
  // Header controls live in the main window's top row (see headerControls());
  // standalone they sit in a collapsible row above the page body.
  mHeaderControls = new QWidget(this);
  mHeaderControls->setObjectName(QStringLiteral("calendarHeaderControls"));
  auto *header = new QHBoxLayout(mHeaderControls);
  header->setContentsMargins(0, 0, 0, 0);
  header->setSpacing(10);
  // Fill the host row and push the group to its right edge: the stretch comes
  // first, so Day|Month and "New meeting" stay together.
  mHeaderControls->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  header->addStretch(1);
  header->addWidget(new QLabel(tr("Day"), mHeaderControls));
  mViewSwitch = new oclero::qlementine::Switch(mHeaderControls);
  mViewSwitch->setObjectName(QStringLiteral("calendarViewSwitch"));
  mViewSwitch->setAccessibleName(tr("Calendar view"));
  // Qlementine's Switch is horizontally expanding; keep it hugging its labels.
  mViewSwitch->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  header->addWidget(mViewSwitch);
  header->addWidget(new QLabel(tr("Month"), mHeaderControls));
  header->addSpacing(16);
  mCreateEventButton = new QPushButton(tr("New meeting"), mHeaderControls);
  mCreateEventButton->setObjectName(QStringLiteral("newCalendarEvent"));
  mCreateEventButton->setIcon(QIcon(":/icons/calendar-plus-solid-full.svg"));
  mCreateEventButton->setIconSize(QSize(18, 18));
  mCreateEventButton->setToolTip(tr(": EVENT_ADD_BUTTON"));
  mCreateEventButton->setCursor(Qt::PointingHandCursor);
  header->addWidget(mCreateEventButton);
  auto *standaloneHeader = new QWidget(this);
  standaloneHeader->setObjectName(QStringLiteral("calendarStandaloneHeader"));
  auto *standaloneLayout = new QHBoxLayout(standaloneHeader);
  standaloneLayout->setContentsMargins(0, 0, 0, 0);
  standaloneLayout->addWidget(mHeaderControls);
  mUi->pageLayout->addWidget(standaloneHeader);
  mUi->pageLayout->setSpacing(0);

  auto *body = new QHBoxLayout;
  body->setSpacing(20);
  mUi->pageLayout->addLayout(body, 1);

  // Left column: calendar card (day calendar / month picker), one swappable
  // info panel (day summary / meeting inspector) and the quick slots.
  auto *left = new QWidget(this);
  left->setObjectName(QStringLiteral("calendarLeftColumn"));
  left->setFixedWidth(400);
  mLeftColumn = left;
  auto *leftLayout = new QVBoxLayout(left);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(12);
  mCalendarCardStack = new QStackedWidget(left);
  mCalendarCardStack->setObjectName(QStringLiteral("calendarCardStack"));
  mCalendarWidget = new RoundedCalendarWidget(mCalendarCardStack);
  mCalendarWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  mMonthPicker = new MonthPickerWidget(mCalendarCardStack);
  mCalendarCardStack->addWidget(mCalendarWidget);
  mCalendarCardStack->addWidget(mMonthPicker);
  mCalendarCardStack->setFixedHeight(std::max(mCalendarWidget->minimumHeight(),
                                              mMonthPicker->minimumHeight()));
  leftLayout->addWidget(mCalendarCardStack);

  // One bordered card hosts both pages (day info / meeting info).
  auto *infoCard = new InfoPanelCard(left);
  infoCard->setObjectName(QStringLiteral("infoPanelCard"));
  infoCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  auto *infoCardLayout = new QVBoxLayout(infoCard);
  infoCardLayout->setContentsMargins(16, 16, 16, 16);
  mInfoStack = new QStackedWidget(infoCard);
  mInfoStack->setObjectName(QStringLiteral("calendarInfoStack"));
  infoCardLayout->addWidget(mInfoStack);
  auto *summaryScroll = new QScrollArea(mInfoStack);
  summaryScroll->setObjectName(QStringLiteral("calendarDayInfoScroll"));
  summaryScroll->setWidgetResizable(true);
  summaryScroll->setFrameShape(QFrame::NoFrame);
  summaryScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  mDaySummaryWidget = new DaySummaryWidget(summaryScroll);
  summaryScroll->setWidget(mDaySummaryWidget);
  // The card provides the background; scroll viewports must not paint their own.
  summaryScroll->viewport()->setAutoFillBackground(false);
  mDaySummaryWidget->setAutoFillBackground(false);
  mInfoStack->addWidget(summaryScroll);
  mInspectorScroll = new QScrollArea(mInfoStack);
  mInspectorScroll->setObjectName(QStringLiteral("calendarInspectorScroll"));
  mInspectorScroll->setWidgetResizable(true);
  mInspectorScroll->setFrameShape(QFrame::NoFrame);
  mInspectorScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *inspectorContent = new QWidget;
  inspectorContent->setObjectName(QStringLiteral("calendarInspectorContent"));
  mInspectorLayout = new QVBoxLayout(inspectorContent);
  mInspectorLayout->setContentsMargins(0, 0, 0, 0);
  mInspectorLayout->setSpacing(8);
  mInspectorLayout->addStretch(1);
  mInspectorScroll->setWidget(inspectorContent);
  mInspectorScroll->viewport()->setAutoFillBackground(false);
  inspectorContent->setAutoFillBackground(false);
  mInfoStack->addWidget(mInspectorScroll);
  leftLayout->addWidget(infoCard, 1);
  mQuickSlotsWidget = new QuickSlotsWidget(left);
  leftLayout->addWidget(mQuickSlotsWidget);
  body->addWidget(left);

  // Centre: the original day timeline, or the month grid.
  mCalendarStack = new QStackedWidget(this);
  mCalendarStack->setObjectName(QStringLiteral("calendarCenterStack"));
  mTimelineWidget = new QTimelineWidget(model, mCalendarStack);
  mCalendarStack->addWidget(mTimelineWidget);
  // The month grid caps its row height; when the window is too short it
  // scrolls, when too tall the grid stays top-aligned with empty space below.
  auto *monthScroll = new QScrollArea(mCalendarStack);
  monthScroll->setObjectName(QStringLiteral("calendarMonthScroll"));
  monthScroll->setWidgetResizable(true);
  monthScroll->setFrameShape(QFrame::NoFrame);
  monthScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  mMonthCalendar = new MonthCalendarWidget(monthScroll);
  monthScroll->setWidget(mMonthCalendar);
  mCalendarStack->addWidget(monthScroll);
  body->addWidget(mCalendarStack, 1);

  connectSignals();
  initDefaultStates();
}

QEventInfoPage::~QEventInfoPage() = default;

void QEventInfoPage::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  // ~400px like the original layout, narrower on small windows so the
  // timeline / month grid keep a usable width.
  mLeftColumn->setFixedWidth(std::clamp(width() * 38 / 100, 320, 400));
}

void QEventInfoPage::connectSignals() {
  connect(mViewSwitch, &QAbstractButton::toggled, this, &QEventInfoPage::setMonthView);
  connect(mCalendarWidget, &RoundedCalendarWidget::clicked, this, &QEventInfoPage::onCalendarClicked);
  connect(mMonthPicker, &MonthPickerWidget::monthSelected, this, [this](const QDate month) {
    // Keep the day of month where it exists; the month grid follows the date.
    const auto day = std::min(mSelectedDate.day(), month.daysInMonth());
    onCalendarClicked(QDate(month.year(), month.month(), day));
  });
  connect(mMonthCalendar, &MonthCalendarWidget::dateSelected, this, &QEventInfoPage::onCalendarClicked);
  connect(mMonthCalendar, &MonthCalendarWidget::eventSelected, this, &QEventInfoPage::selectMonthEvent);
  if (mModel) {
    connect(mModel, &QTimelineModel::eventsLoaded, this, &QEventInfoPage::refreshCalendar);
    connect(mModel, &QAbstractItemModel::dataChanged, this, &QEventInfoPage::refreshCalendar);
    connect(mModel, &QAbstractItemModel::rowsInserted, this, &QEventInfoPage::refreshCalendar);
    connect(mModel, &QAbstractItemModel::rowsRemoved, this, &QEventInfoPage::refreshCalendar);
  }
  connect(mCreateEventButton, &QPushButton::clicked, this,
          &QEventInfoPage::onCreateEventClicked);

  connect(mTimelineWidget, &QTimelineWidget::eventSelected, this,
          &QEventInfoPage::onTimelineEventSelected);
  connect(mTimelineWidget, &QTimelineWidget::eventEditRequested, this,
          &QEventInfoPage::onTimelineEventEditRequested);
  connect(mTimelineWidget, &QTimelineWidget::eventDeleteRequested, this,
          &QEventInfoPage::onTimelineEventDeleteRequested);
  connect(mTimelineWidget, &QTimelineWidget::createEventRequested, this,
          &QEventInfoPage::openQuickEventDialog);
  connect(mTimelineWidget, &QTimelineWidget::needSceneUpdate, this,
          &QEventInfoPage::refreshQuickSlots);
  connect(mQuickSlotsWidget, &QuickSlotsWidget::quickSlotSelected, this,
          &QEventInfoPage::openQuickEventDialog);
  connect(mDaySummaryWidget, &DaySummaryWidget::eventHighlightRequested, this,
          &QEventInfoPage::onDaySummaryEventHighlightRequested);

  connect(this, &QEventInfoPage::clientResolved, this,
          &QEventInfoPage::onClientResolved);
}

void QEventInfoPage::initDefaultStates() {
  mSelectedDate = QDate::currentDate();
  updateCalendarHighlights();
  mTimelineWidget->onSelectedDayChanged(mSelectedDate);
  refreshCalendar();
  refreshQuickSlots();
  refreshDaySummary();
}

void QEventInfoPage::onCalendarClicked(const QDate &date) {
  if (!date.isValid()) {
    return;
  }
  if (date != mSelectedDate) {
    showInspector(std::nullopt);
  }
  mSelectedDate = date;
  {
    // The model announces the loaded day; one explicit refresh below is enough.
    const QScopedValueRollback<bool> navigating(mNavigating, true);
    mTimelineWidget->onSelectedDayChanged(date);
  }
  refreshCalendar(); // also refreshes quick slots and the day summary
}

void QEventInfoPage::openEventOnDay(const int64_t eventId, const qint64 dayMs) {
  const auto date =
      QDateTime::fromMSecsSinceEpoch(dayMs, QTimeZone::systemTimeZone()).date();
  onCalendarClicked(date);
  editEventWithDialog(eventId);
}

void QEventInfoPage::setMonthView(bool enabled) {
  const QSignalBlocker blocker(mViewSwitch);
  mViewSwitch->setChecked(enabled);
  mCalendarStack->setCurrentIndex(enabled ? 1 : 0);
  mCalendarCardStack->setCurrentIndex(enabled ? 1 : 0);
  refreshCalendar();
}

bool QEventInfoPage::isMonthView() const { return mViewSwitch->isChecked(); }

void QEventInfoPage::refreshCalendar() {
  if (mNavigating) {
    return;
  }
  mCalendarWidget->setSelectedDate(mSelectedDate);
  if (mMonthPicker->displayedMonth().year() != mSelectedDate.year() ||
      mMonthPicker->displayedMonth().month() != mSelectedDate.month()) {
    mMonthPicker->setDisplayedMonth(mSelectedDate);
  }
  if (isMonthView()) {
    mMonthCalendar->setSelectedDate(mSelectedDate);
    const auto [first, last] = MonthCalendarWidget::visibleRange(mSelectedDate);
    const auto events = mModel ? mModel->eventsForRange(first, last) : QVector<DuckEvent>{};
    mMonthCalendar->setMonthAndEvents(mSelectedDate, events);
    syncInspector(events);
  } else if (mSelectedEvent && mModel) {
    // The hidden month grid is rebuilt when the view switches back to it; only
    // the selected day is projected to keep the inspector in sync.
    syncInspector(mModel->eventsForRange(mSelectedDate, mSelectedDate));
  }
  refreshQuickSlots();
  refreshDaySummary();
}

void QEventInfoPage::syncInspector(const QVector<DuckEvent> &events) {
  if (!mSelectedEvent) {
    return;
  }
  const auto selected = std::find_if(events.cbegin(), events.cend(),
      [this](const DuckEvent &event) { return sameOccurrence(*mSelectedEvent, event); });
  showInspector(selected != events.cend() ? std::optional<DuckEvent>(*selected) : std::nullopt);
}

void QEventInfoPage::selectMonthEvent(DuckEvent event) {
  if (!event.start_date || !mModel) {
    return;
  }
  // Range-local negative IDs never enter the day model. Overnight continuations
  // resolve on the occurrence's actual start day as well.
  onCalendarClicked(QDateTime::fromMSecsSinceEpoch(*event.start_date).date());
  const auto &day = mModel->events();
  const auto selected = std::find_if(day.cbegin(), day.cend(),
      [&event](const DuckEvent &candidate) { return sameOccurrence(event, candidate); });
  showInspector(selected != day.cend() ? std::optional<DuckEvent>(*selected) : std::nullopt);
}

std::optional<QEventInfoPage::ShownSeriesRule>
QEventInfoPage::shownSeriesRule(const std::optional<DuckEvent> &event) const {
  if (!event || !event->series_id || !mModel) {
    return std::nullopt;
  }
  const auto series = mModel->eventSeriesById(*event->series_id);
  if (!series) {
    return std::nullopt;
  }
  return ShownSeriesRule{series->recurrence_rule, series->recurrence_until};
}

void QEventInfoPage::showInspector(const std::optional<DuckEvent> &event, bool force) {
  if (!force && mInspector && event && mSelectedEvent &&
      sameOccurrence(*mSelectedEvent, *event) && sameEventData(*mSelectedEvent, *event) &&
      shownSeriesRule(event) == mShownSeriesRule) {
    return; // nothing changed: keep the widget (scroll position, focus, call state)
  }
  if (mInspector) {
    mInspectorLayout->removeWidget(mInspector);
    mInspector->loadEvent(nullptr); // invalidate pending secure-storage callbacks
    mInspector->setObjectName(QString{});
    mInspector->hide();
    mInspector->deleteLater();
    mInspector.clear();
  }
  mSelectedEvent = event;
  mShownSeriesRule = shownSeriesRule(event);
  mInfoStack->setCurrentIndex(event.has_value() ? 1 : 0);
  if (!event) {
    return;
  }
  mInspector = new QEventDetailsWidget(this);
  mInspector->setObjectName(QStringLiteral("calendarInspector"));
  mInspector->setInspectorMode(true);
  mInspector->setMeetingCoordinator(mMeetingCoordinator);
  QEventItem copy(*event);
  mInspector->loadEvent(&copy);
  if (event->series_id && mModel) {
    if (const auto series = mModel->eventSeriesById(*event->series_id)) {
      mInspector->setRecurrenceRule(QString::fromStdString(series->recurrence_rule),
                                    series->recurrence_until);
    }
  }
  setupSeriesCall(mInspector, *event);
  connect(mInspector, &QEventDetailsWidget::backRequested, this,
          [this] { showInspector(std::nullopt); });
  connect(mInspector, &QEventDetailsWidget::editRequested, this, &QEventInfoPage::editSelectedEvent);
  connect(mInspector, &QEventDetailsWidget::openLiveKitMeetingRequested,
          this, &QEventInfoPage::openLiveKitMeetingRequested);
  // Keep the details at their natural height: a vertically stretched details
  // widget spreads its rows across the whole panel.
  mInspectorLayout->insertWidget(0, mInspector, 0, Qt::AlignTop);
}

void QEventInfoPage::editSelectedEvent() {
  if (!mSelectedEvent) {
    return;
  }
  const auto selected = *mSelectedEvent;
  selectMonthEvent(selected);
  if (mSelectedEvent) {
    editEventWithDialog(mSelectedEvent->id);
  }
}

void QEventInfoPage::updateCalendarHighlights() const {
  QTextCharFormat currentDayFormat;
  currentDayFormat.setFontWeight(QFont::DemiBold);
  currentDayFormat.setUnderlineStyle(QTextCharFormat::SingleUnderline);
  currentDayFormat.setUnderlineColor(pcm::widgets::constants::kCalendarCurrentDayUnderlineColor);
  currentDayFormat.setForeground(pcm::widgets::constants::kCalendarCurrentDayForegroundColor);
  mCalendarWidget->setDateTextFormat(QDate::currentDate(), currentDayFormat);
}

QWidget *QEventInfoPage::headerControls() const { return mHeaderControls; }

void QEventInfoPage::setSeriesCallService(pcm::meeting::SeriesCallService *service) {
  mSeriesCallService = service;
  refreshCalendar();
  if (mSelectedEvent) {
    showInspector(mSelectedEvent, true); // call state comes from the service
  }
}

bool QEventInfoPage::canDeleteFutureOccurrences(const int64_t seriesId) const {
  return !mModel || !mModel->isSeriesSplitBlocked(seriesId);
}

void QEventInfoPage::setupSeriesCall(QEventDetailsWidget *widget, const DuckEvent &event) {
  if (!mSeriesCallService || !mModel || !event.series_id.has_value()) {
    return;
  }
  const auto seriesId = *event.series_id;
  const auto series = mModel->eventSeriesById(seriesId);
  if (!series.has_value()) {
    return;
  }
  if (mSeriesCallService->isSeriesBacked(*series)) {
    widget->setSeriesCall(mSeriesCallService.data(), seriesId,
                          mSeriesCallService->joinTargetFor(event).value_or(QString{}));
    return;
  }
  if (!pcm::meeting::SeriesCallService::isLegacyLiveKitSeries(*series)) {
    return;
  }
  widget->setLegacyMigration(mSeriesCallService.data(), seriesId);
  connect(widget, &QEventDetailsWidget::migrateToPermanentLinkRequested, this,
          [this, widget](const qint64 id) {
            const auto current = mModel->eventSeriesById(id);
            if (!current.has_value() || !mSeriesCallService) {
              return;
            }
            const auto preview = [&current](const std::string &timezone) {
              return pcm::recurrence::previewOccurrences(
                  *current, timezone, QDateTime::currentDateTimeUtc(), 5);
            };
            pcm::eventpage::SeriesTimezoneDialog dialog(
                preview, QString::fromStdString(pcm::recurrence::systemScheduleTimezone()),
                widget);
            if (dialog.exec() != QDialog::Accepted || dialog.selectedTimezone().isEmpty()) {
              return;
            }
            mSeriesCallService->migrateLegacySeries(id, dialog.selectedTimezone());
          });
}

QString QEventInfoPage::confirmNewSeriesTimezone() {
  const auto system = QString::fromStdString(pcm::recurrence::systemScheduleTimezone());
  if (!system.isEmpty()) {
    return system;
  }
  // The machine's timezone is unknown to the server: never guess, ask. A new
  // series has no earlier dates to compare, so only the zone's validity counts.
  const auto preview = [](const std::string &timezone) -> std::optional<QVector<QDateTime>> {
    if (timezone.empty() || pcm::recurrence::isSupportedScheduleTimezone(timezone)) {
      return QVector<QDateTime>{};
    }
    return std::nullopt;
  };
  pcm::eventpage::SeriesTimezoneDialog dialog(preview, QString{}, this);
  if (dialog.exec() != QDialog::Accepted) {
    return {};
  }
  return dialog.selectedTimezone();
}

void QEventInfoPage::onCreateEventClicked() { openEventDialog(std::nullopt); }

void QEventInfoPage::openQuickEventDialog(const QTime &startTime,
                                          const int durationMinutes) {
  auto dialog = QDialog(this);
  dialog.setModal(true);
  dialog.setWindowTitle(tr(": EVENT_ADD_BUTTON"));

  auto layout = QVBoxLayout(&dialog);
  layout.setContentsMargins(0, 0, 0, 0);

  auto *detailsWidget = new QEventDetailsWidget(&dialog);
  detailsWidget->setMeetingCoordinator(mMeetingCoordinator);
  connect(detailsWidget, &QEventDetailsWidget::openLiveKitMeetingRequested, this,
          [this, &dialog](const QString &meetingRef) {
            dialog.reject();
            emit openLiveKitMeetingRequested(meetingRef);
          });
  detailsWidget->setDialogMode(true);
  detailsWidget->setConflictChecker(
      [this](const DuckEvent &event) { return checkEventConflict(event); });
  layout.addWidget(detailsWidget);

  mActiveEventDetailsWidget = detailsWidget;

  connect(detailsWidget, &QEventDetailsWidget::provideEventSave, this,
          &QEventInfoPage::onEventSaved);
  connect(detailsWidget, &QEventDetailsWidget::provideEditingCanceled, this,
          &QEventInfoPage::onEditingCanceled);
  connect(detailsWidget, &QEventDetailsWidget::provideDialogAccept, &dialog,
          &QDialog::accept);
  connect(detailsWidget, &QEventDetailsWidget::provideEditingCanceled, &dialog,
          &QDialog::reject);
  connect(detailsWidget, &QEventDetailsWidget::provideFillClientComboBox, this,
          [this](QComboBox *comboBox) {
            emit provideFillClientComboBox(comboBox);
          });

  detailsWidget->startCreatingNewEvent(mSelectedDate, startTime, durationMinutes);

  dialog.resize(560, 700);
  dialog.exec();

  mActiveEventDetailsWidget.clear();
}

void QEventInfoPage::openEventDialog(const std::optional<DuckEvent> &event,
                                     const std::optional<int64_t> clientId) {
  auto dialog = QDialog(this);
  dialog.setModal(true);
  dialog.setWindowTitle(event.has_value() ? tr(": EVENT_EDIT_BUTTON")
                              : tr(": EVENT_ADD_BUTTON"));

  auto layout = QVBoxLayout(&dialog);
  layout.setContentsMargins(0, 0, 0, 0);

  auto *detailsWidget = new QEventDetailsWidget(&dialog);
  detailsWidget->setMeetingCoordinator(mMeetingCoordinator);
  connect(detailsWidget, &QEventDetailsWidget::openLiveKitMeetingRequested, this,
          [this, &dialog](const QString &meetingRef) {
            dialog.reject();
            emit openLiveKitMeetingRequested(meetingRef);
          });
  detailsWidget->setDialogMode(true);
  detailsWidget->setConflictChecker(
      [this](const DuckEvent &event) { return checkEventConflict(event); });
  layout.addWidget(detailsWidget);

  mActiveEventDetailsWidget = detailsWidget;

  connect(detailsWidget, &QEventDetailsWidget::provideEventSave, this,
          &QEventInfoPage::onEventSaved);
  connect(detailsWidget, &QEventDetailsWidget::provideEditingCanceled, this,
          &QEventInfoPage::onEditingCanceled);
  connect(detailsWidget, &QEventDetailsWidget::provideDialogAccept, &dialog,
          &QDialog::accept);
  connect(detailsWidget, &QEventDetailsWidget::provideEditingCanceled, &dialog,
          &QDialog::reject);
  connect(detailsWidget, &QEventDetailsWidget::provideFillClientComboBox, this,
          [this](QComboBox *comboBox) {
            emit provideFillClientComboBox(comboBox);
          });

  std::unique_ptr<QEventItem> editingEvent;
  if (event.has_value()) {
    editingEvent = std::make_unique<QEventItem>(*event);
    detailsWidget->startEditingEvent(editingEvent.get(), clientId);
    setupSeriesCall(detailsWidget, *event);
    if (event->series_id.has_value()) {
      const auto series = mTimelineWidget->eventSeriesById(*event->series_id);
      if (series.has_value()) {
        detailsWidget->setRecurrenceRule(
            QString::fromStdString(series->recurrence_rule),
            series->recurrence_until);
      }
    }
  } else {
    detailsWidget->startCreatingNewEvent(mSelectedDate);
  }

  dialog.resize(560, 700);
  dialog.exec();

  mActiveEventDetailsWidget.clear();
}

void QEventInfoPage::onTimelineEventSelected(const int64_t eventId) {
  showInspector(mTimelineWidget->eventById(eventId));
}

void QEventInfoPage::onTimelineEventEditRequested(const int64_t eventId) {
  editEventWithDialog(eventId);
}

void QEventInfoPage::onTimelineEventDeleteRequested(const int64_t eventId) {
  const auto event = mTimelineWidget ? mTimelineWidget->eventById(eventId) : std::nullopt;
  if (!event.has_value()) {
    return;
  }

  if (event->series_id.has_value()) {
    const auto scope = askRecurringDeleteScope(this, canDeleteFutureOccurrences(*event->series_id));
    if (scope == RecurringDeleteScope::Cancel) {
      return;
    }
    if (scope == RecurringDeleteScope::WholeSeries) {
      if (!mTimelineWidget->deactivateEventSeries(*event->series_id)) {
        qCWarning(logEventInfo) << "Failed to deactivate recurring event series";
        return;
      }
      mTimelineWidget->onSelectedDayChanged(mSelectedDate);
      refreshQuickSlots();
      refreshDaySummary();
      return;
    }
    if (scope == RecurringDeleteScope::FutureOccurrences) {
      if (!event->original_occurrence_start.has_value() ||
          !mTimelineWidget->removeFutureEventSeriesOccurrences(
              *event->series_id, *event->original_occurrence_start)) {
        qCWarning(logEventInfo) << "Failed to remove future recurring event occurrences";
        return;
      }
      mTimelineWidget->onSelectedDayChanged(mSelectedDate);
      refreshQuickSlots();
      refreshDaySummary();
      return;
    }
  }

  if (pcm::app_settings::confirmEventDeletion()) {
    const auto reply =
        QMessageBox::question(this, tr(": EVENT_DELETE_TITLE"),
                              tr(": EVENT_DELETE_CONFIRMATION"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply != QMessageBox::Yes) {
      return;
    }
  }

  mTimelineWidget->removeEvent(eventId);
  mTimelineWidget->onSelectedDayChanged(mSelectedDate);
  refreshQuickSlots();
  refreshDaySummary();
}

void QEventInfoPage::editEventWithDialog(const int64_t eventId) {
  const auto event = mTimelineWidget ? mTimelineWidget->eventById(eventId) : std::nullopt;
  if (!event.has_value()) {
    return;
  }

  qCDebug(logEventInfo) << "Selected event with ID:" << eventId;

  std::optional<int64_t> clientId{std::nullopt};
  if (event->is_work_event) {
    if (event->series_id.has_value()) {
      const auto series = mTimelineWidget->eventSeriesById(*event->series_id);
      clientId = series ? series->client_id : std::nullopt;
    } else {
      emit provideClientByEventId(eventId);
      clientId = mClientId;
    }
  }

  openEventDialog(event, clientId);
}

void QEventInfoPage::onEventSaved(QEventItem *event) {
  if (!event)
    return;
  qCDebug(logEventInfo) << "Event saved with ID:" << event->getId();

  auto eventDetails = event->toEvent();
  const auto selectedClientId =
      mActiveEventDetailsWidget ? mActiveEventDetailsWidget->selectedClientId() : 0;
  const auto selectedClientName =
      mActiveEventDetailsWidget ? mActiveEventDetailsWidget->selectedClientName() : QString{};
  const auto rejectSave = [this]() {
    if (mActiveEventDetailsWidget) {
      mActiveEventDetailsWidget->rejectPendingSave();
    }
  };

  if (mActiveEventDetailsWidget && mActiveEventDetailsWidget->isCreatingNewEvent() &&
      mActiveEventDetailsWidget->isRecurring()) {
    QString publishTimezone;
    const bool publish = mSeriesCallService && mActiveEventDetailsWidget->wantsNewLiveKitSeries();
    if (publish) {
      publishTimezone = confirmNewSeriesTimezone();
      if (publishTimezone.isEmpty()) {
        rejectSave();
        return;
      }
    }
    const auto seriesId = mTimelineWidget->addEventSeries(
        eventDetails, selectedClientId, mActiveEventDetailsWidget->recurrenceRule(),
        mActiveEventDetailsWidget->recurrenceUntilMs(), publishTimezone);
    if (seriesId > 0 && publish) {
      mSeriesCallService->ensureInvitation(seriesId);
    }
    if (seriesId <= 0) {
      qCWarning(logEventInfo) << "Failed to persist event series in DB";
      rejectSave();
      return;
    }
    event->setId(seriesId);
  } else if (mActiveEventDetailsWidget && mActiveEventDetailsWidget->isCreatingNewEvent()) {
    const auto id = mTimelineWidget->addEvent(
        eventDetails, !pcm::app_settings::preventEventOverlaps());
    if (id <= 0) {
      qCWarning(logEventInfo) << "Failed to persist event in DB";
      rejectSave();
      return;
    }
    eventDetails.id = id;
    event->setId(id);
  } else if (eventDetails.is_virtual_occurrence) {
    const auto scope = eventDetails.series_id.has_value()
                           ? askRecurringEditScope(this)
                           : RecurringEditScope::SingleOccurrence;
    if (scope == RecurringEditScope::Cancel) {
      rejectSave();
      return;
    }
    if (scope == RecurringEditScope::WholeSeries) {
      if (!mTimelineWidget->updateEventSeries(
              eventDetails, *eventDetails.series_id, selectedClientId,
              mActiveEventDetailsWidget ? mActiveEventDetailsWidget->recurrenceRule()
                                        : QString{},
              mActiveEventDetailsWidget ? mActiveEventDetailsWidget->recurrenceUntilMs()
                                        : std::nullopt)) {
        qCWarning(logEventInfo) << "Failed to update recurring event series";
        rejectSave();
        return;
      }
    } else {
      eventDetails.id = -1;
      const auto id = mTimelineWidget->addEvent(
          eventDetails, !pcm::app_settings::preventEventOverlaps());
      if (id <= 0) {
        qCWarning(logEventInfo) << "Failed to materialize recurring event occurrence";
        rejectSave();
        return;
      }
      eventDetails.id = id;
      event->setId(id);
    }
  } else {
    mTimelineWidget->updateEvent(
        eventDetails, !pcm::app_settings::preventEventOverlaps());
  }

  if (eventDetails.id > 0) {
    emit provideClientEventPairSave(selectedClientId, eventDetails.id);
    event->setClientName(eventDetails.is_work_event ? selectedClientName : QString{});
  }

  // Force reload from DB to avoid stale UI state.
  mTimelineWidget->onSelectedDayChanged(mSelectedDate);
  refreshQuickSlots();
  refreshDaySummary();
}

void QEventInfoPage::onEditingCanceled() {}
void QEventInfoPage::onClientResolved(int64_t clientId) { mClientId = clientId; }

void QEventInfoPage::refreshAppearance() {
  if (!mTimelineWidget) {
    return;
  }

  mTimelineWidget->updateScene();
  mTimelineWidget->update();
  refreshCalendar();
  refreshQuickSlots();
  refreshDaySummary();
}

void QEventInfoPage::refreshQuickSlots() const {
  if (!mQuickSlotsWidget || !mTimelineWidget) {
    return;
  }
  mQuickSlotsWidget->setSelectedDate(mSelectedDate);
  mQuickSlotsWidget->setBusyIntervals(currentBusyIntervals());
}

void QEventInfoPage::refreshDaySummary() const {
  if (!mDaySummaryWidget || !mTimelineWidget) {
    return;
  }
  const auto nowMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
  const auto summary = pcm::recurrence::computeDaySummary(
      mTimelineWidget->events(), currentBusyIntervals(), pcm::app_settings::workDayStart(),
      pcm::app_settings::workDayEnd(), mSelectedDate, nowMs,
      pcm::app_settings::defaultSessionDurationMinutes());
  mDaySummaryWidget->setSummary(summary);
}

void QEventInfoPage::onDaySummaryEventHighlightRequested(const int64_t eventId) {
  if (mTimelineWidget) {
    mTimelineWidget->highlightEvent(eventId);
  }
}

QVector<QPair<QDateTime, QDateTime>> QEventInfoPage::currentBusyIntervals() const {
  QVector<QPair<QDateTime, QDateTime>> intervals;
  intervals.reserve(mTimelineWidget ? mTimelineWidget->events().size() : 0);

  for (const auto &event : mTimelineWidget->events()) {
    if (!event.start_date.has_value() || !event.end_date.has_value()) {
      continue;
    }

    const auto start =
        QDateTime::fromMSecsSinceEpoch(*event.start_date, QTimeZone::systemTimeZone());
    const auto end =
        QDateTime::fromMSecsSinceEpoch(*event.end_date, QTimeZone::systemTimeZone());
    if (!start.isValid() || !end.isValid() || start >= end) {
      continue;
    }

    const auto bufferedStart = start.addSecs(-event.buffer_before_minutes * 60);
    const auto bufferedEnd = end.addSecs(event.buffer_after_minutes * 60);
    intervals.append(qMakePair(bufferedStart, bufferedEnd));
  }

  return intervals;
}

std::optional<DuckEvent> QEventInfoPage::checkEventConflict(const DuckEvent &event) const {
  if (!pcm::app_settings::preventEventOverlaps() || !mTimelineWidget) {
    return std::nullopt;
  }
  return mTimelineWidget->findConflict(event);
}
