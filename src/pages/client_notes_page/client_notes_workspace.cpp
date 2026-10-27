#include "client_notes_page.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include "app_settings.h"
#include <QJsonDocument>
#include <QLineEdit>
#include <QMessageBox>
#include <QQmlContext>
#include <QQuickWidget>
#include <QQuickItem>
#include <QQuickWindow>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QToolButton>
#include <QMenu>
#include <QSplitter>

ClientNotesPage::~ClientNotesPage() {
  if (mExternalHeader) {
    for (auto *menu : mExternalHeader->findChildren<QMenu *>()) delete menu;
    delete mExternalHeader;
    mExternalHeader = nullptr;
  }
  delete mQuickWorkspace;
  mQuickWorkspace = nullptr;
}

void ClientNotesPage::enableQuickWorkspace() {
  if (mQuickWorkspace) return;
  Q_INIT_RESOURCE(client_notes_qml);
  if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  auto *root = qobject_cast<QVBoxLayout *>(layout());
  mHeaderSurface->hide();
  mJournal->hide();
  mComposerSurface->hide();
  mQuickWorkspace = new QQuickWidget(this);
  mQuickWorkspace->setObjectName("clientNotesQuickWorkspace");
  mQuickWorkspace->setResizeMode(QQuickWidget::SizeRootObjectToView);
  mQuickWorkspace->setClearColor(palette().color(QPalette::Base));
  mQuickWorkspace->rootContext()->setContextProperty("journalBridge", this);
  root->setContentsMargins(0, 0, 0, 0);
  root->addWidget(mQuickWorkspace, 1);
  mQuickWorkspace->setSource(QUrl("qrc:/sessio/journal/ClientNotesWorkspace.qml"));
  connect(mComposer, &QPlainTextEdit::textChanged, this, &ClientNotesPage::draftChanged);
  connect(mJournalList, &QListWidget::currentRowChanged, this, &ClientNotesPage::workspaceChanged);
  emit workspaceChanged();
}

QWidget *ClientNotesPage::headerControls() {
  if (mExternalHeader) return mExternalHeader;
  enableQuickWorkspace();
  mExternalHeader = new QWidget(this);
  mExternalHeader->setObjectName("notesHeaderControls");
  auto *row = new QHBoxLayout(mExternalHeader);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(12);
  auto *title = new QLabel(tr("Notes"), mExternalHeader);
  auto heading = title->font(); heading.setPointSize(16); heading.setBold(true); title->setFont(heading);
  row->addWidget(title);
  auto *participant = new QToolButton(mExternalHeader);
  participant->setObjectName("notesHeaderParticipant");
  participant->setMinimumWidth(180);
  participant->setMaximumWidth(380);
  participant->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  participant->setAccessibleName(tr("Participant"));
  participant->setPopupMode(QToolButton::InstantPopup);
  participant->setToolButtonStyle(Qt::ToolButtonTextOnly);
  auto *menu = new QMenu(participant);
  participant->setMenu(menu);
  row->addWidget(participant, 1);
  row->addStretch();
  auto *card = new QPushButton(tr("Client card"), mExternalHeader);
  row->addWidget(card);
  connect(card, &QPushButton::clicked, this, &ClientNotesPage::openWorkspaceClient);
  const auto sync = [this, participant, card, menu] {
    const auto entries = participants();
    // Keep the menu stable during journal/filter updates, including while it is open.
    if (menu->property("participants").toList() != entries) {
      menu->clear();
      menu->setProperty("participants", entries);
      for (const auto &value : entries) {
        const auto item = value.toMap();
        auto *action = menu->addAction(item.value("title").toString());
        action->setData(item.value("id"));
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, id = item.value("id").toString()] { selectParticipant(id); });
      }
    }
    participant->setText(participantId().isEmpty() ? tr("Select participant") : currentClientTitle());
    for (auto *action : menu->actions()) action->setChecked(action->data().toString() == participantId());
    participant->setEnabled(!entries.isEmpty());
    card->setEnabled(!participantId().isEmpty());
  };
  connect(this, &ClientNotesPage::workspaceChanged, mExternalHeader, sync);
  sync();
  if (mQuickWorkspace->rootObject()) mQuickWorkspace->rootObject()->setProperty("externalHeader", true);
  return mExternalHeader;
}

