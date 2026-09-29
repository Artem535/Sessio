#include "event_item.h"
#include "../widgets/app_settings.h"
#include "../widgets/meeting_utils.h"
#include "../widgets/constants.hpp"
#include <QIcon>
#include <QLocale>
#include <QMenu>
#include <QTimeZone>
#include <vector>

Q_LOGGING_CATEGORY(logPcmEventItem, "pcm.EventItem")

namespace {
void setBufferMinutes(const DuckEvent &event, std::int64_t &beforeMinutes,
                      std::int64_t &afterMinutes) {
  beforeMinutes = event.buffer_before_minutes;
  afterMinutes = event.buffer_after_minutes;
}

void setProviderFields(const DuckEvent &event,
                       std::optional<pcm::meeting::ProviderKind> &providerKind,
                       QString &meetingRef, std::optional<QString> &invitationState) {
  providerKind = event.provider_kind.has_value()
                     ? pcm::meeting::providerKindFromString(*event.provider_kind)
                     : std::nullopt;
  meetingRef = QString::fromStdString(event.meeting_ref.value_or(""));
  invitationState = event.invitation_state.has_value()
                         ? std::make_optional(QString::fromStdString(*event.invitation_state))
                         : std::nullopt;
}
} // namespace

namespace {
constexpr int64_t kPaymentPendingId = 1;
constexpr int64_t kPaymentPaidId = 2;
constexpr int64_t kPaymentCanceledId = 3;
constexpr int64_t kPaymentRefundedId = 4;
constexpr int64_t kPaymentSkippedId = 5;
constexpr int64_t kEventScheduledId = 1;

QString formatEventCost(const std::optional<double>& cost) {
  if (!cost.has_value()) {
    return {};
  }

  return QLocale(QLocale::Russian).toString(*cost, 'f', 0) + QStringLiteral(" ") +
         pcm::app_settings::currencySymbol();
}

QString paymentStatusLabel(const int64_t paymentStatusId) {
  switch (paymentStatusId) {
  case kPaymentPaidId:
    return QEventItem::tr("Paid");
  case kPaymentCanceledId:
    return QEventItem::tr("Canceled");
  case kPaymentRefundedId:
    return QEventItem::tr("Refunded");
  case kPaymentSkippedId:
    return QEventItem::tr("Skipped");
  case kPaymentPendingId:
  default:
    return QEventItem::tr("Pending");
  }
}

QString eventStatusLabel(const int64_t eventStatusId) {
  switch (eventStatusId) {
  case 2:
    return QEventItem::tr("Completed");
  case 3:
    return QEventItem::tr("Canceled");
  case 4:
    return QEventItem::tr("Confirmed");
  case 5:
    return QEventItem::tr("No show");
  case 6:
    return QEventItem::tr("Rescheduled");
  case 1:
  default:
    return QEventItem::tr("Scheduled");
  }
}
} // namespace

QEventItem::QEventItem(const int64_t id, const QString &title,
                       const QDateTime &startTime, const QDateTime &endTime,
                       const bool isWorkItem)
    : mIsWorkItem(isWorkItem), mSize(100, 100), mTitle(title),
      mStartTime(startTime), mEndTime(endTime), mId(id) {
  qCInfo(logPcmEventItem) << "QEventItem::QEventItem"
                          << "Created event:" << title << "("
                          << startTime.toString() << "-" << endTime.toString()
                          << ")";

  setFlag(QGraphicsItem::ItemIsMovable);
  setFlag(QGraphicsItem::ItemIsSelectable);
  setFlag(QGraphicsItem::ItemSendsGeometryChanges);

  updateDuration();
}

