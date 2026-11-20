#pragma once

#include "index_store.h"
#include "outline_connector.h"
#include "search.h"
#include "sessio_litert.h"

#include <QObject>
#include <QMetaType>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>
#include <utility>

class QThread;

namespace sessio::rag {

struct RagCard {
  QString title;
  QString url;
  QString snippet;
  double score = 0;
  QString fullText; // untruncated answer/chunk/description for the inline detail
  QString kind;     // project | faq | wiki
};

// LLM rewrite of one utterance: a standalone wiki query, its category, and
// spoken project descriptions ("производитель удобрений") for the passport
// resolver. Empty query falls back to the deterministic rules.
struct RewriteResult {
  std::string query;
  std::string category;
  std::vector<std::string> projects;
};

// Injected so tests (and a runtime-less build) can substitute fakes; the app
// wires makeEmbedder to sessio::litert::loadEmbedder and makeIndex to a file
// database under the user data dir.
struct RagDeps {
  std::function<std::unique_ptr<sessio::litert::Embedder>()> makeEmbedder;
  std::function<std::unique_ptr<IndexStore>()> makeIndex;
  OutlineConfig outline; // empty baseUrl disables wiki sync
  bool searchAllPhrases = false; // bypass the question router
  bool useTrainedRouter = false; // embedding head instead of rules
  // Optional LLM query rewrite (local model or cloud, wired by the controller
  // with pseudonymization for cloud). Runs on the worker thread.
  std::function<RewriteResult(const std::string &utterance, const std::string &context)> rewrite;
};

// Background RAG lane on a dedicated low-priority thread (PrompterWorker
// pattern): wiki sync -> chunk -> embed -> index, plus utterance search.
// The GUI thread only queues requests; results carry the caller's generation
// and stale generations are dropped. With RAG disabled or the runtime/index
// unavailable the worker reports failed() and the prompter keeps working.
class RagWorker final : public QObject {
  Q_OBJECT
public:
  explicit RagWorker(QObject *parent = nullptr);
  ~RagWorker() override;
  bool start(const RagDeps &deps);
  void stop();
  void syncNow();
  void search(int generation, const QString &utterance, const QString &context = {});
signals:
  void ready();
  void indexingProgress(int done, int total);
  void resultReady(int generation, const QVector<sessio::rag::RagCard> &cards);
  void failed(const QString &reason);
  void stopped();
private:
  struct Core;
  QThread *mThread = nullptr;
  Core *mCore = nullptr;
  bool mStarted = false;
};

} // namespace sessio::rag

Q_DECLARE_METATYPE(sessio::rag::RagCard)
