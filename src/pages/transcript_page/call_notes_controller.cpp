#include "call_notes_controller.h"
#include "app_settings.h"
#include "meeting_history.h"
#include "notes_common.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace {
constexpr qsizetype kMaxLivePoints = 40;
QString timeLabel(qint64 ms) {
  return QStringLiteral("%1:%2").arg(ms / 60000).arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'));
}
// A format-2 summary list: at most `max` items, each a validItem with text.
bool validList(const QJsonValue &list, qsizetype max, const QSet<QString> &ids, bool tasks = false);
bool validItem(const QJsonObject &item, const QSet<QString> &ids) {
  const auto text = item["text"].toString();
  if (text.size() > 6000 || !item["evidence_ids"].isArray()) return false;
  const auto evidence = item["evidence_ids"].toArray();
  if (!text.trimmed().isEmpty() && evidence.isEmpty()) return false;
  for (const auto &id : evidence) if (!id.isString() || !ids.contains(id.toString())) return false;
  return true;
}
bool validList(const QJsonValue &list, qsizetype max, const QSet<QString> &ids, bool tasks) {
  if (!list.isArray() || list.toArray().size() > max) return false;
  for (const auto &value : list.toArray()) {
    const auto item = value.toObject();
    if (!value.isObject() || item["text"].toString().trimmed().isEmpty()) return false;
    if (tasks) {
      if (!item["meeting"].isBool() || !item["date"].isString() || !item["time"].isString()) return false;
      if (!validItem({{"text", item["text"]}, {"evidence_ids", item["evidence_ids"]}}, ids)) return false;
    } else if (!validItem(item, ids)) {
      return false;
    }
  }
  return true;
}
void removeDraftFiles(const QJsonArray &attachments, const QString &root, qint64 materialId) {
  const auto canonicalRoot = QFileInfo(root).canonicalFilePath();
  if (canonicalRoot.isEmpty()) return;
  for (const auto &value : attachments) {
    const auto id = value.toObject()["id"].toString();
    if (QUuid::fromString(id).isNull()) continue;
    const QFileInfo folder(QDir(root).filePath(QStringLiteral("calls/%1/draft/%2").arg(materialId).arg(id)));
    const auto canonical = folder.canonicalFilePath();
    if (!folder.isSymLink() && canonical.startsWith(canonicalRoot + QLatin1Char('/')))
      QDir(canonical).removeRecursively();
  }
}
}

