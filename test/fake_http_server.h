#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

// A scripted HTTP/1.1 server for tests. It runs on the thread that creates it,
// so a test that blocks in an event loop (CloudEngine::generate) still serves.
class FakeHttpServer : public QTcpServer {
public:
  struct Response {
    int status = 200;
    QByteArray body;
    int delayMs = 0;
  };
  struct Request {
    QByteArray method, path, body;
    QHash<QByteArray, QByteArray> headers; // lower-case names
  };

  explicit FakeHttpServer(QObject *parent = nullptr) : QTcpServer(parent) {
    listen(QHostAddress::LocalHost, 0);
    connect(this, &QTcpServer::newConnection, this, [this] {
      while (auto *socket = nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      }
    });
  }
  QUrl url(const QString &path = {}) const {
    return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(path));
  }
  // Responses are served in order; the last one repeats.
  void enqueue(Response response) { mResponses.append(std::move(response)); }
  const QList<Request> &requests() const { return mRequests; }

private:
  void read(QTcpSocket *socket) {
    auto &buffer = mBuffers[socket];
    buffer += socket->readAll();
    const auto headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) return;
    const auto lines = buffer.left(headerEnd).split('\n');
    Request request;
    const auto first = lines.value(0).trimmed().split(' ');
    request.method = first.value(0);
    request.path = first.value(1);
    for (int i = 1; i < lines.size(); ++i) {
      const auto colon = lines[i].indexOf(':');
      if (colon > 0) request.headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
    }
    const auto length = request.headers.value("content-length").toInt();
    if (buffer.size() < headerEnd + 4 + length) return;
    request.body = buffer.mid(headerEnd + 4, length);
    mBuffers.remove(socket);
    mRequests.append(request);
    const auto index = qMin(mRequests.size(), mResponses.size()) - 1;
    const Response response = index >= 0 ? mResponses[index] : Response{500, {}, 0};
    QPointer<QTcpSocket> guard(socket);
    QTimer::singleShot(response.delayMs, this, [guard, response] {
      if (!guard) return;
      guard->write("HTTP/1.1 " + QByteArray::number(response.status) + " X\r\nContent-Type: application/json\r\n"
                   "Content-Length: " + QByteArray::number(response.body.size()) + "\r\nConnection: close\r\n\r\n" +
                   response.body);
      guard->disconnectFromHost();
    });
  }
  QList<Response> mResponses;
  QList<Request> mRequests;
  QHash<QTcpSocket *, QByteArray> mBuffers;
};
