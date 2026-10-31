#pragma once

#include "calls_page.h"
#include "device_manager.h"
#include "token_backend_client.h"

#include <QMainWindow>

namespace pcm {
class AppRoleSwitcher;
}

class ClientModeWindow final : public QMainWindow {
  Q_OBJECT

public:
  ClientModeWindow(pcm::video::DeviceManager *deviceManager,
                   pcm::tokenclient::TokenBackendClient *tokenClient, QWidget *parent = nullptr);

  // Not owned. Without a switcher the "Switch to specialist mode" action stays
  // disabled; Application injects the one that restarts the process.
  void setRoleSwitcher(const pcm::AppRoleSwitcher *switcher);

private:
  void openSettingsDialog();
  void switchToSpecialistMode();

  // Not owned: Application owns the token client and outlives this window.
  pcm::tokenclient::TokenBackendClient *mTokenClient;
  const pcm::AppRoleSwitcher *mRoleSwitcher = nullptr;
  QAction *mSwitchRoleAction = nullptr;
};
