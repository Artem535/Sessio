// src/timeline_widget/timeline_widget.h
#pragma once

#include <QLoggingCategory>
#include <QObject>
#include <QVBoxLayout>
#include <QWidget>
#include <QDate>
#include <QPointer>
#include <QString>
#include <optional>

#include "event_item.h"
#include "database.h"
#include "event_view.h"
#include "qtimeline_model.h"


class QTimelineWidget final : public QWidget {
    Q_OBJECT

public:
  QTimelineWidget(QTimelineModel *model, QWidget *parent);
  ~QTimelineWidget() override;

public slots:
    void onSelectedDayChanged(const QDate &date) const;
    void highlightEvent(int64_t eventId) const;

    [[nodiscard]] int64_t addEvent(const DuckEvent &event, bool allowOverlap = true) const;
    [[nodiscard]] int64_t addEventSeries(const DuckEvent &event, int64_t clientId,
                                         const QString &recurrenceRule,
                                         std::optional<int64_t> recurrenceUntilMs,
                                         const QString &publishTimezone = {}) const;
    [[nodiscard]] bool updateEventSeries(const DuckEvent &event, int64_t seriesId,
                                         int64_t clientId,
                                         const QString &recurrenceRule,
                                         std::optional<int64_t> recurrenceUntilMs) const;
    [[nodiscard]] bool deactivateEventSeries(int64_t seriesId) const;
    [[nodiscard]] bool removeFutureEventSeriesOccurrences(
        int64_t seriesId, int64_t occurrenceStartMs) const;

    void updateScene();

    void updateEvent(const DuckEvent &event, bool allowOverlap = true) const;
    void removeEvent(int64_t id) const;
    [[nodiscard]] bool hasConflict(const DuckEvent &event) const;
    [[nodiscard]] std::optional<DuckEvent> findConflict(const DuckEvent &event) const;
    const QVector<DuckEvent> &events() const;
    std::optional<DuckEvent> eventById(int64_t eventId) const;
    std::optional<DuckEventSeries> eventSeriesById(int64_t seriesId) const;

signals:
    void eventSelected(int64_t eventId);
    void eventEditRequested(int64_t eventId);
    void eventDeleteRequested(int64_t eventId);
    void createEventRequested(const QTime &startTime, int durationMinutes);

    void needSceneUpdate();

private:
    QVBoxLayout *mLayout = nullptr;
    QEventView *mEventView = nullptr;
    QTimelineModel *mModel = nullptr;
};
