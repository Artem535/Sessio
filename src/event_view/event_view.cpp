#include "event_view.h"
#include "../widgets/app_settings.h"
#include "../widgets/constants.hpp"

#include <QContextMenuEvent>
#include <QMenu>
#include <QPainterPath>
#include <QRegion>
#include <QScrollBar>
#include <QSet>
#include <QTimer>

#include "qtimeline_model.h"
#include "schema.hpp"

Q_LOGGING_CATEGORY(logEventView, "pcm.EventView")

namespace {
constexpr int kHighlightDurationMs = 2500;
} // namespace

QEventView::QEventView(QWidget *parent)
    : QGraphicsView(parent), mModel(nullptr) {
  mScene = new QGraphicsScene(this);
  setScene(mScene);

  setFrameShape(QFrame::NoFrame);
  setObjectName("eventTimelineView");
  setRenderHint(QPainter::Antialiasing);
  setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  const auto borderCss =
      pcm::widgets::constants::kSurfaceBorderColor.name(QColor::HexArgb);
  const auto backgroundCss =
      pcm::widgets::constants::kSurfaceBackgroundColor.name(QColor::HexArgb);
  setStyleSheet(
      QString(
          "QGraphicsView#eventTimelineView {"
          " border: 1px solid %1;"
          " border-radius: 16px;"
          " background: %2;"
          "}"
          "QGraphicsView#eventTimelineView QWidget {"
          " border-radius: 16px;"
          "}")
          .arg(borderCss, backgroundCss));

  updateSceneSize();
}

void QEventView::setModel(QTimelineModel *model) {
  if (mModel) {
    disconnect(mModel, nullptr, this, nullptr);
  }

  mModel = model;

  connect(mModel, &QTimelineModel::rowsInserted, this,
          &QEventView::onRowsInserted);
  connect(mModel, &QTimelineModel::rowsRemoved, this,
          &QEventView::onRowsRemoved);
  connect(mModel, &QTimelineModel::dataChanged, this,
          &QEventView::onDataChanged);
  connect(mModel, &QTimelineModel::modelReset, this, &QEventView::onModelReset);
  connect(mModel, &QTimelineModel::eventsLoaded, this, [this] {
    mAutoScrollPending = true;
    QTimer::singleShot(0, this, &QEventView::scrollToDayStart);
  });
}

void QEventView::scrollToDayStart() {
  if (!isVisible() || viewport()->height() <= 0) {
    return; // applied from showEvent/resizeEvent once the view has a size
  }
  mAutoScrollPending = false;
  int firstMinute = 8 * 60;
  bool any = false;
  for (auto it = mSceneItems.cbegin(); it != mSceneItems.cend(); ++it) {
    const auto time = it.value()->getStartTime().time();
    const int minute = time.hour() * 60 + time.minute();
    if (!any || minute < firstMinute) {
      firstMinute = minute;
    }
    any = true;
  }
  auto *bar = verticalScrollBar();
  const int target = static_cast<int>(firstMinute * mPixelPerMin);
  const int viewTop = bar->value();
  const int margin = static_cast<int>(30 * mPixelPerMin);
  if (target >= viewTop && target + margin <= viewTop + viewport()->height()) {
    return; // already visible
  }
  bar->setValue(std::max(0, target - margin));
}

void QEventView::showEvent(QShowEvent *event) {
  QGraphicsView::showEvent(event);
  if (mAutoScrollPending) {
    QTimer::singleShot(0, this, &QEventView::scrollToDayStart);
  }
}

void QEventView::onRowsInserted(const QModelIndex &parent, int first,
                                int last) {
  Q_UNUSED(parent)

  for (int row = first; row <= last; ++row) {
    QModelIndex index = mModel->index(row, 0, parent);
    if (!index.isValid())
      continue;

    QVariant var = index.data(QTimelineModel::EventDataRole);
    if (!var.canConvert<DuckEvent>())
      continue;

    DuckEvent event = var.value<DuckEvent>();
    QEventItem *item = new QEventItem(event);

    connect(item, &QEventItem::itemSelected, this,
            &QEventView::onEventSelected);
    connect(item, &QEventItem::editRequested, this,
            &QEventView::onEventEditRequested);
    connect(item, &QEventItem::deleteRequested, this,
            &QEventView::onEventDeleteRequested);

    mScene->addItem(item);
    mSceneItems.insert(event.id, item);
  }

  updateScene();
  viewport()->update();
}

void QEventView::onRowsRemoved(const QModelIndex &parent, int first, int last) {
  Q_UNUSED(parent)
  Q_UNUSED(first)
  Q_UNUSED(last)
  if (!mModel) {
    return;
  }

  QSet<int64_t> existingIds;
  const int rowCount = mModel->rowCount(QModelIndex());
  for (int row = 0; row < rowCount; ++row) {
    const QModelIndex index = mModel->index(row, 0, QModelIndex());
    if (!index.isValid()) {
      continue;
    }
    existingIds.insert(index.data(QTimelineModel::IdRole).toLongLong());
  }

  for (auto it = mSceneItems.begin(); it != mSceneItems.end();) {
    if (!existingIds.contains(it.key())) {
      mScene->removeItem(it.value());
      delete it.value();
      it = mSceneItems.erase(it);
      continue;
    }
    ++it;
  }

  updateScene();
  viewport()->update();
}

