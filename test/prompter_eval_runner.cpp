// Synthetic evaluation runner. Inputs must not contain real meeting data.
#include "live_points_pipeline.h"
#include "prompter_engine.h"
#include "summary_pipeline.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace pcm::prompter;

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (argc != 4) {
    QTextStream(stderr) << "usage: prompter_eval_runner FIXTURE MODEL OUTPUT.jsonl\n";
    return 2;
  }
  QFile fixture(QString::fromLocal8Bit(argv[1]));
  if (!fixture.open(QIODevice::ReadOnly)) return 2;
  const auto fixtureBytes = fixture.readAll();
  const auto cases = QJsonDocument::fromJson(fixtureBytes).object();
  if (cases["version"] != "issue-180-new-synthetic-v1") return 2;
  const auto fixtureHash = QString::fromLatin1(QCryptographicHash::hash(fixtureBytes, QCryptographicHash::Sha256).toHex());
  QFile modelFile(QString::fromLocal8Bit(argv[2]));
  if (!modelFile.open(QIODevice::ReadOnly)) return 2;
  QCryptographicHash modelHasher(QCryptographicHash::Sha256);
  if (!modelHasher.addData(&modelFile)) return 2;
  const auto modelHash = QString::fromLatin1(modelHasher.result().toHex());
  QCryptographicHash promptsHasher(QCryptographicHash::Sha256);
  for (const auto *name : {"live_points.txt", "summary_leaf.txt", "summary_overview.txt"}) {
    QFile prompt(QStringLiteral(":/call-prompter/") + QLatin1String(name));
    if (!prompt.open(QIODevice::ReadOnly)) return 2;
    promptsHasher.addData(prompt.readAll());
  }
  const auto promptsHash = QString::fromLatin1(promptsHasher.result().toHex());
  const auto outputPath = QString::fromLocal8Bit(argv[3]);
  QSet<QString> completed;
  QFile existing(outputPath);
  if (existing.open(QIODevice::ReadOnly)) {
    while (!existing.atEnd()) {
      const auto object = QJsonDocument::fromJson(existing.readLine()).object();
      const auto id = object["id"].toString();
      if (id.isEmpty() || completed.contains(id) || object["fixture_sha256"] != fixtureHash ||
          object["model_sha256"] != modelHash || object["prompts_sha256"] != promptsHash) return 2;
      completed.insert(id);
    }
  }
  QFile output(outputPath);
  if (!output.open(QIODevice::WriteOnly | QIODevice::Append)) return 2;
  GemmaOptions options;
  options.acceleration = GemmaOptions::Acceleration::Cpu;
  QTemporaryDir cache;
  if (!cache.isValid()) return 2;
  auto engine = createGemmaEngine(QString::fromLocal8Bit(argv[2]), cache.path(), options);
  if (!engine) return 3;
  for (const auto &task : {QStringLiteral("key_points"), QStringLiteral("dialogues")}) {
    for (const auto &entry : cases[task].toArray()) {
      const auto item = entry.toObject();
      const auto id = item["id"].toString();
      if (completed.contains(id)) continue;
      int calls = 0, validJson = 0;
      QJsonArray leafLengths;
      QElapsedTimer timer;
      timer.start();
      QJsonObject result{{"id", id}, {"task", task}, {"fixture_sha256", fixtureHash},
                         {"model_sha256", modelHash}, {"prompts_sha256", promptsHash}};
      try {
        const Generate generate = [&](const QString &system, const QByteArray &payload, const QByteArray &schema) {
          ++calls;
          std::mutex mutex;
          std::condition_variable condition;
          bool finished = false;
          std::thread watchdog([&] {
            std::unique_lock lock(mutex);
            if (!condition.wait_for(lock, std::chrono::seconds(45), [&] { return finished; }))
              engine->cancel();
          });
          QJsonValue reply;
          try {
            reply = engine->generate(system, payload, schema);
          } catch (...) {
            { std::lock_guard lock(mutex); finished = true; }
            condition.notify_one();
            watchdog.join();
            throw;
          }
          { std::lock_guard lock(mutex); finished = true; }
          condition.notify_one();
          watchdog.join();
          if (reply.isObject()) ++validJson;
          const auto note = reply.toObject()["note"].toObject();
          if (note["text"].isString()) leafLengths.append(note["text"].toString().size());
          return reply;
        };
        const auto utterances = item["utterances"].toArray();
        const auto answer = task == "key_points"
            ? processLivePoints(utterances, {}, generate)
            : processSummary(utterances, SummaryContext{QDate(2026, 10, 29), {}}, generate);
        result.insert("answer", answer);
      } catch (const std::exception &error) {
        result.insert("error", QString::fromUtf8(error.what()));
      }
      result.insert("calls", calls);
      result.insert("json_objects", validJson);
      result.insert("leaf_note_lengths_utf16", leafLengths);
      result.insert("elapsed_ms", static_cast<double>(timer.elapsed()));
      output.write(QJsonDocument(result).toJson(QJsonDocument::Compact) + '\n');
      output.flush();
      QTextStream(stderr) << id << " " << timer.elapsed() << " ms\n";
    }
  }
  return 0;
}
