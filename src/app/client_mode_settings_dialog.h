#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;

namespace pcm::tokenclient {
class TokenBackendClient;
}

// Client mode settings include the call display name and token backend URL, for a client who got a
// code and passcode without a sessio:// link naming the backend. Accepting
// retargets the token client immediately and persists the URL to Config.
// Deliberately not the specialist SettingsDialog (backups, encryption,
// notifications, database), none of which exist in client mode.
class ClientModeSettingsDialog final : public QDialog {
  Q_OBJECT

public:
  // tokenClient is not owned and must outlive the dialog.
  explicit ClientModeSettingsDialog(pcm::tokenclient::TokenBackendClient *tokenClient,
                                    QWidget *parent = nullptr);

  void accept() override;

private:
  pcm::tokenclient::TokenBackendClient *mTokenClient;
  QLineEdit *mUrlEdit{nullptr};
  QLineEdit *mDisplayNameEdit{nullptr};
  QLabel *mErrorLabel{nullptr};
};
