#include "rag_worker.h"

#include "markdown_chunker.h"
#include "rewrite.h"
#include "router.h"
#include "router_head.h"
#include "entities.h"

#include <QThread>
#include <QDebug>

#include <algorithm>
#include <atomic>
#include <exception>

namespace sessio::rag {

namespace {

constexpr int kCards = 3;
// FAQ fast-path cosine distance ceiling; calibrated on P1.
constexpr double kFaqMaxDistance = 0.20;
// Curated cross-cutting FAQs often contain several relevant questions; show a
// few instead of only the single best one.
constexpr int kFaqCards = 3;

// The retrieval recipe of the 16.11 corpus snapshot: title, heading trail,
// then the chunk text, behind the document prefix.
std::string documentText(const Chunk &chunk) {
  std::string trail;
  for (size_t i = 1; i < chunk.path.size(); ++i) {
    if (!trail.empty()) trail += " > ";
    trail += chunk.path[i];
  }
  const std::string title = chunk.path.empty() ? "" : chunk.path.front();
  return "title: none | text: " + title + "\n" + trail + "\n" + chunk.text;
}

} // namespace

struct RagWorker::Core : public QObject {
  Q_OBJECT
public:
  RagDeps deps;
  std::unique_ptr<sessio::litert::Embedder> embedder;
  std::unique_ptr<IndexStore> index;
  std::vector<Entity> entities;
  std::vector<Passport> passports;
  OutlineConnector connector;
  std::atomic<int> latestGeneration{0};
  std::atomic<bool> stopping{false};
  bool syncing = false;

  explicit Core(QObject *parent = nullptr) : QObject(parent) {
    // Member QObjects are not children by default; without this the connector
    // (and its QNetworkAccessManager) would stay on the creating thread while
    // Core's slots run on the worker thread.
    connector.setParent(this);
    connect(&connector, &OutlineConnector::finished, this, &Core::onDocuments);
    connect(&connector, &OutlineConnector::failed, this, [this](const QString &reason) {
      syncing = false;
      emit failed(reason);
    });
  }

  void init() {
    index = deps.makeIndex ? deps.makeIndex() : nullptr;
    if (!index || !index->ok()) {
      emit failed(QStringLiteral("index store unavailable"));
      return;
    }
    embedder = deps.makeEmbedder ? deps.makeEmbedder() : nullptr;
    if (!embedder) {
      emit failed(QStringLiteral("embedding runtime unavailable"));
      return;
    }
    try {
      entities = index->allEntities();
      qWarning() << "RAG: entities" << entities.size();
      passports = index->allPassports();
      qWarning() << "RAG: passports" << passports.size();
    } catch (const std::exception &) {
      emit failed(QStringLiteral("index read failed"));
      return;
    }
    if (!deps.outline.baseUrl.isEmpty())
      startSync();
    else
      emit ready();
    qWarning() << "RAG: init ok, sync" << !deps.outline.baseUrl.isEmpty();
  }

  void startSync() {
    if (syncing || stopping.load()) return;
    syncing = true;
    connector.fetch(deps.outline);
  }

  void onDocuments(const QVector<OutlineDocument> &documents) {
    int done = 0;
    for (const auto &doc : documents) {
      if (stopping.load()) break;
      const auto revision = index->documentRevision(doc.info.docId);
      if (revision && QString::number(*revision).toStdString() == doc.info.revision) {
        ++done;
        continue;
      }
      const auto chunks = chunkMarkdown(doc.info.title, doc.text.toStdString());
      std::vector<std::string> texts;
      texts.reserve(chunks.size());
      for (const auto &chunk : chunks) texts.push_back(documentText(chunk));
      std::vector<std::vector<float>> vectors;
      if (!texts.empty() &&
          embedder->embed(texts, &vectors) != sessio::litert::Status::Ok) {
        emit failed(QStringLiteral("document embedding failed"));
        break;
      }
      std::vector<IndexedChunk> indexed;
      indexed.reserve(chunks.size());
      for (size_t i = 0; i < chunks.size(); ++i)
        indexed.push_back({chunks[i].text, std::move(vectors[i])});
      std::string error;
      if (!index->upsertDocument(doc.info, indexed, &error)) {
        emit failed(QString::fromStdString(error));
        break;
      }
      ++done;
      emit indexingProgress(done, documents.size());
    }
    syncing = false;
    if (!stopping.load()) emit ready();
  }

