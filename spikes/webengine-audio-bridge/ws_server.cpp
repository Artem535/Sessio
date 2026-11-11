#include "ws_server.h"

#include "audio_stats.h"

#include <QCryptographicHash>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <cstdio>

WsServer::WsServer(AudioStats *stats, QString token, QString origin, QObject *parent)
    : QObject(parent), stats_(stats), token_(std::move(token)), origin_(std::move(origin)) {}

quint16 WsServer::listen() {
  server_ = new QTcpServer(this);
  connect(server_, &QTcpServer::newConnection, this, &WsServer::onNewConnection);
  if (!server_->listen(QHostAddress::LocalHost, 0)) return 0;
  return server_->serverPort();
}

void WsServer::onNewConnection() {
  while (QTcpSocket *sock = server_->nextPendingConnection()) {
    sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    conns_.insert(sock, {});
    connect(sock, &QTcpSocket::readyRead, this, [this, sock] { onReadyRead(sock); });
    connect(sock, &QTcpSocket::disconnected, this, [this, sock] {
      conns_.remove(sock);
      sock->deleteLater();
    });
  }
}

void WsServer::onReadyRead(QTcpSocket *sock) {
  auto it = conns_.find(sock);
  if (it == conns_.end()) return;
  it->buf += sock->readAll();
  if (!it->upgraded && !handshake(sock, *it)) return;
  if (it->upgraded) parseFrames(sock, *it);
}

bool WsServer::handshake(QTcpSocket *sock, Conn &c) {
  const int end = c.buf.indexOf("\r\n\r\n");
  if (end < 0) return false;
  const QList<QByteArray> lines = c.buf.left(end).split('\n');
  c.buf.remove(0, end + 4);

  const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
  const QUrl url(QString::fromLatin1(requestLine.value(1)));
  QByteArray key, origin;
  for (const QByteArray &raw : lines.mid(1)) {
    const int colon = raw.indexOf(':');
    if (colon < 0) continue;
    const QByteArray name = raw.left(colon).trimmed().toLower();
    const QByteArray value = raw.mid(colon + 1).trimmed();
    if (name == "sec-websocket-key") key = value;
    else if (name == "origin") origin = value;
  }
  const bool tokenOk = QUrlQuery(url).queryItemValue("token") == token_;
  // The token is single-use: a second page cannot reuse it.
  if (key.isEmpty() || !tokenOk || QString::fromUtf8(origin) != origin_) {
    std::fprintf(stderr, "ws: rejected handshake (origin=%s tokenOk=%d)\n", origin.constData(), tokenOk);
    sock->write("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
    sock->disconnectFromHost();
    return false;
  }
  token_.clear();
  const QByteArray accept =
      QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1).toBase64();
  sock->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
              "Sec-WebSocket-Accept: " + accept + "\r\n\r\n");
  c.upgraded = true;
  return true;
}

void WsServer::parseFrames(QTcpSocket *sock, Conn &c) {
  for (;;) {
    if (c.buf.size() < 2) return;
    const auto *p = reinterpret_cast<const uint8_t *>(c.buf.constData());
    const bool fin = p[0] & 0x80;
    const int opcode = p[0] & 0x0f;
    const bool masked = p[1] & 0x80;
    uint64_t len = p[1] & 0x7f;
    qsizetype pos = 2;
    if (len == 126) {
      if (c.buf.size() < 4) return;
      len = (uint64_t(p[2]) << 8) | p[3];
      pos = 4;
    } else if (len == 127) {
      if (c.buf.size() < 10) return;
      len = 0;
      for (int i = 0; i < 8; ++i) len = (len << 8) | p[2 + i];
      pos = 10;
    }
    if (!masked || len > (1u << 20)) {  // clients must mask; cap message size
      sock->abort();
      return;
    }
    if (c.buf.size() < pos + 4 + qsizetype(len)) return;
    const uint8_t *mask = p + pos;
    pos += 4;
    QByteArray payload(qsizetype(len), Qt::Uninitialized);
    for (uint64_t i = 0; i < len; ++i) payload[qsizetype(i)] = char(p[pos + i] ^ mask[i % 4]);
    c.buf.remove(0, pos + qsizetype(len));

    if (opcode == 0x2 && fin) {
      stats_->onChunk(payload);
    } else if (opcode == 0x8) {
      sock->write(QByteArray("\x88\x00", 2));
      sock->disconnectFromHost();
      return;
    } else if (opcode == 0x9) {
      QByteArray pong;
      pong.append(char(0x8a)).append(char(payload.size())).append(payload);
      sock->write(pong);
    } else {
      stats_->countMalformed();
    }
  }
}
