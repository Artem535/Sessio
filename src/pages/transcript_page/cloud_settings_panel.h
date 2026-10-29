#pragma once

#include <QWidget>
#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

// Settings for cloud AI models: provider address, model, key, consent, hiding
// of names and the engine of each feature. Everything but the key is stored
// through pcm::app_settings; the key is only handed out through keyChanged().
class CloudSettingsPanel final : public QWidget {
  Q_OBJECT
public:
  explicit CloudSettingsPanel(QWidget *parent = nullptr);
  // The key currently in the keychain (empty: none); used by "Test connection".
  void setStoredKey(const QString &key);
  // Asks for the terms; true when accepted. The default shows CloudConsentDialog.
  void setConsentAsker(std::function<bool(const QString &host, bool pseudonymize)> asker) { mAskConsent = std::move(asker); }
signals:
  // A new key typed by the user, or empty after "Remove key".
  void keyChanged(QString key);
  // The terms were accepted: the caller may now read the key from the keychain.
  void consentAccepted();
private:
  void refresh();
  void runTest();
  QLineEdit *mUrl, *mModel, *mKey;
  QSpinBox *mContext;
  QCheckBox *mPseudonymize;
  QComboBox *mAgreements, *mLivePoints, *mSummary;
  QPushButton *mEnable, *mRevoke, *mRemoveKey, *mTest;
  QLabel *mConsentState, *mTestResult, *mKeyState;
  QString mStoredKey;
  std::function<bool(const QString &, bool)> mAskConsent;
};
