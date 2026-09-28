#include "client_mode_settings_dialog.h"
#include "config.h"
#include "token_backend_client.h"
#include "../widgets/meeting_utils.h"

#include <QDebug>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

ClientModeSettingsDialog::ClientModeSettingsDialog(
    pcm::tokenclient::TokenBackendClient *tokenClient, QWidget *parent)
    : QDialog(parent), mTokenClient(tokenClient) {
  setWindowTitle(tr("Settings"));
  setModal(true);

  auto *layout = new QVBoxLayout(this);
  auto *title = new QLabel(tr("Token backend URL"), this);
  auto *description = new QLabel(
      tr("Address of the service that issued your invitation. Your specialist can tell you "
         "this if joining by code fails."),
      this);
  description->setWordWrap(true);

  mUrlEdit = new QLineEdit(this);
  mUrlEdit->setObjectName(QStringLiteral("tokenBackendUrlEdit"));
  mUrlEdit->setPlaceholderText(tr("https://token-backend.example.com"));

  // Prefer the saved value; a corrupt config must not keep the dialog from
  // opening, so fall back to what the token client is using right now.
  QString currentUrl = mTokenClient->baseUrl();
  try {
    currentUrl = QString::fromStdString(pcm::config::Config::read_config().token_backend_base_url);
  } catch (const std::exception &error) {
    qWarning() << "ClientModeSettingsDialog: failed to read config:" << error.what();
  }
  mUrlEdit->setText(currentUrl);

  mErrorLabel = new QLabel(tr("Enter a valid http or https address."), this);
  mErrorLabel->setObjectName(QStringLiteral("tokenBackendUrlError"));
  mErrorLabel->setVisible(false);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &ClientModeSettingsDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &ClientModeSettingsDialog::reject);

  layout->addWidget(title);
  layout->addWidget(description);
  layout->addWidget(mUrlEdit);
  layout->addWidget(mErrorLabel);
  layout->addWidget(buttons);
}

void ClientModeSettingsDialog::accept() {
  QString url = mUrlEdit->text().trimmed();
  if (!pcm::meeting::isValidMeetingUrl(url)) {
    mErrorLabel->setVisible(true);
    return;
  }
  // Request paths start with '/', so a trailing slash would double it.
  while (url.endsWith(QLatin1Char('/'))) {
    url.chop(1);
  }

  // Takes effect for the next join without a restart, even if saving fails.
  mTokenClient->setBaseUrl(url);

  // A failed read skips the save: writing a default-constructed Config over
  // an unreadable file would reset the stored app role to "Unset".
  try {
    auto conf = pcm::config::Config::read_config();
    conf.token_backend_base_url = url.toStdString();
    pcm::config::Config::save_config(conf);
  } catch (const std::exception &error) {
    qWarning() << "ClientModeSettingsDialog: failed to save the token backend URL:"
               << error.what();
    QMessageBox::warning(this, tr("Token backend"),
                         tr("The token backend URL applies until Sessio is closed, but could "
                            "not be saved:\n%1")
                             .arg(QString::fromUtf8(error.what())));
  }

  QDialog::accept();
}
