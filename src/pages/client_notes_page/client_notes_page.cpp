#include "client_notes_page.h"

#include "change_log_text.h"
#include "../../widgets/app_settings.h"
#include "../../widgets/accent_color.h"
#include "../../widgets/constants.hpp"
#include "../../widgets/surface_paint_filter.h"

#include <oclero/qlementine/widgets/SegmentedControl.hpp>

#include <QDesktopServices>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QListWidgetItem>
#include <QLocale>
#include <QMenu>
#include <QMimeDatabase>
#include <QPixmap>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QTimeZone>
#include <QUrl>

#include <algorithm>
#include <limits>
#include <variant>

namespace {
QFrame *makeSurface(QWidget *parent = nullptr) {
  auto *frame = new QFrame(parent);
  frame->setObjectName("notesSurface");
  const QString styleSheet = QStringLiteral(
                                  "#notesSurface {"
                                  " background: rgba(255, 255, 255, 0.05);"
                                  " border: 1px solid rgba(255, 255, 255, 0.08);"
                                  " border-radius: %1px;"
                                  "}")
                                  .arg(pcm::widgets::constants::kCardCornerRadius);
  frame->setStyleSheet(styleSheet);
  return frame;
}

} // namespace

ClientNotesPage::ClientNotesPage(std::shared_ptr<pcm::database::Database> db,
                                 QWidget *parent)
    : QWidget(parent), mDb(std::move(db)) {
  buildUi();
  reloadNotes();
}

void ClientNotesPage::setClientInfo(const std::optional<DuckClient> &client) {
  mCurrentClient = client;
  mPendingAttachments.clear();
  refreshPendingAttachments();
  mClientNameLabel->setText(client.has_value()
                                ? tr("%1 → Notes").arg(currentClientTitle())
                                : currentClientTitle());
  mOpenClientCardButton->setEnabled(client.has_value());
  reloadNotes();
}

void ClientNotesPage::refresh() { reloadNotes(); }

void ClientNotesPage::onAddNoteClicked() {
  if (!mDb || !mCurrentClient.has_value() || mCurrentClient->id <= 0) {
    return;
  }

  const auto markdown = mComposer->toPlainText().trimmed();
  if (markdown.isEmpty() && mPendingAttachments.isEmpty()) {
    return;
  }

  DuckClientNote note;
  note.client_id = mCurrentClient->id;
  note.body_markdown = markdown.toStdString();
  note.created_at = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
  note.updated_at = note.created_at;
  if (mPendingLinkedEvent.has_value()) {
    if (mPendingLinkedEvent->is_virtual_occurrence) {
      note.linked_series_id = mPendingLinkedEvent->series_id;
      note.linked_occurrence_start_ms = mPendingLinkedEvent->original_occurrence_start;
    } else {
      note.linked_event_id = mPendingLinkedEvent->id;
    }
  }

  const auto newNoteId = mDb->add_client_note(note);
  if (newNoteId <= 0) {
    return;
  }

  persistPendingAttachments(newNoteId);
  mComposer->clear();
  mPendingAttachments.clear();
  refreshPendingAttachments();
  mLinkManuallySet = false;
  reloadNotes();

  mSaveStatusLabel->setText(tr("Note saved"));
  mSaveStatusLabel->setVisible(true);
  QTimer::singleShot(2000, this, [this]() { mSaveStatusLabel->setVisible(false); });
}

void ClientNotesPage::onAttachFilesClicked() {
  const auto filePaths = QFileDialog::getOpenFileNames(
      this, tr("Attach files"), QString(),
      tr("All files (*);;Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.svg)"));
  if (filePaths.isEmpty()) {
    return;
  }

  QMimeDatabase mimeDatabase;
  for (const auto &path : filePaths) {
    QFileInfo fileInfo(path);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
      continue;
    }

    const auto mimeType = mimeDatabase.mimeTypeForFile(fileInfo);
    PendingAttachment attachment;
    attachment.sourcePath = path;
    attachment.fileName = fileInfo.fileName();
    attachment.mimeType = mimeType.name();
    attachment.isImage = mimeType.name().startsWith("image/");
    mPendingAttachments.push_back(attachment);
  }

  refreshPendingAttachments();
}

void ClientNotesPage::onPendingAttachmentActivated(QListWidgetItem *item) {
  if (!item) {
    return;
  }

  const auto row = mPendingAttachmentsList->row(item);
  if (row < 0 || row >= mPendingAttachments.size()) {
    return;
  }

  mPendingAttachments.removeAt(row);
  refreshPendingAttachments();
}

void ClientNotesPage::onOpenClientCardClicked() {
  if (!mCurrentClient.has_value()) {
    return;
  }

  emit openClientCardRequested(mCurrentClient);
}

void ClientNotesPage::onFeedFilterChanged() {
  const auto index = mFeedFilterControl->currentIndex();
  switch (index) {
  case 1:
    mFeedFilter = FeedFilter::Sessions;
    break;
  case 2:
    mFeedFilter = FeedFilter::Notes;
    break;
  default:
    mFeedFilter = FeedFilter::All;
    break;
  }
  reloadNotes();
}

