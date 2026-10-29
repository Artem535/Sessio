#include "app_settings.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace settings = pcm::app_settings;

namespace {
class CloudSettings : public ::testing::Test {
protected:
  void SetUp() override {
    QCoreApplication::setOrganizationName("SessioCloudSettingsTests");
    QCoreApplication::setApplicationName("cloud");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, mDir.path());
    QSettings().clear();
  }
  QTemporaryDir mDir;
};
} // namespace

TEST_F(CloudSettings, DefaultsAreOffAndLocal) {
  for (const char *feature : {"agreements", "live_points", "summary"}) EXPECT_EQ(settings::featureEngine(feature), "local");
  EXPECT_FALSE(settings::cloudConsentValid());
  EXPECT_TRUE(settings::cloudPseudonymize());
  EXPECT_EQ(settings::cloudContextTokens(), 32768);
}

TEST_F(CloudSettings, FeatureEngineIgnoredWithoutConsent) {
  settings::setCloudBaseUrl("https://api.example.com/v1");
  settings::setFeatureEngine("summary", "cloud");
  EXPECT_EQ(settings::featureEngine("summary"), "local");
  settings::acceptCloudConsent();
  EXPECT_EQ(settings::featureEngine("summary"), "cloud");
  EXPECT_EQ(settings::featureEngine("live_points"), "local");
}

TEST_F(CloudSettings, ConsentTiedToUrl) {
  settings::setCloudBaseUrl("https://api.example.com/v1");
  settings::acceptCloudConsent();
  settings::setFeatureEngine("summary", "cloud");
  settings::setCloudBaseUrl("https://other.example.org/v1");
  EXPECT_FALSE(settings::cloudConsentValid());
  EXPECT_EQ(settings::featureEngine("summary"), "local");
  // Consent for the old address does not come back when the address does.
  settings::setCloudBaseUrl("https://api.example.com/v1");
  EXPECT_FALSE(settings::cloudConsentValid());
  settings::acceptCloudConsent();
  EXPECT_EQ(settings::featureEngine("summary"), "local");
}

TEST_F(CloudSettings, TrailingSlashAndHostCaseKeepConsent) {
  settings::setCloudBaseUrl("https://api.example.com/v1");
  settings::acceptCloudConsent();
  settings::setCloudBaseUrl("https://API.example.com/v1/");
  EXPECT_TRUE(settings::cloudConsentValid());
}

TEST_F(CloudSettings, RevokeDisablesAllFeatures) {
  settings::setCloudBaseUrl("https://api.example.com/v1");
  settings::acceptCloudConsent();
  for (const char *feature : {"agreements", "live_points", "summary"}) settings::setFeatureEngine(feature, "cloud");
  settings::revokeCloudConsent();
  for (const char *feature : {"agreements", "live_points", "summary"}) EXPECT_EQ(settings::featureEngine(feature), "local");
  settings::acceptCloudConsent();  // consent again does not silently re-enable features
  for (const char *feature : {"agreements", "live_points", "summary"}) EXPECT_EQ(settings::featureEngine(feature), "local");
}

TEST_F(CloudSettings, ContextTokensHaveAFloor) {
  settings::setCloudContextTokens(100);
  EXPECT_EQ(settings::cloudContextTokens(), 2048);
}

TEST_F(CloudSettings, NoSettingHoldsAKey) {
  settings::setCloudBaseUrl("https://api.example.com/v1");
  settings::setCloudModel("m");
  settings::setCloudPseudonymize(false);
  QSettings s;
  for (const auto &key : s.allKeys()) EXPECT_FALSE(key.contains("key", Qt::CaseInsensitive)) << key.toStdString();
}