CallNotesController::CallNotesController(std::shared_ptr<pcm::database::Database> db, QObject *parent,
                                       const QString &attachmentRoot)
    : QObject(parent), mDb(std::move(db)),
      mAttachmentRoot(attachmentRoot.isEmpty() ? pcm::app_settings::attachmentsStorageRoot() : attachmentRoot) {
  mBatch.setSingleShot(true); mBatch.setInterval(1800);
  connect(&mBatch, &QTimer::timeout, this, &CallNotesController::flush);
  mRetry.setSingleShot(true);
  connect(&mRetry, &QTimer::timeout, this, [this] { mDirty = true; flush(); });
}
bool CallNotesController::persist() {
  if (!mMaterialId) return false;
  mState.remove("storage_error");
  try {
    if (mDb->save_call_material(mMaterialId, mRevision,
          QJsonDocument(mState).toJson(QJsonDocument::Compact).toStdString())) { ++mRevision; return true; }
  } catch (...) {}
  mEnabled = false; mBatch.stop();
  mState["storage_error"] = true;
  emit changed(); return false;
}
bool CallNotesController::syncStored(bool force) {
  try {
    const auto latest = mDb->get_call_material(mMaterialId);
    if (latest.id <= 0) return false;
    if (!force && latest.revision == mRevision) return true;
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(latest.json), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return false;
    mState = doc.object(); mRevision = latest.revision; mState["ended"] = mEnded;
    return true;
  } catch (...) { return false; }
}
void CallNotesController::load(qint64 id) {
  mBatch.stop(); stopRetrying(); ++mGeneration; mInFlight = false; mRequest = {}; mDirty = false;
  mFinalPending = false; mEnabled = false; mMaterialId = 0; mRevision = 0; mState = {};
  mDeferred = false;
  try {
    const auto stored = mDb->get_call_material(id);
    if (stored.id > 0) {
      mMaterialId = stored.id; mRevision = stored.revision;
      if (!stored.json.empty()) {
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(stored.json), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) { mMaterialId = 0; emit changed(); return; }
        mState = doc.object();
      }
      mEnded = mState["ended"].toBool(true);
      for (const auto *key : {"live_status", "final_status"}) {
        if (mState.value(key) == "running" || mState.value(key) == "queued") mState[key] = "interrupted";
      }
    }
  } catch (...) {}
  emit changed();
}
void CallNotesController::beginCall(std::optional<int64_t> event) {
  if (mInFlight) cancel();
  try { load(mDb->create_call_material(event && *event > 0 ? event : std::nullopt)); }
  catch (...) { load(0); }
  mTranscriptId = 0; mEnded = false; mState["ended"] = false;
  persist(); emit changed();
}
void CallNotesController::openMaterial(qint64 id) {
  if (mInFlight) cancel();
  mTranscriptId = 0; load(id);
  try { const auto ids = mDb->get_call_material_transcript_ids(id); if (!ids.empty()) mTranscriptId = ids.back(); }
  catch (...) {}
  mEnded = true; mState["ended"] = true;
  emit changed();
}
void CallNotesController::openTranscript(qint64 id) {
  if (mInFlight) cancel();
  try {
    auto stored = mDb->get_call_material_for_transcript(id);
    if (stored.id <= 0) {
      const auto transcript = mDb->get_transcript(id);
      if (!transcript) { load(0); return; }
      const auto material = mDb->create_call_material(transcript->event_id);
      if (!mDb->link_call_material_transcript(material, id)) { load(0); return; }
      stored = mDb->get_call_material(material);
    }
    load(stored.id); mTranscriptId = id;
    const auto row = mDb->get_transcript(id);
    if (row && row->status != "recording") { mEnded = true; mState["ended"] = true; }
  } catch (...) { load(0); }
  emit changed();
}
void CallNotesController::attachTranscript(qint64 id) {
  try {
    if (mInFlight) cancel();
    if (mMaterialId <= 0) beginCall();
    if (!mDb->link_call_material_transcript(mMaterialId, id)) return;
    const auto stored = mDb->get_call_material(mMaterialId);
    mRevision = stored.revision;
    mState = QJsonDocument::fromJson(QByteArray::fromStdString(stored.json)).object();
    mState["ended"] = mEnded;
    mTranscriptId = id; mDirty = true;
    if (mEnabled && !mInFlight) mBatch.start();
  } catch (...) {}
  emit changed();
}
QJsonArray CallNotesController::phrases() const {
  QJsonArray out;
  if (!mMaterialId) return out;
  for (const auto id : mDb->get_call_material_transcript_ids(mMaterialId)) {
    const auto transcript = mDb->get_transcript(id);
    if (!transcript || transcript->consent_revoked_at) continue;
    for (const auto &p : mDb->get_transcript_phrases(id)) {
      out.append(QJsonObject{{"id", QString::number(p.id)}, {"speaker", QString::fromStdString(p.speaker_name.value_or(p.track_role))},
          {"text", QString::fromStdString(p.text)}, {"start_ms", static_cast<qint64>(p.start_ms)},
          {"end_ms", static_cast<qint64>(p.end_ms)}, {"transcript_id", static_cast<qint64>(id)}});
    }
  }
  return out;
}
void CallNotesController::addPhrase(const DuckTranscriptPhrase &p) {
  if (p.transcript_id == mTranscriptId) markDirty();
}
void CallNotesController::transcriptChanged(qint64 transcriptId) {
  if (transcriptId != mTranscriptId) return;
  if (!mInFlight && mMaterialId > 0) syncStored(); // Pick up derived notes the change invalidated.
  markDirty();
}
void CallNotesController::markDirty() {
  mDirty = true;
  // A pending retry already covers new phrases; keep its backoff.
  if (mEnabled && !mInFlight && !mEnded && !mRetry.isActive()) mBatch.start();
  emit changed();
}
void CallNotesController::setGenerationEnabled(bool enabled) {
  mEnabled = enabled;
  if (enabled) mDeferred = false;
  if (!enabled) { mBatch.stop(); stopRetrying(); return; }
  if (mDirty && !mInFlight) mBatch.start();
}
// The meeting summary is offered, never started: finalStatus() reports
// "offered" until the user asks for it.
void CallNotesController::finishCall() {
  mEnded = true; mState["ended"] = true;
  persist(); emit changed();
}
void CallNotesController::requestFinal() {
  if (mMaterialId <= 0 || !mEnded) return;
  if (!syncStored()) { mState["storage_error"] = true; emit changed(); return; }
  emit generationRequested();
  if (mDeferred && !mEnabled) return;
  if (!mEnabled) { mState["final_status"] = "unavailable"; persist(); emit changed(); return; }
  mFinalPending = true; mState["final_status"] = "queued";
  persist(); flush(); emit changed();
}
void CallNotesController::deferFinal() {
  ++mGeneration; mEnabled = false; mDeferred = true; mInFlight = false;
  mRequest = {}; mFinalPending = true; mBatch.stop(); stopRetrying(); mState["final_status"] = "queued";
  persist(); emit changed();
}
void CallNotesController::flush() {
  mBatch.stop();
  if (!mEnabled || mInFlight || !mMaterialId) return;
  mRetry.stop();
  if (mFinalPending) dispatch(true);
  else if (mDirty && !mEnded) dispatch(false);
}
void CallNotesController::refreshFromStorage() {
  if (!mInFlight && mMaterialId > 0 && syncStored(true)) emit changed();
}
void CallNotesController::dispatch(bool final) {
  if (!syncStored()) { mState["storage_error"] = true; emit changed(); return; }
  QJsonArray inputs;
  try { inputs = phrases(); } catch (...) { workerFailed(); return; }
  if (inputs.isEmpty()) {
    mState[final ? "final_status" : "live_status"] = "unavailable";
    mFinalPending = false; mDirty = false; persist(); emit changed(); return;
  }
  QJsonArray known;
  if (!final) {
    // Live points are incremental: only phrases after the last successful run.
    const auto through = mState["live_through_id"].toInteger();
    QJsonArray fresh;
    for (const auto &p : inputs) if (p.toObject()["id"].toString().toLongLong() > through) fresh.append(p);
    for (const auto &point : livePoints()) known.append(point.toObject()["text"]);
    if (fresh.isEmpty() || known.size() >= kMaxLivePoints) { mDirty = false; return; }
    inputs = fresh;
  }
  mDirty = false; mFinalPending = false;
  mRequest = {{"protocol_version", 1}, {"task", final ? "notes" : "live_points"}, {"material_id", mMaterialId},
      {"request_token", QUuid::createUuid().toString(QUuid::WithoutBraces)},
      {"request_id", ++mRequestId}, {"session_generation", mGeneration}, {"ledger_revision", mRevision},
      {"final", final}, {"utterances", inputs}};
  if (!final) mRequest["known_points"] = known;
  else {
    mRequest["task"] = "summary";
    mRequest["call_date"] = callDate().toString(Qt::ISODate);
    QJsonArray agreements;
    for (const auto &value : loadMaterialAgreements(*mDb, mMaterialId)) {
      const auto record = value.toObject();
      if (record["active"].toBool(true) && !record["text"].toString().trimmed().isEmpty())
        agreements.append(record["text"].toString().trimmed());
    }
    mRequest["agreements"] = agreements;
  }
  mState[final ? "final_status" : "live_status"] = "running";
  mState.remove("error_reason");
  if (!persist()) return;
  mInFlight = true; emit changed(); emit requestReady(mRequest);
}
void CallNotesController::cancel() {
  const auto token = mRequest["request_token"].toString();
  if (mMaterialId > 0) syncStored();
  mEnabled = false;
  mDeferred = false;
  ++mGeneration; mInFlight = false; mFinalPending = false; mBatch.stop(); stopRetrying(); mRequest = {};
  mState[mEnded ? "final_status" : "live_status"] = "cancelled";
  persist(); emit changed(); emit cancelRequested(token);
}
void CallNotesController::revoke() {
  cancel(); mEnabled = false; mState.remove("live"); mState.remove("final");
  mState["final_status"] = "cancelled"; mState["live_status"] = "cancelled";
  persist(); emit changed();
}
void CallNotesController::transcriptDeleted(qint64 id) {
  try {
    const auto linked = mDb->get_call_material_transcript_ids(mMaterialId);
    if (id != mTranscriptId && std::find(linked.begin(), linked.end(), id) == linked.end()) return;
    revoke();
  } catch (...) { revoke(); }
}
void CallNotesController::workerFailed() { fail(QStringLiteral("runtime")); }
void CallNotesController::fail(const QString &reason) {
  if (!mInFlight) return;
  mState[mRequest["final"].toBool() ? "final_status" : "live_status"] = "error";
  // A summary the user asked for while a live request ran would otherwise stay queued forever.
  if (mFinalPending) mState["final_status"] = "error";
  mState["error_reason"] = reason;
  mInFlight = false; mFinalPending = false; mBatch.stop(); stopRetrying(); mEnabled = false;
  ++mGeneration; mRequest = {}; persist(); emit changed();
}
// Live notes rerun on the whole call so far. A failed update must not stop
// them for the rest of a long call: try again with a growing delay, picking
// up phrases that arrived meanwhile. The final note stays a manual action.
void CallNotesController::retryLater() {
  constexpr int kMaxRetryDelayMs = 5 * 60 * 1000;
  const int delay = static_cast<int>(std::min<qint64>(kMaxRetryDelayMs, qint64(mRetryDelayMs) << std::min(mRetries, 8)));
  ++mRetries;
  mInFlight = false; mRequest = {}; mBatch.stop();
  mState["live_status"] = "queued";
  mRetry.start(delay);
  persist(); emit changed();
}
void CallNotesController::stopRetrying() { mRetry.stop(); mRetries = 0; }
void CallNotesController::handleResult(const QJsonObject &result) {
  if (!mInFlight || result["task"] != mRequest["task"] || result["material_id"] != mMaterialId ||
      result["request_id"] != mRequest["request_id"] || result["session_generation"] != mGeneration ||
      result["request_token"] != mRequest["request_token"] || result["ledger_revision"] != mRequest["ledger_revision"]) return;
  if (result.contains("error") || !result["notes"].isObject()) {
    const auto reason = result["reason"].toString(QStringLiteral("runtime"));
    const bool transient = reason == "runtime" || reason == "timeout" || reason == "invalid_output";
    if (transient && !mRequest["final"].toBool() && !mEnded && mEnabled) retryLater();
    else fail(reason);
    return;
  }
  const bool final = mRequest["final"].toBool();
  const auto inputs = mRequest["utterances"].toArray();
  QJsonArray current;
  try { current = phrases(); } catch (...) { workerFailed(); return; }
  QSet<QString> allowed;
  for (const auto &p : inputs) {
    if (!current.contains(p)) { mInFlight = false; mState[final ? "final_status" : "live_status"] = "stale"; persist(); emit changed(); return; }
    allowed.insert(p.toObject()["id"].toString());
  }
  auto notes = result["notes"].toObject();
  if (!final) { appendLivePoints(notes, inputs, allowed); return; }
  if (notes["format"].toInt() != 2 || !notes["overview"].isObject() || !validItem(notes["overview"].toObject(), allowed) ||
      !validList(notes["key_points"], 10, allowed) || !validList(notes["tasks"], 8, allowed, true) ||
      !validList(notes["open_questions"], 5, allowed)) { fail(QStringLiteral("invalid_output")); return; }
  if (current != inputs) {
    mInFlight = false; mFinalPending = true; flush(); return;
  }
  // Another view may have saved manual text while inference was running.
  // Merge the validated proposal onto that revision, preserving manual edits.
  if (!syncStored()) { fail(QStringLiteral("storage")); return; }
  notes["sources"] = inputs; notes["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
  mState[final ? "final" : "live"] = notes;
  mState[final ? "final_status" : "live_status"] = "ready";
  mState["processed_ms"] = inputs.last().toObject()["end_ms"];
  mInFlight = false; mRequest = {}; mRetries = 0; persist(); emit changed();
  if (mFinalPending || (mDirty && !mEnded)) mBatch.start();
}
void CallNotesController::appendLivePoints(const QJsonObject &notes, const QJsonArray &inputs,
                                           const QSet<QString> &allowed) {
  if (notes.size() != 1 || !notes["key_points"].isArray()) { fail(QStringLiteral("invalid_output")); return; }
  const auto found = notes["key_points"].toArray();
  for (const auto &point : found)
    if (!point.isObject() || point.toObject()["text"].toString().trimmed().isEmpty() ||
        !validItem(point.toObject(), allowed)) { fail(QStringLiteral("invalid_output")); return; }
  // Another view may have saved manual text while inference was running.
  if (!syncStored()) { fail(QStringLiteral("storage")); return; }
  auto live = mState["live"].toObject();
  live.remove("note");
  auto points = live["key_points"].toArray();
  QSet<QString> cited;
  QSet<QString> known;
  for (const auto &point : points)
    known.insert(pcm::prompter::notes::pointKey(point.toObject()["text"].toString()));
  for (const auto &point : found) {
    if (points.size() >= kMaxLivePoints) break;
    const auto key = pcm::prompter::notes::pointKey(point.toObject()["text"].toString());
    if (known.contains(key)) continue;
    known.insert(key);
    points.append(point);
    for (const auto &id : point.toObject()["evidence_ids"].toArray()) cited.insert(id.toString());
  }
  // Keep only the phrases the points cite: an edit elsewhere in the call does
  // not make the live points stale.
  auto sources = live["sources"].toArray();
  for (const auto &p : inputs) if (cited.contains(p.toObject()["id"].toString())) sources.append(p);
  live["key_points"] = points; live["sources"] = sources;
  if (!live.contains("id")) live["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
  mState["live"] = live;
  mState["live_status"] = "ready";
  mState["processed_ms"] = inputs.last().toObject()["end_ms"];
  mState["live_through_id"] = inputs.last().toObject()["id"].toString().toLongLong();
  mInFlight = false; mRequest = {}; mRetries = 0; persist(); emit changed();
  if (mFinalPending || (mDirty && !mEnded)) mBatch.start();
}
QString CallNotesController::manualText() const { return mState["manual_text"].toString(); }
QJsonArray CallNotesController::noteEntries() const { return mState["manual_entries"].toArray(); }

QStringList CallNotesController::draftAttachmentPaths() const {
  QStringList paths;
  const auto root = QFileInfo(mAttachmentRoot).canonicalFilePath();
  if (root.isEmpty()) return paths;
  for (const auto &value : mState["manual_draft_attachments"].toArray()) {
    const auto attachment = value.toObject();
    const auto id = attachment["id"].toString();
    if (QUuid::fromString(id).isNull()) continue;
    const auto relative = QDir::cleanPath(attachment["relative_path"].toString());
    const auto prefix = QStringLiteral("calls/%1/draft/%2/").arg(mMaterialId).arg(id);
    if (QDir::isAbsolutePath(relative) || !relative.startsWith(prefix)) continue;
    const QFileInfo file(QDir(root).filePath(relative));
    if (file.canonicalPath().startsWith(root + QLatin1Char('/')) &&
        (!file.exists() || file.canonicalFilePath().startsWith(root + QLatin1Char('/'))))
      paths.append(file.absoluteFilePath());
  }
  return paths;
}

bool CallNotesController::setDraftAttachmentPaths(const QStringList &paths) {
  if (!mMaterialId || paths.size() > 64 || !syncStored()) return false;
  const auto previous = mState["manual_draft_attachments"].toArray();
  const auto previousPaths = draftAttachmentPaths();
  if (paths == previousPaths) return true;
  QJsonArray next, created, removed;
  QSet<QString> seen;
  for (const auto &path : paths) {
    const QFileInfo source(path);
    if (!source.isFile() || !source.isReadable()) { removeDraftFiles(created, mAttachmentRoot, mMaterialId); return false; }
    const auto canonical = source.canonicalFilePath();
    if (seen.contains(canonical)) continue;
    seen.insert(canonical);
    bool reused = false;
    for (const auto &value : previous) {
      const auto stored = value.toObject();
      if (QFileInfo(QDir(mAttachmentRoot).filePath(stored["relative_path"].toString())).canonicalFilePath() == canonical) {
        next.append(stored); reused = true; break;
      }
    }
    if (reused) continue;
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto directory = QStringLiteral("calls/%1/draft/%2").arg(mMaterialId).arg(id);
    const auto relative = directory + QLatin1Char('/') + source.fileName();
    const QJsonObject attachment{{"id", id}, {"file_name", source.fileName()}, {"relative_path", relative}};
    created.append(attachment);
    if (!QDir().mkpath(QDir(mAttachmentRoot).filePath(directory)) ||
        !QFile::copy(source.absoluteFilePath(), QDir(mAttachmentRoot).filePath(relative))) {
      removeDraftFiles(created, mAttachmentRoot, mMaterialId); return false;
    }
    next.append(attachment);
  }
  for (const auto &value : previous) if (!next.contains(value)) removed.append(value);
  const auto before = mState; mState["manual_draft_attachments"] = next;
  if (!persist()) { mState = before; removeDraftFiles(created, mAttachmentRoot, mMaterialId); return false; }
  removeDraftFiles(removed, mAttachmentRoot, mMaterialId);
  emit changed(); return true;
}

QString CallNotesController::noteAttachmentPath(const QString &entryId, const QString &attachmentId) const {
  const auto root = QFileInfo(mAttachmentRoot).canonicalFilePath();
  if (root.isEmpty()) return {};
  for (const auto &value : noteEntries()) {
    const auto entry = value.toObject();
    if (entry["id"].toString() != entryId) continue;
    for (const auto &item : entry["attachments"].toArray()) {
      const auto attachment = item.toObject();
      if (attachment["id"].toString() != attachmentId) continue;
      const auto relative = attachment["relative_path"].toString();
      if (relative.isEmpty() || QDir::isAbsolutePath(relative)) return {};
      const auto expectedPrefix = QStringLiteral("calls/%1/%2/%3/").arg(mMaterialId).arg(entryId, attachmentId);
      const auto clean = QDir::cleanPath(relative);
      if (!clean.startsWith(expectedPrefix)) return {};
      const QFileInfo file(QDir(root).filePath(clean));
      const auto canonical = file.canonicalFilePath();
      return file.isFile() && canonical.startsWith(root + QLatin1Char('/')) ? canonical : QString{};
    }
  }
  return {};
}

QString CallNotesController::saveNoteEntry(const QString &markdown, const QStringList &attachmentPaths,
                                         const QString &entryId) {
  if (!mMaterialId || markdown.size() > 100000 || attachmentPaths.size() > 64) return {};
  if (!entryId.isEmpty() && QUuid::fromString(entryId).isNull()) return {};
  const auto draftBefore = manualText();
  const auto draftAttachmentsBefore = mState["manual_draft_attachments"].toArray();
  QJsonObject knownEntry;
  if (!entryId.isEmpty())
    for (const auto &value : noteEntries())
      if (value.toObject()["id"].toString() == entryId) knownEntry = value.toObject();
  if (!syncStored()) return {};
  const auto id = entryId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : entryId;
  auto entries = noteEntries();
  int index = -1;
  for (int i = 0; i < entries.size(); ++i)
    if (entries[i].toObject()["id"].toString() == id) { index = i; break; }
  if (!entryId.isEmpty() && index < 0) return {};
  auto entry = index < 0 ? QJsonObject{} : entries[index].toObject();
  if (!entryId.isEmpty() && (knownEntry.isEmpty() || knownEntry != entry)) return {};
  auto attachments = entry["attachments"].toArray();
  if (attachments.size() + attachmentPaths.size() > 64 ||
      (markdown.trimmed().isEmpty() && attachments.isEmpty() && attachmentPaths.isEmpty())) return {};
  QStringList copiedDirectories;
  auto cleanup = [&] { for (const auto &directory : copiedDirectories) QDir(directory).removeRecursively(); };
  QMimeDatabase mime;
  for (const auto &source : attachmentPaths) {
    const QFileInfo info(source);
    if (!info.isFile() || !info.isReadable()) { cleanup(); return {}; }
    const auto attachmentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto directory = QStringLiteral("calls/%1/%2/%3").arg(mMaterialId).arg(id, attachmentId);
    const auto absoluteDirectory = QDir(mAttachmentRoot).filePath(directory);
    if (!QDir().mkpath(absoluteDirectory)) { cleanup(); return {}; }
    copiedDirectories.append(absoluteDirectory);
    const auto relative = directory + QLatin1Char('/') + info.fileName();
    if (!QFile::copy(info.absoluteFilePath(), QDir(mAttachmentRoot).filePath(relative))) {
      cleanup(); return {};
    }
    attachments.append(QJsonObject{{"id", attachmentId}, {"file_name", info.fileName()},
      {"relative_path", relative}, {"mime_type", mime.mimeTypeForFile(info).name()},
      {"size_bytes", info.size()}});
  }
  const auto now = QDateTime::currentMSecsSinceEpoch();
  entry["id"] = id; entry["body_markdown"] = markdown;
  entry["updated_at"] = now;
  if (!entry.contains("created_at")) entry["created_at"] = now;
  entry["attachments"] = attachments;
  // Merge onto the newest material so finalization and another notes view cannot
  // replace the saved history. Copied files are published only with the entry.
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (!syncStored()) break;
    const auto before = mState;
    entries = noteEntries();
    int current = -1;
    for (int i = 0; i < entries.size(); ++i)
      if (entries[i].toObject()["id"].toString() == id) { current = i; break; }
    if (!entryId.isEmpty() && current < 0) break;
    if (!entryId.isEmpty() && entries[current].toObject() != knownEntry) break;
    if (current < 0) entries.append(entry); else entries[current] = entry;
    mState["manual_entries"] = entries;
    if (entryId.isEmpty() && (manualText() == markdown || manualText() == draftBefore))
      mState["manual_text"] = QString{};
    const bool clearDraftAttachments = entryId.isEmpty() &&
        mState["manual_draft_attachments"].toArray() == draftAttachmentsBefore;
    if (clearDraftAttachments) mState["manual_draft_attachments"] = QJsonArray{};
    if (persist()) {
      if (clearDraftAttachments) removeDraftFiles(draftAttachmentsBefore, mAttachmentRoot, mMaterialId);
      emit changed(); return id;
    }
    mState = before;
  }
  cleanup(); return {};
}

bool CallNotesController::removeNoteEntry(const QString &entryId) {
  if (QUuid::fromString(entryId).isNull() || !syncStored()) return false;
  auto entries = noteEntries();
  for (int i = 0; i < entries.size(); ++i) {
    if (entries[i].toObject()["id"].toString() != entryId) continue;
    const auto before = mState; entries.removeAt(i); mState["manual_entries"] = entries;
    if (!persist()) { mState = before; return false; }
    const auto relative = QStringLiteral("calls/%1/%2").arg(mMaterialId).arg(entryId);
    const auto root = QFileInfo(mAttachmentRoot).canonicalFilePath();
    const auto directory = QFileInfo(QDir(mAttachmentRoot).filePath(relative)).canonicalFilePath();
    if (!root.isEmpty() && directory.startsWith(root + QLatin1Char('/'))) QDir(directory).removeRecursively();
    emit changed(); return true;
  }
  return false;
}
QJsonObject CallNotesController::summary() const {
  const auto final = mState["final"].toObject();
  if (final.isEmpty() || final["dismissed"].toBool()) return {};
  if (final["format"].toInt() == 2) return final;
  // Saved before format 2: a short note and its points.
  return {{"format", 1}, {"overview", final["note"]}, {"key_points", final["key_points"]},
          {"tasks", QJsonArray{}}, {"open_questions", QJsonArray{}}, {"sources", final["sources"]}};
}
// Live notes keep only key points; a note left by older versions is ignored.
QJsonObject CallNotesController::draft() const { return summary()["overview"].toObject(); }
QJsonArray CallNotesController::keyPoints() const {
  return mState["final"].isObject() ? summary()["key_points"].toArray() : livePoints();
}
QDate CallNotesController::callDate() const {
  try {
    const auto material = mDb->get_call_material(mMaterialId);
    if (material.event_id)
      if (const auto event = mDb->get_event(*material.event_id); event && event->start_date)
        return QDateTime::fromMSecsSinceEpoch(*event->start_date).toLocalTime().date();
    if (material.created_at > 0) return QDateTime::fromMSecsSinceEpoch(material.created_at).toLocalTime().date();
  } catch (...) {}
  return QDate::currentDate();
}
QJsonArray CallNotesController::livePoints() const { return mState["live"].toObject()["key_points"].toArray(); }
QJsonObject CallNotesController::source(const QString &id) const {
  for (const auto *key : {"final", "live"})
    for (const auto &p : mState[key].toObject()["sources"].toArray())
      if (p.toObject()["id"].toString() == id) return p.toObject();
  return {};
}
bool CallNotesController::sourcesCurrent() const {
  const auto packet = mState[mState["final"].isObject() ? "final" : "live"].toObject();
  if (packet.isEmpty()) return false;
  try {
    const auto current = phrases();
    for (const auto &p : packet["sources"].toArray()) if (!current.contains(p)) return false;
    return true;
  } catch (...) { return false; }
}
bool CallNotesController::setManualText(const QString &text) {
  if (text.size() > 100000) return false;
  const auto before = mState; mState["manual_text"] = text;
  if (!persist()) {
    // Source edits can invalidate derived state in another view. Merge only
    // this user's manual text onto the latest revision, never stale proposals.
    try {
      const auto latest = mDb->get_call_material(mMaterialId);
      QJsonParseError error;
      const auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(latest.json), &error);
      if (latest.id <= 0 || error.error != QJsonParseError::NoError || !doc.isObject()) { mState = before; return false; }
      mState = doc.object(); mRevision = latest.revision; mState["manual_text"] = text;
      if (!persist()) { mState = before; return false; }
    } catch (...) { mState = before; return false; }
  }
  emit changed(); return true;
}
bool CallNotesController::editDraft(const QString &text) {
  if (!mState["final"].isObject() || !sourcesCurrent() || text.trimmed().isEmpty() || text.size() > 6000) return false;
  const auto before = mState;
  auto packet = mState["final"].toObject();
  const auto field = packet["format"].toInt() == 2 ? "overview" : "note";
  auto note = packet[field].toObject(); note["text"] = text;
  packet[field] = note; mState["final"] = packet;
  if (!persist()) { mState = before; return false; } emit changed(); return true;
}
bool CallNotesController::dismissDraft() {
  if (!mState["final"].isObject()) return false;
  const auto before = mState;
  auto packet = mState["final"].toObject(); packet["dismissed"] = true; mState["final"] = packet;
  if (!persist()) { mState = before; return false; } emit changed(); return true;
}
// Agreements live in the prompter state and are shown by their own widget;
// only remember which were saved so the prompter does not offer them twice.
bool CallNotesController::appendAgreement(const QString &id, const QString &text) {
  if (id.isEmpty() || text.trimmed().isEmpty() || text.size() > 16000) return false;
  auto saved = mState["agreement_notes"].toObject();
  if (saved.contains(id)) return true;
  const auto before = mState;
  saved[id] = true; mState["agreement_notes"] = saved;
  if (!persist()) { mState = before; return false; }
  emit changed(); return true;
}
QString CallNotesController::liveStatus() const { return mState["live_status"].toString("idle"); }
QString CallNotesController::finalStatus() const {
  const bool hasFinal = mState["final"].isObject() && !mState["final"].toObject()["dismissed"].toBool();
  if (hasFinal && !sourcesCurrent()) return QStringLiteral("stale");
  const auto status = mState["final_status"].toString("idle");
  // An ended meeting with a transcript and no (or a dismissed) summary offers one.
  if (mEnded && !hasFinal && mTranscriptId > 0 && (status == "idle" || status == "ready"))
    return QStringLiteral("offered");
  return status;
}
QString CallNotesController::statusText() const {
  if (mState["storage_error"].toBool()) return tr("Could not save call materials. Reopen the call and try again.");
  const auto status = mEnded ? finalStatus() : liveStatus();
  if (status == "running") return mEnded ? tr("Summarizing the meeting…") : tr("Updating key points…");
  if (status == "queued") return tr("Waiting for local processing");
  if (status == "ready") return mEnded ? tr("Meeting summary saved with this call") : tr("Key points updated through %1").arg(timeLabel(mState["processed_ms"].toInteger()));
  if (status == "error") {
    const auto reason = mState["error_reason"].toString();
    // Decoding is greedy: the same transcript gives the same reply, so a retry
    // only helps once the input changes (more phrases, an edited source).
    if (reason == "invalid_output")
      return mEnded ? tr("The model's reply could not be used for this transcript. Write the note yourself or edit the transcript and retry.")
                    : tr("The model's reply could not be used. Retry once more of the conversation is transcribed.");
    if (reason == "too_long") return tr("The transcript is too long for local notes.");
    if (reason == "storage") return tr("Could not save the generated notes. Try again.");
    if (reason == "timeout") return tr("The local model took too long. Try again.");
    return tr("The local model did not respond. Try again.");
  }
  if (status == "stale") return tr("The transcript changed. Generate the note again.");
  if (status == "cancelled") return tr("Processing cancelled. Your notes are saved.");
  if (status == "interrupted") return tr("Processing was interrupted. You can retry.");
  if (status == "unavailable") return tr("A saved transcript and a local model are required.");
  if (status == "offered") return tr("Summarize the whole meeting when you are ready.");
  return tr("Notes are saved with this call. A client is optional.");
}
