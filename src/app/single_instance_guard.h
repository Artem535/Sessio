#pragma once

#include <QLocalServer>
#include <QLockFile>
#include <QObject>

// Cross-platform single-instance detection plus a tiny IPC channel for
// forwarding a sessio:// launch URL (or a plain "second launch happened, no
// URL" activation) from a secondary process to the primary one.
//
// Primacy is decided by an atomic, PID-aware QLockFile, not by whether
// QLocalServer::listen() succeeds: on Unix, listen() failing with
// AddressInUseError is a reliable "someone else is already listening"
// signal, but on Windows, QLocalServer uses named pipes, and Qt's own docs
// note platform differences that mean a second process's listen() call can
// also report success — which would let two processes both believe they are
// primary. QLockFile::tryLock() is atomic and consistent across platforms,
// and it already detects and recovers from a stale lock left behind by a
// process that crashed without releasing it, so it is used here as the sole
// source of truth for "am I primary"; QLocalServer is kept only as the IPC
// transport for the primary instance to receive forwarded URLs.
class SingleInstanceGuard final : public QObject {
  Q_OBJECT

public:
  explicit SingleInstanceGuard(QObject *parent = nullptr);

  [[nodiscard]] bool isPrimaryInstance() const { return mIsPrimary; }
  void forwardToPrimaryInstance(const QString &url);

signals:
  void urlReceivedFromSecondaryInstance(QString url);

private:
  static constexpr auto kServerName = "Sessio-single-instance";

  QLockFile mLockFile;
  QLocalServer mServer;
  bool mIsPrimary = false;
};
