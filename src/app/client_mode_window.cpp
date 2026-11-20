#include "client_mode_window.h"
#include "app_role_switcher.h"
#include "client_mode_settings_dialog.h"
#include "role_switch_prompt.h"

#include <QAction>
#include <QMenuBar>

ClientModeWindow::ClientModeWindow(pcm::video::DeviceManager *deviceManager,
                                   pcm::tokenclient::TokenBackendClient *tokenClient,
                                   QWidget *parent)
    : QMainWindow(parent), mTokenClient(tokenClient) {
  setWindowTitle(tr("Sessio"));
  setCentralWidget(new CallsPage(/*specialistMode=*/false, deviceManager, tokenClient, this));

  // A single menu action is the whole settings surface in client mode: it
  // exposes the call name and token backend URL (see ClientModeSettingsDialog).
  auto *settingsAction = menuBar()->addAction(tr("Settings"));
  settingsAction->setObjectName(QStringLiteral("clientModeSettingsAction"));
  connect(settingsAction, &QAction::triggered, this, &ClientModeWindow::openSettingsDialog);

  // Returns to the specialist application (confirmation + restart). Disabled
  // until Application injects the switcher.
  mSwitchRoleAction = menuBar()->addAction(tr("Switch to specialist mode…"));
  mSwitchRoleAction->setObjectName(QStringLiteral("clientModeSwitchRoleAction"));
  mSwitchRoleAction->setEnabled(false);
  connect(mSwitchRoleAction, &QAction::triggered, this, &ClientModeWindow::switchToSpecialistMode);
}

void ClientModeWindow::setRoleSwitcher(const pcm::AppRoleSwitcher *switcher) {
  mRoleSwitcher = switcher;
  mSwitchRoleAction->setEnabled(switcher != nullptr);
}

void ClientModeWindow::switchToSpecialistMode() {
  if (mRoleSwitcher == nullptr) {
    return;
  }
  pcm::confirmAndSwitchRole(this, *mRoleSwitcher, pcm::config::AppRole::Specialist);
}

void ClientModeWindow::openSettingsDialog() {
  ClientModeSettingsDialog dialog(mTokenClient, this);
  dialog.exec();
}
