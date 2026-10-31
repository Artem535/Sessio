#include "app_settings.h"
#include "cloud_consent_dialog.h"
#include "cloud_settings_panel.h"
#include "fake_http_server.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>

namespace settings = pcm::app_settings;

namespace {
class CloudPanelTest : public ::testing::Test {
protected:
  void SetUp() override {
    QCoreApplication::setOrganizationName("SessioCloudPanelTests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    QSettings().clear();
  }
  template <class T> T *child(CloudSettingsPanel &panel, const char *name) { return panel.findChild<T *>(name); }
  QTemporaryDir dir;
};
QByteArray chat(const QByteArray &content) {
  return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{
      {"finish_reason", "stop"}, {"message", QJsonObject{{"content", QString::fromUtf8(content)}}}}}}}).toJson(QJsonDocument::Compact);
}
} // namespace

TEST_F(CloudPanelTest, EngineCombosAreDisabledUntilConsent) {
  CloudSettingsPanel panel;
  for (const char *name : {"engineAgreements", "engineLivePoints", "engineSummary"})
    EXPECT_FALSE(child<QComboBox>(panel, name)->isEnabled()) << name;
  EXPECT_TRUE(child<QPushButton>(panel, "cloudEnable")->isVisibleTo(&panel));
  EXPECT_FALSE(child<QPushButton>(panel, "cloudRevoke")->isVisibleTo(&panel));
}

TEST_F(CloudPanelTest, AcceptingTheTermsEnablesTheCombos) {
  CloudSettingsPanel panel;
  QString askedHost;
  panel.setConsentAsker([&](const QString &host, bool) { askedHost = host; return true; });
  child<QLineEdit>(panel, "cloudBaseUrl")->setText("https://api.example.com/v1");
  child<QLineEdit>(panel, "cloudModel")->setText("m1");
  child<QPushButton>(panel, "cloudEnable")->click();
  EXPECT_EQ(askedHost, "api.example.com");
  EXPECT_TRUE(settings::cloudConsentValid());
  EXPECT_TRUE(child<QComboBox>(panel, "engineSummary")->isEnabled());
  child<QComboBox>(panel, "engineSummary")->setCurrentIndex(1);
  EXPECT_EQ(settings::featureEngine("summary"), "cloud");
  EXPECT_EQ(settings::featureEngine("live_points"), "local");
}

TEST_F(CloudPanelTest, DecliningTheTermsLeavesCloudOff) {
  CloudSettingsPanel panel;
  panel.setConsentAsker([](const QString &, bool) { return false; });
  child<QLineEdit>(panel, "cloudBaseUrl")->setText("https://api.example.com/v1");
  child<QLineEdit>(panel, "cloudModel")->setText("m1");
  child<QPushButton>(panel, "cloudEnable")->click();
  EXPECT_FALSE(settings::cloudConsentValid());
}

TEST_F(CloudPanelTest, EnablingNeedsAddressAndModel) {
  CloudSettingsPanel panel;
  bool asked = false;
  panel.setConsentAsker([&](const QString &, bool) { asked = true; return true; });
  child<QPushButton>(panel, "cloudEnable")->click();
  EXPECT_FALSE(asked);
  EXPECT_FALSE(settings::cloudConsentValid());
}

TEST_F(CloudPanelTest, ChangingTheAddressWithdrawsConsentAndResetsTheCombos) {
  CloudSettingsPanel panel;
  panel.setConsentAsker([](const QString &, bool) { return true; });
  auto *url = child<QLineEdit>(panel, "cloudBaseUrl");
  url->setText("https://api.example.com/v1");
  child<QLineEdit>(panel, "cloudModel")->setText("m1");
  child<QPushButton>(panel, "cloudEnable")->click();
  child<QComboBox>(panel, "engineSummary")->setCurrentIndex(1);
  url->setText("https://other.example.org/v1");
  emit url->editingFinished();
  EXPECT_FALSE(settings::cloudConsentValid());
  EXPECT_FALSE(child<QComboBox>(panel, "engineSummary")->isEnabled());
  EXPECT_EQ(child<QComboBox>(panel, "engineSummary")->currentData().toString(), "local");
}