bool ClientNotesPage::eventFilter(QObject *watched, QEvent *event) {
  if (watched == mComposer && event->type() == QEvent::KeyPress) {
    auto *keyEvent = static_cast<QKeyEvent *>(event);
    if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) &&
        keyEvent->modifiers().testFlag(Qt::ControlModifier)) {
      onAddNoteClicked();
      return true;
    }
  }

  return QWidget::eventFilter(watched, event);
}

void ClientNotesPage::buildUi() {
  auto *rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(pcm::widgets::constants::kPanelPadding,
                                 pcm::widgets::constants::kPanelPadding,
                                 pcm::widgets::constants::kPanelPadding,
                                 pcm::widgets::constants::kPanelPadding);
  rootLayout->setSpacing(pcm::widgets::constants::kPanelPadding);

  // DO NOT use makeSurface() (stylesheet) here — this surface hosts
  // mFeedFilterControl (SegmentedControl), and setStyleSheet() cascades
  // through Qt's QObject parent/child tree, wrapping style() in a
  // QStyleSheetStyle proxy. SegmentedControl paints via
  // qobject_cast<QlementineStyle*>(style()), which fails against that
  // proxy and falls back to flat QPalette colors. Paint the background via
  // an event filter instead (see analytics_page.cpp for the same pattern).
  auto *headerSurface = new QFrame(this);
  headerSurface->installEventFilter(new pcm::widgets::SurfacePaintFilter(headerSurface));
  auto *headerLayout = new QHBoxLayout(headerSurface);
  headerLayout->setContentsMargins(
      pcm::widgets::constants::kNotesHeaderHorizontalPadding,
      pcm::widgets::constants::kNotesHeaderVerticalPadding,
      pcm::widgets::constants::kNotesHeaderHorizontalPadding,
      pcm::widgets::constants::kNotesHeaderVerticalPadding);
  headerLayout->setSpacing(10);

  mClientNameLabel = new QLabel(tr("No client selected"), headerSurface);
  auto clientNameFont = mClientNameLabel->font();
  clientNameFont.setPointSize(clientNameFont.pointSize() + 2);
  clientNameFont.setBold(true);
  mClientNameLabel->setFont(clientNameFont);
  mClientNameLabel->setStyleSheet("color: rgba(255, 255, 255, 0.92);");

  mAppointmentSummaryLabel = new QLabel(headerSurface);
  mAppointmentSummaryLabel->setStyleSheet("color: rgba(255, 255, 255, 0.65);");
  mAppointmentSummaryLabel->setVisible(false);

  mOpenClientCardButton = new QPushButton(tr("Open client card"), headerSurface);
  mOpenClientCardButton->setCursor(Qt::PointingHandCursor);
  mOpenClientCardButton->setFlat(true);
  mOpenClientCardButton->setEnabled(false);

  auto *titleColumn = new QVBoxLayout();
  titleColumn->setContentsMargins(0, 0, 0, 0);
  titleColumn->setSpacing(2);
  titleColumn->addWidget(mClientNameLabel);
  titleColumn->addWidget(mAppointmentSummaryLabel);

  mFeedFilterControl = new oclero::qlementine::SegmentedControl(headerSurface);
  mFeedFilterControl->addItem(tr("All"), {}, {}, QStringLiteral("all"));
  mFeedFilterControl->addItem(tr("Sessions"), {}, {}, QStringLiteral("sessions"));
  mFeedFilterControl->addItem(tr("Notes"), {}, {}, QStringLiteral("notes"));
  mFeedFilterControl->setCurrentIndex(0);

  headerLayout->addLayout(titleColumn);
  headerLayout->addStretch();
  headerLayout->addWidget(mFeedFilterControl);
  headerLayout->addWidget(mOpenClientCardButton);
  rootLayout->addWidget(headerSurface);

  auto *feedSurface = makeSurface(this);
  auto *feedSurfaceLayout = new QVBoxLayout(feedSurface);
  feedSurfaceLayout->setContentsMargins(0, 0, 0, 0);
  feedSurfaceLayout->setSpacing(0);

  mScrollArea = new QScrollArea(feedSurface);
  mScrollArea->setFrameShape(QFrame::NoFrame);
  mScrollArea->setWidgetResizable(true);
  mScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  mFeedWidget = new QWidget(mScrollArea);
  mFeedLayout = new QVBoxLayout(mFeedWidget);
  mFeedLayout->setContentsMargins(16, 16, 16, 16);
  mFeedLayout->setSpacing(pcm::widgets::constants::kNotesFeedItemSpacing);

  mEmptyLabel = new QLabel(tr("No notes yet"), mFeedWidget);
  mEmptyLabel->setAlignment(Qt::AlignCenter);
  mEmptyLabel->setStyleSheet("color: rgba(255, 255, 255, 0.55);");
  mFeedLayout->addWidget(mEmptyLabel);
  mFeedLayout->addStretch();

  mScrollArea->setWidget(mFeedWidget);
  feedSurfaceLayout->addWidget(mScrollArea);

  mJumpToTodayButton = new QPushButton(tr("Jump to today"), feedSurface);
  mJumpToTodayButton->setCursor(Qt::PointingHandCursor);
  mJumpToTodayButton->setVisible(false);
  mJumpToLatestButton = new QPushButton(tr("Jump to latest"), feedSurface);
  mJumpToLatestButton->setCursor(Qt::PointingHandCursor);
  mJumpToLatestButton->setVisible(false);
  auto *jumpButtonsLayout = new QHBoxLayout();
  jumpButtonsLayout->setContentsMargins(0, 0, 0, 0);
  jumpButtonsLayout->addWidget(mJumpToTodayButton);
  jumpButtonsLayout->addWidget(mJumpToLatestButton);
  feedSurfaceLayout->addLayout(jumpButtonsLayout);
  rootLayout->addWidget(feedSurface, 1);

  auto *composerSurface = makeSurface(this);
  auto *composerLayout = new QVBoxLayout(composerSurface);
  composerLayout->setContentsMargins(
      pcm::widgets::constants::kNotesHeaderHorizontalPadding,
      pcm::widgets::constants::kNotesHeaderVerticalPadding,
      pcm::widgets::constants::kNotesHeaderHorizontalPadding,
      pcm::widgets::constants::kNotesHeaderHorizontalPadding);
  composerLayout->setSpacing(pcm::widgets::constants::kNotesComposerSpacing);

  mComposer = new QPlainTextEdit(composerSurface);
  mComposer->setPlaceholderText(tr("Write a note in Markdown..."));
  mComposer->setMinimumHeight(120);
  mComposer->installEventFilter(this);

  mSaveStatusLabel = new QLabel(composerSurface);
  mSaveStatusLabel->setStyleSheet("color: rgba(120, 220, 150, 0.9);");
  mSaveStatusLabel->setVisible(false);

  mPendingAttachmentsList = new QListWidget(composerSurface);
  mPendingAttachmentsList->setVisible(false);
  mPendingAttachmentsList->setAlternatingRowColors(false);
  mPendingAttachmentsList->setSelectionMode(QAbstractItemView::NoSelection);
  mPendingAttachmentsList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  mPendingAttachmentsList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  mPendingAttachmentsList->setMaximumHeight(120);
  mPendingAttachmentsList->setStyleSheet(
      "QListWidget {"
      " background: rgba(255, 255, 255, 0.03);"
      " border: 1px solid rgba(255, 255, 255, 0.06);"
      " border-radius: 10px;"
      " color: rgba(255, 255, 255, 0.82);"
      "}"
      "QListWidget::item {"
      " padding: 8px 10px;"
      " border-bottom: 1px solid rgba(255, 255, 255, 0.05);"
      "}"
      "QListWidget::item:selected {"
      " background: rgba(255, 255, 255, 0.06);"
      "}");

  mAttachFilesButton = new QPushButton(tr("Attach files"), composerSurface);
  mAttachFilesButton->setCursor(Qt::PointingHandCursor);
  mLinkSessionButton = new QPushButton(tr("Link to a session"), composerSurface);
  mLinkSessionButton->setCursor(Qt::PointingHandCursor);
  mAddNoteButton = new QPushButton(tr("Add note"), composerSurface);
  mAddNoteButton->setCursor(Qt::PointingHandCursor);

  composerLayout->addWidget(mComposer);
  composerLayout->addWidget(mSaveStatusLabel);
  composerLayout->addWidget(mPendingAttachmentsList);
  auto *actionsLayout = new QHBoxLayout();
  actionsLayout->setContentsMargins(0, 0, 0, 0);
  actionsLayout->setSpacing(pcm::widgets::constants::kNotesComposerSpacing);
  actionsLayout->addWidget(mAttachFilesButton, 0);
  actionsLayout->addWidget(mLinkSessionButton, 0);
  actionsLayout->addStretch();
  actionsLayout->addWidget(mAddNoteButton, 0);
  composerLayout->addLayout(actionsLayout);
  rootLayout->addWidget(composerSurface);

  connect(mAttachFilesButton, &QPushButton::clicked, this,
          &ClientNotesPage::onAttachFilesClicked);
  connect(mAddNoteButton, &QPushButton::clicked, this,
          &ClientNotesPage::onAddNoteClicked);
  connect(mPendingAttachmentsList, &QListWidget::itemDoubleClicked, this,
          &ClientNotesPage::onPendingAttachmentActivated);
  connect(mOpenClientCardButton, &QPushButton::clicked, this,
          &ClientNotesPage::onOpenClientCardClicked);
  connect(mFeedFilterControl, &oclero::qlementine::SegmentedControl::currentIndexChanged, this,
          &ClientNotesPage::onFeedFilterChanged);
  connect(mLinkSessionButton, &QPushButton::clicked, this,
          &ClientNotesPage::onLinkSessionButtonClicked);
  connect(mScrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this,
          &ClientNotesPage::updateJumpButtonsVisibility);
  connect(mScrollArea->verticalScrollBar(), &QScrollBar::rangeChanged, this,
          &ClientNotesPage::updateJumpButtonsVisibility);
  connect(mJumpToLatestButton, &QPushButton::clicked, this, [this]() {
    mScrollArea->verticalScrollBar()->setValue(mScrollArea->verticalScrollBar()->maximum());
  });
  connect(mJumpToTodayButton, &QPushButton::clicked, this, [this]() {
    if (mTodayAnchorWidget) {
      mScrollArea->ensureWidgetVisible(mTodayAnchorWidget);
    }
  });
}