QVariantList ClientNotesPage::participants() const {
  QVariantList result;
  if (!mDb) return result;
  for (const auto &client : mDb->get_clients()) {
    auto title = QString::fromStdString(client->name.value_or("") + " " + client->last_name.value_or("")).trimmed();
    if (title.isEmpty()) title = tr("Unnamed client");
    result.append(QVariantMap{{"id", QString::number(client->id)}, {"title", title}});
  }
  return result;
}

QString ClientNotesPage::participantId() const {
  return mCurrentClient ? QString::number(mCurrentClient->id) : QString{};
}

QVariantList ClientNotesPage::journal() const {
  QVariantList result;
  for (int i = 0; i < mJournalList->count(); ++i) {
    if (mJournalList->item(i)->isHidden()) continue;
    const auto &entry = mJournalEntries[static_cast<size_t>(i)];
    result.append(QVariantMap{{"key", entry.key}, {"title", entry.title},
      {"preview", entry.preview.simplified().left(160)}, {"session", entry.session},
      {"date", QDateTime::fromMSecsSinceEpoch(entry.timestamp).toLocalTime().toString("dd.MM.yyyy · HH:mm")}});
  }
  return result;
}

QVariantMap ClientNotesPage::workspaceDocument() const {
  QVariantMap result{{"client", currentClientTitle()}, {"link", mLinkSessionButton->text()}};
  const int index = mJournalList->currentRow();
  if (index < 0 || index >= static_cast<int>(mJournalEntries.size())) return result;
  const auto &entry = mJournalEntries[static_cast<size_t>(index)];
  result.insert("key", entry.key);
  result.insert("title", entry.title);
  result.insert("markdown", journalEntryMarkdown(entry));
  result.insert("meeting", entry.event.has_value() || !entry.calls.isEmpty());
  bool hasTranscript = false;
  QVariantList materials, transcripts;
  for (const auto &call : entry.calls) {
    hasTranscript |= !call.toObject()["journal_transcripts"].toArray().isEmpty();
    const auto state = call.toObject();
    materials.append(QVariantMap{{"id", QString::number(state["journal_material_id"].toInteger())},
      {"title", QDateTime::fromMSecsSinceEpoch(state["journal_created_at"].toInteger()).toLocalTime().toString("dd.MM.yyyy HH:mm")}});
    for (const auto &id : state["journal_transcripts"].toArray())
      transcripts.append(QVariantMap{{"id", QString::number(id.toInteger())}, {"title", QString::number(id.toInteger())}});
  }
  result.insert("materials", materials);
  result.insert("transcripts", transcripts);
  result.insert("transcript", hasTranscript);
  QVariantList attachments;
  for (const auto &note : entry.notes)
    for (const auto &file : mDb->get_note_attachments(note.id))
      attachments.append(QVariantMap{{"name", QString::fromStdString(file.file_name.value_or(""))},
        {"path", QDir(pcm::app_settings::attachmentsStorageRoot()).filePath(QString::fromStdString(file.relative_path.value_or("")))}});
  for (const auto &call : entry.calls) {
    const auto state = call.toObject();
    for (const auto &value : state["manual_entries"].toArray()) {
      const auto note = value.toObject();
      for (const auto &value : note["attachments"].toArray()) {
        const auto file = value.toObject();
        attachments.append(QVariantMap{{"name", file["file_name"].toString()},
          {"material", state["journal_material_id"].toInteger()}, {"entry", note["id"].toString()}, {"id", file["id"].toString()}});
      }
    }
  }
  result.insert("attachments", attachments);
  return result;
}

