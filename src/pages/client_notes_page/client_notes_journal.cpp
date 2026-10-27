#include "client_notes_page.h"
#include "surface_paint_filter.h"
#include <oclero/qlementine/widgets/SegmentedControl.hpp>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QImageReader>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QPrintDialog>
#include <QPrinter>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolButton>
#include <QSet>
#include <QUrl>
#include <algorithm>

namespace {
QJsonObject packet(const QJsonObject &state) {
  return state[state["final"].isObject() ? "final" : "live"].toObject();
}
QString summary(const QJsonObject &state) {
  if (!state["ended"].toBool(true) || !state["final"].isObject()) return {};
  const auto shown = state["final"].toObject();
  return shown["dismissed"].toBool() ? QString{} : shown["note"].toObject()["text"].toString();
}
QString plainPreview(const QString &markdown) {
  QTextDocument document;
  document.setMarkdown(markdown);
  return document.toPlainText().simplified().left(150);
}
QVBoxLayout *card(QVBoxLayout *parent, const QString &title) {
  auto *frame = new QFrame;
  auto *layout = new QVBoxLayout(frame);
  layout->setContentsMargins(28, 18, 28, 18);
  layout->setSpacing(12);
  auto *heading = new QLabel(title, frame);
  heading->setTextFormat(Qt::PlainText);
  heading->setWordWrap(true);
  auto font = heading->font(); font.setPixelSize(18); font.setBold(true); heading->setFont(font);
  layout->addWidget(heading);
  parent->insertWidget(parent->count() - 1, frame);
  return layout;
}
void body(QVBoxLayout *layout, const QString &text) {
  auto *view = new QTextBrowser;
  view->setObjectName("notesEntryBody");
  view->setFrameShape(QFrame::NoFrame);
  view->setOpenExternalLinks(true);
  view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto readingFont = view->font(); readingFont.setPixelSize(17);
  view->setFont(readingFont);
  view->document()->setDefaultFont(readingFont);
  view->document()->setDocumentMargin(0);
  view->setMarkdown(text);
  layout->addWidget(view);
}
}