void ClientNotesPage::reloadNotes() {
  clearNotes();

  if (!mCurrentClient.has_value() || mCurrentClient->id <= 0) {
    mEmptyLabel->setText(tr("Select a client to open notes."));
    mEmptyLabel->setVisible(true);
    mComposer->setEnabled(false);
    mAttachFilesButton->setEnabled(false);
    mLinkSessionButton->setEnabled(false);
    mAddNoteButton->setEnabled(false);
    mPendingAttachmentsList->setEnabled(false);
    mAppointmentSummaryLabel->setVisible(false);
    mJumpToTodayButton->setVisible(false);
    mJumpToLatestButton->setVisible(false);
    return;
  }

  mComposer->setEnabled(true);
  mAttachFilesButton->setEnabled(true);
  mLinkSessionButton->setEnabled(true);
  mAddNoteButton->setEnabled(true);
  mPendingAttachmentsList->setEnabled(true);

  const auto notes = mDb ? mDb->get_client_notes(mCurrentClient->id)
                         : std::vector<DuckClientNote>{};

  QVector<DuckEvent> events;
  if (mDb) {
    const auto windowStart = QDateTime::currentDateTime().addMonths(-3);
    const auto windowEnd = QDateTime::currentDateTime().addMonths(3);
    events = pcm::recurrence::eventsForClient(*mDb, mCurrentClient->id, windowStart, windowEnd);
  }
  updateAppointmentSummary(events);

  const auto changeLogEntries =
      mDb ? mDb->get_event_change_log_for_client(mCurrentClient->id)
          : std::vector<DuckEventChangeLog>{};

  mCachedFeedEvents = events;
  if (!mLinkManuallySet) {
    mPendingLinkedEvent = nearestPastEvent(events);
  }
  updateLinkButtonText();

  using FeedItem = std::variant<DuckClientNote, DuckEvent, DuckEventChangeLog>;
  std::vector<FeedItem> items;
  if (mFeedFilter != FeedFilter::Sessions) {
    for (const auto &note : notes) {
      items.emplace_back(note);
    }
  }
  if (mFeedFilter != FeedFilter::Notes) {
    for (const auto &event : events) {
      items.emplace_back(event);
    }
    for (const auto &entry : changeLogEntries) {
      items.emplace_back(entry);
    }
  }

  if (items.empty()) {
    mEmptyLabel->setText(tr("No entries yet"));
    mEmptyLabel->setVisible(true);
    mJumpToTodayButton->setVisible(false);
    mJumpToLatestButton->setVisible(false);
    return;
  }

  const auto timestampOf = [](const FeedItem &item) -> qint64 {
    return std::visit(
        [](const auto &value) -> qint64 {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, DuckClientNote>) {
            return value.created_at.value_or(0);
          } else if constexpr (std::is_same_v<T, DuckEventChangeLog>) {
            return value.occurred_at;
          } else {
            return value.start_date.value_or(0);
          }
        },
        item);
  };
  std::stable_sort(items.begin(), items.end(), [&](const FeedItem &left, const FeedItem &right) {
    return timestampOf(left) < timestampOf(right);
  });

  mEmptyLabel->setVisible(false);
  QDate previousDate;
  const auto today = QDate::currentDate();
  qint64 bestDayDiff = std::numeric_limits<qint64>::max();
  for (const auto &item : items) {
    const auto timestampMs = timestampOf(item);
    const auto itemDate =
        timestampMs > 0
            ? QDateTime::fromMSecsSinceEpoch(timestampMs, QTimeZone::systemTimeZone()).date()
            : QDate();
    if (itemDate.isValid() && itemDate != previousDate) {
      auto *divider = addDateDivider(itemDate);
      previousDate = itemDate;

      const auto dayDiff = std::abs(itemDate.daysTo(today));
      if (dayDiff < bestDayDiff) {
        bestDayDiff = dayDiff;
        mTodayAnchorWidget = divider;
      }
    }
    std::visit(
        [this](const auto &value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, DuckClientNote>) {
            addNoteBubble(value);
          } else if constexpr (std::is_same_v<T, DuckEventChangeLog>) {
            addChangeLogEntry(value);
          } else {
            addSessionEntry(value);
          }
        },
        item);
  }

  updateJumpButtonsVisibility();

  QMetaObject::invokeMethod(
      mScrollArea->verticalScrollBar(), "setValue", Qt::QueuedConnection,
      Q_ARG(int, mScrollArea->verticalScrollBar()->maximum()));
}

