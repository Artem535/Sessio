#include "audio_bridge_server.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QTcpSocket>
#include <QThread>

#include <gtest/gtest.h>

#include <atomic>
#include <mutex>
#include <vector>

using pcm::webcall::AudioBridgeServer;
using pcm::webcall::AudioChunk;

namespace {

const QString kOrigin = QStringLiteral("sessio-call://app");

// gtest_main provides main(); the tests need an event loop for sockets.
class QtEnv : public ::testing::Environment {
public:
  void SetUp() override {
    static int argc = 1;
    static char arg0[] = "audio_bridge_server_tests";
    static char *argv[] = {arg0, nullptr};
    app_ = new QCoreApplication(argc, argv);
  }
  void TearDown() override { delete app_; }
private:
  QCoreApplication *app_ = nullptr;
};
const auto *const kEnv = ::testing::AddGlobalTestEnvironment(new QtEnv);

template <class Pred> bool waitFor(Pred pred, int ms = 3000) {
  QElapsedTimer t;
  t.start();
  while (!pred()) {
    if (t.elapsed() > ms) return false;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  return true;
}

struct Delivered {
  QString id;
  uint32_t seq;
  std::vector<int16_t> samples;
};

class AudioBridgeServerTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Members by reference, not `this`: the analyzer cannot prove `this` non-null here.
    server = new AudioBridgeServer(kOrigin, [&thread = thread, &mutex = mutex, &chunks = chunks](const AudioChunk &c) {
      EXPECT_EQ(QThread::currentThread(), &thread);  // sink runs on the server thread
      std::lock_guard lock(mutex);
      chunks.push_back({c.participantId, c.seq, std::vector<int16_t>(c.samples, c.samples + c.count)});
    });
    server->moveToThread(&thread);
    thread.start();
    QMetaObject::invokeMethod(server, "listen", Qt::BlockingQueuedConnection, qReturnArg(port));
    ASSERT_NE(port, 0);
    QObject::connect(server, &AudioBridgeServer::malformedFrame, server, [this] { ++malformed; },
                     Qt::DirectConnection);
  }
  void TearDown() override {
    for (auto *s : sockets) s->abort();
    qDeleteAll(sockets);
    thread.quit();
    thread.wait();
    delete server;  // owner thread has stopped
  }

  QString issueToken() {
    QString t;
    QMetaObject::invokeMethod(server, "issueToken", Qt::BlockingQueuedConnection, qReturnArg(t));
    return t;
  }
  size_t chunkCount() {
    std::lock_guard lock(mutex);
    return chunks.size();
  }

  // Opens a socket and sends the upgrade request; returns once the response headers arrived.
  QTcpSocket *open(const QString &token, const QString &origin, QByteArray *response = nullptr) {
    auto *s = new QTcpSocket;
    sockets.push_back(s);
    s->connectToHost("127.0.0.1", port);
    EXPECT_TRUE(s->waitForConnected(2000));
    const QByteArray req = "GET /?token=" + token.toUtf8() + " HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\n"
                           "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                           "Sec-WebSocket-Version: 13\r\nOrigin: " + origin.toUtf8() + "\r\n\r\n";
    s->write(req);
    QByteArray resp;
    waitFor([&] {
      resp += s->readAll();
      return resp.contains("\r\n\r\n") || s->state() == QAbstractSocket::UnconnectedState;
    });
    resp += s->readAll();
    if (response) *response = resp;
    return s;
  }
  QTcpSocket *openOk() {
    QByteArray r;
    auto *s = open(issueToken(), kOrigin, &r);
    EXPECT_TRUE(r.startsWith("HTTP/1.1 101"));
    return s;
  }

  static QByteArray maskedFrame(uint8_t firstByte, const QByteArray &payload) {
    const char mask[4] = {0x11, 0x22, 0x33, 0x44};
    QByteArray f;
    f.append(char(firstByte));
    if (payload.size() < 126) {
      f.append(char(0x80 | payload.size()));
    } else if (payload.size() < 65536) {
      f.append(char(0x80 | 126)).append(char(payload.size() >> 8)).append(char(payload.size() & 0xff));
    } else {
      f.append(char(0x80 | 127));
      const quint64 n = payload.size();
      for (int i = 7; i >= 0; --i) f.append(char((n >> (8 * i)) & 0xff));
    }
    f.append(mask, 4);
    for (int i = 0; i < payload.size(); ++i) f.append(char(payload[i] ^ mask[i % 4]));
    return f;
  }
  static QByteArray audioFrame(const QByteArray &id, const std::vector<int16_t> &pcm, uint32_t seq) {
    QByteArray p;
    p.append(char(1)).append(char(id.size())).append(2, '\0');
    p.append(reinterpret_cast<const char *>(&seq), 4);
    const double sent = 0;
    p.append(reinterpret_cast<const char *>(&sent), 8);
    p.append(id);
    p.append(reinterpret_cast<const char *>(pcm.data()), qsizetype(pcm.size() * 2));
    return p;
  }
  static bool closed(QTcpSocket *s) {
    return waitFor([&] {
      s->readAll();
      return s->state() == QAbstractSocket::UnconnectedState;
    });
  }

