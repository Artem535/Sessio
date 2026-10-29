#include "call_prompter_controller.h"
#include <QJsonDocument>
#include <QUuid>

namespace {
bool validIds(const QJsonValue &value, const QSet<QString> &allowed) {
  if (!value.isArray() || value.toArray().isEmpty() || value.toArray().size() > 24) return false;
  QSet<QString> seen;
  for (const auto &id : value.toArray()) {
    if (!id.isString() || !allowed.contains(id.toString()) || seen.contains(id.toString())) return false;
    seen.insert(id.toString());
  }
  return true;
}
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
constexpr int kMaxBatch = 6;
constexpr int kMaxFailures = 8; // consecutive failed requests before pausing
}

CallPrompterController::CallPrompterController(std::shared_ptr<pcm::database::Database> db, QObject *parent)
    : QObject(parent), mDb(std::move(db)) {
  mBatchTimer.setSingleShot(true); mBatchTimer.setInterval(1500);
  connect(&mBatchTimer, &QTimer::timeout, this, &CallPrompterController::flush);
  setStatus(tr("Prompter is off"));
}
void CallPrompterController::setStatus(const QString &status) {
  mStatus = status; emit statusChanged(status);
}
QJsonObject CallPrompterController::state() const {
  return {{"cards", mCards}, {"records", mRecords}, {"suggestions", mSuggestions}, {"last_processed", mLastProcessed}};
}
void CallPrompterController::restore(const QJsonObject &s) {
  mCards = s["cards"].toArray(); mRecords = s["records"].toArray();
  mSuggestions = s["suggestions"].toArray(); mLastProcessed = s["last_processed"].toInteger();
}
bool CallPrompterController::persist() {
  const auto json = QJsonDocument(state()).toJson(QJsonDocument::Compact).toStdString();
  if (!mDb->save_prompter_state(mTranscriptId, mRevision, json)) {
    mRunning = false; mBatchTimer.stop();
    setStatus(tr("Could not save prompter changes. Reopen the transcript and try again."));
    return false;
  }
  ++mRevision; return true;
}
void CallPrompterController::beginTranscript(qint64 transcriptId) {
  endCall(); ++mGeneration; mTranscriptId = transcriptId; mRevision = 0; mLastProcessed = 0;
  mBatchLimit = kMaxBatch; mFailures = 0; mSkipLookback = false;
  mCards = {}; mRecords = {}; mSuggestions = {}; mSources.clear(); mLookback = {};
  try {
    const auto row = mDb->get_transcript(transcriptId);
    if (!row) { mTranscriptId = 0; setStatus(tr("Transcript unavailable")); emit cardsChanged(); return; }
    const auto stored = mDb->get_prompter_state(transcriptId);
    mRevision = stored.revision;
    if (!stored.json.empty()) {
      QJsonParseError error;
      const auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(stored.json), &error);
      if (error.error != QJsonParseError::NoError || !doc.isObject()) throw std::runtime_error("Invalid state");
      restore(doc.object());
    }
    for (const auto &p : mDb->get_transcript_phrases(transcriptId)) addPhrase(p);
    setStatus(tr("Prompter is paused"));
  } catch (...) { mTranscriptId = 0; setStatus(tr("Could not open prompter data")); }
  emit cardsChanged();
}
void CallPrompterController::addPhrase(const DuckTranscriptPhrase &p) {
  if (p.transcript_id != mTranscriptId || p.id <= 0 || mSources.contains(QString::number(p.id))) return;
  auto speaker = QString::fromStdString(p.speaker_name.value_or(p.track_role));
  // Persisted unnamed tracks have only a role label; participant attribution needs review.
  if (speaker.isEmpty()) speaker = tr("Participant");
  QJsonObject source{{"id", QString::number(p.id)}, {"speaker", speaker},
      {"text", QString::fromStdString(p.text)}, {"start_ms", static_cast<qint64>(p.start_ms)}, {"end_ms", static_cast<qint64>(p.end_ms)}};
  mSources.insert(QString::number(p.id), source);
  if (p.id > mLastProcessed) mPending.append(source);
  else { mLookback.append(source); while (mLookback.size() > 2) mLookback.removeFirst(); }
  if (mRunning && !mInFlight) mBatchTimer.start();
}
void CallPrompterController::resume() {
  if (!mTranscriptId || !mDb->get_transcript(mTranscriptId)) { setStatus(tr("Start transcription first")); return; }
  mRunning = true; mFailures = 0; setStatus(tr("Waiting for completed phrases"));
  if (!mPending.isEmpty()) mBatchTimer.start();
}
void CallPrompterController::pause() {
  mRunning = false; ++mGeneration; mBatchTimer.stop();
  if (mInFlight) {
    QJsonArray pending = mInFlightPhrases;
    for (const auto &p : mPending) pending.append(p);
    mPending = pending;
  }
  mInFlight = false; mInFlightPhrases = {}; mRequest = {};
  setStatus(tr("Prompter is paused"));
}
void CallPrompterController::endCall() { pause(); mPending = {}; }
bool CallPrompterController::refreshFromStorage() {
  if (!mTranscriptId) return false;
  try {
    const auto stored = mDb->get_prompter_state(mTranscriptId);
    if (stored.revision == mRevision || stored.json.empty()) return false;
    const auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(stored.json));
    if (!doc.isObject()) return false;
    // An in-flight reply was computed against the old ledger: drop it and
    // send its phrases again with the new records as candidates.
    const bool running = mRunning;
    if (mInFlight) pause();
    restore(doc.object()); mRevision = stored.revision;
    if (running && !mRunning) resume();
  } catch (...) { return false; }
  emit cardsChanged(); return true;
}
void CallPrompterController::transcriptDeleted(qint64 id) {
  if (id == mTranscriptId) beginTranscript(0);
}
bool CallPrompterController::evidenceAvailable(const QString &id) const {
  const auto index = cardIndex(id);
  return index >= 0 && evidenceUnchanged(mCards[index].toObject());
}
bool CallPrompterController::evidenceUnchanged(const QJsonObject &card) const {
  try {
    const auto snapshots = card["evidence_snapshot"].toArray();
    if (snapshots.isEmpty()) return false;
    const auto phrases = mDb->get_transcript_phrases(mTranscriptId);
    for (const auto &value : snapshots) {
      const auto snapshot = value.toObject(); bool found = false;
      for (const auto &p : phrases) {
        if (QString::number(p.id) != snapshot["id"].toString()) continue;
        auto speaker = QString::fromStdString(p.speaker_name.value_or(p.track_role));
        if (speaker.isEmpty()) speaker = tr("Participant");
        found = QString::fromStdString(p.text) == snapshot["text"].toString() && speaker == snapshot["speaker"].toString();
        break;
      }
      if (!found) return false;
    }
    return true;
  } catch (...) { return false; }
}
void CallPrompterController::workerFailed() { pause(); setStatus(tr("Local prompter failed. Check its settings and retry.")); }
// Decoding is greedy, so the same input fails the same way. Retry the batch
// in halves, then once without lookback, then skip the phrase. Only a run of
// failures, as from a broken runtime, pauses the prompter.
void CallPrompterController::batchFailed() {
  if (++mFailures >= kMaxFailures) { workerFailed(); return; }
  const auto phrases = mInFlightPhrases;
  const bool hadLookback = mRequest["utterances"].toArray().size() > phrases.size();
  mInFlight = false; mInFlightPhrases = {}; mRequest = {};
  if (phrases.size() > 1 || hadLookback) {
    mBatchLimit = qMax<int>(1, phrases.size() / 2);
    mSkipLookback = phrases.size() == 1;
    for (auto i = phrases.size(); i > 0; --i) mPending.prepend(phrases[i - 1]);
    setStatus(tr("Retrying a smaller part of the conversation"));
  } else {
    const auto previous = state();
    for (const auto &p : phrases) mLastProcessed = qMax(mLastProcessed, p.toObject()["id"].toString().toLongLong());
    if (!persist()) { restore(previous); return; }
    mSkipLookback = false;
    setStatus(tr("Skipped a phrase the local model could not process"));
  }
  if (mRunning && !mPending.isEmpty()) mBatchTimer.start();
}
void CallPrompterController::flush() {
  mBatchTimer.stop();
  if (!mRunning || mInFlight || mPending.isEmpty()) return;
  QJsonArray utterances = mSkipLookback ? QJsonArray{} : mLookback;
  int chars = 0;
  for (const auto &p : utterances) chars += p.toObject()["text"].toString().size();
  // Lookback is optional context and must not starve a valid queued phrase.
  const auto nextSize = mPending.first().toObject()["text"].toString().size();
  while (!utterances.isEmpty() && chars + nextSize > 11000) {
    chars -= utterances.first().toObject()["text"].toString().size();
    utterances.removeFirst();
  }
  mInFlightPhrases = {};
  while (!mPending.isEmpty() && mInFlightPhrases.size() < mBatchLimit) {
    const auto p = mPending.first();
    const auto size = p.toObject()["text"].toString().size();
    if (size > 6000 || chars + size > 11000) {
      if (mInFlightPhrases.isEmpty()) { workerFailed(); setStatus(tr("A phrase is too long. Review it in the transcript.")); return; }
      break;
    }
    utterances.append(p); mInFlightPhrases.append(p); mPending.removeFirst(); chars += size;
  }
  QJsonArray candidates;
  for (const auto &r : mRecords) if (r.toObject()["active"].toBool(true)) candidates.append(r);
  for (const auto &value : mCards) {
    const auto c = value.toObject();
    if (c["decision"].toString() == "pending" && c["kind"].toString() == "accept" &&
        c["target_id"].toString().isEmpty() && !c["unresolved"].toBool())
      candidates.append(QJsonObject{{"id", c["id"]}, {"text", c["action"]}, {"participants", c["participants"]}});
  }
  while (candidates.size() > 32) candidates.removeFirst();
  mRequest = {{"protocol_version", 1}, {"request_id", ++mRequestId}, {"session_generation", mGeneration},
      {"ledger_revision", mRevision}, {"utterances", utterances}, {"candidates", candidates},
      {"call_date", (mCallDate.isValid() ? mCallDate : QDate::currentDate()).toString(Qt::ISODate)}};
  mInFlight = true;
  setStatus(tr("Processing conversation · %n phrases waiting", nullptr, mPending.size()));
  emit requestReady(mRequest);
}
void CallPrompterController::handleResult(const QJsonObject &result) {
  if (!mInFlight || result["request_id"] != mRequest["request_id"] ||
      result["session_generation"] != mRequest["session_generation"]) return;
  if (result["ledger_revision"].toInteger(-1) != mRevision) {
    pause(); resume(); return; // Keep all in-flight phrases for a fresh snapshot.
  }
  if (result["protocol_version"].toInt() != 1 || result.contains("error") ||
      !result["events"].isArray() || result["events"].toArray().size() > 12 || !result["suggestions"].isArray()) {
    batchFailed(); return;
  }
  QSet<QString> ids, speakers, targets;
  for (const auto &u : mRequest["utterances"].toArray()) {
    ids.insert(u.toObject()["id"].toString()); speakers.insert(u.toObject()["speaker"].toString());
  }
  for (const auto &r : mRequest["candidates"].toArray()) targets.insert(r.toObject()["id"].toString());
  // Lookback phrases are context: an event citing only them was proposed by an earlier request.
  QSet<QString> fresh;
  for (const auto &p : mInFlightPhrases) fresh.insert(p.toObject()["id"].toString());
  QJsonArray proposed;
  for (const auto &value : result["events"].toArray()) {
    const auto event = value.toObject(); const auto kind = event["kind"].toString();
    const auto target = event["target_id"].toString(); const auto action = event["action"].toString().trimmed();
    if (!value.isObject() || (kind != "accept" && kind != "cancel") || action.isEmpty() || action.size() > 2000 ||
        !validIds(event["evidence_ids"], ids) || !validIds(event["participants"], speakers) ||
        (!target.isEmpty() && !targets.contains(target)) || !event["unresolved"].isBool() ||
        (event.contains("meeting") && !event["meeting"].isBool()) ||
        (event.contains("date") && !event["date"].isString()) || (event.contains("time") && !event["time"].isString())) {
      batchFailed(); return;
    }
    bool citesFresh = false;
    for (const auto &sourceId : event["evidence_ids"].toArray()) citesFresh = citesFresh || fresh.contains(sourceId.toString());
    if (!citesFresh) continue;
    auto card = event;
    card["extracted_action"] = event["action"];
    QJsonArray snapshots;
    for (const auto &sourceId : event["evidence_ids"].toArray()) snapshots.append(mSources.value(sourceId.toString()));
    card["evidence_snapshot"] = snapshots;
    card["unresolved"] = event["unresolved"].toBool() || (kind == "cancel" && target.isEmpty());
    bool duplicate = false;
    for (const auto &old : mCards) {
      const auto c = old.toObject();
      const auto extracted = c.contains("extracted_action") ? c["extracted_action"] : c["action"];
      if (c["kind"] == card["kind"] && extracted == card["extracted_action"] && c["evidence_ids"] == card["evidence_ids"]) duplicate = true;
    }
    if (!duplicate) { card["id"] = uuid(); card["decision"] = "pending"; proposed.append(card); }
  }
  const auto previous = state();
  for (const auto &card : proposed) mCards.append(card);
  mSuggestions = {};
  for (const auto &hint : result["suggestions"].toArray()) {
    const auto h = hint.toObject();
    if (!hint.isObject() || h["text"].toString().isEmpty() || h["text"].toString().size() > 400 || !validIds(h["evidence_ids"], ids)) continue;
    mSuggestions.append(h); break;
  }
  for (const auto &p : mInFlightPhrases) mLastProcessed = qMax(mLastProcessed, p.toObject()["id"].toString().toLongLong());
  if (!persist()) { restore(previous); emit cardsChanged(); return; }
  for (const auto &p : mInFlightPhrases) { mLookback.append(p); while (mLookback.size() > 2) mLookback.removeFirst(); }
  const auto last = mInFlightPhrases.last().toObject()["end_ms"].toInteger();
  mBatchLimit = kMaxBatch; mFailures = 0; mSkipLookback = false;
  mInFlight = false; mInFlightPhrases = {}; mRequest = {};
  setStatus(tr("Processed through %1:%2 · %n phrases waiting", nullptr, mPending.size())
            .arg(last / 60000).arg((last / 1000) % 60, 2, 10, QLatin1Char('0')));
  emit cardsChanged();
  if (mRunning && !mPending.isEmpty()) mBatchTimer.start();
}
int CallPrompterController::cardIndex(const QString &id) const {
  for (int i = 0; i < mCards.size(); ++i) if (mCards[i].toObject()["id"].toString() == id) return i;
  return -1;
}
bool CallPrompterController::acceptCard(const QString &id, const QString &editedText) {
  const auto index = cardIndex(id); if (index < 0) return false;
  auto card = mCards[index].toObject();
  if (card["decision"].toString() != "pending" || card["unresolved"].toBool()) return false;
  if (!evidenceUnchanged(card)) return false;
  const auto action = editedText.isEmpty() ? card["action"].toString() : editedText.trimmed();
  if (action.isEmpty() || action.size() > 2000) return false;
  const auto old = state();
  auto target = card["target_id"].toString(); int recordIndex = -1;
  for (int i = 0; i < mRecords.size(); ++i) if (mRecords[i].toObject()["id"].toString() == target) recordIndex = i;
  if (!target.isEmpty() && recordIndex < 0) {
    const auto originalIndex = cardIndex(target);
    if (originalIndex < 0) return false;
    auto original = mCards[originalIndex].toObject();
    if (original["decision"].toString() != "pending" || original["kind"].toString() != "accept" ||
        !original["target_id"].toString().isEmpty()) return false;
    original["decision"] = "superseded"; mCards[originalIndex] = original;
    card["previous_action"] = original["action"];
  } else if (recordIndex >= 0) {
    if (!mRecords[recordIndex].toObject()["active"].toBool(true)) return false;
    card["previous_action"] = mRecords[recordIndex].toObject()["text"];
  }
  QJsonObject record{{"id", target.isEmpty() ? id : target}, {"text", action},
      {"participants", card["participants"]}, {"active", card["kind"].toString() != "cancel"}, {"evidence_ids", card["evidence_ids"]},
      {"meeting", card["meeting"].toBool(false)}, {"date", card["date"].toString()}, {"time", card["time"].toString()}};
  if (recordIndex >= 0) mRecords[recordIndex] = record; else mRecords.append(record);
  card["action"] = action; card["decision"] = "accepted"; mCards[index] = card;
  if (!persist()) { restore(old); emit cardsChanged(); return false; }
  emit cardsChanged(); return true;
}
bool CallPrompterController::resolveCard(const QString &id, const QString &targetId) {
  const auto index = cardIndex(id); if (index < 0) return false;
  auto card = mCards[index].toObject();
  if (card["decision"].toString() != "pending" || !card["unresolved"].toBool()) return false;
  bool valid = targetId.isEmpty() && card["kind"].toString() == "accept";
  for (const auto &r : mRecords) if (r.toObject()["id"].toString() == targetId && r.toObject()["active"].toBool(true)) valid = true;
  for (const auto &value : mCards) {
    const auto c = value.toObject();
    if (c["id"].toString() == targetId && targetId != id && c["decision"].toString() == "pending" &&
        c["kind"].toString() == "accept" && c["target_id"].toString().isEmpty() && !c["unresolved"].toBool()) valid = true;
  }
  if (!valid) return false;
  const auto old = state(); card["target_id"] = targetId; card["unresolved"] = false; mCards[index] = card;
  if (!persist()) { restore(old); emit cardsChanged(); return false; }
  emit cardsChanged(); return true;
}
bool CallPrompterController::rejectCard(const QString &id) {
  const auto index = cardIndex(id); if (index < 0) return false;
  auto card = mCards[index].toObject(); if (card["decision"].toString() != "pending") return false;
  const auto old = state(); card["decision"] = "rejected"; mCards[index] = card;
  if (!persist()) { restore(old); emit cardsChanged(); return false; }
  emit cardsChanged(); return true;
}
std::vector<int64_t> CallPrompterController::clientIds() const {
  return mTranscriptId ? mDb->get_transcript_client_ids(mTranscriptId) : std::vector<int64_t>{};
}
bool CallPrompterController::saveNote(const QString &id, qint64 clientId) {
  const auto index = cardIndex(id); if (index < 0) return false;
  auto card = mCards[index].toObject();
  if (card["decision"].toString() != "accepted") return false;
  if (!evidenceUnchanged(card)) return false;
  const auto body = tr("Call agreement (transcript #%1):\n%2%3").arg(mTranscriptId)
      .arg(card["kind"].toString() == "cancel" ? tr("Cancelled: ") : QString(), card["action"].toString());
  const auto note = mDb->save_prompter_note(mTranscriptId, id.toStdString(), clientId, body.toStdString());
  if (!note) { setStatus(tr("Could not save the agreement to client notes")); return false; }
  const auto old = state(); card["note_id"] = static_cast<qint64>(note); mCards[index] = card;
  if (!persist()) { restore(old); emit cardsChanged(); return false; }
  emit cardsChanged(); return true;
}
