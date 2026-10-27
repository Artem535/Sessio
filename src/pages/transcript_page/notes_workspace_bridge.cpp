#include "notes_workspace_bridge.h"
#include "call_notes_controller.h"
#include "meeting_history.h"
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QQuickTextDocument>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QUrl>

struct NotesWorkspaceBridge::Context {
  QPointer<CallNotesController> storage;
  QTextDocument *draft = nullptr;
  QTextDocument *edit = nullptr;
  QString editingId, persistedDraft;
  QStringList editFiles;
  QJsonObject editedEntry;
  bool failed = false;
};

NotesWorkspaceBridge::NotesWorkspaceBridge(std::shared_ptr<pcm::database::Database> db,
    CallNotesController *controller, QWidget *dialogParent, QObject *parent)
    : QObject(parent), mDb(std::move(db)), mLive(controller), mDialogParent(dialogParent) {
  connect(this, &NotesWorkspaceBridge::stateChanged, this, &NotesWorkspaceBridge::editorStatusChanged);
  if (controller) {
    connect(controller, &CallNotesController::changed, this, &NotesWorkspaceBridge::refresh);
    refresh();
  }
}
NotesWorkspaceBridge::~NotesWorkspaceBridge() = default;

NotesWorkspaceBridge::Context *NotesWorkspaceBridge::context() const {
  const auto found = mContexts.find(mSelected);
  return found == mContexts.end() ? nullptr : found->second.get();
}
NotesWorkspaceBridge::Context *NotesWorkspaceBridge::ensureContext(CallNotesController *controller) {
  if (!controller || controller->materialId() <= 0) return nullptr;
  const auto id = controller->materialId();
  for (auto &[existingId, existing] : mContexts) {
    if (existingId != id && existing->storage == controller) {
      auto *saved = mFactory ? mFactory(existingId, this) : nullptr;
      if (!saved) saved = new CallNotesController(mDb, this);
      if (saved->materialId() != existingId) saved->openMaterial(existingId);
      existing->storage = saved;
      connect(saved, &CallNotesController::changed, this, &NotesWorkspaceBridge::refresh);
    }
  }
  if (const auto found = mContexts.find(id); found != mContexts.end()) return found->second.get();
  auto item = std::make_unique<Context>();
  auto *result = item.get(); result->storage = controller;
  result->draft = new QTextDocument(this); result->edit = new QTextDocument(this);
  auto readingFont = result->draft->defaultFont(); readingFont.setPixelSize(17);
  result->draft->setDefaultFont(readingFont); result->edit->setDefaultFont(readingFont);
  result->persistedDraft = controller->manualText();
  result->draft->setMarkdown(result->persistedDraft);
  result->draft->setModified(false);
  connect(result->draft, &QTextDocument::contentsChanged, this, [this, result] { documentEdited(result); });
  mContexts.emplace(id, std::move(item));
  return result;
}
void NotesWorkspaceBridge::documentEdited(Context *item) {
  if (mLoading || mMutating || !item->storage) return;
  QScopedValueRollback guard(mMutating, true);
  const auto markdown = item->draft->toMarkdown();
  item->failed = !item->storage->setManualText(markdown);
  if (!item->failed) item->persistedDraft = markdown;
  mSaveStatus = item->failed ? tr("Could not save — keep this page open") : tr("Draft saved");
  emit editorStatusChanged();
}
void NotesWorkspaceBridge::refresh() {
  if (mMutating) return;
  // An explicit modal session remains bound to its original material while
  // ensureContext rehomes the previous live controller on a subsequent call.
  const bool wasCurrent = !mSessionDocument && (!context() || (mLive && context()->storage == mLive));
  if (wasCurrent && mLive && mLive->materialId() <= 0) mSelected = 0;
  if (mLive && mLive->materialId() > 0) {
    auto *live = ensureContext(mLive);
    if (wasCurrent && live) mSelected = mLive->materialId();
  }
  if (auto *item = context(); item && item->storage && !mMutating && !item->failed) {
    const auto stored = item->storage->manualText();
    if (stored != item->persistedDraft) {
      QScopedValueRollback guard(mLoading, true);
      // Append-only AI additions keep the document's undo stack intact.
      if (!item->persistedDraft.isEmpty() && stored.startsWith(item->persistedDraft)) {
        QTextCursor cursor(item->draft); cursor.movePosition(QTextCursor::End);
        cursor.insertMarkdown(stored.mid(item->persistedDraft.size()));
      } else item->draft->setMarkdown(stored);
      item->persistedDraft = stored;
    }
  }
  if (mQuickDocument && context()) {
    auto *desired = activeDocument();
    if (mQuickDocument->textDocument() != desired) activateDocument();
  }
  emit stateChanged();
  emit editorStatusChanged();
}
void NotesWorkspaceBridge::attachDocument(QQuickTextDocument *document) {
  mQuickDocument = document; activateDocument();
}
void NotesWorkspaceBridge::activateDocument() {
  if (!mQuickDocument || !context()) return;
  mQuickDocument->setTextDocument(activeDocument());
  emit documentActivated();
}
QTextDocument *NotesWorkspaceBridge::activeDocument() const {
  if (mSessionDocument) return mSessionDocument;
  return context() ? (editing() ? context()->edit : context()->draft) : nullptr;
}
bool NotesWorkspaceBridge::editorDirty() const {
  return mSessionDocument && (mSessionDocument->toMarkdown() != mSessionOriginal ||
      mSessionFiles != mSessionOriginalFiles);
}
void NotesWorkspaceBridge::beginEditorSession() {
  if (!context() || !context()->storage || mSessionDocument) return;
  mSessionDocument = new QTextDocument(this);
  auto readingFont = mSessionDocument->defaultFont(); readingFont.setPixelSize(17);
  mSessionDocument->setDefaultFont(readingFont);
  connect(mSessionDocument, &QTextDocument::contentsChanged, this, &NotesWorkspaceBridge::editorStatusChanged);
  const auto entries = context()->storage->noteEntries();
  const bool hasDraft = !context()->storage->manualText().trimmed().isEmpty() ||
      !context()->storage->draftAttachmentPaths().isEmpty();
  selectEditorNote(!hasDraft && !entries.isEmpty() ? entries.last().toObject()["id"].toString() : QString{});
}
bool NotesWorkspaceBridge::selectEditorNote(const QString &id) {
  if (!mSessionDocument || !context() || !context()->storage) return false;
  if (mSessionReady && editorDirty()) {
    mSaveStatus = tr("Save or cancel the current edit before opening another note.");
    emit editorStatusChanged(); return false;
  }
  QJsonObject entry;
  if (!id.isEmpty()) {
    for (const auto &value : context()->storage->noteEntries())
      if (value.toObject()["id"].toString() == id) entry = value.toObject();
    if (entry.isEmpty()) return false;
  }
  mSessionEntry = entry;
  mSessionDraft = context()->storage->manualText();
  mSessionFiles = id.isEmpty() ? context()->storage->draftAttachmentPaths() : QStringList{};
  mSessionOriginalFiles = mSessionFiles;
  mSessionDocument->setMarkdown(id.isEmpty() ? mSessionDraft : entry["body_markdown"].toString());
  mSessionDocument->setModified(false);
  mSessionOriginal = mSessionDocument->toMarkdown();
  mSessionReady = true;
  mSaveStatus.clear(); activateDocument(); emit stateChanged(); return true;
}
bool NotesWorkspaceBridge::saveEditorSession() {
  if (!mSessionDocument || !context() || !context()->storage) return false;
  auto *storage = context()->storage.data();
  storage->refreshFromStorage();
  bool conflict = false;
  if (editorNoteId().isEmpty()) {
    conflict = storage->manualText() != mSessionDraft ||
        storage->draftAttachmentPaths() != mSessionOriginalFiles;
  } else {
    conflict = true;
    for (const auto &value : storage->noteEntries())
      if (value.toObject() == mSessionEntry) conflict = false;
  }
  if (conflict) {
    mSaveStatus = tr("This note changed in another window. Keep this editor open and copy your changes.");
    emit editorStatusChanged(); return false;
  }
  const auto markdown = mSessionDocument->isModified() ? mSessionDocument->toMarkdown()
      : editorNoteId().isEmpty() ? mSessionDraft : mSessionEntry["body_markdown"].toString();
  const auto id = storage->saveNoteEntry(markdown, mSessionFiles, editorNoteId());
  if (id.isEmpty()) {
    mSaveStatus = tr("Could not save — keep this page open"); emit editorStatusChanged(); return false;
  }
  mSessionOriginal = mSessionDocument->toMarkdown(); mSessionOriginalFiles = mSessionFiles;
  mSaveStatus.clear(); emit editorStatusChanged(); return true;
}
bool NotesWorkspaceBridge::editing() const { return context() && !context()->editingId.isEmpty(); }
QString NotesWorkspaceBridge::currentMaterialId() const { return mLive ? QString::number(mLive->materialId()) : QString{}; }
void NotesWorkspaceBridge::setClient(const std::optional<DuckClient> &client) { mClient = client; emit stateChanged(); }
void NotesWorkspaceBridge::setMeeting(const QString &details, const QString &status) {
  mMeeting = details; mMeetingStatus = status; emit stateChanged();
}
void NotesWorkspaceBridge::setAgreements(const QJsonArray &agreements) { mAgreements = agreements; emit stateChanged(); }

