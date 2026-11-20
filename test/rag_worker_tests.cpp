#include "rag_worker.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include <atomic>
#include <memory>

namespace {

using sessio::rag::IndexStore;
using sessio::rag::IndexedChunk;
using sessio::rag::DocumentInfo;
using sessio::rag::RagCard;
using sessio::rag::RagDeps;
using sessio::rag::RagWorker;
using sessio::rag::kEmbeddingDim;

#ifndef SESSIO_LADYBUG_EXTENSION_DIR
#define SESSIO_LADYBUG_EXTENSION_DIR ""
#endif

std::vector<float> basis(int axis) {
  std::vector<float> vec(kEmbeddingDim, 0.0f);
  vec[axis] = 1.0f;
  return vec;
}

class FakeEmbedder final : public sessio::litert::Embedder {
public:
  explicit FakeEmbedder(std::atomic<int> *calls) : mCalls(calls) {}
  sessio::litert::Status embed(const std::vector<std::string> &texts,
                               std::vector<std::vector<float>> *vectors) override {
    ++*mCalls;
    vectors->assign(texts.size(), basis(0));
    return sessio::litert::Status::Ok;
  }
private:
  std::atomic<int> *mCalls;
};

DocumentInfo doc(const std::string &id) {
  DocumentInfo out;
  out.docId = id;
  out.title = "Title " + id;
  out.url = "https://example.invalid/" + id;
  out.collection = "Presale";
  out.revision = "1";
  return out;
}

struct App {
  int argc = 1;
  char *argv[2] = {const_cast<char *>("test"), nullptr};
  QCoreApplication core{argc, argv};
};

class RagWorkerTest : public ::testing::Test {
protected:
  void SetUp() override {
    if (std::string(SESSIO_LADYBUG_EXTENSION_DIR).empty())
      GTEST_SKIP() << "SESSIO_LADYBUG_DIR not configured";
    deps.makeEmbedder = [this] { return std::make_unique<FakeEmbedder>(&calls); };
    deps.makeIndex = [] {
      auto store = std::make_unique<IndexStore>(":memory:", SESSIO_LADYBUG_EXTENSION_DIR, nullptr);
      std::string error;
      store->upsertDocument(doc("d1"), {{"отели кейс внедрения", basis(0)}}, &error);
      store->upsertDocument(doc("d2"), {{"ритейл магазины проект", basis(1)}}, &error);
      return store;
    };
  }
  RagDeps deps;
  std::atomic<int> calls{0};
};

TEST_F(RagWorkerTest, QuestionProducesCards) {
  App app;
  RagWorker worker;
  QSignalSpy ready(&worker, &RagWorker::ready);
  QSignalSpy results(&worker, &RagWorker::resultReady);
  QSignalSpy failed(&worker, &RagWorker::failed);
  ASSERT_TRUE(worker.start(deps));
  QTRY_VERIFY_WITH_TIMEOUT(ready.count() == 1, 5000);
  worker.search(1, QStringLiteral("Где отели?"));
  QTRY_VERIFY_WITH_TIMEOUT(results.count() == 1, 5000);
  EXPECT_EQ(failed.count(), 0);
  EXPECT_EQ(results.at(0).at(0).toInt(), 1);
  const auto cards = results.at(0).at(1).value<QVector<RagCard>>();
  ASSERT_FALSE(cards.isEmpty());
  EXPECT_FALSE(cards.first().url.isEmpty());
  EXPECT_EQ(cards.first().kind, QStringLiteral("wiki"));
  EXPECT_FALSE(cards.first().fullText.isEmpty());
}

TEST_F(RagWorkerTest, FaqCardCarriesFullAnswer) {
  App app;
  RagWorker worker;
  const QString answer = QStringLiteral("Полный ответ. ") + QString(800, QChar('x'));
  deps.makeIndex = [&answer] {
    auto store = std::make_unique<IndexStore>(":memory:", SESSIO_LADYBUG_EXTENSION_DIR, nullptr);
    std::string error;
    sessio::rag::FaqEntry entry;
    entry.docId = "d1";
    entry.question = "Где отели?";
    entry.answer = answer.toStdString();
    entry.url = "https://example.invalid/faq";
    entry.embedding = basis(0);
    store->upsertFaq({entry}, &error);
    return store;
  };
  QSignalSpy ready(&worker, &RagWorker::ready);
  QSignalSpy results(&worker, &RagWorker::resultReady);
  ASSERT_TRUE(worker.start(deps));
  QTRY_VERIFY_WITH_TIMEOUT(ready.count() == 1, 5000);
  worker.search(1, QStringLiteral("Где отели?"));
  QTRY_VERIFY_WITH_TIMEOUT(results.count() == 1, 5000);
  const auto cards = results.at(0).at(1).value<QVector<RagCard>>();
  ASSERT_EQ(cards.size(), 1);
  EXPECT_EQ(cards.first().kind, QStringLiteral("faq"));
  EXPECT_EQ(cards.first().fullText, answer);
  EXPECT_LT(cards.first().snippet.size(), answer.size());
}

TEST_F(RagWorkerTest, MultipleCloseFaqsAreReturned) {
  App app;
  RagWorker worker;
  deps.makeIndex = [] {
    auto store = std::make_unique<IndexStore>(":memory:", SESSIO_LADYBUG_EXTENSION_DIR, nullptr);
    std::string error;
    sessio::rag::FaqEntry first;
    first.docId = "d1";
    first.question = "Какие он-прем модели использовались на проектах?";
    first.answer = "Ответ про модели.";
    first.embedding = basis(0);
    sessio::rag::FaqEntry second;
    second.docId = "d2";
    second.question = "На каких проектах было он-прем развёртывание?";
    second.answer = "Ответ про развёртывание.";
    second.embedding = basis(0);
    store->upsertFaq({first, second}, &error);
    return store;
  };
  QSignalSpy ready(&worker, &RagWorker::ready);
  QSignalSpy results(&worker, &RagWorker::resultReady);
  ASSERT_TRUE(worker.start(deps));
  QTRY_VERIFY_WITH_TIMEOUT(ready.count() == 1, 5000);
  worker.search(1, QStringLiteral("Какие он-прем модели использовались на проектах?"));
  QTRY_VERIFY_WITH_TIMEOUT(results.count() == 1, 5000);
  const auto cards = results.at(0).at(1).value<QVector<RagCard>>();
  ASSERT_EQ(cards.size(), 2);
  EXPECT_EQ(cards[0].kind, QStringLiteral("faq"));
  EXPECT_EQ(cards[1].kind, QStringLiteral("faq"));
}

TEST_F(RagWorkerTest, DeclarativeUtteranceSkipsEmbedding) {
  App app;
  RagWorker worker;
  QSignalSpy ready(&worker, &RagWorker::ready);
  QSignalSpy results(&worker, &RagWorker::resultReady);
  ASSERT_TRUE(worker.start(deps));
  QTRY_VERIFY_WITH_TIMEOUT(ready.count() == 1, 5000);
  const int before = calls.load();
  worker.search(1, QStringLiteral("Да, нам это интересно."));
  QTRY_VERIFY_WITH_TIMEOUT(results.count() == 1, 5000);
  EXPECT_TRUE(results.at(0).at(1).value<QVector<RagCard>>().isEmpty());
  EXPECT_EQ(calls.load(), before);
}

TEST_F(RagWorkerTest, StaleGenerationDropped) {
  App app;
  RagWorker worker;
  QSignalSpy ready(&worker, &RagWorker::ready);
  QSignalSpy results(&worker, &RagWorker::resultReady);
  ASSERT_TRUE(worker.start(deps));
  QTRY_VERIFY_WITH_TIMEOUT(ready.count() == 1, 5000);
  worker.search(1, QStringLiteral("Где отели?"));
  worker.search(2, QStringLiteral("Где ритейл?"));
  QTRY_VERIFY_WITH_TIMEOUT(results.count() >= 1, 5000);
  QTest::qWait(100);
  for (const auto &args : results) EXPECT_EQ(args.at(0).toInt(), 2);
}

TEST_F(RagWorkerTest, MissingRuntimeDegradesWithFailed) {
  App app;
  RagWorker worker;
  deps.makeEmbedder = [] { return nullptr; };
  QSignalSpy failed(&worker, &RagWorker::failed);
  QSignalSpy ready(&worker, &RagWorker::ready);
  ASSERT_TRUE(worker.start(deps));
  QTRY_VERIFY_WITH_TIMEOUT(failed.count() == 1, 5000);
  EXPECT_EQ(ready.count(), 0);
  EXPECT_TRUE(failed.at(0).at(0).toString().contains("runtime"));
}

TEST_F(RagWorkerTest, SearchAllPhrasesBypassesRouter) {
  App app;
  RagWorker worker;
  deps.searchAllPhrases = true;
  QSignalSpy ready(&worker, &RagWorker::ready);
  QSignalSpy results(&worker, &RagWorker::resultReady);
  ASSERT_TRUE(worker.start(deps));
  QTRY_VERIFY_WITH_TIMEOUT(ready.count() == 1, 5000);
  worker.search(1, QStringLiteral("последний проект по ритейл"));
  QTRY_VERIFY_WITH_TIMEOUT(results.count() == 1, 5000);
  EXPECT_FALSE(results.at(0).at(1).value<QVector<RagCard>>().isEmpty());
}

class KeyedEmbedder final : public sessio::litert::Embedder {
public:
  sessio::litert::Status embed(const std::vector<std::string> &texts,
                               std::vector<std::vector<float>> *vectors) override {
    vectors->clear();
    for (const auto &text : texts)
      vectors->push_back(text.find("ритейл") != std::string::npos ? basis(0) : basis(5));
    return sessio::litert::Status::Ok;
  }
};

TEST_F(RagWorkerTest, LlmRewriteRescuesGarbledUtterance) {
  App app;
  RagWorker worker;
  deps.makeEmbedder = [] { return std::make_unique<KeyedEmbedder>(); };
  deps.rewrite = [](const std::string &, const std::string &) {
    return sessio::rag::RewriteResult{"проекты по ритейлу", "cases", {}};
  };
  deps.searchAllPhrases = true;
  QSignalSpy ready(&worker, &RagWorker::ready);
  QSignalSpy results(&worker, &RagWorker::resultReady);
  ASSERT_TRUE(worker.start(deps));
  QTRY_VERIFY_WITH_TIMEOUT(ready.count() == 1, 5000);
  worker.search(1, QStringLiteral("кот сани проект был на ритейле"));
  QTRY_VERIFY_WITH_TIMEOUT(results.count() == 1, 5000);
  const auto cards = results.at(0).at(1).value<QVector<RagCard>>();
  ASSERT_FALSE(cards.isEmpty());
  EXPECT_TRUE(cards.first().url.contains(QStringLiteral("d2")));
}

TEST_F(RagWorkerTest, ProjectDescriptionResolvesToPassportCard) {
  App app;
  RagWorker worker;
  deps.makeIndex = [] {
    auto store = std::make_unique<IndexStore>(":memory:", SESSIO_LADYBUG_EXTENSION_DIR, nullptr);
    std::string error;
    store->upsertDocument(doc("d1"), {{"отели кейс внедрения", basis(0)}}, &error);
    sessio::rag::Passport passport;
    passport.name = "Acme";
    passport.description = "производитель удобрений, он-прем развёртывание";
    passport.docId = "d9";
    passport.url = "https://example.invalid/d9";
    passport.embedding = basis(0);
    store->upsertPassports({passport}, &error);
    return store;
  };
  deps.rewrite = [](const std::string &, const std::string &) {
    return sessio::rag::RewriteResult{"", "", {"производитель удобрений"}};
  };
  deps.searchAllPhrases = true;
  QSignalSpy ready(&worker, &RagWorker::ready);
  QSignalSpy results(&worker, &RagWorker::resultReady);
  ASSERT_TRUE(worker.start(deps));
  QTRY_VERIFY_WITH_TIMEOUT(ready.count() == 1, 5000);
  worker.search(1, QStringLiteral("последние проекты это был производитель удобрений"));
  QTRY_VERIFY_WITH_TIMEOUT(results.count() == 1, 5000);
  const auto cards = results.at(0).at(1).value<QVector<RagCard>>();
  ASSERT_FALSE(cards.isEmpty());
  EXPECT_EQ(cards.first().title, QStringLiteral("Acme"));
  EXPECT_TRUE(cards.first().url.contains(QStringLiteral("d9")));
}

TEST_F(RagWorkerTest, EmptyLlmRewriteFallsBackToRules) {
  App app;
  RagWorker worker;
  deps.makeEmbedder = [] { return std::make_unique<KeyedEmbedder>(); };
  deps.rewrite = [](const std::string &, const std::string &) {
    return sessio::rag::RewriteResult{};
  };
  QSignalSpy ready(&worker, &RagWorker::ready);
  QSignalSpy results(&worker, &RagWorker::resultReady);
  ASSERT_TRUE(worker.start(deps));
  QTRY_VERIFY_WITH_TIMEOUT(ready.count() == 1, 5000);
  worker.search(1, QStringLiteral("кот сани проект"));
  QTRY_VERIFY_WITH_TIMEOUT(results.count() == 1, 5000);
  EXPECT_TRUE(results.at(0).at(1).value<QVector<RagCard>>().isEmpty());
}

TEST_F(RagWorkerTest, SyncWithUnreachableOutlineFailsWithoutCrash) {
  App app;
  RagWorker worker;
  deps.outline.baseUrl = QUrl(QStringLiteral("http://127.0.0.1:1"));
  deps.outline.apiToken = QStringLiteral("test-token");
  QSignalSpy failed(&worker, &RagWorker::failed);
  ASSERT_TRUE(worker.start(deps));
  QTRY_VERIFY_WITH_TIMEOUT(failed.count() >= 1, 10000);
}

TEST_F(RagWorkerTest, StopAndDestroyEmitsStopped) {
  App app;
  auto *worker = new RagWorker;
  QSignalSpy ready(worker, &RagWorker::ready);
  QSignalSpy stopped(worker, &RagWorker::stopped);
  ASSERT_TRUE(worker->start(deps));
  QTRY_VERIFY_WITH_TIMEOUT(ready.count() == 1, 5000);
  worker->stop();
  delete worker;
  EXPECT_EQ(stopped.count(), 1);
}

} // namespace
