#include "settings_dialog.h"
#include "fake_token_backend_credential_store.h"

#include <QApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QTest>
#include <gtest/gtest.h>

TEST(SettingsDialogLiveKitSectionTest, SavingWritesBaseUrlToConfigAndCredentialToStore) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);

  auto *baseUrlEdit = dialog.findChild<QLineEdit *>("liveKitBaseUrlEdit");
  auto *credentialEdit = dialog.findChild<QLineEdit *>("liveKitBearerCredentialEdit");
  auto *saveButton = dialog.findChild<QPushButton *>("liveKitSaveButton");
  ASSERT_NE(baseUrlEdit, nullptr);
  ASSERT_NE(credentialEdit, nullptr);
  ASSERT_NE(saveButton, nullptr);

  baseUrlEdit->setText("https://token.example.test");
  credentialEdit->setText("bearer-secret");
  QTest::mouseClick(saveButton, Qt::LeftButton);

  const auto savedConfig = pcm::config::Config::read_config();
  EXPECT_EQ(savedConfig.token_backend_base_url, "https://token.example.test");
  EXPECT_TRUE(credentialStore->mHasCredential);
  EXPECT_EQ(credentialStore->mCredential, QStringLiteral("bearer-secret"));
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