void QEventItem::updateFromEvent(const DuckEvent &event) {
  mIsWorkItem = event.is_work_event;
  mSize = QSize(100, 100);

  mTitle = QString::fromStdString(event.name.value_or(""));
  mClientName = QString::fromStdString(event.client_name.value_or(""));
  mCost = event.cost;
  mEventStatusId = event.event_stat_id > 0 ? event.event_stat_id : kEventScheduledId;
  mPaymentStatusId = event.payment_stat_id > 0 ? event.payment_stat_id : kPaymentPendingId;
  mCancellationReason = QString::fromStdString(event.cancellation_reason.value_or(""));
  mCanceledBy = QString::fromStdString(event.canceled_by.value_or(""));
  mIsOnline = event.is_online;
  mMeetingUrl = QString::fromStdString(event.meeting_url);
  setProviderFields(event, mProviderKind, mMeetingRef, mInvitationState);
  mSeriesId = event.series_id;
  mOriginalOccurrenceStart = event.original_occurrence_start;
  mIsVirtualOccurrence = event.is_virtual_occurrence;
  setBufferMinutes(event, mBufferBeforeMinutes, mBufferAfterMinutes);
  const auto startUtc =
      QDateTime::fromMSecsSinceEpoch(event.start_date.value_or(0), QTimeZone::UTC);
  const auto endUtc =
      QDateTime::fromMSecsSinceEpoch(event.end_date.value_or(0), QTimeZone::UTC);
  mStartTime = startUtc.toLocalTime();
  mEndTime = endUtc.toLocalTime();
  mId = event.id;

  qCInfo(logPcmEventItem) << "QEventItem::QEventItem"
                          << "Created event:" << mTitle << "("
                          << mStartTime.toString() << "-" << mEndTime.toString()
                          << ")";

  setFlag(QGraphicsItem::ItemIsMovable);
  setFlag(QGraphicsItem::ItemIsSelectable);
  setFlag(QGraphicsItem::ItemSendsGeometryChanges);

  updateDuration();
  update();
}

QEventItem::QEventItem(const DuckEvent &event) {
  mIsWorkItem = event.is_work_event;
  mSize = QSize(100, 100);
  mTitle = QString::fromStdString(event.name.value_or("Undefined"));
  mClientName = QString::fromStdString(event.client_name.value_or(""));
  mCost = event.cost;
  mEventStatusId = event.event_stat_id > 0 ? event.event_stat_id : kEventScheduledId;
  mPaymentStatusId = event.payment_stat_id > 0 ? event.payment_stat_id : kPaymentPendingId;
  mCancellationReason = QString::fromStdString(event.cancellation_reason.value_or(""));
  mCanceledBy = QString::fromStdString(event.canceled_by.value_or(""));
  mIsOnline = event.is_online;
  mMeetingUrl = QString::fromStdString(event.meeting_url);
  setProviderFields(event, mProviderKind, mMeetingRef, mInvitationState);
  mSeriesId = event.series_id;
  mOriginalOccurrenceStart = event.original_occurrence_start;
  mIsVirtualOccurrence = event.is_virtual_occurrence;
  setBufferMinutes(event, mBufferBeforeMinutes, mBufferAfterMinutes);
  const auto startUtc =
      QDateTime::fromMSecsSinceEpoch(event.start_date.value_or(0), QTimeZone::UTC);
  const auto endUtc =
      QDateTime::fromMSecsSinceEpoch(event.end_date.value_or(0), QTimeZone::UTC);
  mStartTime = startUtc.toLocalTime();
  mEndTime = endUtc.toLocalTime();
  mId = event.id;

  qCInfo(logPcmEventItem) << "QEventItem::QEventItem"
                          << "Created event:" << mTitle << "("
                          << mStartTime.toString() << "-" << mEndTime.toString()
                          << ")";

  setFlag(QGraphicsItem::ItemIsMovable);
  setFlag(QGraphicsItem::ItemIsSelectable);
  setFlag(QGraphicsItem::ItemSendsGeometryChanges);

  updateDuration();
}

