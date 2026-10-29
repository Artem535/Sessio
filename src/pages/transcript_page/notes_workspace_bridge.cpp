#include "notes_workspace_bridge.h"
#include "call_notes_controller.h"
#include "call_notes_page.h"
#include "meeting_history.h"
#include "prompter_panel.h"
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
#include <QUuid>

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
  auto *ctx = context();
  return ctx ? (editing() ? ctx->edit : ctx->draft) : nullptr;
}
bool NotesWorkspaceBridge::editorDirty() const {
  if (mSessionDocument)
    return mSessionDocument->toMarkdown() != mSessionOriginal || mSessionFiles != mSessionOriginalFiles;
  for (const auto &[id, item] : mContexts) {
    if (!item->editingId.isEmpty() && (item->edit->isModified() || !item->editFiles.isEmpty()))
      return true;
    // A refused autosave leaves the only copy of the draft in this document.
    if (item->failed) return true;
  }
  return false;
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
void NotesWorkspaceBridge::setPrompterPanel(PrompterPanel *panel) {
  if (mPrompter == panel) return;
  mPrompter = panel;
  if (!panel) { emit stateChanged(); return; }
  connect(panel, &PrompterPanel::stateChanged, this, &NotesWorkspaceBridge::stateChanged);
  if (auto *ctrl = panel->controller()) {
    connect(ctrl, &CallPrompterController::cardsChanged, this, &NotesWorkspaceBridge::stateChanged);
    connect(ctrl, &CallPrompterController::statusChanged, this, &NotesWorkspaceBridge::stateChanged);
  }
  emit stateChanged();
}

QVariantMap NotesWorkspaceBridge::state() const {
  const bool current = !mLive || mSelected == mLive->materialId();
  QVariantMap result{{"client", mClient ? QString::fromStdString(mClient->name.value_or("")) + " " +
      QString::fromStdString(mClient->last_name.value_or("")) : tr("Meeting notes")},
      {"meeting", mMeeting}, {"materialId", QString::number(mSelected)}, {"current", mLive && mSelected == mLive->materialId()}};
  auto *item = context();
  const bool ended = item && item->storage ? item->storage->ended() : (mLive ? mLive->ended() : false);
  result["ended"] = ended;
  result["paymentError"] = mPaymentError;
  result["canChangePayment"] = false;
  if (current && mPrompter && mPrompter->controller()) {
    auto *ctrl = mPrompter->controller();
    const bool controls = mPrompter->controlsVisible() && !ended;
    const QString hint = mPrompter->activeHint();
    QVariantList cards;
    int pending = 0;
    for (const auto &value : ctrl->cards()) {
      const auto card = value.toObject();
      const auto id = card["id"].toString();
      const auto decision = card["decision"].toString();
      if (decision == "pending") ++pending;
      if (decision == "rejected" || decision == "superseded") continue;
      const bool unresolved = card["unresolved"].toBool();
      const QString title = unresolved ? PrompterPanel::tr("Needs clarification")
          : card["kind"].toString() == "cancel" ? PrompterPanel::tr("Proposed cancellation")
          : !card["target_id"].toString().isEmpty() ? PrompterPanel::tr("Proposed change")
          : PrompterPanel::tr("Agreement");
      QString previous = card["previous_action"].toString();
      if (previous.isEmpty())
        for (const auto &r : ctrl->records())
          if (r.toObject()["id"] == card["target_id"]) previous = r.toObject()["text"].toString();
      if (previous.isEmpty())
        for (const auto &c : ctrl->cards())
          if (c.toObject()["id"] == card["target_id"]) previous = c.toObject()["action"].toString();
      const bool sourceValid = ctrl->evidenceAvailable(id);
      const bool editable = decision == "pending" && !unresolved && sourceValid;
      const bool savedToNotes = card["note_id"].toInteger() > 0 || mPrompter->isCallNoteSaved(id);
      QVariantList sources;
      for (const auto &evidence : card["evidence_ids"].toArray()) {
        const auto evidenceId = evidence.toString();
        QJsonObject source = ctrl->source(evidenceId);
        for (const auto &snapshot : card["evidence_snapshot"].toArray())
          if (snapshot.toObject()["id"].toString() == evidenceId) source = snapshot.toObject();
        const auto ms = source["start_ms"].toInteger();
        sources.append(QVariantMap{
          {"id", evidenceId},
          {"quote", source["speaker"].toString() + QStringLiteral(": «") + source["text"].toString() + QStringLiteral("»")},
          {"label", PrompterPanel::tr("Source · %1:%2").arg(ms / 60000).arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))},
          {"tooltip", source["text"].toString()}
        });
      }
      cards.append(QVariantMap{
        {"id", id},
        {"decision", decision},
        {"unresolved", unresolved},
        {"title", title},
        {"action", card["action"].toString()},
        {"previous", previous.isEmpty() ? QString{} : PrompterPanel::tr("Previous agreement: %1").arg(previous)},
        {"sourceValid", sourceValid},
        {"sourceWarning", PrompterPanel::tr("The source was changed or deleted. Review the transcript.")},
        {"sources", sources},
        {"canAccept", editable},
        {"canEdit", editable},
        {"canReject", decision == "pending"},
        {"canResolve", decision == "pending" && unresolved},
        {"savedToNotes", savedToNotes},
        {"canSaveToNotes", (editable || decision == "accepted") && sourceValid && !savedToNotes}
      });
    }
    result["prompterVisible"] = controls || !cards.isEmpty() || !hint.isEmpty();
    result["prompterControls"] = controls;
    result["prompterRunning"] = ctrl->running();
    result["prompterStatus"] = ctrl->status();
    result["prompterBadge"] = pending > 0 ? CallNotesPage::tr("%n new", nullptr, pending) : QString{};
    result["prompterHint"] = hint;
    result["prompterCards"] = cards;
  } else {
    result["prompterVisible"] = false;
    result["prompterControls"] = false;
    result["prompterRunning"] = false;
    result["prompterStatus"] = QString{};
    result["prompterBadge"] = QString{};
    result["prompterHint"] = QString{};
    result["prompterCards"] = QVariantList{};
  }
  if (!item || !item->storage) return result;
  auto *storage = item->storage.data();
  const auto material = mDb->get_call_material(storage->materialId());
  result["date"] = QDateTime::fromMSecsSinceEpoch(material.created_at).toLocalTime().toString("dd MMMM yyyy · HH:mm");
  result["title"] = tr("Meeting notes");
  if (material.event_id)
    if (const auto event = mDb->get_event(*material.event_id)) {
      result["title"] = QString::fromStdString(event->name.value_or(""));
      result["canChangePayment"] = ended && event->is_work_event;
      result["paymentStatus"] = static_cast<int>(event->payment_stat_id);
      if (event->start_date) result["date"] = QDateTime::fromMSecsSinceEpoch(*event->start_date).toLocalTime().toString("dd MMMM yyyy · HH:mm");
    }
  const auto summary = storage->summary();
  const auto records = agreementRecords(storage);
  result["summary"] = storage->draft()["text"].toString();
  result["summaryState"] = storage->ended() ? storage->finalStatus() : storage->liveStatus();
  result["summaryProblem"] = storage->statusText();
  result["busy"] = storage->busy(); result["sourcesCurrent"] = storage->sourcesCurrent();
  result["points"] = summary["key_points"].toArray().toVariantList();
  result["livePoints"] = storage->livePoints().toVariantList();
  result["openQuestions"] = summary["open_questions"].toArray().toVariantList();
  QSet<QString> agreed;
  for (const auto &r : records)
    if (r.toObject()["active"].toBool(true)) agreed.insert(r.toObject()["text"].toString().toCaseFolded().simplified());
  QVariantList tasks;
  for (const auto &value : summary["tasks"].toArray()) {
    auto task = value.toObject().toVariantMap();
    task["accepted"] = agreed.contains(task["text"].toString().toCaseFolded().simplified());
    tasks.append(task);
  }
  result["tasks"] = tasks;
  result["summarySources"] = storage->draft()["evidence_ids"].toArray().toVariantList();
  result["agreements"] = decoratedAgreements(records);
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
  auto ids = context() && context()->storage ? mDb->get_call_material_transcript_ids(context()->storage->materialId()) : std::vector<int64_t>{};
  if (ids.empty() && mPrompter && mPrompter->controller() && mPrompter->controller()->transcriptId() > 0)
    ids.push_back(mPrompter->controller()->transcriptId());
  for (const auto transcript : ids)
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
  auto *ctx = context();
  if (!ctx || !ctx->storage) return;
  QTextCursor cursor(mSessionDocument ? mSessionDocument : ctx->draft); cursor.movePosition(QTextCursor::End);
  cursor.insertBlock(); cursor.insertText(ctx->storage->draft()["text"].toString());
}
void NotesWorkspaceBridge::showSource(const QString &id) { bool ok; const auto phrase = id.toLongLong(&ok); if (ok && phrase > 0) emit sourceRequested(phrase); }
void NotesWorkspaceBridge::retrySummary() {
  if (!context() || !context()->storage) return;
  if (context()->storage->ended()) context()->storage->requestFinal();
  else emit retryRequested();
}
void NotesWorkspaceBridge::summarizeMeeting() {
  if (context() && context()->storage && context()->storage->ended()) context()->storage->requestFinal();
}
bool NotesWorkspaceBridge::setPaymentStatus(int status) {
  if (status < 1 || status > 5 || !context() || !context()->storage || !context()->storage->ended())
    return false;
  try {
    const auto material = mDb->get_call_material(context()->storage->materialId());
    if (!material.event_id) return false;
    const auto event = mDb->get_event(*material.event_id);
    if (!event || !event->is_work_event) return false;
    auto updated = *event;
    updated.payment_stat_id = status;
    if (!mDb->update_event(updated, true)) throw std::runtime_error("payment update failed");
    mPaymentError.clear();
    emit stateChanged();
    emit paymentChanged();
    return true;
  } catch (...) {
    mPaymentError = tr("Could not update payment status.");
    emit stateChanged();
    return false;
  }
}
QJsonArray NotesWorkspaceBridge::agreementRecords(CallNotesController *storage) const {
  return mLive == storage ? mAgreements : loadMaterialAgreements(*mDb, storage->materialId());
}
QVariantList NotesWorkspaceBridge::decoratedAgreements(const QJsonArray &records) const {
  QVariantList out;
  for (const auto &value : records) {
    auto record = value.toObject().toVariantMap();
    const auto eventId = value.toObject()["event_id"].toInteger();
    bool inCalendar = false;
    // The event may have been deleted from the calendar since it was linked.
    try { inCalendar = eventId > 0 && mDb->get_event(eventId) != nullptr; } catch (...) {}
    record["inCalendar"] = inCalendar;
    record["canAddToCalendar"] = value.toObject()["active"].toBool(true) && value.toObject()["meeting"].toBool() && !inCalendar;
    out.append(record);
  }
  return out;
}
void NotesWorkspaceBridge::agreementsWritten(CallNotesController *storage) {
  // The live prompter holds the same records: reload it, then the copy here.
  if (mLive == storage) {
    if (mPrompter && mPrompter->controller()) mPrompter->controller()->refreshFromStorage();
    mAgreements = loadMaterialAgreements(*mDb, storage->materialId());
  }
  emit stateChanged();
}
void NotesWorkspaceBridge::addAgreementToCalendar(const QString &recordId) {
  // The dialog runs a nested event loop that may replace or delete the controller.
  QPointer<CallNotesController> storage = context() ? context()->storage.data() : nullptr;
  if (!storage || !mCalendarOpener) return;
  const auto materialId = storage->materialId();
  QJsonObject record;
  for (const auto &value : agreementRecords(storage))
    if (value.toObject()["id"].toString() == recordId) record = value.toObject();
  if (record.isEmpty()) return;
  std::optional<DuckEvent> callEvent;
  try {
    const auto material = mDb->get_call_material(materialId);
    if (material.event_id)
      if (const auto event = mDb->get_event(*material.event_id)) callEvent = *event;
  } catch (...) {}
  const auto name = mClient ? (QString::fromStdString(mClient->name.value_or("")) + ' ' +
                               QString::fromStdString(mClient->last_name.value_or(""))).trimmed() : QString();
  const auto t = agreementEventTemplate(record, callEvent, mClient ? std::make_optional<int64_t>(mClient->id) : std::nullopt,
                                        name, storage->callDate());
  const auto eventId = mCalendarOpener(t);
  if (!eventId || *eventId <= 0) return;
  if (setMaterialAgreementEvent(*mDb, materialId, recordId, *eventId) && storage) agreementsWritten(storage);
}
void NotesWorkspaceBridge::openAgreementEvent(const QString &recordId) {
  auto *storage = context() ? context()->storage.data() : nullptr;
  if (!storage || !mEventOpener) return;
  for (const auto &value : agreementRecords(storage))
    if (value.toObject()["id"].toString() == recordId && value.toObject()["event_id"].toInteger() > 0)
      mEventOpener(value.toObject()["event_id"].toInteger());
}
bool NotesWorkspaceBridge::acceptSummaryTask(int index) {
  auto *storage = context() ? context()->storage.data() : nullptr;
  if (!storage) return false;
  const auto tasks = state()["tasks"].toList();
  if (index < 0 || index >= tasks.size() || tasks[index].toMap()["accepted"].toBool()) return false;
  const auto task = storage->summary()["tasks"].toArray()[index].toObject();
  const QJsonObject record{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"text", task["text"]},
      {"participants", QJsonArray{}}, {"active", true}, {"evidence_ids", task["evidence_ids"]},
      {"meeting", task["meeting"].toBool()}, {"date", task["date"].toString()}, {"time", task["time"].toString()}};
  if (!appendMaterialAgreement(*mDb, storage->materialId(), record)) return false;
  agreementsWritten(storage);
  return true;
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
void NotesWorkspaceBridge::startPrompter() {
  if (mPrompter) mPrompter->triggerStart();
}
void NotesWorkspaceBridge::pausePrompter() {
  if (mPrompter) mPrompter->triggerPause();
}
void NotesWorkspaceBridge::dismissPrompterHint() {
  if (mPrompter) mPrompter->dismissHint();
}
void NotesWorkspaceBridge::prompterCardAction(const QString &cardId, const QString &action) {
  if (mPrompter) mPrompter->triggerCardAction(cardId, action);
}
void NotesWorkspaceBridge::showPrompterSource(const QString &id) {
  bool ok = false; const auto phrase = id.toLongLong(&ok);
  if (!ok || phrase <= 0) return;
  if (mPrompter) emit mPrompter->sourceRequested(phrase);
  else emit sourceRequested(phrase);
}
