#include "cloud_settings_panel.h"

#include "app_settings.h"
#include "cloud_consent_dialog.h"
#include "cloud_engine.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>
#include <thread>

namespace {
QString hostOf(const QString &url) {
  const auto host = QUrl(url.trimmed()).host();
  return host.isEmpty() ? url.trimmed() : host;
}
QString reasonText(const QString &reason) {
  if (reason == "no key") return QCoreApplication::translate("CloudSettingsPanel", "No API key is set.");
  if (reason == "auth") return QCoreApplication::translate("CloudSettingsPanel", "The provider rejected the API key.");
  if (reason == "model") return QCoreApplication::translate("CloudSettingsPanel", "The provider does not know this model.");
  if (reason == "unsupported") return QCoreApplication::translate("CloudSettingsPanel", "The provider does not support strict JSON schema output (response_format json_schema), which Sessio requires.");
  if (reason == "rate limit") return QCoreApplication::translate("CloudSettingsPanel", "The provider is limiting requests. Try again later.");
  if (reason == "server") return QCoreApplication::translate("CloudSettingsPanel", "The provider reported an error.");
  if (reason == "network") return QCoreApplication::translate("CloudSettingsPanel", "No connection to the provider.");
  if (reason == "timeout") return QCoreApplication::translate("CloudSettingsPanel", "The provider took too long to answer.");
  return QCoreApplication::translate("CloudSettingsPanel", "The provider rejected the request.");
}
} // namespace