DuckEvent QEventItem::toEvent() const {
  DuckEvent event;
  event.id = mId;
  event.name = mTitle.toStdString();
  event.is_work_event = mIsWorkItem;
  event.event_stat_id = mEventStatusId;
  event.start_date = mStartTime.toUTC().toMSecsSinceEpoch();
  event.end_date = mEndTime.toUTC().toMSecsSinceEpoch();
  event.duration = static_cast<int64_t>(mDuration) * 60;
  event.cost = mCost;
  event.payment_stat_id = mIsWorkItem ? mPaymentStatusId : kPaymentSkippedId;
  if (!mCancellationReason.isEmpty()) {
    event.cancellation_reason = mCancellationReason.toStdString();
  }
  if (!mCanceledBy.isEmpty()) {
    event.canceled_by = mCanceledBy.toStdString();
  }
  event.is_online = mIsOnline;
  event.meeting_url = mMeetingUrl.trimmed().toStdString();
  event.provider_kind = mProviderKind.has_value()
                             ? std::make_optional(pcm::meeting::providerKindToString(*mProviderKind))
                             : std::nullopt;
  event.meeting_ref =
      mMeetingRef.trimmed().isEmpty() ? std::nullopt : std::make_optional(mMeetingRef.trimmed().toStdString());
  event.invitation_state = mInvitationState.has_value()
                                ? std::make_optional(mInvitationState->toStdString())
                                : std::nullopt;
  event.series_id = mSeriesId;
  event.original_occurrence_start = mOriginalOccurrenceStart;
  event.is_virtual_occurrence = mIsVirtualOccurrence;
  event.buffer_before_minutes = mBufferBeforeMinutes;
  event.buffer_after_minutes = mBufferAfterMinutes;
  return event;
}

QRectF QEventItem::boundingRect() const {
  constexpr qreal penWidth = 1.0;
  constexpr int labelWidth = pcm::widgets::constants::kWidthLabel;

  auto xOffset = mIsWorkItem ? 0 : mSize.width();
  xOffset += labelWidth;

  return {xOffset + penWidth, penWidth, mSize.width() + penWidth,
          mSize.height() + penWidth};
}

void QEventItem::updateSize(const QSize &newSize) {
  if (mSize == newSize)
    return;
  mSize = newSize;

  constexpr int offset = pcm::widgets::constants::kWidthLabel / 2;
  mSize.setWidth(mSize.width() - offset);

  emit prepareGeometryChange();
  update();
}

QSize QEventItem::getSize() const { return mSize; }
QDateTime QEventItem::getStartTime() const { return mStartTime; }
QDateTime QEventItem::getEndTime() const { return mEndTime; }
QString QEventItem::getTitle() const { return mTitle; }
QString QEventItem::getClientName() const { return mClientName; }
std::optional<double> QEventItem::cost() const { return mCost; }
int64_t QEventItem::eventStatusId() const { return mEventStatusId; }
int64_t QEventItem::paymentStatusId() const { return mPaymentStatusId; }
QString QEventItem::cancellationReason() const { return mCancellationReason; }
QString QEventItem::canceledBy() const { return mCanceledBy; }
bool QEventItem::isOnline() const { return mIsOnline; }
QString QEventItem::meetingUrl() const { return mMeetingUrl; }
std::optional<pcm::meeting::ProviderKind> QEventItem::providerKind() const { return mProviderKind; }
QString QEventItem::meetingRef() const { return mMeetingRef; }
std::optional<QString> QEventItem::invitationState() const { return mInvitationState; }
int64_t QEventItem::getId() const { return mId; }
bool QEventItem::isWorkItem() const { return mIsWorkItem; };

void QEventItem::setTitle(const QString &title) {
  if (mTitle == title)
    return;
  mTitle = title;
  update();
}

void QEventItem::setClientName(const QString &clientName) {
  if (mClientName == clientName)
    return;
  mClientName = clientName;
  update();
}

void QEventItem::setCost(std::optional<double> cost) {
  if (mCost == cost)
    return;
  mCost = std::move(cost);
  update();
}

