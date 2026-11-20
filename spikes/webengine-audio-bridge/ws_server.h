#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

class AudioStats;
class QTcpServer;
class QTcpSocket;

// Minimal RFC 6455 server for the spike: 127.0.0.1 only, one-shot token and
// Origin check, client binary frames only (no fragmentation, no extensions).
// Production would use QtWebSockets; it is not installed in this toolchain.
class WsServer final : public QObject {
  Q_OBJECT
public:
  WsServer(AudioStats *stats, QString token, QString origin, QObject *parent = nullptr);
  // Call on the thread that owns this object.
  Q_INVOKABLE quint16 listen();

private:
  struct Conn {
    QByteArray buf;
    bool upgraded = false;
  };
  void onNewConnection();
  void onReadyRead(QTcpSocket *sock);
  bool handshake(QTcpSocket *sock, Conn &c);
  void parseFrames(QTcpSocket *sock, Conn &c);

  AudioStats *stats_;
  QString token_;
  QString origin_;
  QTcpServer *server_ = nullptr;
  QHash<QTcpSocket *, Conn> conns_;
};