void ClientNotesPage::updateAppointmentSummary(const QVector<DuckEvent> &events) {
  const auto nowMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
  const auto summary = pcm::recurrence::lastAndNextAppointment(events, nowMs);

  const auto describe = [](const std::optional<DuckEvent> &event) -> QString {
    if (!event.has_value() || !event->start_date.has_value()) {
      return {};
    }
    return QDateTime::fromMSecsSinceEpoch(*event->start_date, QTimeZone::systemTimeZone())
        .toString("dd.MM.yyyy HH:mm");
  };

  const auto lastText = describe(summary.last);
  const auto nextText = describe(summary.next);
  if (lastText.isEmpty() && nextText.isEmpty()) {
    mAppointmentSummaryLabel->setVisible(false);
    return;
  }

  QStringList parts;
  if (!lastText.isEmpty()) {
    parts << tr("Last: %1").arg(lastText);
  }
  if (!nextText.isEmpty()) {
    parts << tr("Next: %1").arg(nextText);
  }
  mAppointmentSummaryLabel->setText(parts.join(QStringLiteral("  ·  ")));
  mAppointmentSummaryLabel->setVisible(true);
}

std::optional<DuckEvent> ClientNotesPage::nearestPastEvent(const QVector<DuckEvent> &events) const {
  const auto nowMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
  std::optional<DuckEvent> best;
  for (const auto &event : events) {
    if (!event.start_date.has_value() || *event.start_date > nowMs) {
      continue;
    }
    if (!best.has_value() || *event.start_date > *best->start_date) {
      best = event;
    }
  }
  return best;
}