void QEventItem::setEventStatusId(const int64_t eventStatusId) {
  const auto normalizedId = eventStatusId > 0 ? eventStatusId : kEventScheduledId;
  if (mEventStatusId == normalizedId) {
    return;
  }
  mEventStatusId = normalizedId;
  update();
}

void QEventItem::setPaymentStatusId(const int64_t paymentStatusId) {
  const auto normalizedId = paymentStatusId > 0 ? paymentStatusId : kPaymentPendingId;
  if (mPaymentStatusId == normalizedId) {
    return;
  }
  mPaymentStatusId = normalizedId;
  update();
}

void QEventItem::setCancellationReason(const QString &reason) {
  const auto normalizedReason = reason.trimmed();
  if (mCancellationReason == normalizedReason) {
    return;
  }
  mCancellationReason = normalizedReason;
  update();
}

void QEventItem::setCanceledBy(const QString &canceledBy) {
  const auto normalizedCanceledBy = canceledBy.trimmed();
  if (mCanceledBy == normalizedCanceledBy) {
    return;
  }
  mCanceledBy = normalizedCanceledBy;
  update();
}

void QEventItem::setOnline(const bool online) {
  if (mIsOnline == online)
    return;
  mIsOnline = online;
  update();
}

void QEventItem::setHighlighted(const bool highlighted) {
  if (mIsHighlighted == highlighted) {
    return;
  }
  mIsHighlighted = highlighted;
  update();
}

void QEventItem::setMeetingUrl(const QString &meetingUrl) {
  const auto normalizedUrl = meetingUrl.trimmed();
  if (mMeetingUrl == normalizedUrl)
    return;
  mMeetingUrl = normalizedUrl;
  update();
}

void QEventItem::setProviderKind(std::optional<pcm::meeting::ProviderKind> kind) {
  if (mProviderKind == kind)
    return;
  mProviderKind = kind;
  update();
}

void QEventItem::setMeetingRef(const QString &meetingRef) {
  const auto normalizedRef = meetingRef.trimmed();
  if (mMeetingRef == normalizedRef)
    return;
  mMeetingRef = normalizedRef;
  update();
}

void QEventItem::setInvitationState(std::optional<QString> state) {
  if (mInvitationState == state)
    return;
  mInvitationState = std::move(state);
  update();
}

int64_t QEventItem::bufferBeforeMinutes() const { return mBufferBeforeMinutes; }
int64_t QEventItem::bufferAfterMinutes() const { return mBufferAfterMinutes; }

void QEventItem::setBufferBeforeMinutes(const int64_t minutes) {
  mBufferBeforeMinutes = std::max<int64_t>(0, minutes);
}

void QEventItem::setBufferAfterMinutes(const int64_t minutes) {
  mBufferAfterMinutes = std::max<int64_t>(0, minutes);
}

void QEventItem::setStartTime(const QDateTime &startTime) {
  if (mStartTime == startTime)
    return;

  if (startTime > mEndTime) {
    qCWarning(logPcmEventItem) << "QEventItem::setStartTime"
                               << "Invalid start time:" << startTime;
    return;
  }

  mStartTime = startTime;
  updateDuration();
  update();
}

void QEventItem::setEndTime(const QDateTime &endTime) {
  if (mEndTime == endTime)
    return;

  if (endTime < mStartTime) {
    qCWarning(logPcmEventItem) << "QEventItem::setEndTime"
                               << "Invalid end time:" << endTime;
    return;
  }

  mEndTime = endTime;
  updateDuration();
  update();
}

void QEventItem::setTimeRange(const QDateTime &startTime,
                              const QDateTime &endTime) {
  if (startTime > endTime) {
    qCWarning(logPcmEventItem) << "QEventItem::setTimeRange"
                               << "Invalid time range:" << startTime << endTime;
    return;
  }

  if (mStartTime == startTime && mEndTime == endTime) {
    return;
  }

  mStartTime = startTime;
  mEndTime = endTime;
  updateDuration();
  update();
}