void ClientNotesPage::buildJournal() {
  auto *root = qobject_cast<QVBoxLayout *>(layout());
  mJournalTools = new QWidget(this);
  auto *tools = new QGridLayout(mJournalTools);
  tools->setContentsMargins(0, 0, 0, 0);
  mJournalFilter = new QComboBox(mJournalTools);
  mJournalFilter->setObjectName("notesJournalFilter");
  mJournalFilter->addItems({tr("All materials"), tr("Sessions and protocols"), tr("Free notes"), tr("Agreements")});
  mJournalSearch = new QLineEdit(mJournalTools);
  mJournalSearch->setObjectName("notesJournalSearch");
  mJournalSearch->setPlaceholderText(tr("Search notes and summaries…"));
  mJournalSearch->setClearButtonEnabled(true);
  auto *add = new QPushButton(tr("New note"), mJournalTools);
  add->setObjectName("notesJournalNew");
  auto *exportButton = new QPushButton(tr("Export"), mJournalTools);
  exportButton->setObjectName("notesJournalExport");
  tools->addWidget(add, 0, 0);
  tools->addWidget(exportButton, 0, 1);
  tools->addWidget(mJournalSearch, 1, 0, 1, 2);
  tools->addWidget(mJournalFilter, 2, 0, 1, 2);
  tools->setColumnStretch(0, 1);

  mJournal = new QSplitter(Qt::Horizontal, this);
  mJournal->setObjectName("notesJournalSplitter");
  mJournal->setChildrenCollapsible(false);
  mJournal->setHandleWidth(10);
  auto *history = new QWidget(mJournal);
  history->setObjectName("notesHistoryRail");
  auto *historyLayout = new QVBoxLayout(history);
  historyLayout->setContentsMargins(0, 0, 12, 0);
  historyLayout->setSpacing(12);
  historyLayout->addWidget(mJournalTools);
  mJournalList = new QListWidget(history);
  mJournalList->setObjectName("notesJournalList");
  mJournalList->setWordWrap(true);
  mJournalList->setSpacing(4);
  mJournalList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  mJournalList->setMinimumWidth(180);
  mJournalList->setFrameShape(QFrame::NoFrame);
  historyLayout->addWidget(mJournalList, 1);
  mJournalDocument = new QWidget(mJournal);
  mJournalDocument->installEventFilter(this);
  mJournalDocument->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  auto *document = new QVBoxLayout(mJournalDocument);
  document->setContentsMargins(0, 0, 0, 0);
  document->setSpacing(12);
  mJournalActions = new QWidget(mJournalDocument);
  mJournalActions->setObjectName("notesJournalActions");
  auto *journalActionsLayout = new QHBoxLayout(mJournalActions);
  journalActionsLayout->setContentsMargins(0, 12, 0, 0);
  journalActionsLayout->setSpacing(8);
  mJournalDocument->setAutoFillBackground(true);
  mJournalDocument->setBackgroundRole(QPalette::Window);
  mFeedSurface->setObjectName("journalDocumentSurface");
  mFeedSurface->setStyleSheet("#journalDocumentSurface { background: transparent; border: none; }");
  mJournal->addWidget(history);
  mJournal->addWidget(mJournalDocument);
  mJournal->setStretchFactor(0, 1);
  mJournal->setStretchFactor(1, 3);
  mJournal->setSizes({280, 820});
  connect(mJournalList, &QListWidget::currentRowChanged, this, &ClientNotesPage::showJournalEntry);
  connect(mJournalSearch, &QLineEdit::textChanged, this, &ClientNotesPage::filterJournal);
  connect(mJournalFilter, &QComboBox::currentIndexChanged, this, &ClientNotesPage::filterJournal);
  connect(add, &QPushButton::clicked, this, [this] {
    // A free note is independent of whichever meeting is currently selected.
    mPendingLinkedEvent.reset(); mLinkManuallySet = true; updateLinkButtonText();
    mComposerSurface->show(); focusComposer();
  });
  connect(exportButton, &QPushButton::clicked, this, [this] {
    const auto path = QFileDialog::getSaveFileName(this, tr("Export journal"), {}, tr("Markdown (*.md)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    const auto data = journalMarkdown().toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
      QMessageBox::warning(this, tr("Export journal"), tr("Could not save the journal file."));
  });
  setJournalMode(true);
}

void ClientNotesPage::setMaterialAttachmentResolver(std::function<QString(qint64, const QString &, const QString &)> resolver) {
  mMaterialAttachmentResolver = std::move(resolver);
  if (mJournalMode) showJournalEntry();
}

void ClientNotesPage::setJournalMode(bool enabled) {
  if (!mJournal || mJournalMode == enabled) return;
  mJournalMode = enabled;
  auto *root = qobject_cast<QVBoxLayout *>(layout());
  if (enabled) {
    auto readingFont = font();
    readingFont.setPointSizeF(std::max(12.0, readingFont.pointSizeF()));
    mFeedWidget->setFont(readingFont);
    mFeedWidget->setMaximumWidth(QWIDGETSIZE_MAX);
    mFeedWidget->setObjectName("notesJournalPaper");
    // QSS paints the rounded paper; an auto-fill would paint square corners first.
    mFeedWidget->setAutoFillBackground(false);
    mFeedWidget->setAttribute(Qt::WA_StyledBackground, true);
    mFeedWidget->setBackgroundRole(QPalette::Base);
    mFeedWidget->setStyleSheet("#notesJournalPaper { background: palette(base); border: 1px solid palette(mid); border-radius: 12px; }"
                              "#notesJournalPaper QTextBrowser { background: transparent; border: none; }");
    mFeedLayout->setContentsMargins(20, 20, 20, 28);
    mFeedLayout->setSpacing(6);
    mScrollArea->setContentsMargins(24, 20, 24, 20);
    auto toolFont = font(); toolFont.setPixelSize(15); mJournalTools->setFont(toolFont);
    mScrollArea->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    mScrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; }");
    mScrollArea->viewport()->setAutoFillBackground(false);
    root->removeWidget(mFeedSurface);
    root->removeWidget(mComposerSurface);
    auto *documentLayout = qobject_cast<QVBoxLayout *>(mJournalDocument->layout());
    documentLayout->addWidget(mJournalActions, 0, Qt::AlignHCenter);
    documentLayout->addWidget(mFeedSurface, 1);
    mJournalDocument->layout()->addWidget(mComposerSurface);
    documentLayout->setAlignment(mComposerSurface, Qt::AlignHCenter);
    mComposerSurface->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    mComposerSurface->setMaximumWidth(QWIDGETSIZE_MAX);
    mComposerSurface->setStyleSheet("#notesSurface { background: palette(base); border: 1px solid palette(mid); border-radius: 12px; }");
    mComposer->setMinimumHeight(140);
    mComposer->setMaximumHeight(220);
    auto editorFont = mComposer->font(); editorFont.setPixelSize(17);
    mComposer->setFont(editorFont);
    root->addWidget(mJournal, 1);
    mFeedFilterControl->hide();
    mComposerSurface->hide();
  } else {
    mJournalDocument->layout()->removeWidget(mJournalActions); mJournalActions->hide();
    mComposerSurface->setMinimumWidth(0);
    mComposerSurface->setMaximumWidth(QWIDGETSIZE_MAX);
    mComposer->setMinimumHeight(220);
    mComposer->setMaximumHeight(QWIDGETSIZE_MAX);
    mFeedWidget->setStyleSheet({});
    mFeedWidget->setFont(font());
    mFeedWidget->setMaximumWidth(QWIDGETSIZE_MAX);
    mJournalDocument->layout()->removeWidget(mFeedSurface);
    mJournalDocument->layout()->removeWidget(mComposerSurface);
    root->removeWidget(mJournal);
    root->addWidget(mFeedSurface, 1);
    root->addWidget(mComposerSurface);
    mFeedSurface->show(); mComposerSurface->show();
  }
  mJournal->setVisible(enabled);
  mJournalTools->setVisible(enabled);
}

void ClientNotesPage::reloadJournal() {
  const auto selected = mJournalList->currentItem() ? mJournalList->currentItem()->data(Qt::UserRole).toString() : QString{};
  mJournalEntries.clear();
  if (mDb && mCurrentClient && mCurrentClient->id > 0) {
    auto events = mDb->get_events_for_client(mCurrentClient->id);
    QVector<DuckEvent> appointments;
    for (const auto &event : events) appointments.append(event);
    updateAppointmentSummary(appointments);
    mCachedFeedEvents = appointments;
    auto meeting = [this](const QString &key, qint64 time, const std::optional<DuckEvent> &event) -> JournalEntry & {
      const auto found = std::find_if(mJournalEntries.begin(), mJournalEntries.end(), [&](const auto &entry) { return entry.key == key; });
      if (found != mJournalEntries.end()) return *found;
      JournalEntry entry;
      entry.key = key; entry.timestamp = time; entry.session = true; entry.event = event;
      entry.title = event ? QString::fromStdString(event->name.value_or("")) : tr("Call materials");
      if (entry.title.isEmpty()) entry.title = tr("Session");
      mJournalEntries.push_back(std::move(entry));
      return mJournalEntries.back();
    };
    for (const auto &event : events)
      meeting("event_" + QString::number(event.id), event.start_date.value_or(0), event);
    for (const auto &material : mDb->get_call_materials_for_client(mCurrentClient->id)) {
      auto state = QJsonDocument::fromJson(QByteArray::fromStdString(material.json)).object();
      std::optional<DuckEvent> event;
      if (material.event_id)
        if (const auto stored = mDb->get_event(*material.event_id)) event = *stored;
      const auto key = event ? "event_" + QString::number(event->id) : "call_" + QString::number(material.id);
      auto &entry = meeting(key, event ? event->start_date.value_or(material.created_at) : material.created_at, event);
      state["journal_material_id"] = static_cast<qint64>(material.id);
      state["journal_created_at"] = static_cast<qint64>(material.created_at);
      QJsonArray ids, agreements;
      for (const auto id : mDb->get_call_material_transcript_ids(material.id)) {
        ids.append(static_cast<qint64>(id));
        const auto records = QJsonDocument::fromJson(QByteArray::fromStdString(mDb->get_prompter_state(id).json)).object()["records"].toArray();
        for (const auto &value : records) {
          const auto record = value.toObject();
          if (record["active"].toBool(true) && !record["text"].toString().trimmed().isEmpty()) agreements.append(record);
        }
      }
      state["journal_transcripts"] = ids;
      state["journal_agreements"] = agreements;
      entry.hasAgreements = entry.hasAgreements || !agreements.isEmpty();
      entry.calls.append(state);
      if (entry.preview.isEmpty()) entry.preview = summary(state);
      if (entry.preview.isEmpty() && !state["manual_entries"].toArray().isEmpty())
        entry.preview = state["manual_entries"].toArray().first().toObject()["body_markdown"].toString();
    }
    for (const auto &note : mDb->get_client_notes(mCurrentClient->id)) {
      const auto linked = pcm::recurrence::resolveNoteLink(*mDb, note);
      if (linked) {
        auto &entry = meeting("event_" + QString::number(linked->id) +
            (linked->is_virtual_occurrence ? "_" + QString::number(linked->start_date.value_or(0)) : QString{}),
            linked->start_date.value_or(0), *linked);
        entry.notes.push_back(note);
        if (entry.preview.isEmpty()) entry.preview = QString::fromStdString(note.body_markdown.value_or(""));
      } else {
        JournalEntry entry;
        entry.key = "note_" + QString::number(note.id); entry.timestamp = note.created_at.value_or(0);
        entry.preview = QString::fromStdString(note.body_markdown.value_or(""));
        entry.title = plainPreview(entry.preview).left(65);
        if (entry.title.isEmpty()) entry.title = tr("Note with attachments");
        entry.notes.push_back(note); mJournalEntries.push_back(std::move(entry));
      }
    }
  }
  std::stable_sort(mJournalEntries.begin(), mJournalEntries.end(), [](const auto &a, const auto &b) {
    return a.timestamp > b.timestamp;
  });
  {
    const QSignalBlocker blocker(mJournalList);
    mJournalList->clear();
    for (const auto &entry : mJournalEntries) {
      QString badges = entry.session ? tr("Session") : tr("Free note");
      if (!entry.calls.isEmpty()) badges += " · " + tr("Call materials");
      if (entry.hasAgreements) badges += " · " + tr("Agreements");
      auto *item = new QListWidgetItem(QDateTime::fromMSecsSinceEpoch(entry.timestamp).toLocalTime().toString("dd.MM.yyyy · HH:mm") +
          "\n" + entry.title + "\n" + plainPreview(entry.preview) + "\n" + badges, mJournalList);
      item->setData(Qt::UserRole, entry.key);
      item->setSizeHint(QSize(240, entry.preview.trimmed().isEmpty() ? 92 : 120));
      auto *preview = new QWidget(mJournalList);
      auto historyFont = font(); historyFont.setPixelSize(15); preview->setFont(historyFont);
      preview->setAutoFillBackground(true);
      preview->setAttribute(Qt::WA_TransparentForMouseEvents);
      auto *column = new QVBoxLayout(preview);
      column->setContentsMargins(12, 10, 12, 10);
      column->setSpacing(6);
      auto *time = new QLabel(QDateTime::fromMSecsSinceEpoch(entry.timestamp).toLocalTime().toString("dd.MM.yyyy · HH:mm"), preview);
      time->setForegroundRole(QPalette::Text);
      auto *title = new QLabel(entry.title, preview);
      title->setTextFormat(Qt::PlainText);
      title->setWordWrap(true);
      auto font = title->font(); font.setBold(true); title->setFont(font);
      auto *snippet = new QLabel(plainPreview(entry.preview), preview);
      snippet->setTextFormat(Qt::PlainText);
      snippet->setWordWrap(true);
      snippet->setForegroundRole(QPalette::Text);
      snippet->setMaximumHeight(38);
      snippet->setVisible(!entry.preview.trimmed().isEmpty());
      auto *chips = new QLabel(badges, preview);
      chips->setTextFormat(Qt::PlainText);
      chips->setForegroundRole(QPalette::Link);
      for (auto *label : {time, title, snippet, chips}) {
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        column->addWidget(label);
      }
      mJournalList->setItemWidget(item, preview);
      if (entry.key == selected) mJournalList->setCurrentItem(item);
    }
  }
  const bool valid = mCurrentClient && mCurrentClient->id > 0;
  mComposer->setEnabled(valid); mAddNoteButton->setEnabled(valid);
  mAttachFilesButton->setEnabled(valid); mLinkSessionButton->setEnabled(valid);
  mJournalTools->setEnabled(valid);
  if (!valid) mAppointmentSummaryLabel->hide();
  filterJournal();
}

void ClientNotesPage::filterJournal() {
  const auto query = mJournalSearch->text().trimmed();
  const auto filter = mJournalFilter->currentIndex();
  int first = -1;
  for (int i = 0; i < mJournalList->count(); ++i) {
    const auto &entry = mJournalEntries[static_cast<size_t>(i)];
    QString text = entry.title + "\n" + entry.preview;
    for (const auto &note : entry.notes) text += "\n" + QString::fromStdString(note.body_markdown.value_or(""));
    for (const auto &value : entry.calls) {
      const auto state = value.toObject();
      text += "\n" + summary(state);
      for (const auto &note : state["manual_entries"].toArray()) text += "\n" + note.toObject()["body_markdown"].toString();
      for (const auto &agreement : state["journal_agreements"].toArray()) text += "\n" + agreement.toObject()["text"].toString();
    }
    const bool visible = (filter == 0 || (filter == 1 && entry.session) || (filter == 2 && !entry.session) ||
                          (filter == 3 && entry.hasAgreements)) && text.contains(query, Qt::CaseInsensitive);
    mJournalList->item(i)->setHidden(!visible);
    if (visible && first < 0) first = i;
  }
  const int current = mJournalList->currentRow();
  if (current < 0 || mJournalList->item(current)->isHidden()) mJournalList->setCurrentRow(first);
  showJournalEntry();
  emit workspaceChanged();
}

void ClientNotesPage::showJournalEntry() {
  if (!mJournalMode) return;
  auto *actionsRow = qobject_cast<QHBoxLayout *>(mJournalActions->layout());
  while (auto *item = actionsRow->takeAt(0)) { delete item->widget(); delete item; }
  mJournalActions->hide();
  for (int i = 0; i < mJournalList->count(); ++i) {
    if (auto *preview = mJournalList->itemWidget(mJournalList->item(i))) {
      auto colors = preview->palette();
      const auto base = palette().color(QPalette::Base);
      const auto accent = palette().color(QPalette::Highlight);
      colors.setColor(QPalette::Window, i == mJournalList->currentRow()
          ? QColor::fromRgbF(base.redF() * .88 + accent.redF() * .12,
                            base.greenF() * .88 + accent.greenF() * .12,
                            base.blueF() * .88 + accent.blueF() * .12) : base);
      preview->setPalette(colors);
    }
  }
  clearNotes();
  const int row = mJournalList->currentRow();
  if (row < 0 || static_cast<size_t>(row) >= mJournalEntries.size()) {
    mEmptyLabel->setText(tr("No matching materials")); mEmptyLabel->show(); return;
  }
  const auto &entry = mJournalEntries[static_cast<size_t>(row)];
  mEmptyLabel->hide();
  auto *header = card(mFeedLayout, entry.title);
  if (auto *title = header->itemAt(0)->widget()) {
    auto font = title->font(); font.setPixelSize(28); title->setFont(font);
  }
  auto *date = new QLabel(QDateTime::fromMSecsSinceEpoch(entry.timestamp).toLocalTime().toString("dd MMMM yyyy · HH:mm"));
  date->setForegroundRole(QPalette::Text); header->addWidget(date);
  auto *moreMenu = new QMenu(mJournalActions);
  auto *print = moreMenu->addAction(tr("Print"));
  print->setObjectName("notesJournalPrint");
  connect(print, &QAction::triggered, this, [this, markdown = journalEntryMarkdown(entry)] {
    QPrinter printer;
    QPrintDialog dialog(&printer, this);
    if (dialog.exec() != QDialog::Accepted) return;
    QTextDocument document; document.setMarkdown(markdown); document.print(&printer);
  });
  auto *exportEntry = moreMenu->addAction(tr("Export note"));
  connect(exportEntry, &QAction::triggered, this, [this, markdown = journalEntryMarkdown(entry)] {
    const auto path = QFileDialog::getSaveFileName(this, tr("Export note"), {}, tr("Markdown (*.md)"));
    if (path.isEmpty()) return;
    QSaveFile file(path); const auto data = markdown.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
      QMessageBox::warning(this, tr("Export note"), tr("Could not save the journal file."));
  });
  if (entry.event) addEventLink(header, header->parentWidget(), *entry.event);
  QVBoxLayout *ownNotes = nullptr;
  auto notesSection = [&]() {
    if (!ownNotes) {
      ownNotes = card(mFeedLayout, tr("My notes"));
      ownNotes->parentWidget()->setObjectName("notesJournalOwnNotesSection");
    }
    return ownNotes;
  };
  if (!entry.notes.empty()) {
    for (const auto &note : entry.notes) {
      const auto text = QString::fromStdString(note.body_markdown.value_or(""));
      const auto files = mDb->get_note_attachments(note.id);
      if (!text.trimmed().isEmpty()) body(notesSection(), text);
      if (!files.empty()) addAttachmentWidgets(notesSection(), files);
    }
  }
  qint64 latestMaterial = 0;
  QSet<qint64> transcripts;
  auto *materialMenu = new QMenu(moreMenu);
  for (const auto &value : entry.calls) {
    const auto state = value.toObject();
    const auto id = state["journal_material_id"].toInteger();
    latestMaterial = std::max(latestMaterial, id);
    const auto caption = QDateTime::fromMSecsSinceEpoch(state["journal_created_at"].toInteger())
        .toLocalTime().toString("dd MMM yyyy · HH:mm") + QStringLiteral(" · #%1").arg(id);
    auto *action = materialMenu->addAction(caption);
    action->setObjectName("notesJournalMaterialAction");
    connect(action, &QAction::triggered, this, [this, id] { emit openCallMaterialRequested(id); });
    for (const auto &transcript : state["journal_transcripts"].toArray()) transcripts.insert(transcript.toInteger());
  }
  auto *contextLabel = new QLabel(tr("Call materials"));
  auto contextColors = contextLabel->palette();
  auto contextInk = contextColors.color(QPalette::WindowText); contextInk.setAlphaF(0.65);
  contextColors.setColor(QPalette::WindowText, contextInk); contextLabel->setPalette(contextColors);
  actionsRow->addWidget(contextLabel);
  actionsRow->addStretch();
  if (latestMaterial > 0) {
    auto *edit = new QPushButton(tr("Edit"));
    edit->setObjectName("notesJournalEditMeeting");
    edit->setDefault(true);
    edit->setMinimumHeight(40);
    auto actionFont = edit->font(); actionFont.setPixelSize(15); edit->setFont(actionFont);
    actionsRow->addWidget(edit);
    connect(edit, &QPushButton::clicked, this, [this, latestMaterial] { emit openCallMaterialRequested(latestMaterial); });
    if (entry.calls.size() > 1 || transcripts.size() > 1) {
      materialMenu->setTitle(tr("Call materials"));
      moreMenu->addSeparator(); moreMenu->addMenu(materialMenu);
    }
  }
  if (!transcripts.isEmpty()) {
    auto ids = transcripts.values(); std::sort(ids.begin(), ids.end(), std::greater<qint64>());
    auto *open = new QPushButton(tr("Transcript")); open->setObjectName("notesJournalOpenTranscript");
    open->setMinimumHeight(40);
    auto actionFont = open->font(); actionFont.setPixelSize(15); open->setFont(actionFont);
    actionsRow->addWidget(open);
    connect(open, &QPushButton::clicked, this, [this, id = ids.first()] { emit openTranscriptRequested(id); });
    for (const auto id : ids) {
      auto *action = materialMenu->addAction(tr("Open transcript") + QStringLiteral(" · #%1").arg(id));
      connect(action, &QAction::triggered, this, [this, id] { emit openTranscriptRequested(id); });
    }
  }
  auto *more = new QToolButton;
  more->setObjectName("notesJournalMore");
  more->setText(QStringLiteral("⋯")); more->setAccessibleName(tr("More actions"));
  more->setPopupMode(QToolButton::InstantPopup); more->setMenu(moreMenu);
  more->setMinimumSize(40, 40); actionsRow->addWidget(more);
  moreMenu->setParent(more, Qt::Popup);
  moreMenu->hide();
  mJournalActions->show();
  QVBoxLayout *aiSection = nullptr;
  QStringList agreementLines, pointLines;
  // Reserve the own-notes section before summaries even if only a later call
  // in this appointment contains a note.
  for (const auto &value : entry.calls)
    for (const auto &note : value.toObject()["manual_entries"].toArray()) {
      const auto savedNote = note.toObject();
      if (!savedNote["body_markdown"].toString().trimmed().isEmpty() || !savedNote["attachments"].toArray().isEmpty())
        notesSection();
    }
  for (const auto &value : entry.calls) {
    const auto state = value.toObject();
    const auto materialId = state["journal_material_id"].toInteger();
    for (const auto &note : state["manual_entries"].toArray()) {
      const auto savedNote = note.toObject();
      const auto noteId = savedNote["id"].toString();
      const auto text = savedNote["body_markdown"].toString();
      if (!text.trimmed().isEmpty()) body(notesSection(), text);
      for (const auto &attachment : savedNote["attachments"].toArray()) {
        const auto storedFile = attachment.toObject();
        const auto attachmentId = storedFile["id"].toString();
        auto resolve = [this, materialId, noteId, attachmentId] {
          return mMaterialAttachmentResolver ? mMaterialAttachmentResolver(materialId, noteId, attachmentId) : QString{};
        };
        auto *file = new QPushButton(storedFile["file_name"].toString());
        file->setObjectName("notesJournalOpenAttachment");
        file->setEnabled(bool(mMaterialAttachmentResolver));
        connect(file, &QPushButton::clicked, this, [resolve] {
          const auto path = resolve();
          if (!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
        notesSection()->addWidget(file, 0, Qt::AlignLeft);
        if (storedFile["mime_type"].toString().startsWith("image/")) {
          auto *preview = new QLabel; preview->setObjectName("notesJournalAttachmentPreview");
          preview->hide(); notesSection()->addWidget(preview);
          auto *toggle = new QPushButton(tr("Preview"));
          toggle->setObjectName("notesJournalPreviewAttachment");
          toggle->setCheckable(true); toggle->setEnabled(bool(mMaterialAttachmentResolver));
          notesSection()->addWidget(toggle, 0, Qt::AlignLeft);
          connect(toggle, &QPushButton::toggled, this, [resolve, preview](bool shown) {
            const auto path = shown ? resolve() : QString{};
            if (shown && !path.isEmpty()) {
              QImageReader reader(path); reader.setAutoTransform(true);
              const auto size = reader.size();
              if (size.isValid()) reader.setScaledSize(size.scaled(320, 200, Qt::KeepAspectRatio));
              preview->setPixmap(QPixmap::fromImage(reader.read()));
            }
            preview->setVisible(shown && !preview->pixmap().isNull());
          });
        }
      }
    }
    const auto text = summary(state);
    if (!text.trimmed().isEmpty()) {
      auto *ai = aiSection;
      if (!ai) {
        ai = aiSection = card(mFeedLayout, tr("AI summary"));
        ai->parentWidget()->setObjectName("aiSummaryBubble");
        auto colors = ai->parentWidget()->palette();
        const auto base = colors.color(QPalette::Base);
        const auto accent = colors.color(QPalette::Highlight);
        colors.setColor(QPalette::Window, QColor::fromRgbF(base.redF() * .94 + accent.redF() * .06,
            base.greenF() * .94 + accent.greenF() * .06, base.blueF() * .94 + accent.blueF() * .06));
        ai->parentWidget()->setPalette(colors);
        ai->parentWidget()->setAutoFillBackground(true);
      }
      body(ai, text);
    }
    const auto agreements = state["journal_agreements"].toArray();
    if (!agreements.isEmpty()) {
      for (const auto &agreement : agreements) agreementLines << "- " + agreement.toObject()["text"].toString();
    }
    const auto points = packet(state)["key_points"].toArray();
    if (!points.isEmpty()) {
      for (const auto &point : points) pointLines << "- " + point.toObject()["text"].toString();
    }
  }
  agreementLines.removeDuplicates(); pointLines.removeDuplicates();
  if (!agreementLines.isEmpty()) body(card(mFeedLayout, tr("Agreements and homework")), agreementLines.join('\n'));
  if (!pointLines.isEmpty()) body(card(mFeedLayout, tr("Key points")), pointLines.join('\n'));
  for (int i = 0; i < mFeedLayout->count(); ++i)
    if (auto *widget = mFeedLayout->itemAt(i)->widget()) {
      widget->setMaximumWidth(QWIDGETSIZE_MAX);
      widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
      mFeedLayout->setAlignment(widget, {});
    }
  arrangeCompactUi();
}

QString ClientNotesPage::journalMarkdown() const {
  QString text = "# " + currentClientTitle() + "\n\n";
  for (const auto &entry : mJournalEntries) text += journalEntryMarkdown(entry);
  return text;
}

QString ClientNotesPage::journalEntryMarkdown(const JournalEntry &entry) const {
  QString text = "## " + entry.title + "\n\n" + QDateTime::fromMSecsSinceEpoch(entry.timestamp).toLocalTime().toString("dd.MM.yyyy HH:mm") + "\n\n";
  for (const auto &note : entry.notes) text += QString::fromStdString(note.body_markdown.value_or("")) + "\n\n";
  for (const auto &value : entry.calls) {
    const auto state = value.toObject();
    for (const auto &note : state["manual_entries"].toArray()) text += note.toObject()["body_markdown"].toString() + "\n\n";
    const auto ai = summary(state);
    if (!ai.isEmpty()) text += "### " + tr("AI summary") + "\n\n" + ai + "\n\n";
    for (const auto &agreement : state["journal_agreements"].toArray()) text += "- " + agreement.toObject()["text"].toString() + "\n";
    for (const auto &point : packet(state)["key_points"].toArray()) text += "- " + point.toObject()["text"].toString() + "\n";
    text += '\n';
  }
  return text;
}
