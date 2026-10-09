#include <gtest/gtest.h>
#include "config.h"

#include <Poco/Environment.h>
#include <Poco/File.h>
#include <Poco/TemporaryFile.h>

#include <filesystem>
#include <string>

TEST(ConfigTest, Initialize) {
    EXPECT_NO_THROW(pcm::config::Config());
}

// read_config() -> modify -> save_config() is how every caller updates a
// single field. It must write back to the existing file, not silently drop
// the change (config_pth is an rfl::Skip field and used to come back empty).
TEST(ConfigTest, ReadModifySaveRoundTripUpdatesExistingFile) {
    pcm::config::Config initial;
    initial.app_role = "Client";
    initial.token_backend_base_url = "https://old.example.test";
    pcm::config::Config::save_config(initial);

    auto conf = pcm::config::Config::read_config();
    EXPECT_EQ(conf.config_pth.value().toString(),
              pcm::config::Config().config_pth.value().toString());
    conf.token_backend_base_url = "https://new.example.test";
    pcm::config::Config::save_config(conf);

    const auto reread = pcm::config::Config::read_config();
    EXPECT_EQ(reread.token_backend_base_url, "https://new.example.test");
    EXPECT_EQ(reread.app_role, "Client");
    Poco::File(pcm::config::Config().config_pth.value()).remove();
}

TEST(ConfigTest, SaveToUnwritablePathThrows) {
    pcm::config::Config conf;
    // A directory where the file should be: the save cannot open it.
    const auto path = conf.config_pth.value().toString();
    std::filesystem::create_directories(path);
    EXPECT_ANY_THROW(pcm::config::Config::save_config(conf));
    std::filesystem::remove_all(path);
}

int main(int argc, char **argv)
{
  // Config resolves to Poco::Path::configHome() (the developer's real
  // ~/.config on Linux). Point it, and HOME for macOS, at a scratch directory
  // before any Config is constructed so these tests never touch real config.
  const std::string isolatedHome = Poco::TemporaryFile::tempName();
  std::filesystem::create_directories(isolatedHome);
  Poco::Environment::set("XDG_CONFIG_HOME", isolatedHome);
  Poco::Environment::set("HOME", isolatedHome);

  ::testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  std::filesystem::remove_all(isolatedHome);
  return result;
}