  void doSearch(int generation, const QString &utterance, const QString &context) {
    if (generation != latestGeneration.load() || stopping.load()) return;
    const auto text = utterance.toStdString();
    bool searchWorthy = false;
    if (deps.useTrainedRouter) {
      std::vector<std::vector<float>> vectors;
      if (embedder->embed({text}, &vectors) != sessio::litert::Status::Ok || vectors.size() != 1) {
        emit resultReady(generation, {});
        return;
      }
      searchWorthy = headShouldSearch(vectors.front());
    } else {
      searchWorthy = shouldSearch(text);
    }
    if (!deps.searchAllPhrases && !searchWorthy) {
      emit resultReady(generation, {});
      return;
    }
    const EmbedFn embed = [this](const std::string &prefixed, std::vector<float> *out) {
      std::vector<std::vector<float>> vectors;
      if (embedder->embed({prefixed}, &vectors) != sessio::litert::Status::Ok || vectors.size() != 1)
        return false;
      *out = std::move(vectors.front());
      return true;
    };
    std::string primaryQuery;
    std::string category;
    std::vector<std::string> descriptions;
    if (deps.rewrite) {
      const auto rewritten = deps.rewrite(text, context.toStdString());
      if (generation != latestGeneration.load() || stopping.load()) return;
      primaryQuery = rewritten.query;
      category = rewritten.category;
      descriptions = std::move(rewritten.projects);
    }
    if (primaryQuery.empty()) primaryQuery = rewriteQuery(text, context.toStdString());
    if (category.empty()) category = classifyCategory(primaryQuery);
    qWarning() << "RAG: rewrite gen" << generation << "worthy" << searchWorthy
               << "cat" << QString::fromStdString(category) << "desc" << static_cast<int>(descriptions.size())
               << "queryChars" << static_cast<int>(primaryQuery.size());
    QVector<RagCard> cards;
    // Project descriptions resolve against the passport collection without
    // any question phrasing: the speaker may only describe the project.
    for (const auto &description : descriptions) {
      std::string passportError;
      const auto hits = resolvePassports(embed, passports, description, 2, &passportError);
      for (const auto &hit : hits) {
        qWarning() << "RAG: passport hit dist" << hit.distance;
        const auto url = QString::fromStdString(hit.url);
        const auto duplicate = std::any_of(cards.begin(), cards.end(),
                                           [&url](const RagCard &card) { return card.url == url; });
        if (duplicate) continue;
        cards.push_back({QString::fromStdString(hit.name), url,
                         QString::fromStdString(cardSnippet(hit.description)), 1.0 - hit.distance,
                         QString::fromStdString(hit.description), QStringLiteral("project")});
      }
    }
    // FAQ fast path answers questions, not filler: without a question signal
    // (router, a non-other category or project descriptions) short utterances
    // like "угу" embed close to everything and produce noise cards.
    const bool faqAllowed = searchWorthy || category != "other" || !descriptions.empty();
    if (faqAllowed) {
      // Search FAQs with the spoken phrase as well as the LLM rewrite: a raw
      // phrase is often much closer to a curated FAQ than a long paraphrase.
      std::vector<std::string> faqQueries{text};
      if (!primaryQuery.empty() && primaryQuery != text) faqQueries.push_back(primaryQuery);
      std::vector<FaqHit> faqs;
      const auto addFaq = [&faqs](const FaqHit &hit) {
        if (hit.question.empty() || hit.distance > kFaqMaxDistance) return;
        const auto it = std::find_if(faqs.begin(), faqs.end(),
                                     [&hit](const FaqHit &f) { return f.question == hit.question; });
        if (it == faqs.end()) faqs.push_back(hit);
        else if (hit.distance < it->distance) *it = hit;
      };
      for (const auto &query : faqQueries) {
        std::vector<float> queryVector;
        if (!embed("task: search result | query: " + query, &queryVector)) continue;
        std::string faqError;
        for (const auto &hit : index->searchFaq(queryVector, 3, &faqError)) addFaq(hit);
      }
      std::sort(faqs.begin(), faqs.end(),
                [](const FaqHit &a, const FaqHit &b) { return a.distance < b.distance; });
      if (!faqs.empty())
        qWarning() << "RAG: faq candidates" << faqs.size() << "best" << faqs[0].distance;
      for (size_t i = 0; i < faqs.size() && static_cast<int>(i) < kFaqCards; ++i) {
        auto url = QString::fromStdString(faqs[i].url);
        if (url.isEmpty() && !deps.outline.baseUrl.isEmpty()) {
          // Cross-cutting FAQs have no single source page; open the wiki search
          // for the question instead of showing a card with an empty link.
          const auto encoded = QUrl::toPercentEncoding(QString::fromStdString(faqs[i].question));
          url = deps.outline.baseUrl
                    .resolved(QUrl(QStringLiteral("/search?query=") + QString::fromUtf8(encoded)))
                    .toString();
        }
        cards.push_back({QString::fromStdString(faqs[i].question), url,
                         QString::fromStdString(cardSnippet(faqs[i].answer)), 1.0 - faqs[i].distance,
                         QString::fromStdString(faqs[i].answer), QStringLiteral("faq")});
      }
    }
    std::vector<std::string> variants;
    const auto addVariant = [&variants](const std::string &v) {
      if (!v.empty() && std::find(variants.begin(), variants.end(), v) == variants.end())
        variants.push_back(v);
    };
    addVariant(primaryQuery);
    addVariant(rewriteQuery(text, context.toStdString()));
    addVariant(rewriteQuery(text, ""));
    addVariant(rewriteQuery(context.toStdString(), ""));
    std::vector<std::string> boostDocIds;
    for (const auto &match : matchEntities(text, entities)) {
      addVariant(match.name);
      for (const auto &docId : match.primaryDocs)
        if (std::find(boostDocIds.begin(), boostDocIds.end(), docId) == boostDocIds.end())
          boostDocIds.push_back(docId);
    }
    std::string error;
    std::vector<SearchResult> results;
    try {
      results = searchWikiBoosted(*index, embed, variants, boostDocIds, category, kCards, &error);
    } catch (const std::exception &e) {
      if (error.empty()) error = e.what();
      qWarning() << "RAG: search threw" << e.what();
    }
    qWarning() << "RAG: search gen" << generation << "worthy" << searchWorthy
               << "hits" << results.size() << "err" << QString::fromStdString(error);
    if (generation != latestGeneration.load() || stopping.load()) return;
    for (const auto &result : results) {
      const auto url = QString::fromStdString(result.doc.url);
      const auto duplicate = std::any_of(cards.begin(), cards.end(),
                                         [&url](const RagCard &card) { return card.url == url; });
      if (duplicate) continue;
      // A chunk that is only a rule, fence or punctuation gives no hint; skip
      // it instead of showing "\" or "---" as the card text.
      const auto snippet = cardSnippet(result.snippet);
      if (snippet.empty()) continue;
      cards.push_back({QString::fromStdString(result.doc.title), url,
                       QString::fromStdString(snippet), result.score,
                       QString::fromStdString(result.snippet), QStringLiteral("wiki")});
    }
    emit resultReady(generation, cards);
  }