TEST_F(CloudPanelTest, WithdrawingConsentSwitchesEverythingBackToLocal) {
  CloudSettingsPanel panel;
  panel.setConsentAsker([](const QString &, bool) { return true; });
  child<QLineEdit>(panel, "cloudBaseUrl")->setText("https://api.example.com/v1");
  child<QLineEdit>(panel, "cloudModel")->setText("m1");
  child<QPushButton>(panel, "cloudEnable")->click();
  child<QComboBox>(panel, "engineAgreements")->setCurrentIndex(1);
  child<QPushButton>(panel, "cloudRevoke")->click();
  EXPECT_EQ(settings::featureEngine("agreements"), "local");
  EXPECT_FALSE(child<QComboBox>(panel, "engineAgreements")->isEnabled());
}

TEST_F(CloudPanelTest, KeyIsHandedOutNeverPrefilledAndNeverStoredInSettings) {
  CloudSettingsPanel panel;
  QSignalSpy keys(&panel, &CloudSettingsPanel::keyChanged);
  auto *field = child<QLineEdit>(panel, "cloudApiKey");
  field->setText("sk-secret");
  emit field->editingFinished();
  ASSERT_EQ(keys.count(), 1);
  EXPECT_EQ(keys[0][0].toString(), "sk-secret");
  EXPECT_TRUE(field->text().isEmpty());
  for (const auto &key : QSettings().allKeys()) EXPECT_FALSE(QSettings().value(key).toString().contains("sk-secret"));
  panel.keyWriteFinished(true);
  child<QPushButton>(panel, "cloudRemoveKey")->click();
  ASSERT_EQ(keys.count(), 2);
  EXPECT_TRUE(keys[1][0].toString().isEmpty());
  panel.keyWriteFinished(true);
  // A stored key is announced, not shown.
  panel.setStoredKey("sk-stored");
  EXPECT_TRUE(field->text().isEmpty());
  EXPECT_FALSE(child<QLabel>(panel, "cloudKeyState")->text().contains("sk-stored"));
}

TEST_F(CloudPanelTest, PseudonymizationDefaultsOnAndIsStored) {
  CloudSettingsPanel panel;
  auto *box = child<QCheckBox>(panel, "cloudPseudonymize");
  EXPECT_TRUE(box->isChecked());
  box->setChecked(false);
  EXPECT_FALSE(settings::cloudPseudonymize());
}

TEST_F(CloudPanelTest, InvalidProviderAddressCannotReceiveConsent) {
  CloudSettingsPanel panel;
  bool asked = false;
  panel.setConsentAsker([&](const QString &, bool) { asked = true; return true; });
  child<QLineEdit>(panel, "cloudBaseUrl")->setText("not-an-address");
  child<QLineEdit>(panel, "cloudModel")->setText("m1");
  child<QPushButton>(panel, "cloudEnable")->click();
  EXPECT_FALSE(asked);
  EXPECT_FALSE(settings::cloudConsentValid());
}

TEST_F(CloudPanelTest, EditedConfigurationClearsPreviousProbeResult) {
  CloudSettingsPanel panel;
  auto *result = child<QLabel>(panel, "cloudTestResult");
  result->setText("Connected");
  child<QLineEdit>(panel, "cloudModel")->setText("different-model");
  EXPECT_TRUE(result->text().isEmpty());
}

TEST_F(CloudPanelTest, KeyStorageIsReportedOnlyAfterSuccessfulCompletion) {
  CloudSettingsPanel panel;
  auto *field = child<QLineEdit>(panel, "cloudApiKey");
  field->setText("sk-secret");
  emit field->editingFinished();
  auto *state = child<QLabel>(panel, "cloudKeyState");
  EXPECT_FALSE(state->text().contains("A key is stored"));
  EXPECT_TRUE(state->text().contains("Saving"));
  panel.keyWriteFinished(false);
  EXPECT_TRUE(state->text().contains("could not"));
  EXPECT_FALSE(state->text().contains("sk-secret"));
}

