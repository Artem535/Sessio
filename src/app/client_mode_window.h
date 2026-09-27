#pragma once

#include "calls_page.h"
#include "device_manager.h"
#include "token_backend_client.h"

#include <QMainWindow>

class ClientModeWindow final : public QMainWindow {
  Q_OBJECT

public:
  ClientModeWindow(pcm::video::DeviceManager *deviceManager,
                   pcm::tokenclient::TokenBackendClient *tokenClient, QWidget *parent = nullptr);
};
