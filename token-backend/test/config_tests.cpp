#include "config.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cstdlib>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// Every environment variable Config::fromEnv() reads. Saved and restored
// wholesale so adding a variable does not mean adding another save/restore
// pair — the previous version of this file grew one per variable and silently
// leaked whichever the author forgot.
const std::vector<const char *> kConfigEnvVars = {
    "PORT",
    "DB_PATH",
    "LIVEKIT_API_KEY",
    "LIVEKIT_API_SECRET",
    "LIVEKIT_WS_ENDPOINT",
    "INVITATION_BASE_URL",
    "TOKEN_TTL_SECONDS",
};

} // namespace

class ConfigTest : public ::testing::Test {
protected:
  void SetUp() override {
    for (const char *name : kConfigEnvVars) {
      const char *value = std::getenv(name);
      saved_[name] = value ? std::optional<std::string>(value) : std::nullopt;
      unsetenv(name);
    }
  }

  void TearDown() override {
    for (const char *name : kConfigEnvVars) {
      const auto &value = saved_[name];
      if (value) {
        setenv(name, value->c_str(), 1);
      } else {
        unsetenv(name);
      }
    }
  }

  // Sets every required variable so a test can then unset exactly the one it
  // is about.
  static void setAllRequired() {
    setenv("LIVEKIT_API_KEY", "test-key", 1);
    setenv("LIVEKIT_API_SECRET", "test-secret", 1);
    setenv("LIVEKIT_WS_ENDPOINT", "wss://livekit.example.test", 1);
    setenv("INVITATION_BASE_URL", "https://join.example.test/j/", 1);
  }

private:
  std::map<std::string, std::optional<std::string>> saved_;
};

TEST_F(ConfigTest, ThrowsWhenLiveKitApiKeyMissing) {
  setAllRequired();
  unsetenv("LIVEKIT_API_KEY");

  EXPECT_THROW(
      {
        try {
          pcm::tokenbackend::Config::fromEnv();
          FAIL() << "Expected std::runtime_error";
        } catch (const std::runtime_error &e) {
          EXPECT_THAT(std::string(e.what()), ::testing::HasSubstr("LIVEKIT_API_KEY"));
          throw;
        }
      },
      std::runtime_error);
}

TEST_F(ConfigTest, ThrowsWhenLiveKitApiSecretMissing) {
  setAllRequired();
  unsetenv("LIVEKIT_API_SECRET");

  EXPECT_THROW(
      {
        try {
          pcm::tokenbackend::Config::fromEnv();
          FAIL() << "Expected std::runtime_error";
        } catch (const std::runtime_error &e) {
          EXPECT_THAT(std::string(e.what()), ::testing::HasSubstr("LIVEKIT_API_SECRET"));
          throw;
        }
      },
      std::runtime_error);
}

// LIVEKIT_WS_ENDPOINT used to default to a real production server address
// reached over unencrypted ws://, so a deploy that forgot it came up healthy
// and silently pointed every call at that server in plaintext. Startup
// failure is the only signal the operator gets.
TEST_F(ConfigTest, ThrowsWhenLiveKitWsEndpointMissing) {
  setAllRequired();
  unsetenv("LIVEKIT_WS_ENDPOINT");

  EXPECT_THROW(
      {
        try {
          pcm::tokenbackend::Config::fromEnv();
          FAIL() << "Expected std::runtime_error";
        } catch (const std::runtime_error &e) {
          EXPECT_THAT(std::string(e.what()), ::testing::HasSubstr("LIVEKIT_WS_ENDPOINT"));
          throw;
        }
      },
      std::runtime_error);
}

TEST_F(ConfigTest, ThrowsWhenLiveKitWsEndpointIsEmpty) {
  setAllRequired();
  setenv("LIVEKIT_WS_ENDPOINT", "", 1);

  EXPECT_THROW(pcm::tokenbackend::Config::fromEnv(), std::runtime_error);
}

// INVITATION_BASE_URL used to default to "https://example.invalid/join/", so a
// deploy that forgot it came up healthy and handed out dead join links with no
// error anywhere. Startup failure is the only signal the operator gets.
TEST_F(ConfigTest, ThrowsWhenInvitationBaseUrlMissing) {
  setAllRequired();
  unsetenv("INVITATION_BASE_URL");

  EXPECT_THROW(
      {
        try {
          pcm::tokenbackend::Config::fromEnv();
          FAIL() << "Expected std::runtime_error";
        } catch (const std::runtime_error &e) {
          EXPECT_THAT(std::string(e.what()), ::testing::HasSubstr("INVITATION_BASE_URL"));
          throw;
        }
      },
      std::runtime_error);
}

TEST_F(ConfigTest, ThrowsWhenInvitationBaseUrlIsEmpty) {
  setAllRequired();
  setenv("INVITATION_BASE_URL", "", 1);

  EXPECT_THROW(pcm::tokenbackend::Config::fromEnv(), std::runtime_error);
}

TEST_F(ConfigTest, RejectsNamedInvitationFormatFieldsAtStartup) {
  setAllRequired();
  setenv("INVITATION_BASE_URL", "sessio://join?code={code}&passcode={passcode}", 1);

  EXPECT_THROW(
      {
        try {
          pcm::tokenbackend::Config::fromEnv();
          FAIL() << "Expected std::runtime_error";
        } catch (const std::runtime_error &e) {
          EXPECT_THAT(std::string(e.what()), ::testing::HasSubstr("positional '{}'"));
          throw;
        }
      },
      std::runtime_error);
}

TEST_F(ConfigTest, SucceedsWithRequiredVarsAndAppliesDefaults) {
  setAllRequired();

  pcm::tokenbackend::Config config = pcm::tokenbackend::Config::fromEnv();

  EXPECT_EQ(config.liveKitApiKey, "test-key");
  EXPECT_EQ(config.liveKitApiSecret, "test-secret");
  EXPECT_EQ(config.liveKitWsEndpoint, "wss://livekit.example.test");
  EXPECT_EQ(config.invitationBaseUrl, "https://join.example.test/j/");
  EXPECT_EQ(config.port, 8080);
  EXPECT_EQ(config.dbPath, "token-backend.sqlite3");
  EXPECT_EQ(config.tokenTtlSeconds, 600);
}

TEST_F(ConfigTest, SucceedsWithAllVarsSet) {
  setenv("LIVEKIT_API_KEY", "my-key", 1);
  setenv("LIVEKIT_API_SECRET", "my-secret", 1);
  setenv("INVITATION_BASE_URL", "https://psy.example.com/join/", 1);
  setenv("LIVEKIT_WS_ENDPOINT", "wss://livekit.example.com", 1);
  setenv("PORT", "9000", 1);
  setenv("DB_PATH", "custom.db", 1);
  setenv("TOKEN_TTL_SECONDS", "3600", 1);

  pcm::tokenbackend::Config config = pcm::tokenbackend::Config::fromEnv();

  EXPECT_EQ(config.liveKitApiKey, "my-key");
  EXPECT_EQ(config.liveKitApiSecret, "my-secret");
  EXPECT_EQ(config.invitationBaseUrl, "https://psy.example.com/join/");
  EXPECT_EQ(config.liveKitWsEndpoint, "wss://livekit.example.com");
  EXPECT_EQ(config.port, 9000);
  EXPECT_EQ(config.dbPath, "custom.db");
  EXPECT_EQ(config.tokenTtlSeconds, 3600);
}