CloudSettingsPanel::CloudSettingsPanel(QWidget *parent) : QWidget(parent) {
  mAskConsent = [this](const QString &host, bool pseudonymize) {
    CloudConsentDialog dialog(host, pseudonymize, this);
    return dialog.exec() == QDialog::Accepted;
  };
  auto *layout = new QVBoxLayout(this);
  auto *intro = new QLabel(tr("Optional. Use a cloud model for meeting summaries, key points and agreements. Call text is sent to the provider you choose below, so cloud models are off until you accept the terms."), this);
  intro->setWordWrap(true);
  layout->addWidget(intro);

  auto *provider = new QGroupBox(tr("Provider (OpenAI-compatible)"), this);
  auto *form = new QFormLayout(provider);
  mUrl = new QLineEdit(provider); mUrl->setObjectName("cloudBaseUrl");
  mUrl->setPlaceholderText(QStringLiteral("https://api.example.com/v1"));
  form->addRow(tr("Address"), mUrl);
  mModel = new QLineEdit(provider); mModel->setObjectName("cloudModel");
  form->addRow(tr("Model"), mModel);
  mKey = new QLineEdit(provider); mKey->setObjectName("cloudApiKey");
  mKey->setEchoMode(QLineEdit::Password);
  form->addRow(tr("API key"), mKey);
  mKeyState = new QLabel(provider); mKeyState->setObjectName("cloudKeyState"); mKeyState->setWordWrap(true);
  mRemoveKey = new QPushButton(tr("Remove key"), provider); mRemoveKey->setObjectName("cloudRemoveKey");
  auto *keyRow = new QWidget(provider);
  auto *keyRowLayout = new QHBoxLayout(keyRow);
  keyRowLayout->setContentsMargins(0, 0, 0, 0);
  keyRowLayout->addWidget(mKeyState, 1); keyRowLayout->addWidget(mRemoveKey);
  form->addRow(keyRow);
  mContext = new QSpinBox(provider); mContext->setObjectName("cloudContext");
  mContext->setRange(2048, 2000000); mContext->setSingleStep(1024);
  mContext->setToolTip(tr("The model's context window. Recorded for future use; summaries currently use small parts of the call."));
  form->addRow(tr("Context size (tokens)"), mContext);
  mTest = new QPushButton(tr("Test connection"), provider); mTest->setObjectName("cloudTest");
  mTestResult = new QLabel(provider); mTestResult->setObjectName("cloudTestResult"); mTestResult->setWordWrap(true);
  mTestResult->setTextFormat(Qt::PlainText);
  auto *testRow = new QWidget(provider);
  auto *testLayout = new QHBoxLayout(testRow);
  testLayout->setContentsMargins(0, 0, 0, 0);
  testLayout->addWidget(mTest); testLayout->addWidget(mTestResult, 1);
  form->addRow(testRow);
  layout->addWidget(provider);

  auto *privacy = new QGroupBox(tr("Privacy"), this);
  auto *privacyLayout = new QVBoxLayout(privacy);
  mPseudonymize = new QCheckBox(tr("Hide names, phone numbers and e-mail addresses before sending"), privacy);
  mPseudonymize->setObjectName("cloudPseudonymize");
  privacyLayout->addWidget(mPseudonymize);
  mConsentState = new QLabel(privacy); mConsentState->setObjectName("cloudConsentState"); mConsentState->setWordWrap(true);
  privacyLayout->addWidget(mConsentState);
  auto *consentRow = new QWidget(privacy);
  auto *consentLayout = new QHBoxLayout(consentRow);
  consentLayout->setContentsMargins(0, 0, 0, 0);
  mEnable = new QPushButton(tr("Enable cloud models…"), consentRow); mEnable->setObjectName("cloudEnable");
  mRevoke = new QPushButton(tr("Withdraw consent"), consentRow); mRevoke->setObjectName("cloudRevoke");
  consentLayout->addWidget(mEnable); consentLayout->addWidget(mRevoke); consentLayout->addStretch(1);
  privacyLayout->addWidget(consentRow);
  layout->addWidget(privacy);

  auto *features = new QGroupBox(tr("Where each task runs"), this);
  auto *featureForm = new QFormLayout(features);
  const auto combo = [features](const char *name) {
    auto *box = new QComboBox(features);
    box->setObjectName(QString::fromLatin1(name));
    box->addItem(QCoreApplication::translate("CloudSettingsPanel", "Local model"), QStringLiteral("local"));
    box->addItem(QCoreApplication::translate("CloudSettingsPanel", "Cloud model"), QStringLiteral("cloud"));
    return box;
  };
  mAgreements = combo("engineAgreements");
  mLivePoints = combo("engineLivePoints");
  mSummary = combo("engineSummary");
  featureForm->addRow(tr("Agreements during the call"), mAgreements);
  featureForm->addRow(tr("Key points during the call"), mLivePoints);
  featureForm->addRow(tr("Meeting summary after the call"), mSummary);
  auto *noFallback = new QLabel(tr("If the cloud is unavailable, Sessio says so and does not switch to the local model on its own."), features);
  noFallback->setWordWrap(true);
  featureForm->addRow(noFallback);
  layout->addWidget(features);
  layout->addStretch(1);

  namespace settings = pcm::app_settings;
  connect(mUrl, &QLineEdit::textChanged, this, &CloudSettingsPanel::invalidateProbe);
  connect(mModel, &QLineEdit::textChanged, this, &CloudSettingsPanel::invalidateProbe);
  connect(mKey, &QLineEdit::textChanged, this, &CloudSettingsPanel::invalidateProbe);
  connect(mUrl, &QLineEdit::editingFinished, this, [this] {
    settings::setCloudBaseUrl(mUrl->text());
    refresh();
  });
  connect(mModel, &QLineEdit::editingFinished, this, [this] { settings::setCloudModel(mModel->text()); });
  connect(mContext, &QSpinBox::editingFinished, this, [this] { settings::setCloudContextTokens(mContext->value()); });
  connect(mPseudonymize, &QCheckBox::toggled, this, [](bool on) { settings::setCloudPseudonymize(on); });
  connect(mKey, &QLineEdit::editingFinished, this, [this] {
    const auto key = mKey->text().trimmed();
    if (key.isEmpty()) return;
    mKey->clear();
    mPendingKey = key;
    mKeyBusy = true;
    mKeyStatus = tr("Saving key…");
    refresh();
    emit keyChanged(key);
  });
  connect(mRemoveKey, &QPushButton::clicked, this, [this] {
    mPendingKey.clear(); mKey->clear();
    mKeyBusy = true;
    mKeyStatus = tr("Removing key…");
    invalidateProbe();
    refresh();
    emit keyChanged({});
  });
  connect(mEnable, &QPushButton::clicked, this, [this] {
    settings::setCloudBaseUrl(mUrl->text());
    settings::setCloudModel(mModel->text());
    if (mUrl->text().trimmed().isEmpty() || mModel->text().trimmed().isEmpty()) {
      mConsentState->setText(tr("Enter the provider address and the model first."));
      return;
    }
    if (!validProvider()) {
      mConsentState->setText(tr("Enter an HTTPS provider address, or HTTP for a local provider, without credentials, query or fragment."));
      return;
    }
    const bool accepted = mAskConsent && mAskConsent(hostOf(mUrl->text()), settings::cloudPseudonymize());
    if (accepted) settings::acceptCloudConsent();
    refresh();
    if (accepted) emit consentAccepted();
  });
  connect(mRevoke, &QPushButton::clicked, this, [this] { settings::revokeCloudConsent(); refresh(); });
  connect(mTest, &QPushButton::clicked, this, &CloudSettingsPanel::runTest);
  const auto engineChanged = [](const char *feature, QComboBox *box) {
    return [feature, box] { settings::setFeatureEngine(feature, box->currentData().toString()); };
  };
  connect(mAgreements, &QComboBox::currentIndexChanged, this, engineChanged("agreements", mAgreements));
  connect(mLivePoints, &QComboBox::currentIndexChanged, this, engineChanged("live_points", mLivePoints));
  connect(mSummary, &QComboBox::currentIndexChanged, this, engineChanged("summary", mSummary));

  mUrl->setText(settings::cloudBaseUrl());
  mModel->setText(settings::cloudModel());
  mContext->setValue(settings::cloudContextTokens());
  mPseudonymize->setChecked(settings::cloudPseudonymize());
  refresh();
}