TEST_F(CloudPanelTest, StoredKeyCanBeReadAndRemovedBeforeConsent) {
  CloudSettingsPanel panel;
  QSignalSpy available(&panel, &CloudSettingsPanel::keyAvailable);
  QSignalSpy writes(&panel, &CloudSettingsPanel::keyChanged);
  panel.beginKeyRead();
  EXPECT_FALSE(child<QLineEdit>(panel, "cloudApiKey")->isEnabled());
  panel.keyReadFinished(true, "saved-secret");
  EXPECT_FALSE(settings::cloudConsentValid());
  ASSERT_EQ(available.count(), 1);
  EXPECT_EQ(available[0][0].toString(), "saved-secret");
  EXPECT_TRUE(child<QLineEdit>(panel, "cloudApiKey")->text().isEmpty());
  child<QPushButton>(panel, "cloudRemoveKey")->click();
  ASSERT_EQ(writes.count(), 1);
  EXPECT_TRUE(writes[0][0].toString().isEmpty());
  EXPECT_TRUE(child<QLabel>(panel, "cloudKeyState")->text().contains("Removing"));
  panel.keyWriteFinished(false);
  EXPECT_EQ(available.count(), 1);
  EXPECT_TRUE(child<QPushButton>(panel, "cloudRemoveKey")->isEnabled());
  child<QPushButton>(panel, "cloudRemoveKey")->click();
  panel.keyWriteFinished(true);
  ASSERT_EQ(available.count(), 2);
  EXPECT_TRUE(available[1][0].toString().isEmpty());
  EXPECT_FALSE(child<QPushButton>(panel, "cloudRemoveKey")->isEnabled());
}

TEST_F(CloudPanelTest, KeyReadFailureIsVisibleAndAllowsReplacement) {
  CloudSettingsPanel panel;
  panel.beginKeyRead();
  panel.keyReadFinished(false, {});
  EXPECT_TRUE(child<QLabel>(panel, "cloudKeyState")->text().contains("could not be read"));
  EXPECT_TRUE(child<QLineEdit>(panel, "cloudApiKey")->isEnabled());
}

TEST_F(CloudPanelTest, UnsafeOrMalformedProviderAddressesCannotReceiveConsent) {
  for (const auto &address : {"http://remote.example/v1", "https://user:secret@example.com/v1",
                             "https://example.com/v1?key=secret", "https://example.com/v1#fragment",
                             "file:///tmp/model"}) {
    CloudSettingsPanel panel;
    bool asked = false;
    panel.setConsentAsker([&](const QString &, bool) { asked = true; return true; });
    child<QLineEdit>(panel, "cloudBaseUrl")->setText(address);
    child<QLineEdit>(panel, "cloudModel")->setText("m1");
    child<QPushButton>(panel, "cloudEnable")->click();
    EXPECT_FALSE(asked) << address;
    EXPECT_FALSE(settings::cloudConsentValid()) << address;
  }
}

TEST_F(CloudPanelTest, LoopbackHttpProviderCanReceiveConsent) {
  for (const auto &address : {"http://localhost:11434/v1", "http://127.0.0.1:8000/v1", "http://[::1]:8000/v1"}) {
    CloudSettingsPanel panel;
    panel.setConsentAsker([](const QString &, bool) { return true; });
    child<QLineEdit>(panel, "cloudBaseUrl")->setText(address);
    child<QLineEdit>(panel, "cloudModel")->setText("m1");
    child<QPushButton>(panel, "cloudEnable")->click();
    EXPECT_TRUE(settings::cloudConsentValid()) << address;
  }
}