void QEventView::onDataChanged(const QModelIndex &topLeft,
                               const QModelIndex &bottomRight,
                               const QList<int> &roles) {
  Q_UNUSED(roles)

  for (int row = topLeft.row(); row <= bottomRight.row(); ++row) {
    QModelIndex index = mModel->index(row, 0, topLeft.parent());
    if (!index.isValid())
      continue;

    QVariant idVar = index.data(QTimelineModel::IdRole);
    int64_t eventId = idVar.value<qint64>();

    auto it = mSceneItems.find(eventId);
    if (it != mSceneItems.end()) {
      DuckEvent updatedEvent =
          index.data(QTimelineModel::EventDataRole).value<DuckEvent>();
      it.value()->updateFromEvent(updatedEvent); // Method in QEventItem
    }
  }

  updateScene();
  viewport()->update();
}

void QEventView::onModelReset() {
  for (auto *item : mSceneItems) {
    mScene->removeItem(item);
    delete item;
  }
  mSceneItems.clear();

  const int rowCount = mModel->rowCount(QModelIndex());
  for (int row = 0; row < rowCount; ++row) {
    QModelIndex index = mModel->index(row, 0, QModelIndex());
    if (!index.isValid())
      continue;

    DuckEvent event =
        index.data(QTimelineModel::EventDataRole).value<DuckEvent>();
    QEventItem *item = new QEventItem(event);
    connect(item, &QEventItem::itemSelected, this,
            &QEventView::onEventSelected);
    connect(item, &QEventItem::editRequested, this,
            &QEventView::onEventEditRequested);
    connect(item, &QEventItem::deleteRequested, this,
            &QEventView::onEventDeleteRequested);
    mScene->addItem(item);
    mSceneItems.insert(event.id, item);
  }

  updateScene();
  viewport()->update();
}

void QEventView::clearHighlight() {
  if (!mHighlightedEventId.has_value()) {
    return;
  }
  if (auto *previous = mSceneItems.value(*mHighlightedEventId)) {
    previous->setHighlighted(false);
  }
  mHighlightedEventId = std::nullopt;
}

void QEventView::highlightEvent(const int64_t eventId) {
  clearHighlight();

  auto *item = mSceneItems.value(eventId);
  if (!item) {
    return;
  }

  item->setHighlighted(true);
  mHighlightedEventId = eventId;
  centerOn(item);

  QTimer::singleShot(kHighlightDurationMs, this, [this, eventId]() {
    if (mHighlightedEventId == eventId) {
      clearHighlight();
    }
  });
}

void QEventView::drawBackground(QPainter *painter, const QRectF &rect) {
  QGraphicsView::drawBackground(painter, rect);

  painter->save();
  constexpr qreal kCornerRadius = 16.0;
  const auto viewportSceneRect = mapToScene(viewport()->rect()).boundingRect();
  QPainterPath clipPath;
  clipPath.addRoundedRect(viewportSceneRect, kCornerRadius, kCornerRadius);
  painter->setClipPath(clipPath);

  // DRAW HORIZONTAL AXIS
  {
    painter->setPen(QPen(Qt::gray, 1.0, Qt::DashLine));
    // Draw vertical line that is centered in the viewport
    // TODO: It needed the rect.x()?
    qreal xMid = rect.x() + rect.width() / 2;
    xMid += pcm::widgets::constants::kWidthLabel / 2.0;
    painter->drawLine(QLineF{xMid, rect.top(), xMid, rect.bottom()});
  }

  // Draw separator line
  {
    painter->setPen(QPen(Qt::lightGray, 1.0, Qt::SolidLine));
    constexpr int xCord = pcm::widgets::constants::kWidthLabel;
    painter->drawLine(QLineF(xCord, 0, xCord, rect.bottom()));
  }

  // DRAW TIME AXIS
  {
    constexpr int startHour = 0;
    constexpr int endHour = 24;

    QFont font = painter->font();
    font.setPointSize(8);
    painter->setFont(font);

    for (int hour = startHour; hour < endHour; ++hour) {
      constexpr int stepMinutes = 60;
      const int totalMinutes = hour * stepMinutes;
      const qreal yPos = totalMinutes * mPixelPerMin;

      // Draw horizontal line
      painter->drawLine(QLineF(rect.left(), yPos, rect.right(), yPos));

      // Label for time: HH:00
      auto timeLabel = QString("%1:00");
      // FileWidth - 2 char, filled with 0.
      timeLabel = timeLabel.arg(hour, 2, 10, QChar('0'));

      constexpr int widthLabel = pcm::widgets::constants::kWidthLabel;
      QRectF labelRect(rect.left(), yPos, widthLabel, 20);

      painter->drawText(labelRect, Qt::AlignHCenter | Qt::AlignVCenter,
                        timeLabel);
    }
  }

  painter->restore();
}