void QEventItem::setIsWorkItem(const bool isWorkItem) {
  if (mIsWorkItem == isWorkItem)
    return;
  mIsWorkItem = isWorkItem;
  emit prepareGeometryChange();
  update();
}

void QEventItem::setId(const long long id) {
  mId = id;
  qCDebug(logPcmEventItem) << "EventItem ID changed to:" << mId;
}

unsigned int QEventItem::getDuration() const { return mDuration; }

QVariant QEventItem::itemChange(const GraphicsItemChange change,
                                const QVariant &value) {
  switch (change) {
  case ItemPositionChange: {
    const auto newPos = value.toPointF();
    if (std::abs(newPos.x()) > mSize.width()) {
      const qreal centerX = scene()->sceneRect().center().x();
      if (const bool newWorkItem = newPos.x() < centerX;
          mIsWorkItem != newWorkItem) {
        mIsWorkItem = newWorkItem;
        emit prepareGeometryChange();
        update();
      }
    }
    return QPointF{0, newPos.y()};
  }

  default:
    break;
  }

  return QGraphicsObject::itemChange(change, value);
}

void QEventItem::mousePressEvent(QGraphicsSceneMouseEvent *event) {
  if (event && event->button() == Qt::LeftButton) {
    const auto eventTitle = mTitle;
    setSelected(true);
    qCInfo(logPcmEventItem) << "EventItem::mousePressEvent| Item selected:"
                            << eventTitle;
    emit itemSelected();
    event->accept();
    return;
  }
  QGraphicsObject::mousePressEvent(event);
}

void QEventItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *event) {
  if (!event) {
    return;
  }

  QMenu menu;
  const bool hasValidMeetingUrl = pcm::meeting::isValidInvitationUrl(mMeetingUrl);
  if (mIsOnline) {
    auto *openMeetingAction = menu.addAction(tr("Open meeting"));
    auto *copyMeetingUrlAction = menu.addAction(tr("Copy meeting link"));
    auto *copyMeetingInviteAction = menu.addAction(tr("Copy meeting invite"));
    openMeetingAction->setEnabled(hasValidMeetingUrl);
    copyMeetingUrlAction->setEnabled(hasValidMeetingUrl);
    copyMeetingInviteAction->setEnabled(hasValidMeetingUrl);
    connect(openMeetingAction, &QAction::triggered, this,
            [this]() { pcm::meeting::openMeetingUrl(mMeetingUrl); });
    connect(copyMeetingUrlAction, &QAction::triggered, this,
            [this]() { pcm::meeting::copyMeetingUrl(mMeetingUrl); });
    connect(copyMeetingInviteAction, &QAction::triggered, this, [this]() {
      pcm::meeting::copyMeetingInvite(mMeetingUrl, mClientName,
                                      mStartTime.toUTC().toMSecsSinceEpoch());
    });
    menu.addSeparator();
  }
  auto *editAction = menu.addAction(tr(": EVENT_CONTEXT_EDIT"));
  auto *deleteAction = menu.addAction(tr(": EVENT_CONTEXT_DELETE"));
  connect(editAction, &QAction::triggered, this,
          [this]() { emit editRequested(); });
  connect(deleteAction, &QAction::triggered, this,
          [this]() { emit deleteRequested(); });
  menu.exec(event->screenPos());

  event->accept();
}

void QEventItem::updateDuration() {
  auto startTimeMin = mStartTime.time().minute();
  startTimeMin += mStartTime.time().hour() * 60;

  auto endTimeMin = mEndTime.time().minute();
  endTimeMin += mEndTime.time().hour() * 60;

  mDuration = endTimeMin - startTimeMin;
}