  void shutdown() {
    stopping.store(true);
    connector.cancel();
    index.reset();
    embedder.reset();
  }
signals:
  void ready();
  void indexingProgress(int done, int total);
  void resultReady(int generation, const QVector<sessio::rag::RagCard> &cards);
  void failed(const QString &reason);
};

RagWorker::RagWorker(QObject *parent) : QObject(parent) {}

RagWorker::~RagWorker() {
  stop();
  if (mThread) {
    mThread->quit();
    mThread->wait(5000);
  }
  emit stopped();
}

bool RagWorker::start(const RagDeps &deps) {
  if (mStarted) return true;
  if (!deps.makeEmbedder || !deps.makeIndex) return false;
  qRegisterMetaType<QVector<sessio::rag::RagCard>>();
  mThread = new QThread;
  mThread->setObjectName(QStringLiteral("RagWorker"));
  mCore = new Core;
  mCore->deps = deps;
  mCore->moveToThread(mThread);
  connect(mThread, &QThread::finished, mCore, &QObject::deleteLater);
  connect(mThread, &QThread::finished, mThread, &QObject::deleteLater);
  connect(mCore, &Core::ready, this, &RagWorker::ready);
  connect(mCore, &Core::indexingProgress, this, &RagWorker::indexingProgress);
  connect(mCore, &Core::resultReady, this, &RagWorker::resultReady);
  connect(mCore, &Core::failed, this, &RagWorker::failed);
  connect(mThread, &QThread::finished, this, [this] {
    mStarted = false;
    mCore = nullptr;
    mThread = nullptr;
  });
  mStarted = true;
  mThread->start(QThread::LowPriority);
  QMetaObject::invokeMethod(mCore, [core = mCore] { core->init(); }, Qt::QueuedConnection);
  return true;
}

void RagWorker::stop() {
  if (!mStarted || !mCore) return;
  QMetaObject::invokeMethod(mCore, [core = mCore] { core->shutdown(); }, Qt::QueuedConnection);
}

void RagWorker::syncNow() {
  if (!mStarted || !mCore) return;
  QMetaObject::invokeMethod(mCore, [core = mCore] { core->startSync(); }, Qt::QueuedConnection);
}

void RagWorker::search(int generation, const QString &utterance, const QString &context) {
  if (!mStarted || !mCore) return;
  mCore->latestGeneration.store(generation);
  QMetaObject::invokeMethod(
      mCore, [core = mCore, generation, utterance, context] { core->doSearch(generation, utterance, context); },
      Qt::QueuedConnection);
}

} // namespace sessio::rag

#include "rag_worker.moc"