TEST_F(CloudPanelTest, InFlightProbeCannotApproveAnEditedConfiguration) {
  FakeHttpServer server;
  server.enqueue({200, chat(R"({"ok":true})"), 100});
  CloudSettingsPanel panel;
  child<QLineEdit>(panel, "cloudBaseUrl")->setText(server.url("/v1").toString());
  child<QLineEdit>(panel, "cloudModel")->setText("m1");
  panel.setStoredKey("k");
  child<QPushButton>(panel, "cloudTest")->click();
  QTRY_COMPARE_WITH_TIMEOUT(server.requests().size(), 1, 5000);
  child<QLineEdit>(panel, "cloudModel")->setText("m2");
  QTest::qWait(250);
  EXPECT_TRUE(child<QLabel>(panel, "cloudTestResult")->text().isEmpty());
  EXPECT_TRUE(child<QPushButton>(panel, "cloudTest")->isEnabled());
}

TEST_F(CloudPanelTest, TestConnectionAcceptsAProviderWithJsonSchema) {
  FakeHttpServer server;
  server.enqueue({200, chat(R"({"ok":true})")});
  CloudSettingsPanel panel;
  child<QLineEdit>(panel, "cloudBaseUrl")->setText(server.url("/v1").toString());
  child<QLineEdit>(panel, "cloudModel")->setText("m1");
  panel.setStoredKey("k");
  child<QPushButton>(panel, "cloudTest")->click();
  auto *result = child<QLabel>(panel, "cloudTestResult");
  QTRY_VERIFY_WITH_TIMEOUT(result->text().startsWith("Connected"), 5000);
  EXPECT_TRUE(child<QPushButton>(panel, "cloudTest")->isEnabled());
}

TEST_F(CloudPanelTest, TestConnectionRejectsAProviderWithoutJsonSchema) {
  FakeHttpServer server;
  server.enqueue({400, R"({"error":{"message":"response_format json_schema is not supported"}})"});
  CloudSettingsPanel panel;
  child<QLineEdit>(panel, "cloudBaseUrl")->setText(server.url("/v1").toString());
  child<QLineEdit>(panel, "cloudModel")->setText("m1");
  panel.setStoredKey("k");
  child<QPushButton>(panel, "cloudTest")->click();
  auto *result = child<QLabel>(panel, "cloudTestResult");
  QTRY_VERIFY_WITH_TIMEOUT(result->text().contains("json_schema"), 5000);
}

TEST_F(CloudPanelTest, TestConnectionWithoutAKeyDoesNotCallTheProvider) {
  FakeHttpServer server;
  server.enqueue({200, chat(R"({"ok":true})")});
  CloudSettingsPanel panel;
  child<QLineEdit>(panel, "cloudBaseUrl")->setText(server.url("/v1").toString());
  child<QLineEdit>(panel, "cloudModel")->setText("m1");
  child<QPushButton>(panel, "cloudTest")->click();
  auto *result = child<QLabel>(panel, "cloudTestResult");
  QTRY_VERIFY_WITH_TIMEOUT(result->text().contains("No API key"), 5000);
  EXPECT_EQ(server.requests().size(), 0);
}

TEST_F(CloudPanelTest, ConsentDialogNamesTheHostAndNeedsAnExplicitTick) {
  CloudConsentDialog dialog("api.example.com", true);
  EXPECT_TRUE(dialog.findChild<QLabel *>("cloudConsentHost")->text().contains("api.example.com"));
  EXPECT_FALSE(dialog.acceptButton()->isEnabled());
  dialog.understoodBox()->setChecked(true);
  EXPECT_TRUE(dialog.acceptButton()->isEnabled());
}

TEST_F(CloudPanelTest, ConsentDialogSaysWhenNamesAreNotHidden) {
  CloudConsentDialog dialog("api.example.com", false);
  EXPECT_TRUE(dialog.findChild<QLabel *>("cloudConsentPseudonyms")->text().contains("sent as it is"));
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
