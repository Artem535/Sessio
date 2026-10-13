#include <QCoreApplication>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "engine_factory.h"

using namespace pcm::calltranscription;
namespace fs = std::filesystem;

namespace {

fs::path makeTempDir(const char *tag) {
  const fs::path dir = fs::temp_directory_path() / (std::string("sessio_factory_") + tag + "_" +
                                                    std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
                                                    "_" + std::to_string(reinterpret_cast<uintptr_t>(&tag)));
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir;
}

void touch(const fs::path &p) {
  fs::create_directories(p.parent_path());
  std::ofstream(p).put('x');
}

} // namespace

TEST(EngineFactory, FactoryReportsMissingModelsWithoutThrowing) {
  const fs::path app = makeTempDir("missing");
  auto factory = makeProductionEngineFactory(app);
  QString error;
  std::shared_ptr<pcm::transcription::TranscriptionEngine> engine;
  EXPECT_NO_THROW(engine = factory({}, &error));
  EXPECT_EQ(engine, nullptr);
  EXPECT_FALSE(error.isEmpty());
  EXPECT_FALSE(error.contains(QString::fromStdString(app.string())));
  fs::remove_all(app);
}

TEST(EngineFactory, AvailabilityFalseWithoutModels) {
  const fs::path app = makeTempDir("avail_false");
  QString reason;
  EXPECT_FALSE(transcriptionModelsAvailable(app, {}, &reason));
  EXPECT_FALSE(reason.isEmpty());
  fs::remove_all(app);
}

TEST(EngineFactory, AvailabilityTrueWithFakeModelTree) {
  const fs::path root = makeTempDir("avail_true");
  const fs::path dir = root / "gigaam-v3-rnnt";
  for (const char *f : {"encoder.int8.onnx", "decoder.onnx", "joiner.onnx", "tokens.txt"}) touch(dir / f);
  touch(root / "silero_vad.onnx");
  EXPECT_TRUE(transcriptionModelsAvailable(makeTempDir("app"), root.string()));
  fs::remove_all(root);
}

TEST(EngineFactory, FactoryWithBrokenModelFilesFailsWithoutThrowing) {
  const fs::path root = makeTempDir("broken");
  const fs::path dir = root / "gigaam-v3-rnnt";
  for (const char *f : {"encoder.int8.onnx", "decoder.onnx", "joiner.onnx", "tokens.txt"}) touch(dir / f);
  touch(root / "silero_vad.onnx");
  auto factory = makeProductionEngineFactory(makeTempDir("app2"), root.string());
  QString error;
  std::shared_ptr<pcm::transcription::TranscriptionEngine> engine;
  EXPECT_NO_THROW(engine = factory({}, &error));
  if (!engine) {
    EXPECT_FALSE(error.isEmpty());
    EXPECT_FALSE(error.contains(QString::fromStdString(root.string())));
  }
  fs::remove_all(root);
}

TEST(EngineFactory, SanitizeRemovesRootsContainingSpaces) {
  const fs::path root = fs::temp_directory_path() / "John Smith" / "models";
  const std::string what = "failed to open " + (root / "gigaam" / "encoder.onnx").string() +
                           " at " + root.string() + " and /home/other user/x/tokens.txt";
  const QString text = sanitizeLoadError(what.c_str(), {root, root.parent_path()});
  EXPECT_FALSE(text.contains("John")) << text.toStdString();
  EXPECT_FALSE(text.contains("Smith")) << text.toStdString();
  EXPECT_FALSE(text.contains("/")) << text.toStdString();
  EXPECT_TRUE(text.contains("encoder.onnx"));
}

TEST(EngineFactory, FactoryErrorDoesNotLeakPathWithSpaces) {
  const fs::path root = makeTempDir("spaces") / "John Smith";
  const fs::path dir = root / "gigaam-v3-rnnt";
  for (const char *f : {"encoder.int8.onnx", "decoder.onnx", "joiner.onnx", "tokens.txt"}) touch(dir / f);
  touch(root / "silero_vad.onnx");
  auto factory = makeProductionEngineFactory(makeTempDir("app3"), root.string());
  QString error;
  auto engine = factory({}, &error);
  EXPECT_EQ(engine, nullptr);
  EXPECT_FALSE(error.contains("John")) << error.toStdString();
  EXPECT_FALSE(error.contains("Smith")) << error.toStdString();
  fs::remove_all(root.parent_path());
}
