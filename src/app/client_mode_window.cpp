#include "client_mode_window.h"

ClientModeWindow::ClientModeWindow(pcm::video::DeviceManager *deviceManager,
                                   pcm::tokenclient::TokenBackendClient *tokenClient,
                                   QWidget *parent)
    : QMainWindow(parent) {
  setWindowTitle(tr("Sessio"));
  setCentralWidget(new CallsPage(/*specialistMode=*/false, deviceManager, tokenClient, this));
}