  QThread thread;
  AudioBridgeServer *server = nullptr;
  quint16 port = 0;
  std::mutex mutex;
  std::vector<Delivered> chunks;
  std::atomic<int> malformed{0};
  std::vector<QTcpSocket *> sockets;
};

} // namespace

TEST_F(AudioBridgeServerTest, AcceptsValidHandshake) {
  QByteArray resp;
  open(issueToken(), kOrigin, &resp);
  EXPECT_TRUE(resp.startsWith("HTTP/1.1 101"));
  // RFC 6455 section 1.3 sample key/accept pair.
  EXPECT_TRUE(resp.contains("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
}

TEST_F(AudioBridgeServerTest, RejectsWrongOrigin) {
  QByteArray resp;
  open(issueToken(), "https://evil.example", &resp);
  EXPECT_TRUE(resp.startsWith("HTTP/1.1 403"));
}

TEST_F(AudioBridgeServerTest, RejectsReusedToken) {
  const QString token = issueToken();
  QByteArray first, second;
  open(token, kOrigin, &first);
  open(token, kOrigin, &second);
  EXPECT_TRUE(first.startsWith("HTTP/1.1 101"));
  EXPECT_TRUE(second.startsWith("HTTP/1.1 403"));
}

TEST_F(AudioBridgeServerTest, RotatedTokenAcceptsReconnect) {
  const QString first = issueToken();
  const QString second = issueToken();  // the unused first token stays valid
  QByteArray a, b;
  auto *s1 = open(first, kOrigin, &a);
  EXPECT_TRUE(a.startsWith("HTTP/1.1 101"));
  s1->abort();  // dropped bridge
  open(second, kOrigin, &b);
  EXPECT_TRUE(b.startsWith("HTTP/1.1 101"));
}

TEST_F(AudioBridgeServerTest, DeliversChunkToSink) {
  QTcpSocket *s = openOk();
  std::vector<int16_t> pcm(320);
  for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = int16_t(i * 7 - 1000);
  const QByteArray id = QString::fromUtf8("p:Анна").toUtf8();  // 8 bytes, 16 + idLen even
  s->write(maskedFrame(0x82, audioFrame(id, pcm, 7)));
  // And an odd id length: PCM starts on an odd offset.
  s->write(maskedFrame(0x82, audioFrame("abc", pcm, 8)));
  ASSERT_TRUE(waitFor([&] { return chunkCount() == 2; }));
  std::lock_guard lock(mutex);
  EXPECT_EQ(chunks[0].id, QString::fromUtf8("p:Анна"));
  EXPECT_EQ(chunks[0].seq, 7u);
  EXPECT_EQ(chunks[0].samples, pcm);
  EXPECT_EQ(chunks[1].id, QStringLiteral("abc"));
  EXPECT_EQ(chunks[1].samples, pcm);
}

TEST_F(AudioBridgeServerTest, MalformedChunkIsCountedNotDelivered) {
  QTcpSocket *s = openOk();
  s->write(maskedFrame(0x82, QByteArray(10, '\1')));   // too short
  s->write(maskedFrame(0x81, "hello"));                // text frame
  ASSERT_TRUE(waitFor([&] { return malformed == 2; }));
  EXPECT_EQ(chunkCount(), 0u);
  EXPECT_EQ(s->state(), QAbstractSocket::ConnectedState);
}

TEST_F(AudioBridgeServerTest, OversizeFrameAborts) {
  QTcpSocket *s = openOk();
  // Header only: the server must not wait for 2 MiB of payload.
  QByteArray hdr;
  hdr.append(char(0x82)).append(char(0x80 | 127));
  const quint64 n = 2u << 20;
  for (int i = 7; i >= 0; --i) hdr.append(char((n >> (8 * i)) & 0xff));
  hdr.append(4, '\0');
  s->write(hdr);
  EXPECT_TRUE(closed(s));
}

TEST_F(AudioBridgeServerTest, FragmentedFrameAborts) {
  QTcpSocket *s = openOk();
  s->write(maskedFrame(0x02, QByteArray(20, '\0')));  // binary, FIN clear
  EXPECT_TRUE(closed(s));
  EXPECT_EQ(chunkCount(), 0u);
}

TEST_F(AudioBridgeServerTest, CloseAllAbortsConnections) {
  QTcpSocket *s = openOk();
  QMetaObject::invokeMethod(server, "closeAll", Qt::BlockingQueuedConnection);
  EXPECT_TRUE(closed(s));
}