void CloudSettingsPanel::setStoredKey(const QString &key) {
  mStoredKey = key;
  mKeyStatus.clear();
  invalidateProbe();
  refresh();
}

void CloudSettingsPanel::beginKeyRead() {
  mKeyBusy = true;
  mKeyStatus = tr("Reading key from the system keychain…");
  refresh();
}

void CloudSettingsPanel::keyReadFinished(bool ok, const QString &key) {
  mKeyBusy = false;
  if (ok) {
    setStoredKey(key);
    emit keyAvailable(key);
  } else {
    mKeyStatus = tr("The key could not be read from the system keychain. Unlock the keychain and reopen settings to retry.");
    refresh();
  }
}

void CloudSettingsPanel::keyWriteFinished(bool ok) {
  mKeyBusy = false;
  if (ok) {
    setStoredKey(mPendingKey);
    emit keyAvailable(mStoredKey);
  } else {
    mKeyStatus = mPendingKey.isEmpty()
        ? tr("The key could not be removed from the system keychain. Try again.")
        : tr("The key could not be saved in the system keychain. Unlock the keychain and enter it again.");
    refresh();
  }
  mPendingKey.clear();
}

void CloudSettingsPanel::invalidateProbe() {
  ++mProbeGeneration;
  mTestResult->clear();
  mTest->setEnabled(!mKeyBusy);
}

bool CloudSettingsPanel::validProvider() const {
  const QUrl url(mUrl->text().trimmed(), QUrl::StrictMode);
  const auto host = url.host();
  const bool local = host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0
      || QHostAddress(host).isLoopback();
  return url.isValid() && !host.isEmpty()
      && (url.scheme() == QLatin1String("https") || (url.scheme() == QLatin1String("http") && local))
      && url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment();
}

void CloudSettingsPanel::refresh() {
  namespace settings = pcm::app_settings;
  const bool consent = settings::cloudConsentValid();
  mConsentState->setText(consent
      ? tr("Cloud models are enabled for %1.").arg(hostOf(settings::cloudBaseUrl()))
      : tr("Cloud models are off. Nothing is sent anywhere."));
  mEnable->setVisible(!consent);
  mRevoke->setVisible(consent);
  mKeyState->setText(!mKeyStatus.isEmpty() ? mKeyStatus
      : mStoredKey.isEmpty() ? tr("No key stored.") : tr("A key is stored in the system keychain."));
  mKey->setEnabled(!mKeyBusy);
  mRemoveKey->setEnabled(!mKeyBusy && !mStoredKey.isEmpty());
  mTest->setEnabled(!mKeyBusy);
  for (auto *box : {mAgreements, mLivePoints, mSummary}) box->setEnabled(consent);
  const struct { QComboBox *box; const char *feature; } rows[] = {
      {mAgreements, "agreements"}, {mLivePoints, "live_points"}, {mSummary, "summary"}};
  for (const auto &row : rows) {
    const QSignalBlocker blocker(row.box);
    row.box->setCurrentIndex(row.box->findData(settings::featureEngine(row.feature)));
  }
}

void CloudSettingsPanel::runTest() {
  pcm::prompter::CloudConfig config;
  config.baseUrl = mUrl->text().trimmed();
  config.model = mModel->text().trimmed();
  config.apiKey = mKey->text().trimmed().isEmpty() ? mStoredKey : mKey->text().trimmed();
  config.timeoutMs = 30000;
  if (config.baseUrl.isEmpty() || config.model.isEmpty()) {
    mTestResult->setText(tr("Enter the provider address and the model first."));
    return;
  }
  if (!validProvider()) {
    mTestResult->setText(tr("Enter an HTTPS provider address, or HTTP for a local provider, without credentials, query or fragment."));
    return;
  }
  mTest->setEnabled(false);
  mTestResult->setText(tr("Testing…"));
  QPointer<CloudSettingsPanel> guard(this);
  const auto generation = ++mProbeGeneration;
  std::thread([guard, config, generation] {
    const auto probe = pcm::prompter::probeCloud(config);
    QMetaObject::invokeMethod(qApp, [guard, probe, generation] {
      if (!guard || guard->mProbeGeneration != generation) return;
      guard->mTest->setEnabled(true);
      guard->mTestResult->setText(probe.ok ? tr("Connected. The provider supports strict JSON schema output.")
                                           : reasonText(probe.reason));
    }, Qt::QueuedConnection);
  }).detach();
}