QString ClientNotesPage::draft() const { return mComposer->toPlainText(); }
QStringList ClientNotesPage::pendingFiles() const {
  QStringList result;
  for (const auto &file : mPendingAttachments) result.append(file.fileName);
  return result;
}
QString ClientNotesPage::workspaceStatus() const { return mSaveStatusLabel->text(); }
void ClientNotesPage::setDraft(const QString &text) {
  if (text != draft()) {
    mComposer->setPlainText(text);
    mSaveStatusLabel->setText(text.isEmpty() ? QString{} : tr("Unsaved changes"));
    emit draftChanged();
  }
}

void ClientNotesPage::selectParticipant(const QString &id) {
  if (!mDb || id == participantId()) return;
  for (const auto &client : mDb->get_clients())
    if (QString::number(client->id) == id) { setClientInfo(*client); return; }
}

void ClientNotesPage::selectJournalEntry(const QString &key) {
  for (int i = 0; i < mJournalList->count(); ++i)
    if (mJournalList->item(i)->data(Qt::UserRole).toString() == key) {
      mJournalList->setCurrentRow(i);
      emit workspaceChanged();
      return;
    }
}

void ClientNotesPage::searchJournal(const QString &query, int filter) {
  mJournalSearch->setText(query);
  mJournalFilter->setCurrentIndex(qBound(0, filter, 3));
  emit workspaceChanged();
}
void ClientNotesPage::saveWorkspaceNote() {
  onAddNoteClicked();
  emit draftChanged();
  emit workspaceChanged();
}
void ClientNotesPage::attachWorkspaceFiles() { onAttachFilesClicked(); emit draftChanged(); }
void ClientNotesPage::linkWorkspaceSession() { onLinkSessionButtonClicked(); emit workspaceChanged(); }
void ClientNotesPage::openWorkspaceClient() { onOpenClientCardClicked(); }
void ClientNotesPage::removeWorkspaceAttachment(int index) {
  if (index < 0 || index >= mPendingAttachments.size()) return;
  mPendingAttachments.removeAt(index);
  refreshPendingAttachments();
  emit draftChanged();
}
void ClientNotesPage::exportWorkspaceJournal() {
  if (auto *button = findChild<QPushButton *>("notesJournalExport")) button->click();
}
void ClientNotesPage::openWorkspaceAttachment(int index) {
  const auto files = workspaceDocument().value("attachments").toList();
  if (index < 0 || index >= files.size()) return;
  const auto file = files[index].toMap();
  auto path = file.value("path").toString();
  if (file.contains("material") && mMaterialAttachmentResolver)
    path = mMaterialAttachmentResolver(file.value("material").toLongLong(), file.value("entry").toString(), file.value("id").toString());
  if (!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
void ClientNotesPage::openWorkspaceMeeting(const QString &id) {
  const int index = mJournalList->currentRow();
  if (index < 0) return;
  const auto &entry = mJournalEntries[static_cast<size_t>(index)];
  if (!entry.calls.isEmpty()) {
    for (const auto &call : entry.calls) {
      const auto materialId = call.toObject()["journal_material_id"].toInteger();
      if (id.isEmpty() || id == QString::number(materialId)) { emit openCallMaterialRequested(materialId); return; }
    }
  } else if (entry.event) {
    const auto date = QDateTime::fromMSecsSinceEpoch(entry.event->start_date.value_or(0)).toLocalTime().date();
    emit openEventRequested(entry.event->id, QDateTime(date, QTime(0, 0)).toMSecsSinceEpoch());
  }
}
void ClientNotesPage::openWorkspaceTranscript(const QString &id) {
  const int index = mJournalList->currentRow();
  if (index < 0) return;
  for (const auto &call : mJournalEntries[static_cast<size_t>(index)].calls) {
    const auto ids = call.toObject()["journal_transcripts"].toArray();
    for (const auto &value : ids)
      if (id.isEmpty() || id == QString::number(value.toInteger())) { emit openTranscriptRequested(value.toInteger()); return; }
  }
}