void QEventView::updateSceneSize() {
  const QSize viewportSize = viewport()->size();

  const auto sceneHeight =
      qMax(viewportSize.height(), pcm::widgets::constants::kMinTimeAxisHeight);

  mPixelPerMin =
      static_cast<qreal>(sceneHeight) / pcm::widgets::constants::kMinInDay;
  mPixelPerMin = qBound(0.5, mPixelPerMin, 5.0);

  scene()->setSceneRect(0, 0, viewportSize.width(), sceneHeight);
}

void QEventView::onEventSelected() {
  auto *item = qobject_cast<QEventItem *>(sender());
  if (item != nullptr) {
    const auto eventId = static_cast<int64_t>(item->getId());
    qCInfo(logEventView) << "EventDataManager::onEventSelected| "
                         << eventId;
    QTimer::singleShot(0, this, [this, eventId]() {
      emit eventSelected(eventId);
    });
  }
}

void QEventView::onEventEditRequested() {
  auto *item = qobject_cast<QEventItem *>(sender());
  if (item != nullptr) {
    const auto eventId = static_cast<int64_t>(item->getId());
    QTimer::singleShot(0, this, [this, eventId]() {
      emit eventEditRequested(eventId);
    });
  }
}

void QEventView::onEventDeleteRequested() {
  auto *item = qobject_cast<QEventItem *>(sender());
  if (item != nullptr) {
    emit eventDeleteRequested(item->getId());
  }
}

void QEventView::contextMenuEvent(QContextMenuEvent *event) {
  if (!event) {
    return;
  }
  if (itemAt(event->pos()) != nullptr) {
    QGraphicsView::contextMenuEvent(event);
    return;
  }

  showCreateEventMenu(event->pos(), event->globalPos());
  event->accept();
}

void QEventView::showCreateEventMenu(const QPoint &viewportPos, const QPoint &globalPos) {
  const auto scenePos = mapToScene(viewportPos);
  constexpr int kSnapMinutes = 5;
  const auto rawMinutes =
      static_cast<int>(std::round(scenePos.y() / std::max<qreal>(mPixelPerMin, 0.1)));
  const auto snappedMinutes =
      qBound(0, ((rawMinutes + kSnapMinutes / 2) / kSnapMinutes) * kSnapMinutes,
             pcm::widgets::constants::kMinInDay - kSnapMinutes);
  const QTime startTime(snappedMinutes / 60, snappedMinutes % 60);
  const auto durationMinutes = pcm::app_settings::defaultSessionDurationMinutes();

  QMenu menu;
  auto *createAction =
      menu.addAction(tr("Create event at %1").arg(startTime.toString("HH:mm")));
  connect(createAction, &QAction::triggered, this, [this, startTime, durationMinutes]() {
    emit createEventRequested(startTime, durationMinutes);
  });
  menu.exec(globalPos);
}

void QEventView::updateItemsSize() const {
  const QSize viewportSize = viewport()->size();

  for (QGraphicsItem *item : scene()->items()) {
    if (const auto eventItem = dynamic_cast<QEventItem *>(item);
        eventItem != nullptr) {
      // We need to update height of eventItem,
      // it must be equal the duration of the event
      const auto duration = eventItem->getDuration();
      const auto height = static_cast<int>(round(duration * mPixelPerMin));

      eventItem->updateSize({viewportSize.width() / 2, height});
      eventItem->update();
    }
  }
}

void QEventView::updateItemsCords() const {
  for (QGraphicsItem *item : scene()->items()) {
    if (const auto eventItem = dynamic_cast<QEventItem *>(item);
        eventItem != nullptr) {
      const auto datetimePoint = eventItem->getStartTime();

      if (datetimePoint.timeSpec() != Qt::LocalTime)
        qCWarning(logEventView) << "EventItem has non-local time!";

      const auto timePoint = datetimePoint.time();
      const auto countMin = timePoint.minute() + timePoint.hour() * 60;
      const auto yPos = countMin * mPixelPerMin;

      QPointF newPos{0, yPos};

      eventItem->setPos(newPos);
      eventItem->update();
    }
  }
}

void QEventView::resizeEvent(QResizeEvent *event) {
  QGraphicsView::resizeEvent(event);
  constexpr int kCornerRadius = 16;
  QPainterPath clipPath;
  clipPath.addRoundedRect(rect(), kCornerRadius, kCornerRadius);
  setMask(QRegion(clipPath.toFillPolygon().toPolygon()));
  updateScene();
  if (mAutoScrollPending) {
    scrollToDayStart();
  }
}

void QEventView::updateScene() {
  updateSceneSize();
  updateItemsSize();
  updateItemsCords();
}