void ClientNotesPage::updateJumpButtonsVisibility() {
  if (!mScrollArea || !mJumpToLatestButton || !mJumpToTodayButton) {
    return;
  }
  auto *scrollBar = mScrollArea->verticalScrollBar();
  mJumpToLatestButton->setVisible(scrollBar->value() < scrollBar->maximum());

  if (!mTodayAnchorWidget) {
    mJumpToTodayButton->setVisible(false);
    return;
  }
  const auto viewTop = scrollBar->value();
  const auto viewBottom = viewTop + mScrollArea->viewport()->height();
  const auto anchorTop = mTodayAnchorWidget->y();
  const auto anchorBottom = anchorTop + mTodayAnchorWidget->height();
  const bool anchorVisible = anchorBottom > viewTop && anchorTop < viewBottom;
  mJumpToTodayButton->setVisible(!anchorVisible);
}

void ClientNotesPage::updateLinkButtonText() {
  if (!mLinkSessionButton) {
    return;
  }
  if (!mPendingLinkedEvent.has_value() || !mPendingLinkedEvent->start_date.has_value()) {
    mLinkSessionButton->setText(tr("Link to a session"));
    return;
  }
  const auto startAt = QDateTime::fromMSecsSinceEpoch(*mPendingLinkedEvent->start_date,
                                                       QTimeZone::systemTimeZone());
  mLinkSessionButton->setText(tr("Linked: %1").arg(startAt.toString("dd.MM HH:mm")));
}

void ClientNotesPage::onLinkSessionButtonClicked() {
  auto sortedEvents = mCachedFeedEvents;
  const auto nowMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
  std::sort(sortedEvents.begin(), sortedEvents.end(),
            [nowMs](const DuckEvent &left, const DuckEvent &right) {
              return std::abs(left.start_date.value_or(0) - nowMs) <
                     std::abs(right.start_date.value_or(0) - nowMs);
            });

  QMenu menu(this);
  auto *clearAction = menu.addAction(tr("Don't link"));
  menu.addSeparator();
  QHash<QAction *, DuckEvent> actionToEvent;
  const auto count = std::min<qsizetype>(sortedEvents.size(), 8);
  for (qsizetype i = 0; i < count; ++i) {
    const auto &event = sortedEvents.at(i);
    const auto startAt = event.start_date.has_value()
                             ? QDateTime::fromMSecsSinceEpoch(*event.start_date,
                                                              QTimeZone::systemTimeZone())
                             : QDateTime{};
    const auto title = QString::fromStdString(event.name.value_or(tr("Session").toStdString()));
    auto *action = menu.addAction(QString("%1 · %2").arg(
        startAt.isValid() ? startAt.toString("dd.MM.yyyy HH:mm") : tr("Unknown time"), title));
    actionToEvent.insert(action, event);
  }

  const auto chosen = menu.exec(mLinkSessionButton->mapToGlobal(
      QPoint(0, mLinkSessionButton->height())));
  if (!chosen) {
    return;
  }

  mLinkManuallySet = true;
  if (chosen == clearAction) {
    mPendingLinkedEvent = std::nullopt;
  } else {
    mPendingLinkedEvent = actionToEvent.value(chosen);
  }
  updateLinkButtonText();
}

void ClientNotesPage::clearNotes() {
  mTodayAnchorWidget = nullptr;
  while (mFeedLayout->count() > 0) {
    auto *item = mFeedLayout->takeAt(0);
    if (item->widget()) {
      item->widget()->deleteLater();
    }
    delete item;
  }

  mEmptyLabel = new QLabel(mFeedWidget);
  mEmptyLabel->setAlignment(Qt::AlignCenter);
  mEmptyLabel->setStyleSheet("color: rgba(255, 255, 255, 0.55);");
  mFeedLayout->addWidget(mEmptyLabel);
  mFeedLayout->addStretch();
}