void QEventItem::paint(QPainter *painter,
                       const QStyleOptionGraphicsItem *option,
                       QWidget *widget) {
  Q_UNUSED(option)
  Q_UNUSED(widget)

  painter->save();

  // Fill and border colors
  const QColor fillColor = mIsWorkItem ? pcm::app_settings::workEventColor()
                                       : pcm::app_settings::personalEventColor();
  const QPen borderPen(fillColor.darker(165), 1.5);

  painter->setBrush(fillColor);
  painter->setPen(borderPen);

  // Position of the rectangle
  constexpr int xOffset = pcm::widgets::constants::kWidthLabel;
  const int x = mIsWorkItem ? xOffset : xOffset + mSize.width();

  // Must run on every return path of paint(), including the compact-card early
  // returns below, otherwise short sessions get highlighted with no visible
  // border. The 1px overflow outside boundingRect() is intentional (the accent
  // border hugs the card edge) and safe because QEventView uses
  // QGraphicsView::FullViewportUpdate, so no stale pixels are left behind.
  const auto drawHighlightIfNeeded = [&]() {
    if (!mIsHighlighted) {
      return;
    }
    const QPen highlightPen(
        pcm::widgets::constants::kCalendarCurrentDayUnderlineColor, 3);
    painter->setPen(highlightPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(x - 1, -1, mSize.width() + 2, mSize.height() + 2, 6,
                             6);
  };

  painter->drawRoundedRect(x, 0, mSize.width(), mSize.height(), 5, 5);

  painter->setPen(Qt::white);
  const qreal margin_y = 0.12 * mSize.height();
  constexpr qreal margin_x = 10.0;

  if (mIsWorkItem) {
    constexpr int iconSize = 12;
    constexpr int iconTextSpacing = 6;
    const QIcon briefcaseIcon(":/icons/briefcase-solid-full.svg");
    const QIcon userIcon(":/icons/user-solid-full.svg");
    const QIcon coinsIcon(":/icons/coins-solid-full.svg");
    const QIcon onlineIcon(":/icons/calendar-solid-full.svg");
    const auto drawIcon = [&](const QIcon &icon, const qreal x, const qreal y) {
      icon.paint(painter, QRectF(x, y, iconSize, iconSize).toAlignedRect(),
                 Qt::AlignCenter);
    };
    const auto costText = formatEventCost(mCost);
    const qreal left = x + margin_x;
    const qreal right = x + mSize.width() - margin_x;
    const qreal top = margin_y;
    const qreal bottom = mSize.height() - margin_y;
    const qreal rowHeight = 16.0;
    const qreal rowGap = 4.0;
    const qreal itemGap = 10.0;
    const QRectF contentRect(x + 6.0, 2.0, mSize.width() - 12.0, mSize.height() - 4.0);

    if (mSize.height() < 28) {
      QFont compactFont = painter->font();
      compactFont.setBold(true);
      painter->setFont(compactFont);
      const QFontMetricsF metrics(compactFont);
      const qreal textLeft = contentRect.left() + iconSize + iconTextSpacing + 4.0;
      const qreal availableWidth =
          std::max<qreal>(0.0, contentRect.right() - textLeft - 4.0);
      const auto elidedTitle =
          metrics.elidedText(mTitle, Qt::ElideRight, availableWidth);
      drawIcon(briefcaseIcon, contentRect.left() + 4.0,
               contentRect.top() + (contentRect.height() - iconSize) / 2.0);
      const QRectF textRect(textLeft, contentRect.top(),
                            availableWidth, contentRect.height());
      painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elidedTitle);
      drawHighlightIfNeeded();
      painter->restore();
      return;
    }

    QFont titleFont = painter->font();
    titleFont.setBold(true);
    QFont secondaryFont = painter->font();
    secondaryFont.setPointSizeF(secondaryFont.pointSizeF() - 0.5);

    struct FlowEntry {
      QIcon icon;
      QString text;
      QFont font;
      qreal naturalWidth = 0.0;
    };

    std::vector<FlowEntry> entries;
    auto appendEntry = [&](const QIcon& icon, const QString& text, const QFont& font) {
      if (text.isEmpty()) {
        return;
      }
      const QFontMetricsF metrics(font);
      entries.push_back({icon, text, font,
                         iconSize + iconTextSpacing + metrics.horizontalAdvance(text)});
    };

    appendEntry(briefcaseIcon, mTitle, titleFont);

    if (mSize.height() < 44) {
      if (!entries.empty()) {
        const auto& entry = entries.front();
        painter->setFont(entry.font);
        drawIcon(entry.icon, left,
                 contentRect.top() + (contentRect.height() - iconSize) / 2.0);
        const QFontMetricsF metrics(entry.font);
        const qreal textWidth =
            std::max<qreal>(0.0, contentRect.width() - iconSize - iconTextSpacing - 4.0);
        const auto elidedText = metrics.elidedText(entry.text, Qt::ElideRight, textWidth);
        const QRectF textRect(left + iconSize + iconTextSpacing, contentRect.top(),
                              textWidth, contentRect.height());
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elidedText);
      }
      drawHighlightIfNeeded();
      painter->restore();
      return;
    }

    appendEntry(userIcon, mClientName, secondaryFont);
    appendEntry(coinsIcon, costText, secondaryFont);
    appendEntry(onlineIcon, eventStatusLabel(mEventStatusId), secondaryFont);
    appendEntry(coinsIcon, paymentStatusLabel(mPaymentStatusId), secondaryFont);
    if (mIsOnline) {
      appendEntry(onlineIcon, tr("Online"), secondaryFont);
    }

    const qreal maxWidth = right - left;
    std::vector<int> rowStarts{0};
    qreal cursorX = left;
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
      const auto& entry = entries[i];
      if (cursorX > left && cursorX + entry.naturalWidth > right) {
        rowStarts.push_back(i);
        cursorX = left;
      }
      cursorX += std::min(maxWidth, entry.naturalWidth) + itemGap;
    }
    rowStarts.push_back(static_cast<int>(entries.size()));

    const int rowCount = static_cast<int>(rowStarts.size()) - 1;
    const qreal totalHeight = rowCount * rowHeight + std::max(0, rowCount - 1) * rowGap;
    qreal baseY = top;
    if (rowCount <= 1) {
      baseY = contentRect.top() + std::max<qreal>(0.0, (contentRect.height() - totalHeight) / 2.0);
    }

    for (int row = 0; row < rowCount; ++row) {
      qreal rowX = left;
      const qreal rowY = baseY + row * (rowHeight + rowGap);

      for (int i = rowStarts[row]; i < rowStarts[row + 1]; ++i) {
        const auto& entry = entries[i];
        const QFontMetricsF metrics(entry.font);
        const qreal remainingWidth = right - rowX;
        const qreal textWidth =
            std::max<qreal>(0.0, remainingWidth - iconSize - iconTextSpacing);
        if (textWidth <= 6.0 || rowY + rowHeight > bottom) {
          continue;
        }

        painter->setFont(entry.font);
        drawIcon(entry.icon, rowX, rowY + (rowHeight - iconSize) / 2.0);
        const auto elidedText = metrics.elidedText(entry.text, Qt::ElideRight, textWidth);
        const QRectF textRect(rowX + iconSize + iconTextSpacing, rowY,
                              textWidth, rowHeight);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elidedText);
        rowX += std::min(maxWidth, entry.naturalWidth) + itemGap;
      }
    }
  } else {
    const QRectF textRect(x + margin_x, margin_y,
                          mSize.width() - 2.0 * margin_x, mSize.height() * 0.8);
    const auto text = mIsOnline ? QStringLiteral("%1 · %2").arg(mTitle, tr("Online"))
                                : mTitle;
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
  }

  drawHighlightIfNeeded();

  painter->restore();
}
