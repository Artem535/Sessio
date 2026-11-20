// src/timeline_widget/timeline_widget.cpp
#include "timeline_widget.h"
#include "qtimeline_model.h"

#include <QTimer>

Q_LOGGING_CATEGORY(logTimelineWidget, "pcm.timeline")

QTimelineWidget::QTimelineWidget(QTimelineModel *model, QWidget *parent)
    : QWidget(parent), mModel(model) {
  mLayout = new QVBoxLayout(this);
  mLayout->setContentsMargins(0, 0, 0, 0);
  mLayout->setSpacing(0);
  setLayout(mLayout);

  mEventView = new QEventView(this);
  mEventView->setModel(mModel);

  mLayout->addWidget(mEventView);

  connect(mModel, &QTimelineModel::eventsLoaded, this,
          &QTimelineWidget::updateScene);
  connect(mEventView, &QEventView::eventSelected, this,
          &QTimelineWidget::eventSelected);
  connect(mEventView, &QEventView::eventEditRequested, this,
          &QTimelineWidget::eventEditRequested);
  connect(mEventView, &QEventView::eventDeleteRequested, this,
          &QTimelineWidget::eventDeleteRequested);
  connect(mEventView, &QEventView::createEventRequested, this,
          [this](const QTime &startTime, const int durationMinutes) {
            QTimer::singleShot(0, this, [this, startTime, durationMinutes]() {
              emit createEventRequested(startTime, durationMinutes);
            });
          });
  // Defaults
  mModel->loadEventsForDay(QDate::currentDate());

  qCDebug(logTimelineWidget)
      << "QTimelineWidget initialized with QTimelineModel";
}

QTimelineWidget::~QTimelineWidget() = default;

void QTimelineWidget::onSelectedDayChanged(const QDate &date) const {
  qCDebug(logTimelineWidget)
      << "QTimelineWidget::onSelectedDayChanged | Date:" << date;

  mModel->loadEventsForDay(date);
}

int64_t QTimelineWidget::addEvent(const DuckEvent &event,
                                  const bool allowOverlap) const {
  if (!mModel) {
    qCWarning(logTimelineWidget) << "QTimelineWidget::addEvent | Model is null";
    return 0;
  }

  qCDebug(logTimelineWidget) << "QTimelineWidget::addEvent | Adding event:"
                             << QString::fromStdString(event.name.value_or(""));

  return mModel->addEvent(event, allowOverlap);
}

int64_t QTimelineWidget::addEventSeries(const DuckEvent &event,
                                        const int64_t clientId,
                                        const QString &recurrenceRule,
                                        const std::optional<int64_t> recurrenceUntilMs,
                                        const QString &publishTimezone) const {
  if (!mModel) {
    qCWarning(logTimelineWidget) << "QTimelineWidget::addEventSeries | Model is null";
    return 0;
  }

  return mModel->addEventSeries(event, clientId, recurrenceRule, recurrenceUntilMs,
                                publishTimezone);
}

bool QTimelineWidget::updateEventSeries(
    const DuckEvent &event, const int64_t seriesId, const int64_t clientId,
    const QString &recurrenceRule,
    const std::optional<int64_t> recurrenceUntilMs) const {
  return mModel && mModel->updateEventSeries(event, seriesId, clientId,
                                             recurrenceRule, recurrenceUntilMs);
}

bool QTimelineWidget::deactivateEventSeries(const int64_t seriesId) const {
  return mModel && mModel->deactivateEventSeries(seriesId);
}

bool QTimelineWidget::removeFutureEventSeriesOccurrences(
    const int64_t seriesId, const int64_t occurrenceStartMs) const {
  return mModel && mModel->removeFutureEventSeriesOccurrences(seriesId,
                                                              occurrenceStartMs);
}

void QTimelineWidget::updateScene() { emit needSceneUpdate(); }

void QTimelineWidget::updateEvent(const DuckEvent &event,
                                  const bool allowOverlap) const {
  if (!mModel)
    return;
  mModel->updateEvent(event, allowOverlap);
}

void QTimelineWidget::removeEvent(const int64_t id) const {
  if (!mModel)
    return;
  mModel->removeEvent(id);
}

bool QTimelineWidget::hasConflict(const DuckEvent &event) const {
  return mModel && mModel->hasConflict(event);
}

std::optional<DuckEvent> QTimelineWidget::findConflict(const DuckEvent &event) const {
  if (!mModel) {
    return std::nullopt;
  }
  return mModel->findConflict(event);
}

void QTimelineWidget::highlightEvent(const int64_t eventId) const {
  if (mEventView) {
    mEventView->highlightEvent(eventId);
  }
}

const QVector<DuckEvent> &QTimelineWidget::events() const {
  static const QVector<DuckEvent> emptyEvents;
  return mModel ? mModel->events() : emptyEvents;
}

std::optional<DuckEvent> QTimelineWidget::eventById(const int64_t eventId) const {
  if (!mModel) {
    return std::nullopt;
  }

  const auto index = mModel->indexForEventId(eventId);
  if (!index.isValid()) {
    return std::nullopt;
  }

  const auto data = index.data(QTimelineModel::EventDataRole);
  if (!data.canConvert<DuckEvent>()) {
    return std::nullopt;
  }

  return data.value<DuckEvent>();
}

std::optional<DuckEventSeries> QTimelineWidget::eventSeriesById(
    const int64_t seriesId) const {
  return mModel ? mModel->eventSeriesById(seriesId) : std::nullopt;
}