void ClientNotesPage::addNoteBubble(const DuckClientNote &note) {
  auto *bubble = new QFrame(mFeedWidget);
  bubble->setObjectName("noteBubble");
  bubble->setMaximumWidth(pcm::widgets::constants::kNotesBubbleMaxWidth);
  bubble->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
  bubble->setStyleSheet(
      "#noteBubble {"
      " background: rgba(255, 255, 255, 0.04);"
      " border: 1px solid rgba(255, 255, 255, 0.08);"
      " border-radius: 12px;"
      "}");

  auto *layout = new QVBoxLayout(bubble);
  layout->setContentsMargins(
      pcm::widgets::constants::kNotesBubbleHorizontalPadding,
      pcm::widgets::constants::kNotesBubbleVerticalPadding,
      pcm::widgets::constants::kNotesBubbleHorizontalPadding,
      pcm::widgets::constants::kNotesBubbleVerticalPadding);
  layout->setSpacing(8);

  auto *timestampLabel = new QLabel(bubble);
  const auto createdAt =
      note.created_at.has_value()
          ? QDateTime::fromMSecsSinceEpoch(*note.created_at, QTimeZone::systemTimeZone())
          : QDateTime{};
  timestampLabel->setText(createdAt.isValid()
                              ? createdAt.toString("dd.MM.yyyy HH:mm")
                              : tr("Unknown time"));
  timestampLabel->setStyleSheet("color: rgba(255, 255, 255, 0.50);");

  auto *bodyView = new QTextBrowser(bubble);
  bodyView->setFrameShape(QFrame::NoFrame);
  bodyView->setOpenExternalLinks(true);
  bodyView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  bodyView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  bodyView->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  bodyView->setContentsMargins(0, 0, 0, 0);
  bodyView->document()->setDocumentMargin(0);
  bodyView->document()->setDefaultStyleSheet(
      "body { margin: 0px; padding: 0px; }"
      "p, ul, ol { margin-top: 0px; margin-bottom: 0px; padding-top: 0px; padding-bottom: 0px; }"
      "li { margin-top: 0px; margin-bottom: 0px; }");
  auto textOption = bodyView->document()->defaultTextOption();
  textOption.setWrapMode(QTextOption::WrapAnywhere);
  bodyView->document()->setDefaultTextOption(textOption);
  bodyView->setStyleSheet(
      "QTextBrowser {"
      " background: transparent;"
      " color: rgba(255, 255, 255, 0.92);"
      " border: none;"
      " padding: 0px;"
      " margin: 0px;"
      "}");

  const auto markdown = QString::fromStdString(note.body_markdown.value_or(""));
  bodyView->setMarkdown(markdown);
  bodyView->document()->setTextWidth(
      bubble->maximumWidth() - pcm::widgets::constants::kNotesDocumentWidthInset);
  bodyView->document()->adjustSize();
  bodyView->setMinimumHeight(qRound(bodyView->document()->size().height()) + 6);
  bodyView->setMaximumHeight(
      qRound(bodyView->document()->size().height()) +
      pcm::widgets::constants::kNotesBodyHeightExtra);

  layout->addWidget(timestampLabel);

  if (mDb) {
    const auto linkedEvent = pcm::recurrence::resolveNoteLink(*mDb, note);
    if (linkedEvent.has_value() && linkedEvent->start_date.has_value()) {
      auto *linkLabel = new QPushButton(bubble);
      linkLabel->setFlat(true);
      linkLabel->setCursor(Qt::PointingHandCursor);
      const auto startAt = QDateTime::fromMSecsSinceEpoch(*linkedEvent->start_date,
                                                           QTimeZone::systemTimeZone());
      linkLabel->setText(QStringLiteral("🔗 %1").arg(startAt.toString("dd.MM.yyyy HH:mm")));
      linkLabel->setStyleSheet(
          QStringLiteral("QPushButton { text-align: left; color: %1; "
                         "background: transparent; border: none; padding: 0px; }")
              .arg(pcm::widgets::cssRgba(pcm::widgets::accentColor(), 0.9)));
      const auto linkedId = linkedEvent->id;
      const auto dayStartMs =
          QDateTime(startAt.date(), QTime(0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
      connect(linkLabel, &QPushButton::clicked, this, [this, linkedId, dayStartMs]() {
        emit openEventRequested(linkedId, dayStartMs);
      });
      layout->addWidget(linkLabel);
    }
  }

  if (!markdown.trimmed().isEmpty()) {
    layout->addWidget(bodyView);
  } else {
    bodyView->deleteLater();
  }

  if (mDb) {
    addAttachmentWidgets(layout, mDb->get_note_attachments(note.id));
  }

  mFeedLayout->insertWidget(mFeedLayout->count() - 1, bubble, 0, Qt::AlignLeft);
}

void ClientNotesPage::addSessionEntry(const DuckEvent &event) {
  auto *card = new QFrame(mFeedWidget);
  card->setObjectName("sessionEntry");
  card->setMaximumWidth(pcm::widgets::constants::kNotesBubbleMaxWidth);
  card->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
  card->setStyleSheet(
      QStringLiteral("#sessionEntry {"
                     " background: %1;"
                     " border: 1px solid %2;"
                     " border-radius: 12px;"
                     "}")
          .arg(pcm::widgets::cssRgba(pcm::widgets::accentColor(), 0.08),
               pcm::widgets::cssRgba(pcm::widgets::accentColor(), 0.18)));

  auto *layout = new QVBoxLayout(card);
  layout->setContentsMargins(
      pcm::widgets::constants::kNotesBubbleHorizontalPadding,
      pcm::widgets::constants::kNotesBubbleVerticalPadding,
      pcm::widgets::constants::kNotesBubbleHorizontalPadding,
      pcm::widgets::constants::kNotesBubbleVerticalPadding);
  layout->setSpacing(4);

  auto *timeLabel = new QPushButton(card);
  timeLabel->setFlat(true);
  timeLabel->setCursor(Qt::PointingHandCursor);
  const auto startAt =
      event.start_date.has_value()
          ? QDateTime::fromMSecsSinceEpoch(*event.start_date, QTimeZone::systemTimeZone())
          : QDateTime{};
  const auto title = QString::fromStdString(event.name.value_or(tr("Session").toStdString()));
  timeLabel->setText(QString("%1 · %2")
                         .arg(startAt.isValid() ? startAt.toString("dd.MM.yyyy HH:mm")
                                                : tr("Unknown time"),
                              title));
  timeLabel->setStyleSheet(
      "QPushButton { text-align: left; color: rgba(255, 255, 255, 0.90); "
      "font-weight: 600; background: transparent; border: none; padding: 0px; }");

  auto *detailLabel = new QLabel(card);
  QString detailText;
  if (event.event_stat_id == 3 || event.event_stat_id == 6) {
    detailText = event.cancellation_reason.has_value()
                     ? QString::fromStdString(*event.cancellation_reason)
                     : tr("Canceled");
  } else if (event.cost.has_value()) {
    detailText = tr("Cost: %1")
                     .arg(QLocale(QLocale::Russian).toString(*event.cost, 'f', 0) +
                          QStringLiteral(" ") + pcm::app_settings::currencySymbol());
  }
  detailLabel->setText(detailText);
  detailLabel->setStyleSheet("color: rgba(255, 255, 255, 0.55);");
  detailLabel->setVisible(!detailText.isEmpty());

  layout->addWidget(timeLabel);
  if (!detailText.isEmpty()) {
    layout->addWidget(detailLabel);
  } else {
    detailLabel->deleteLater();
  }

  const auto eventId = event.id;
  const auto dayStartMs =
      startAt.isValid()
          ? QDateTime(startAt.date(), QTime(0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch()
          : 0;
  connect(timeLabel, &QPushButton::clicked, this, [this, eventId, dayStartMs]() {
    emit openEventRequested(eventId, dayStartMs);
  });

  mFeedLayout->insertWidget(mFeedLayout->count() - 1, card, 0, Qt::AlignLeft);
}

QLabel *ClientNotesPage::addDateDivider(const QDate &date) {
  auto *divider = new QLabel(mFeedWidget);
  divider->setAlignment(Qt::AlignCenter);
  divider->setText(QStringLiteral("— %1 —").arg(
      QLocale().toString(date, QLocale::LongFormat)));
  divider->setStyleSheet("color: rgba(255, 255, 255, 0.45); background: transparent;");
  mFeedLayout->insertWidget(mFeedLayout->count() - 1, divider);
  return divider;
}

void ClientNotesPage::addChangeLogEntry(const DuckEventChangeLog &entry) {
  const auto text = pcm::client_notes::changeLogLineText(entry);
  if (text.isEmpty()) {
    return;
  }

  auto *line = new QPushButton(text, mFeedWidget);
  line->setFlat(true);
  line->setCursor(Qt::PointingHandCursor);
  line->setStyleSheet(
      "QPushButton { color: rgba(255, 255, 255, 0.45); background: transparent; "
      "border: none; padding: 2px 0px; }"
      "QPushButton:hover { color: rgba(255, 255, 255, 0.65); }");

  if (entry.event_current_start_date.has_value()) {
    const auto sessionAt = QDateTime::fromMSecsSinceEpoch(*entry.event_current_start_date,
                                                           QTimeZone::systemTimeZone());
    line->setToolTip(tr("Session: %1").arg(sessionAt.toString("dd.MM.yyyy HH:mm")));
  }

  const auto eventId = entry.event_id;
  qint64 dayStartMs = 0;
  if (entry.event_current_start_date.has_value()) {
    const auto startAt = QDateTime::fromMSecsSinceEpoch(*entry.event_current_start_date,
                                                         QTimeZone::systemTimeZone());
    dayStartMs =
        QDateTime(startAt.date(), QTime(0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
  }
  connect(line, &QPushButton::clicked, this, [this, eventId, dayStartMs]() {
    emit openEventRequested(eventId, dayStartMs);
  });

  mFeedLayout->insertWidget(mFeedLayout->count() - 1, line, 0, Qt::AlignCenter);
}

void ClientNotesPage::addAttachmentWidgets(
    QVBoxLayout *layout, const std::vector<DuckClientNoteAttachment> &attachments) {
  for (const auto &attachment : attachments) {
    const auto relativePath =
        QString::fromStdString(attachment.relative_path.value_or(""));
    if (relativePath.isEmpty()) {
      continue;
    }

    const auto absolutePath =
        QDir(pcm::app_settings::attachmentsStorageRoot()).filePath(relativePath);
    const auto fileName =
        QString::fromStdString(attachment.file_name.value_or(""));
    const auto mimeType =
        QString::fromStdString(attachment.mime_type.value_or(""));
    const auto isImage = mimeType.startsWith("image/");
    const auto sizeText =
        QLocale().formattedDataSize(attachment.size_bytes.value_or(0));

    auto *row = new QWidget();
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(8);

    auto *nameButton = new QPushButton(
        QString("%1 · %2 · %3")
            .arg(isImage ? tr("Image") : tr("File"), fileName, sizeText));
    nameButton->setFlat(true);
    nameButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    nameButton->setStyleSheet(
        "QPushButton {"
        " text-align: left;"
        " color: rgba(255, 255, 255, 0.84);"
        " background: rgba(255, 255, 255, 0.04);"
        " border: 1px solid rgba(255, 255, 255, 0.08);"
        " border-radius: 10px;"
        " padding: 8px 12px;"
        "}"
        "QPushButton:hover {"
        " background: rgba(255, 255, 255, 0.07);"
        "}");

    auto *openButton = new QPushButton(tr("Open"));
    openButton->setCursor(Qt::PointingHandCursor);
    openButton->setFlat(true);
    openButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    connect(openButton, &QPushButton::clicked, this, [absolutePath]() {
      QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath));
    });

    rowLayout->addWidget(nameButton, 0, Qt::AlignLeft);
    rowLayout->addWidget(openButton, 0, Qt::AlignLeft);
    rowLayout->addStretch();
    layout->addWidget(row);

    if (isImage) {
      nameButton->setCursor(Qt::PointingHandCursor);
      auto *previewLabel = new QLabel();
      previewLabel->setVisible(false);
      previewLabel->setAlignment(Qt::AlignLeft);
      previewLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
      previewLabel->setStyleSheet(
          "background: rgba(255, 255, 255, 0.02);"
          "border-radius: 10px;");
      layout->addWidget(previewLabel);

      connect(nameButton, &QPushButton::clicked, this,
              [previewLabel, absolutePath]() {
                if (previewLabel->pixmap().isNull()) {
                  QImageReader imageReader(absolutePath);
                  imageReader.setAutoTransform(true);
                  const auto image = imageReader.read();
                  if (!image.isNull()) {
                    previewLabel->setPixmap(QPixmap::fromImage(image).scaled(
                        pcm::widgets::constants::kNotesAttachmentPreviewMaxWidth,
                        pcm::widgets::constants::kNotesAttachmentPreviewMaxHeight,
                        Qt::KeepAspectRatio, Qt::SmoothTransformation));
                  }
                }
                previewLabel->setVisible(!previewLabel->isVisible());
              });
    } else {
      nameButton->setEnabled(false);
    }
  }
}

void ClientNotesPage::refreshPendingAttachments() {
  mPendingAttachmentsList->clear();
  for (const auto &attachment : mPendingAttachments) {
    const auto label = attachment.isImage
                           ? tr("Image: %1").arg(attachment.fileName)
                           : tr("File: %1").arg(attachment.fileName);
    mPendingAttachmentsList->addItem(
        tr("%1  •  Double-click to remove").arg(label));
  }

  mPendingAttachmentsList->setVisible(!mPendingAttachments.isEmpty());
}

QString ClientNotesPage::relativeNoteAttachmentPath(const int64_t clientId,
                                                    const int64_t noteId,
                                                    const QString &fileName) const {
  return QString("%1/%2/%3")
      .arg(clientId)
      .arg(noteId)
      .arg(fileName);
}

bool ClientNotesPage::persistPendingAttachments(const int64_t noteId) {
  if (!mCurrentClient.has_value() || noteId <= 0) {
    return false;
  }

  const auto rootPath = pcm::app_settings::attachmentsStorageRoot();
  QDir rootDir(rootPath);
  if (!rootDir.mkpath(QStringLiteral("."))) {
    return false;
  }

  bool allSaved = true;
  for (const auto &pending : mPendingAttachments) {
    QFileInfo sourceInfo(pending.sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
      allSaved = false;
      continue;
    }

    const auto relativePath =
        relativeNoteAttachmentPath(mCurrentClient->id, noteId, pending.fileName);
    const auto absolutePath = rootDir.filePath(relativePath);
    QFileInfo targetInfo(absolutePath);
    QDir().mkpath(targetInfo.path());
    QFile::remove(absolutePath);

    if (!QFile::copy(pending.sourcePath, absolutePath)) {
      allSaved = false;
      continue;
    }

    DuckClientNoteAttachment attachment;
    attachment.note_id = noteId;
    attachment.file_name = pending.fileName.toStdString();
    attachment.relative_path = relativePath.toStdString();
    attachment.mime_type = pending.mimeType.toStdString();
    attachment.size_bytes = sourceInfo.size();
    attachment.created_at = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    if (mDb->add_client_note_attachment(attachment) <= 0) {
      allSaved = false;
    }
  }

  return allSaved;
}

QString ClientNotesPage::currentClientTitle() const {
  if (!mCurrentClient.has_value()) {
    return tr("No client selected");
  }

  const auto firstName = QString::fromStdString(mCurrentClient->name.value_or(""));
  const auto lastName =
      QString::fromStdString(mCurrentClient->last_name.value_or(""));
  const auto fullName = QString("%1 %2").arg(firstName, lastName).trimmed();
  return fullName.isEmpty() ? tr("Unnamed client") : fullName;
}
