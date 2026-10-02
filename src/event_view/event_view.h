#pragma once

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLoggingCategory>
#include <QPainter>
#include <QTime>
#include <QVector>
#include <QWidget>

#include <memory>
#include <optional>

#include "constants.hpp"
#include "event_item.h"
#include "qtimeline_model.h"

class QEventView final : public QGraphicsView {
  Q_OBJECT

public:
  explicit QEventView(QWidget *parent = nullptr);
  void setModel(QTimelineModel *model);

signals:
  void eventSelected(int64_t eventId);
  void eventEditRequested(int64_t eventId);
  void eventDeleteRequested(int64_t eventId);
  void createEventRequested(const QTime &startTime, int durationMinutes);

public slots:
  void updateScene();
  void onRowsInserted(const QModelIndex &parent, int first, int last);
  void onRowsRemoved(const QModelIndex &parent, int first, int last);
  void onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight,
                     const QList<int> &roles);
  void onModelReset();
  void highlightEvent(int64_t eventId);
  // Scroll so the first meeting of the loaded day (or 08:00 when there is none)
  // is visible; a no-op when it already is.
  void scrollToDayStart();

protected:
  void contextMenuEvent(QContextMenuEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void showEvent(QShowEvent *event) override;

private slots:
  void onEventSelected();
  void onEventEditRequested();
  void onEventDeleteRequested();

private:
  QTimelineModel *mModel{};
  QGraphicsScene *mScene;
  int64_t mSelectedDay = -1;
  std::optional<int64_t> mHighlightedEventId;
  qreal mPixelPerMin = pcm::widgets::constants::kPixelPerMin;
  QMap<int64_t, QEventItem *> mSceneItems;
  bool mAutoScrollPending = false;

  void drawBackground(QPainter *painter, const QRectF &rect) override;
  void showCreateEventMenu(const QPoint &viewportPos, const QPoint &globalPos);
  void updateSceneSize();
  void updateItemsSize() const;
  void updateItemsCords() const;
  void clearHighlight();
};
