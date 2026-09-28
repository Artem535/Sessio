#include "single_instance_guard.h"

#include <QAbstractSocket>
#include <QLocalSocket>

SingleInstanceGuard::SingleInstanceGuard(QObject *parent) : QObject(parent) {
  mIsPrimary = mServer.listen(kServerName);

  if (!mIsPrimary && mServer.serverError() == QAbstractSocket::AddressInUseError) {
    // On Unix, listen() fails this way both when another instance is really
    // listening AND when a previous crash left a stale socket file behind
    // with nothing listening on it. Calling QLocalServer::removeServer()
    // unconditionally here (before checking which case this is) would delete
    // a still-live primary instance's socket file out from under it, letting
    // this instance wrongly become primary too — so probe with an actual
    // connection attempt first, and only remove + retry listen() if nothing
    // answers.
    QLocalSocket probe;
    probe.connectToServer(kServerName);
    if (probe.waitForConnected(200)) {
      probe.disconnectFromServer();
    } else {
      QLocalServer::removeServer(kServerName);
      mIsPrimary = mServer.listen(kServerName);
    }
  }

  if (mIsPrimary) {
    connect(&mServer, &QLocalServer::newConnection, this, [this]() {
      auto *socket = mServer.nextPendingConnection();
      connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
        const auto url = QString::fromUtf8(socket->readAll()).trimmed();
        if (!url.isEmpty()) {
          emit urlReceivedFromSecondaryInstance(url);
        }
      });
      connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    });
  }
}

void SingleInstanceGuard::forwardToPrimaryInstance(const QString &url) {
  QLocalSocket socket;
  socket.connectToServer(kServerName);
  if (!socket.waitForConnected(1000)) {
    return;
  }
  socket.write(url.toUtf8());
  socket.waitForBytesWritten(1000);
  socket.disconnectFromServer();
}
