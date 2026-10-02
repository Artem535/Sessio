#include "single_instance_guard.h"
#include "../config/config.h"

#include <QAbstractSocket>
#include <QDebug>
#include <QDir>
#include <QLocalSocket>
#include <QStandardPaths>

namespace {

// Sentinel payload written over the wire when a secondary launch has no
// sessio:// URL to forward (a plain relaunch of the app, e.g. clicking the
// icon again). QLocalSocket::write() with an empty QByteArray never
// triggers the peer's readyRead() at all, so "just activate the primary
// window" needs a real, non-empty payload distinct from "nothing was sent".
// It intentionally does not parse as a sessio:// URL (parseSessioJoinUrl()
// requires scheme() == "sessio"), so the primary side's existing join-link
// parsing naturally treats it as "raise the window, no link to act on".
constexpr auto kActivateSentinel = "activate";

// Per-user lock file path, following the same convention as
// pcm::config::Config (see src/config/config.h): a directory named after
// pcm::config::kAppDirName under the platform's generic per-user config
// location. QStandardPaths::AppDataLocation/AppConfigLocation can't be used
// here because SingleInstanceGuard is constructed before
// QCoreApplication::setOrganizationName()/setApplicationName() run (see
// Application::run()), so those app-specific locations aren't populated yet
// at this point.
QString lockFilePath() {
  const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  const QString dir = base + QLatin1Char('/') + QLatin1String(pcm::config::kAppDirName);
  QDir().mkpath(dir);
  return dir + QStringLiteral("/single-instance.lock");
}

} // namespace

SingleInstanceGuard::SingleInstanceGuard(QObject *parent)
    : QObject(parent), mLockFile(lockFilePath()) {
  // Short timeout: if another process genuinely holds the lock, we want to
  // find that out quickly and become secondary, not block startup.
  mIsPrimary = mLockFile.tryLock(200);

  if (mIsPrimary) {
    // Restrict the local socket to the current user: QLocalServer's
    // underlying Unix domain socket file is otherwise created in a
    // shared filesystem location (typically under /tmp) with default
    // permissions, which would let another user on a shared multi-user
    // Linux machine connect to it and read or spoof forwarded sessio://
    // join URLs (which can carry a short-lived but real invitation code
    // and passcode). Must be set before listen().
    mServer.setSocketOptions(QLocalServer::UserAccessOption);

    if (!mServer.listen(kServerName) &&
        mServer.serverError() == QAbstractSocket::AddressInUseError) {
      // Holding the QLockFile already guarantees no other process is
      // genuinely primary, so an AddressInUseError here can only mean a
      // stale local-socket file left behind by a previous crash (the
      // process that owned it is gone, or QLockFile wouldn't have let us
      // acquire the lock) — safe to remove and retry without probing
      // first, unlike the old listen()-only design where AddressInUseError
      // was ambiguous between "stale file" and "live competing primary".
      QLocalServer::removeServer(kServerName);
      mServer.listen(kServerName);
    }

    if (!mServer.isListening()) {
      // Genuinely exceptional: the pipe/socket name is held by something
      // unrelated to this app's own lifecycle. We still hold the real
      // exclusivity lock, so this process is treated as primary for
      // isPrimaryInstance() purposes — it just won't be able to receive
      // URLs forwarded from later launches.
      qWarning() << "SingleInstanceGuard: failed to start local IPC server "
                    "despite holding the primary lock; URLs forwarded from "
                    "later launches will not be received:"
                 << mServer.errorString();
    }

    connect(&mServer, &QLocalServer::newConnection, this, [this]() {
      auto *socket = mServer.nextPendingConnection();
      connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
        const auto url = QString::fromUtf8(socket->readAll()).trimmed();
        emit urlReceivedFromSecondaryInstance(url);
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
  const QString payload = url.isEmpty() ? QString::fromLatin1(kActivateSentinel) : url;
  socket.write(payload.toUtf8());
  socket.waitForBytesWritten(1000);
  socket.disconnectFromServer();
}
