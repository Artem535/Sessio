#include "audio_bridge_server.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

#include <cstring>
#include <vector>

namespace pcm::webcall {

namespace {
constexpr uint64_t kMaxFrameBytes = 1u << 20;
// A handshake request is a few hundred bytes; anything bigger is not a browser.
constexpr qsizetype kMaxHandshakeBytes = 16 * 1024;
} // namespace

AudioBridgeServer::AudioBridgeServer(QString allowedOrigin, Sink sink, QObject *parent)
    : QObject(parent), allowedOrigin_(std::move(allowedOrigin)), sink_(std::move(sink)) {}

quint16 AudioBridgeServer::listen() {
  if (!server_) {
    server_ = new QTcpServer(this);
    connect(server_, &QTcpServer::newConnection, this, &AudioBridgeServer::onNewConnection);
  }
  if (!server_->isListening() && !server_->listen(QHostAddress::LocalHost, 0)) return 0;
  return server_->serverPort();
}

QString AudioBridgeServer::issueToken() {
  QByteArray raw(16, Qt::Uninitialized);
  QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(raw.data()), raw.size() / 4);
  const QString token = QString::fromLatin1(raw.toHex());
  tokens_.insert(token);
  return token;
}

void AudioBridgeServer::closeAll() {
  // abort() emits disconnected(), which mutates conns_.
  const QList<QTcpSocket *> socks = conns_.keys();
  for (QTcpSocket *sock : socks) sock->abort();
}

void AudioBridgeServer::onNewConnection() {
  while (QTcpSocket *sock = server_->nextPendingConnection()) {
    sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    conns_.insert(sock, {});
    connect(sock, &QTcpSocket::readyRead, this, [this, sock] { onReadyRead(sock); });
    connect(sock, &QTcpSocket::disconnected, this, [this, sock] {
      const auto it = conns_.find(sock);
      if (it == conns_.end()) return;
      const bool wasUpgraded = it->upgraded;
      conns_.erase(it);
      sock->deleteLater();
      if (wasUpgraded) emit clientDisconnected();
    });
  }
}

void AudioBridgeServer::onReadyRead(QTcpSocket *sock) {
  auto it = conns_.find(sock);
  if (it == conns_.end()) return;
  it->buf += sock->readAll();
  if (!it->upgraded && !handshake(sock, *it)) return;
  // parseFrames() may abort the socket, which erases the entry: do not touch
  // `it` afterwards.
  parseFrames(sock, *it);
}

bool AudioBridgeServer::handshake(QTcpSocket *sock, Conn &c) {
  const int end = c.buf.indexOf("\r\n\r\n");
  if (end < 0) {
    if (c.buf.size() > kMaxHandshakeBytes) sock->abort();
    return false;
  }
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
  const QString token = QUrlQuery(url).queryItemValue("token");
  // Validate everything before consuming the token, so a bad origin cannot
  // burn the page's token.
  const bool ok = !key.isEmpty() && QString::fromUtf8(origin) == allowedOrigin_ && tokens_.contains(token);
  if (!ok) {
    sock->write("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    sock->disconnectFromHost();
    return false;
  }
  tokens_.remove(token);
  const QByteArray accept =
      QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1).toBase64();
  sock->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
              "Sec-WebSocket-Accept: " + accept + "\r\n\r\n");
  c.upgraded = true;
  emit clientConnected();
  return true;
}

void AudioBridgeServer::deliver(const QByteArray &payload) {
  auto chunk = parseAudioChunk(payload);
  if (!chunk) {
    emit malformedFrame();
    return;
  }
  // The id length is arbitrary, so the PCM can start on an odd address; handing
  // that to the sink as int16_t* would be UB.
  std::vector<int16_t> aligned;
  if (!isSampleAligned(*chunk)) {
    aligned.resize(chunk->count);
    std::memcpy(aligned.data(), chunk->samples, chunk->count * sizeof(int16_t));
    chunk->samples = aligned.data();
  }
  if (sink_) sink_(*chunk);
}

void AudioBridgeServer::parseFrames(QTcpSocket *sock, Conn &c) {
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
    // Clients must mask; cap message size; fragmentation is not supported
    // (the page sends one chunk per message); control frames are <= 125 bytes.
    const bool fragmented = !fin || opcode == 0x0;
    const bool controlTooLong = opcode >= 0x8 && len > 125;
    if (!masked || len > kMaxFrameBytes || fragmented || controlTooLong) {
      sock->abort();
      return;
    }
    if (c.buf.size() < pos + 4 + qsizetype(len)) return;
    const uint8_t *mask = p + pos;
    pos += 4;
    QByteArray payload(qsizetype(len), Qt::Uninitialized);
    for (uint64_t i = 0; i < len; ++i) payload[qsizetype(i)] = char(p[pos + i] ^ mask[i % 4]);
    c.buf.remove(0, pos + qsizetype(len));

    if (opcode == 0x2) {
      deliver(payload);
    } else if (opcode == 0x8) {
      sock->write(QByteArray("\x88\x00", 2));
      sock->disconnectFromHost();
      return;
    } else if (opcode == 0x9) {
      QByteArray pong;
      pong.append(char(0x8a)).append(char(payload.size())).append(payload);
      sock->write(pong);
    } else if (opcode != 0xA) {
      emit malformedFrame();  // text and reserved opcodes
    }
  }
}

} // namespace pcm::webcall
