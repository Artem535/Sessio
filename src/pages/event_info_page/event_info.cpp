#include "event_info.h"
#include "../../widgets/constants.hpp"
#include "../../widgets/app_settings.h"
#include "ui/pages/ui_eventinfo.h"

#include <QDialog>
#include <QIcon>
#include <QMessageBox>
#include <QSize>
#include <QTextCharFormat>
#include <QTimeZone>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(logEventInfo, "pcm.EventInfo")

namespace {
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

RecurringDeleteScope askRecurringDeleteScope(QWidget *parent) {
  QMessageBox messageBox(parent);
  messageBox.setWindowTitle(QObject::tr("Recurring event"));
  messageBox.setText(QObject::tr("What do you want to delete?"));
  const auto singleButton =
      messageBox.addButton(QObject::tr("Only this event"), QMessageBox::AcceptRole);
  const auto futureButton =
      messageBox.addButton(QObject::tr("This and future events"), QMessageBox::ActionRole);
  const auto seriesButton =
      messageBox.addButton(QObject::tr("Whole series"), QMessageBox::DestructiveRole);
  messageBox.addButton(QMessageBox::Cancel);
  messageBox.setDefaultButton(singleButton);
  messageBox.exec();

  if (messageBox.clickedButton() == singleButton) {
    return RecurringDeleteScope::SingleOccurrence;
  }
  if (messageBox.clickedButton() == futureButton) {
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
      mMeetingCoordinator(meetingCoordinator) {
  mUi->setupUi(this);
  mUi->list_view_layout->setColumnStretch(0, 1);
  mUi->list_view_layout->setColumnStretch(1, 0);

  mSelectedDate = QDate::currentDate();
  mCalendarWidget = new RoundedCalendarWidget(this);
  mCalendarWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  mUi->calendarCardLayout->replaceWidget(mUi->calendar_widget, mCalendarWidget);
  mUi->calendar_widget->hide();
  mUi->calendar_widget->deleteLater();

  mDaySummaryWidget = new DaySummaryWidget(this);
  mUi->verticalLayout->addWidget(mDaySummaryWidget);
  mUi->verticalLayout->addStretch(1);

  mQuickSlotsWidget = new QuickSlotsWidget(this);
  mUi->verticalLayout->addWidget(mQuickSlotsWidget);

  mTimelineWidget = new QTimelineWidget(model, this);
  mUi->list_view_layout->addWidget(mTimelineWidget, 0, 0, 2, 1);

  mCreateEventButton = new QPushButton(this);
  mCreateEventButton->setIcon(QIcon(":/icons/calendar-plus-solid-full.svg"));
  mCreateEventButton->setToolTip(tr(": EVENT_ADD_BUTTON"));
  mCreateEventButton->setIconSize(QSize(18, 18));
  mCreateEventButton->setFixedSize(40, 40);
  mCreateEventButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  mUi->list_view_layout->addWidget(mCreateEventButton, 0, 1,
                                   Qt::AlignTop | Qt::AlignRight);
  mUi->frame_2->setVisible(false);

  connectSignals();
  initDefaultStates();
}

QEventInfoPage::~QEventInfoPage() = default;

void QEventInfoPage::connectSignals() {
  // CalendarWidget -> TimelineWidget
  connect(mCalendarWidget, &RoundedCalendarWidget::clicked, mTimelineWidget,
          &QTimelineWidget::onSelectedDayChanged);
  connect(mCalendarWidget, &RoundedCalendarWidget::clicked, this,
          &QEventInfoPage::onCalendarClicked);
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
  mCalendarWidget->setSelectedDate(mSelectedDate);
  updateCalendarHighlights();
  mTimelineWidget->onSelectedDayChanged(mSelectedDate);
  refreshQuickSlots();
  refreshDaySummary();
}

void QEventInfoPage::onCalendarClicked(const QDate &date) {
  mSelectedDate = date;
  refreshQuickSlots();
  refreshDaySummary();
}

void QEventInfoPage::openEventOnDay(const int64_t eventId, const qint64 dayMs) {
  const auto date =
      QDateTime::fromMSecsSinceEpoch(dayMs, QTimeZone::systemTimeZone()).date();
  mCalendarWidget->setSelectedDate(date);
  mTimelineWidget->onSelectedDayChanged(date);
  onCalendarClicked(date);
  editEventWithDialog(eventId);
}

void QEventInfoPage::updateCalendarHighlights() const {
  QTextCharFormat currentDayFormat;
  currentDayFormat.setFontWeight(QFont::DemiBold);
  currentDayFormat.setUnderlineStyle(QTextCharFormat::SingleUnderline);
  currentDayFormat.setUnderlineColor(
      pcm::widgets::constants::kCalendarCurrentDayUnderlineColor);
  currentDayFormat.setForeground(
      pcm::widgets::constants::kCalendarCurrentDayForegroundColor);

  mCalendarWidget->setDateTextFormat(QDate::currentDate(), currentDayFormat);
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
  editEventWithDialog(eventId);
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
    const auto scope = askRecurringDeleteScope(this);
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
    const auto seriesId = mTimelineWidget->addEventSeries(
        eventDetails, selectedClientId, mActiveEventDetailsWidget->recurrenceRule(),
        mActiveEventDetailsWidget->recurrenceUntilMs());
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