QVariantMap NotesWorkspaceBridge::state() const {
  QVariantMap result{{"client", mClient ? QString::fromStdString(mClient->name.value_or("")) + " " +
      QString::fromStdString(mClient->last_name.value_or("")) : tr("Meeting notes")},
      {"meeting", mMeeting}, {"materialId", QString::number(mSelected)}, {"current", mLive && mSelected == mLive->materialId()}};
  auto *item = context(); if (!item || !item->storage) return result;
  auto *storage = item->storage.data();
  const auto material = mDb->get_call_material(storage->materialId());
  result["date"] = QDateTime::fromMSecsSinceEpoch(material.created_at).toLocalTime().toString("dd MMMM yyyy · HH:mm");
  result["title"] = tr("Meeting notes");
  if (material.event_id)
    if (const auto event = mDb->get_event(*material.event_id)) {
      result["title"] = QString::fromStdString(event->name.value_or(""));
      if (event->start_date) result["date"] = QDateTime::fromMSecsSinceEpoch(*event->start_date).toLocalTime().toString("dd MMMM yyyy · HH:mm");
    }
  result["summary"] = storage->draft()["text"].toString();
  result["summaryState"] = storage->ended() ? storage->finalStatus() : storage->liveStatus();
  result["summaryProblem"] = storage->statusText();
  result["busy"] = storage->busy(); result["sourcesCurrent"] = storage->sourcesCurrent();
  result["points"] = storage->keyPoints().toVariantList();
  result["summarySources"] = storage->draft()["evidence_ids"].toArray().toVariantList();
  result["agreements"] = (mLive == storage ? mAgreements : loadMaterialAgreements(*mDb, storage->materialId())).toVariantList();
  QVariantList files;
  const auto paths = mSessionDocument ? mSessionFiles : editing() ? item->editFiles : storage->draftAttachmentPaths();
  for (const auto &path : paths) files.append(QVariantMap{{"name", QFileInfo(path).fileName()}});
  result["attachments"] = files;
  result["savedAttachments"] = mSessionEntry["attachments"].toArray().toVariantList();
  return result;
}
QVariantList NotesWorkspaceBridge::history() const {
  QVariantList result;
  if (!mClient) return result;
  for (const auto &meeting : loadMeetingHistory(*mDb, mClient->id, mLive ? mLive->materialId() : 0))
    result.append(QVariantMap{{"id", QString::number(meeting.materialId)},
      {"date", QDateTime::fromMSecsSinceEpoch(meeting.startMs).toLocalTime().toString("dd MMM yyyy · HH:mm")},
      {"preview", meetingPreview(meeting)}});
  return result;
}
QVariantList NotesWorkspaceBridge::notes() const {
  if (!context() || !context()->storage) return {};
  return context()->storage->noteEntries().toVariantList();
}
QVariantList NotesWorkspaceBridge::phrases() const {
  QVariantList result;
  if (!context() || !context()->storage) return result;
  for (const auto transcript : mDb->get_call_material_transcript_ids(context()->storage->materialId()))
    for (const auto &phrase : mDb->get_transcript_phrases(transcript))
      result.append(QVariantMap{{"id", QString::number(phrase.id)}, {"text", QString::fromStdString(phrase.text)},
          {"speaker", QString::fromStdString(phrase.speaker_name.value_or(phrase.track_role))},
          {"time", QStringLiteral("%1:%2").arg(phrase.start_ms / 60000).arg((phrase.start_ms / 1000) % 60, 2, 10, QLatin1Char('0'))}});
  return result;
}
void NotesWorkspaceBridge::selectMaterial(const QString &id) {
  bool ok = false; const auto material = id.toLongLong(&ok);
  if (!ok || material <= 0) return;
  // Only allow navigation to this client's history or the bound current call.
  bool allowed = mLive && material == mLive->materialId();
  for (const auto &entry : history()) allowed = allowed || entry.toMap()["id"].toString() == id;
  if (!allowed) return;
  if (!mContexts.contains(material)) {
    auto *storage = mFactory ? mFactory(material, this) : new CallNotesController(mDb, this);
    if (!storage) return;
    if (storage->materialId() != material) storage->openMaterial(material);
    ensureContext(storage);
    connect(storage, &CallNotesController::changed, this, &NotesWorkspaceBridge::refresh);
  }
  mSelected = material; mSaveStatus.clear(); activateDocument(); refresh();
}
void NotesWorkspaceBridge::editNote(const QString &id) {
  auto *item = context(); if (!item || !item->storage) return;
  if (!item->editingId.isEmpty() && item->editingId != id) {
    mSaveStatus = tr("Save or cancel the current edit before opening another note.");
    emit editorStatusChanged(); return;
  }
  for (const auto &value : item->storage->noteEntries()) {
    const auto entry = value.toObject();
    if (entry["id"].toString() != id) continue;
    mSaveStatus.clear();
    if (item->editingId != id) {
      QScopedValueRollback guard(mLoading, true);
      item->editingId = id; item->editFiles.clear(); item->editedEntry = entry;
      item->edit->setMarkdown(entry["body_markdown"].toString());
      item->edit->setModified(false);
    }
    activateDocument(); emit stateChanged(); return;
  }
}
void NotesWorkspaceBridge::cancelEdit() {
  if (!context()) return;
  context()->editingId.clear(); context()->editFiles.clear();
  mSaveStatus.clear();
  activateDocument(); emit stateChanged();
}
bool NotesWorkspaceBridge::saveNote() {
  auto *item = context(); if (!item || !item->storage) return false;
  QScopedValueRollback guard(mMutating, true);
  const bool wasEditing = editing();
  if (editing()) {
    bool unchanged = false;
    for (const auto &value : item->storage->noteEntries())
      if (value.toObject() == item->editedEntry) unchanged = true;
    if (!unchanged) { mSaveStatus = tr("Could not save — keep this page open"); emit editorStatusChanged(); return false; }
  }
  const auto *document = editing() ? item->edit : item->draft;
  const auto text = document->isModified() ? document->toMarkdown()
      : editing() ? item->editedEntry["body_markdown"].toString() : item->persistedDraft;
  const auto files = editing() ? item->editFiles : item->storage->draftAttachmentPaths();
  const auto id = item->storage->saveNoteEntry(text, files, item->editingId);
  if (id.isEmpty()) { mSaveStatus = tr("Could not save — keep this page open"); emit editorStatusChanged(); return false; }
  item->editingId.clear(); item->editFiles.clear();
  if (!wasEditing) {
    item->failed = false;
    QScopedValueRollback loading(mLoading, true);
    item->persistedDraft = item->storage->manualText(); item->draft->setMarkdown(item->persistedDraft);
  }
  mSaveStatus = item->failed ? tr("Could not save — keep this page open") : tr("Note saved");
  activateDocument(); emit stateChanged(); emit editorStatusChanged(); return true;
}
void NotesWorkspaceBridge::format(int start, int end, const QString &kind) {
  if (!context()) return;
  auto *document = activeDocument();
  QTextCursor cursor(document);
  cursor.setPosition(qBound(0, start, document->characterCount() - 1));
  cursor.setPosition(qBound(0, end, document->characterCount() - 1), QTextCursor::KeepAnchor);
  if (kind == "list") { QTextListFormat format; format.setStyle(QTextListFormat::ListDisc); cursor.createList(format); }
  else { QTextCharFormat format;
    if (kind == "bold") format.setFontWeight(cursor.charFormat().fontWeight() >= QFont::Bold ? QFont::Normal : QFont::Bold);
    if (kind == "italic") format.setFontItalic(!cursor.charFormat().fontItalic());
    cursor.mergeCharFormat(format);
  }
}
void NotesWorkspaceBridge::attachFiles() {
  auto *item = context(); if (!item || !item->storage) return;
  const auto files = QFileDialog::getOpenFileNames(mDialogParent, tr("Attach files"));
  if (files.isEmpty()) return;
  if (mSessionDocument) mSessionFiles.append(files);
  else if (editing()) item->editFiles.append(files);
  else {
    auto paths = item->storage->draftAttachmentPaths(); paths.append(files);
    if (!item->storage->setDraftAttachmentPaths(paths)) mSaveStatus = tr("Could not save — keep this page open");
  }
  emit stateChanged();
}
void NotesWorkspaceBridge::removeAttachment(int index) {
  auto *item = context(); if (!item || !item->storage) return;
  auto paths = mSessionDocument ? mSessionFiles : editing() ? item->editFiles : item->storage->draftAttachmentPaths();
  if (index < 0 || index >= paths.size()) return;
  paths.removeAt(index);
  if (mSessionDocument) mSessionFiles = paths;
  else if (editing()) item->editFiles = paths;
  else if (!item->storage->setDraftAttachmentPaths(paths)) mSaveStatus = tr("Could not save — keep this page open");
  emit stateChanged();
}
void NotesWorkspaceBridge::openAttachment(const QString &entry, const QString &attachment) {
  if (!context() || !context()->storage) return;
  const auto path = context()->storage->noteAttachmentPath(entry, attachment);
  if (!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
void NotesWorkspaceBridge::appendSummary() {
  if (!context() || !context()->storage) return;
  QTextCursor cursor(mSessionDocument ? mSessionDocument : context()->draft); cursor.movePosition(QTextCursor::End);
  cursor.insertBlock(); cursor.insertText(context()->storage->draft()["text"].toString());
}
void NotesWorkspaceBridge::showSource(const QString &id) { bool ok; const auto phrase = id.toLongLong(&ok); if (ok && phrase > 0) emit sourceRequested(phrase); }
void NotesWorkspaceBridge::retrySummary() {
  if (!context() || !context()->storage) return;
  if (context()->storage->ended()) context()->storage->requestFinal();
  else emit retryRequested();
}
void NotesWorkspaceBridge::exportDocument() {
  if (!context() || !context()->storage) return;
  const auto path = QFileDialog::getSaveFileName(mDialogParent, tr("Export"), {}, tr("Markdown (*.md)"));
  if (path.isEmpty()) return;
  QString text = "# " + state()["title"].toString() + "\n\n";
  for (const auto &note : notes()) text += note.toMap()["body_markdown"].toString() + "\n\n";
  text += context()->draft->toMarkdown() + "\n\n" + state()["summary"].toString();
  QSaveFile file(path); const auto data = text.toUtf8();
  if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
    mSaveStatus = tr("Could not save — keep this page open"); emit stateChanged();
  }
}
