#include "client_mode_window.h"
#include "client_mode_settings_dialog.h"

#include <QAction>
#include <QMenuBar>

ClientModeWindow::ClientModeWindow(pcm::video::DeviceManager *deviceManager,
                                   pcm::tokenclient::TokenBackendClient *tokenClient,
                                   QWidget *parent)
    : QMainWindow(parent), mTokenClient(tokenClient) {
  setWindowTitle(tr("Sessio"));
  setCentralWidget(new CallsPage(/*specialistMode=*/false, deviceManager, tokenClient, this));

  // A single menu action is the whole settings surface in client mode: it
  // only exposes the token backend URL (see ClientModeSettingsDialog).
  auto *settingsAction = menuBar()->addAction(tr("Settings"));
  settingsAction->setObjectName(QStringLiteral("clientModeSettingsAction"));
  connect(settingsAction, &QAction::triggered, this, &ClientModeWindow::openSettingsDialog);
}

void ClientModeWindow::openSettingsDialog() {
  ClientModeSettingsDialog dialog(mTokenClient, this);
  dialog.exec();
}
