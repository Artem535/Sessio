// P1 acceptance run: replay the 19 draft scenarios through the exact rag-core
// retrieval path the live worker uses (variants + primary-entity boost +
// category boost + FAQ fast-path + distance gate) against a copy of index v2.
// Usage: p1_run <index.lbug dir> <extension dir> <embed model> <p1.jsonl> <out.jsonl>
#include "index_store.h"
#include "rewrite.h"
#include "entities.h"
#include "search.h"
#include "sessio_litert.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cstdio>
#include <string>
#include <vector>

using namespace sessio::rag;

int main(int argc, char **argv) {
  if (argc < 6) return 2;
  std::string error;
  IndexStore index(argv[1], argv[2], &error);
  if (!index.ok()) {
    std::fprintf(stderr, "index: %s\n", error.c_str());
    return 1;
  }
  sessio::litert::EmbedderOptions options;
  options.threads = 4;
  auto embedder = sessio::litert::loadEmbedder(argv[3], options, &error);
  if (!embedder) {
    std::fprintf(stderr, "embedder: %s\n", error.c_str());
    return 1;
  }
  const EmbedFn embed = [&embedder](const std::string &prefixed, std::vector<float> *out) {
    std::vector<std::vector<float>> vectors;
    if (embedder->embed({prefixed}, &vectors) != sessio::litert::Status::Ok || vectors.size() != 1)
      return false;
    *out = std::move(vectors.front());
    return true;
  };
  const auto entities = index.allEntities();
  QFile in(QString::fromUtf8(argv[4]));
  if (!in.open(QIODevice::ReadOnly)) return 1;
  QFile out(QString::fromUtf8(argv[5]));
  if (!out.open(QIODevice::WriteOnly)) return 1;
  int hit = 0, signal = 0, noiseOk = 0, noise = 0;
  while (!in.atEnd()) {
    const auto row = QJsonDocument::fromJson(in.readLine()).object();
    const auto question = row.value("question").toString().toStdString();
    const auto expected = row.value("expected_docs").toArray();
    QJsonArray cards;
    std::vector<float> queryVector;
    if (embed("task: search result | query: " + question, &queryVector)) {
      const auto faq = index.searchFaq(queryVector, 1, &error);
      if (!faq.empty() && faq[0].distance <= 0.20)
        cards.append(QJsonObject{{"kind", "faq"}, {"title", QString::fromStdString(faq[0].question)},
                                 {"url", QString::fromStdString(faq[0].url)}, {"score", 1.0 - faq[0].distance}});
    }
    std::vector<std::string> variants{question, rewriteQuery(question, "")};
    std::vector<std::string> boost;
    for (const auto &match : matchEntities(question, entities)) {
      variants.push_back(match.name);
      for (const auto &docId : match.primaryDocs)
        if (std::find(boost.begin(), boost.end(), docId) == boost.end()) boost.push_back(docId);
    }
    const auto results = searchWikiBoosted(index, embed, variants, boost, classifyCategory(question), 5, &error);
    for (const auto &result : results)
      cards.append(QJsonObject{{"kind", "wiki"}, {"title", QString::fromStdString(result.doc.title)},
                               {"url", QString::fromStdString(result.doc.url)}, {"score", result.score}});
    bool found = expected.isEmpty();
    for (const auto &want : expected) {
      const auto url = want.toObject().value("url").toString();
      for (const auto &card : cards)
        if (card.toObject().value("url").toString().endsWith(url)) found = true;
    }
    if (expected.isEmpty()) {
      ++noise;
      if (cards.isEmpty()) ++noiseOk;
    } else {
      ++signal;
      if (found) ++hit;
    }
    QJsonObject record{{"scenario", row.value("scenario")}, {"question", row.value("question")},
                       {"needs_eyes", row.value("needs_eyes")}, {"hit", found},
                       {"expected_count", expected.size()}, {"cards", cards}};
    out.write(QJsonDocument(record).toJson(QJsonDocument::Compact));
    out.write("\n");
    std::printf("%s %s expected=%d cards=%d hit=%d\n", row.value("scenario").toString().toUtf8().constData(),
                found ? "HIT " : "MISS", int(expected.size()), int(cards.size()), int(found));
  }
  std::printf("SUMMARY signal=%d hit=%d (%.0f%%) noise=%d silent=%d\n", signal, hit,
              signal ? 100.0 * hit / signal : 0.0, noise, noiseOk);
  return 0;
}
