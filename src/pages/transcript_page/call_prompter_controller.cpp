#include "app_settings.h"
#include "cloud_subjects.h"
#include "failure_text.h"
#include "call_prompter_controller.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonDocument>
#include <algorithm>
#include <vector>
#include <QUuid>
#ifdef SESSIO_RAG_ENABLED
#include "rag_worker.h"
#include "index_store.h"
#include "sessio_litert.h"
#include "cloud_engine.h"
#include "pseudonymizer.h"
#include <QStandardPaths>
#endif

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
QString speakerOf(const DuckTranscriptPhrase &p) {
  auto speaker = QString::fromStdString(p.speaker_name.value_or(p.track_role));
  // Persisted unnamed tracks have only a role label; participant attribution needs review.
  return speaker.isEmpty() ? QCoreApplication::translate("CallPrompterController", "Participant") : speaker;
}
void renameAll(QJsonArray &values, const QHash<QString, QString> &renamed) {
  for (auto i = 0; i < values.size(); ++i)
    if (values[i].isString() && renamed.contains(values[i].toString())) values[i] = renamed[values[i].toString()];
}
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
  startRag();
  emit cardsChanged();
}
void CallPrompterController::addPhrase(const DuckTranscriptPhrase &p) {
  if (p.transcript_id != mTranscriptId || p.id <= 0 || mSources.contains(QString::number(p.id))) return;
  QJsonObject source{{"id", QString::number(p.id)}, {"speaker", speakerOf(p)},
      {"text", QString::fromStdString(p.text)}, {"start_ms", static_cast<qint64>(p.start_ms)}, {"end_ms", static_cast<qint64>(p.end_ms)}};
  mSources.insert(QString::number(p.id), source);
#ifdef SESSIO_RAG_ENABLED
  // Every phrase feeds the router: solo/mic-test calls label all speech as
  // practitioner, and the specialist's own questions are worth searching too.
  if (mRagWorker) {
    const auto text = QString::fromStdString(p.text);
    qWarning() << "RAG: phrase gen" << (mRagGeneration + 1) << "chars" << text.size();
    mRagWorker->search(static_cast<int>(++mRagGeneration), text, mRagContext);
    mRagContext = text;
  }
#endif
  if (p.id > mLastProcessed) mPending.append(source);
  else { mLookback.append(source); while (mLookback.size() > 2) mLookback.removeFirst(); }
  if (mRunning && !mInFlight) mBatchTimer.start();
}
void CallPrompterController::speakersRenamed(qint64 transcriptId) {
  if (!mTranscriptId || transcriptId != mTranscriptId) return;
  const bool wasRunning = mRunning;
  if (mInFlight) pause(); // The reply would cite the old names; resend the batch.
  QHash<QString, QString> renamed; // old speaker -> new speaker
  try {
    for (const auto &p : mDb->get_transcript_phrases(mTranscriptId)) {
      const auto id = QString::number(p.id);
      if (!mSources.contains(id)) continue;
      auto source = mSources[id];
      const auto old = source["speaker"].toString(), speaker = speakerOf(p);
      if (old == speaker) continue;
      renamed.insert(old, speaker); source["speaker"] = speaker; mSources.insert(id, source);
    }
  } catch (...) { return; }
  auto refresh = [this](QJsonArray &sources) {
    for (auto i = 0; i < sources.size(); ++i) {
      const auto id = sources[i].toObject()["id"].toString();
      if (mSources.contains(id)) sources[i] = mSources[id];
    }
  };
  refresh(mPending); refresh(mLookback);
  bool saved = true;
  if (!renamed.isEmpty()) {
    const auto previous = state();
    auto relabel = [&](QJsonArray &items) {
      for (auto i = 0; i < items.size(); ++i) {
        auto item = items[i].toObject();
        auto participants = item["participants"].toArray(); renameAll(participants, renamed);
        item["participants"] = participants;
        auto snapshots = item["evidence_snapshot"].toArray();
        for (auto j = 0; j < snapshots.size(); ++j) {
          auto snapshot = snapshots[j].toObject();
          if (renamed.contains(snapshot["speaker"].toString())) snapshot["speaker"] = renamed[snapshot["speaker"].toString()];
          snapshots[j] = snapshot;
        }
        if (item.contains("evidence_snapshot")) item["evidence_snapshot"] = snapshots;
        items[i] = item;
      }
    };
    relabel(mCards); relabel(mRecords);
    if (!persist()) { restore(previous); saved = false; }
    emit cardsChanged();
  }
  if (wasRunning && !mRunning && saved) resume();
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
void CallPrompterController::endCall() { pause(); stopRag(); mPending = {}; }

void CallPrompterController::startRag() {
#ifdef SESSIO_RAG_ENABLED
  if (!pcm::app_settings::ragEnabled() || mRagWorker) return;
  namespace rag = sessio::rag;
  rag::RagDeps deps;
  deps.makeEmbedder = [] {
    auto model = pcm::app_settings::ragEmbedModel();
    if (model.isEmpty() || !QFileInfo::exists(model)) {
      // Out-of-the-box discovery: bundled next to the binary, then the dev
      // cache location, so enabling the checkbox is enough.
      const QStringList candidates = {
          QCoreApplication::applicationDirPath() + QStringLiteral("/models/embeddinggemma-2-text-270m.litertlm"),
          QDir::homePath() + QStringLiteral("/.cache/sessio-rag/models/embeddinggemma-2-text-270m.litertlm")};
      for (const auto &candidate : candidates)
        if (QFileInfo::exists(candidate)) { model = candidate; break; }
    }
    if (model.isEmpty()) return std::unique_ptr<sessio::litert::Embedder>();
    sessio::litert::EmbedderOptions options;
    options.threads = 4;
    options.cacheDir = (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/rag/cache")).toStdString();
    return sessio::litert::loadEmbedder(model.toStdString(), options, nullptr);
  };
  deps.makeIndex = [] {
    const auto dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/rag");
    QDir().mkpath(dir);
    return std::make_unique<rag::IndexStore>((dir + QStringLiteral("/index.lbug")).toStdString(),
                                             SESSIO_RAG_EXTENSION_DIR, nullptr);
  };
  deps.outline.baseUrl = QUrl(pcm::app_settings::ragOutlineUrl());
  deps.outline.apiToken = pcm::app_settings::ragOutlineToken();
  deps.outline.collectionAllowlist = pcm::app_settings::ragAllowlist();
  deps.searchAllPhrases = pcm::app_settings::ragSearchAllPhrases();
  deps.useTrainedRouter = pcm::app_settings::ragUseTrainedRouter();
  const auto rewriteMode = pcm::app_settings::ragRewriteMode();
  if (rewriteMode != "rules") {
    // Subjects snapshot on the GUI thread; the rewrite itself runs on the
    // worker thread. Cloud payloads are pseudonymized and restored locally.
    const auto subjects = pcm::transcriptionui::cloudSubjects(*mDb, {mTranscriptId}, mEventId);
    const bool pseudonymize = pcm::app_settings::cloudPseudonymize();
    deps.rewrite = [rewriteMode, subjects, pseudonymize](const std::string &utterance,
                                                         const std::string &context)
        -> sessio::rag::RewriteResult {
      static const QString kSystem = QStringLiteral(
          "You repair Russian sales-call utterances corrupted by speech recognition and rewrite them as "
          "standalone search queries for a company wiki. Use the dialogue context to fix misheard words and "
          "resolve references. Preserve company, product and person names exactly as given in the context. "
          "Also classify the question category: cases, status, navigation, pricing, integration or other. "
          "Also fill \"projects\": a list of short Russian noun phrases, copied verbatim from the utterance, "
          "describing each mentioned project by what the client does (industry and client type, like "
          "\"производитель удобрений\" or \"строительный ретейл\"). Whenever the speaker mentions any project, "
          "client or past experience, this list MUST NOT be empty: one phrase per mentioned project, even "
          "when no project name is said and the utterance is a statement, not a question. "
          "First fill \"reasoning\": 1-3 sentences in Russian naming which client businesses or projects "
          "the utterance mentions and how you know; then the answer. "
          "Answer with JSON {\"reasoning\": \"...\", \"query\": \"...\", \"category\": \"...\", "
          "\"projects\": [\"...\"]} in Russian. "
          "If the utterance is not a question or request, return it unchanged.");
      // Chain-of-thought lives in the reply, not in native thinking: VSellm
      // thinking tokens consume max_tokens and can leave the content empty.
      static const QByteArray kSchema = QByteArrayLiteral(
          "{\"type\":\"object\",\"properties\":{\"reasoning\":{\"type\":\"string\"},"
          "\"query\":{\"type\":\"string\"},"
          "\"category\":{\"type\":\"string\",\"enum\":[\"cases\",\"status\",\"navigation\",\"pricing\","
          "\"integration\",\"other\"]},\"projects\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}},"
          "\"required\":[\"reasoning\",\"query\",\"category\",\"projects\"]}");
      // The local SFT model was not trained on reasoning/projects fields.
      static const QByteArray kSchemaLocal = QByteArrayLiteral(
          "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"},"
          "\"category\":{\"type\":\"string\",\"enum\":[\"cases\",\"status\",\"navigation\",\"pricing\","
          "\"integration\",\"other\"]},\"projects\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}},"
          "\"required\":[\"query\",\"category\",\"projects\"]}");
      const auto utt = QString::fromStdString(utterance);
      const auto ctx = QString::fromStdString(context);
      std::unique_ptr<pcm::prompter::Pseudonymizer> pseud;
      if (rewriteMode == "cloud" && pseudonymize) {
        pcm::prompter::PseudonymSubjects s;
        const auto list = [&subjects](const char *key) {
          QStringList out;
          for (const auto &v : subjects[key].toArray()) out << v.toString();
          return out;
        };
        s.clientNames = list("client");
        s.specialistNames = list("specialist");
        s.phones = list("phones");
        s.emails = list("emails");
        pseud = std::make_unique<pcm::prompter::Pseudonymizer>(s);
      }
      const auto clean = [&pseud](const QString &t) { return pseud ? pseud->apply(t) : t; };
      QString reply;
      if (rewriteMode == "cloud") {
        pcm::prompter::CloudConfig config;
        config.baseUrl = pcm::app_settings::cloudBaseUrl();
        config.model = pcm::app_settings::cloudModel();
        config.apiKey = pcm::app_settings::cloudApiKeyCached();
        config.maxOutputTokens = 1024;
        config.thinkingDisabled = true;
        auto engine = pcm::prompter::createCloudEngine(config);
        if (!engine) return {};
        const QJsonObject payload{{"context", clean(ctx)}, {"utterance", clean(utt)}};
        try {
          const auto value =
              engine->generate(kSystem, QJsonDocument(payload).toJson(QJsonDocument::Compact), kSchema);
          reply = QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
        } catch (...) { return {}; }
        if (pseud) reply = pseud->restoreText(reply);
      } else {
        // One model per worker thread, loaded lazily on first rewrite.
        static thread_local std::unique_ptr<sessio::litert::Model> model;
        if (!model) {
          const auto path = pcm::app_settings::prompterModel();
          if (path.isEmpty()) return {};
          sessio::litert::ModelOptions options;
          options.threads = 4;
          options.backend = sessio::litert::Backend::CPU;
          options.cacheDir = (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                              + QStringLiteral("/rag/rewrite-cache")).toStdString();
          model = sessio::litert::loadModel(path.toStdString(), options, nullptr);
          if (!model) return {};
        }
        sessio::litert::GenerateOptions options;
        options.maxOutputTokens = 192;
        options.applyPromptTemplate = true;
        options.reusePrefix = false;
        options.jsonSchema = kSchemaLocal.toStdString();
        const auto user = QStringLiteral("Dialogue context (recent phrases):\n%1\nUtterance:\n%2")
                              .arg(ctx.isEmpty() ? QStringLiteral("(none)") : ctx, utt);
        std::string out;
        if (model->generate(kSystem.toStdString(), user.toStdString(), options, &out)
            != sessio::litert::Status::Ok)
          return {};
        reply = QString::fromStdString(out);
      }
      const auto doc = QJsonDocument::fromJson(reply.toUtf8());
      const auto object = doc.object();
      sessio::rag::RewriteResult result;
      result.query = object.value("query").toString().toStdString();
      if (result.query.size() > 240) result.query = result.query.substr(0, 240);
      result.category = object.value("category").toString().toStdString();
      const auto projects = object.value("projects").toArray();
      qWarning() << "RAG: rewrite reply chars" << reply.size() << "projectsRaw" << projects.size();
      for (const auto &value : projects) {
        QString phrase;
        if (value.isString()) phrase = value.toString();
        else if (value.isObject()) {
          const auto o = value.toObject();
          for (const auto &v : o)
            if (v.isString() && !v.toString().isEmpty()) {
              phrase = v.toString();
              break;
            }
        }
        if (!phrase.isEmpty()) result.projects.push_back(phrase.toStdString());
      }
      return result;
    };
  }
  auto *worker = new rag::RagWorker(this);
  connect(worker, &rag::RagWorker::failed, this,
          [](const QString &reason) { qWarning() << "RAG worker:" << reason; });
  connect(worker, &rag::RagWorker::resultReady, this,
          [this](int generation, const QVector<rag::RagCard> &cards) {
    qWarning() << "RAG: cards" << cards.size() << "gen" << generation << "current" << mRagGeneration;
    if (generation != mRagGeneration) return;
    const auto keyOf = [](const QJsonObject &card) {
      const auto url = card.value("url").toString();
      return url.isEmpty() ? card.value("title").toString() : url;
    };
    const auto contains = [&](const QJsonArray &array, const QString &key) {
      return std::any_of(array.begin(), array.end(),
                         [&](const QJsonValue &value) { return keyOf(value.toObject()) == key; });
    };
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QJsonArray array;
    for (const auto &card : cards) {
      const auto key = card.url.isEmpty() ? card.title : card.url;
      if (contains(array, key)) continue;
      array.append(QJsonObject{{"title", card.title}, {"url", card.url},
                               {"snippet", card.snippet}, {"score", card.score},
                               {"fullText", card.fullText}, {"kind", card.kind}, {"ts", now}});
    }
    // Newest context on top; older cards stay below instead of vanishing.
    for (const auto &old : mRagCards) {
      if (array.size() >= 12) break;
      if (!contains(array, keyOf(old.toObject()))) array.append(old);
    }
    // Strict newest-first: a freshly added hint always becomes the top card.
    std::vector<QJsonObject> merged;
    merged.reserve(array.size());
    for (const auto &value : array) merged.push_back(value.toObject());
    std::stable_sort(merged.begin(), merged.end(), [](const QJsonObject &a, const QJsonObject &b) {
      return a.value("ts").toInteger() > b.value("ts").toInteger();
    });
    QJsonArray sorted;
    for (const auto &card : merged) sorted.append(card);
    mRagCards = sorted;
    emit ragCardsChanged();
  });
  mRagGeneration = 0;
  mRagContext.clear();
  mRagCards = QJsonArray{};
  emit ragCardsChanged();
  if (worker->start(deps)) {
    mRagWorker = worker;
    qWarning() << "RAG: worker started";
  } else {
    worker->deleteLater();
    qWarning() << "RAG: worker start refused";
  }
#endif
}

void CallPrompterController::stopRag() {
#ifdef SESSIO_RAG_ENABLED
  if (mRagWorker) {
    mRagWorker->stop();
    mRagWorker->deleteLater();
    mRagWorker = nullptr;
  }
  mRagContext.clear();
  if (!mRagCards.isEmpty()) {
    mRagCards = {};
    emit ragCardsChanged();
  }
#endif
}
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
        found = QString::fromStdString(p.text) == snapshot["text"].toString() && speakerOf(p) == snapshot["speaker"].toString();
        break;
      }
      if (!found) return false;
    }
    return true;
  } catch (...) { return false; }
}
// A cloud failure says nothing about the input, so the batch is retried as it
// is, later. One the user must fix (key, model, provider) pauses the prompter;
// nothing falls back to the local model.
void CallPrompterController::cloudFailed(const QString &detail) {
  pause();  // returns the in-flight phrases to the queue
  setStatus(pcm::transcriptionui::cloudFailureText(detail));
  if (!pcm::transcriptionui::cloudFailureIsTransient(detail) || ++mFailures >= kMaxFailures) return;
  mRunning = true;
  mBatchTimer.start();
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
  if (pcm::app_settings::featureEngine("agreements") == "cloud") {
    mRequest["cloud"] = true;
    mRequest["subjects"] = pcm::transcriptionui::cloudSubjects(*mDb, {mTranscriptId}, mEventId);
  }
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
  if (result.contains("error") && mRequest["cloud"].toBool()) {
    const auto reason = result["reason"].toString();
    if (reason == "cloud") { cloudFailed(result["cloud_detail"].toString(QStringLiteral("request"))); return; }
    if (reason == "timeout") { cloudFailed(QStringLiteral("timeout")); return; }
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
  mEngine = pcm::transcriptionui::engineLabel(result["engine"].toString());
  auto done = tr("Processed through %1:%2 · %n phrases waiting", nullptr, mPending.size())
                  .arg(last / 60000).arg((last / 1000) % 60, 2, 10, QLatin1Char('0'));
  if (!mEngine.isEmpty()) done += QStringLiteral(" · ") + mEngine;
  setStatus(done);
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
void CallPrompterController::removeRagCard(int index) {
  if (index < 0 || index >= mRagCards.size()) return;
  mRagCards.removeAt(index);
  emit ragCardsChanged();
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
