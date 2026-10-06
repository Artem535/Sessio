#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "model_locator.h"

namespace fs = std::filesystem;
using namespace pcm::transcription;

namespace {

int currentPid() {
#ifdef _WIN32
  return _getpid();
#else
  return static_cast<int>(::getpid());
#endif
}

struct TempDir {
  TempDir() {
    path = fs::temp_directory_path() / ("sessio-locator-" + std::to_string(currentPid()) + "-" +
                                        std::to_string(counter++));
    fs::create_directories(path / "bin");  // app dir must exist for "bin/.." to resolve
  }
  ~TempDir() { fs::remove_all(path); }
  static inline int counter = 0;
  fs::path path;
};

void touch(const fs::path& p) {
  fs::create_directories(p.parent_path());
  std::ofstream(p) << "x";
}

void writeModelTree(const fs::path& root) {
  for (const char* f : {"encoder.int8.onnx", "decoder.onnx", "joiner.onnx", "tokens.txt"}) {
    touch(root / "gigaam-v3-rnnt" / f);
  }
  touch(root / "silero_vad.onnx");
}

}  // namespace

TEST(ModelLocatorTest, CandidatesPerPlatform) {
  const fs::path app = "/opt/app/bin";
  EXPECT_EQ(modelRootCandidates(app, Platform::Linux, {}).front(),
            fs::path("/opt/app/bin/../share/sessio/models"));
  EXPECT_EQ(modelRootCandidates("/A/Sessio.app/Contents/MacOS", Platform::MacOS, {}).front(),
            fs::path("/A/Sessio.app/Contents/MacOS/../Resources/models"));
  EXPECT_EQ(modelRootCandidates("C:/Sessio", Platform::Windows, {}).front(),
            fs::path("C:/Sessio/models"));
}

TEST(ModelLocatorTest, OverrideComesFirst) {
  const auto c = modelRootCandidates("/opt/app/bin", Platform::Linux, "/custom");
  ASSERT_EQ(c.size(), 2u);
  EXPECT_EQ(c.front(), fs::path("/custom"));
}

TEST(ModelLocatorTest, FindsCompleteTree) {
  TempDir dir;
  writeModelTree(dir.path / "share" / "sessio" / "models");
  const auto result = locateModels(dir.path / "bin", Platform::Linux);
  ASSERT_TRUE(result.paths.has_value()) << result.error;
  EXPECT_EQ(result.paths->encoder.filename(), "encoder.int8.onnx");
  EXPECT_EQ(result.paths->vad.filename(), "silero_vad.onnx");
  EXPECT_TRUE(result.error.empty());
}

TEST(ModelLocatorTest, ReportsMissingFileByName) {
  TempDir dir;
  const auto root = dir.path / "share" / "sessio" / "models";
  writeModelTree(root);
  fs::remove(root / "gigaam-v3-rnnt" / "joiner.onnx");
  const auto result = locateModels(dir.path / "bin", Platform::Linux);
  EXPECT_FALSE(result.paths.has_value());
  EXPECT_NE(result.error.find("joiner.onnx"), std::string::npos);
}

TEST(ModelLocatorTest, ReportsWhenNoCandidateExists) {
  TempDir dir;
  const auto result = locateModels(dir.path / "bin", Platform::Linux);
  EXPECT_FALSE(result.paths.has_value());
  EXPECT_FALSE(result.error.empty());
}

TEST(ModelLocatorTest, OverrideWinsOverPlatformDefault) {
  TempDir dir;
  writeModelTree(dir.path / "override");
  const auto result = locateModels(dir.path / "bin", Platform::Linux, (dir.path / "override").string());
  ASSERT_TRUE(result.paths.has_value()) << result.error;
  EXPECT_EQ(result.paths->tokens.parent_path().parent_path(), dir.path / "override");
}
