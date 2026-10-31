#pragma once

#include "audio_chunk.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>

class QTcpServer;
class QTcpSocket;

namespace pcm::webcall {

// Minimal RFC 6455 server that receives the page's per-participant PCM:
// 127.0.0.1 only, Origin check, one-shot tokens, client binary frames only
// (no fragmentation, no extensions). QtWebSockets is not available in the
// toolchain. Designed to live on its own QThread: listen() creates the
// QTcpServer, so it belongs to whichever thread runs it. The sink is called
// on that thread.
class AudioBridgeServer final : public QObject {
  Q_OBJECT
public:
  using Sink = std::function<void(const AudioChunk &)>;

  AudioBridgeServer(QString allowedOrigin, Sink sink, QObject *parent = nullptr);

  // Returns the bound port, or 0 on failure. Call on the owning thread.
  Q_INVOKABLE quint16 listen();
  // Every issued token stays valid until a connection uses it once.
  Q_INVOKABLE QString issueToken();
  // Aborts every connection (also used to simulate a dropped bridge).
  Q_INVOKABLE void closeAll();

signals:
  void clientConnected();
  void clientDisconnected();
  void malformedFrame();

private:
  struct Conn {
    QByteArray buf;
    bool upgraded = false;
  };
  void onNewConnection();
  void onReadyRead(QTcpSocket *sock);
  bool handshake(QTcpSocket *sock, Conn &c);
  void parseFrames(QTcpSocket *sock, Conn &c);
  void deliver(const QByteArray &payload);

  QString allowedOrigin_;
  Sink sink_;
  QTcpServer *server_ = nullptr;
  QSet<QString> tokens_;
  QHash<QTcpSocket *, Conn> conns_;
};

} // namespace pcm::webcall
