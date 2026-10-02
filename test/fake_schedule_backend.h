#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <functional>

// Scriptable HTTP/1.1 fake of the token-backend schedule routes. Unlike
// FakeTokenBackendServer it keeps every request, serves several connections,
// can delay, drop (lost ACK) or never answer, and supports a response script
// or a handler callback. Test double only: no TLS, no real authorization.
class FakeScheduleBackend final : public QObject {
  Q_OBJECT

public:
  struct Request {
    QString method;
    QString path;
    QByteArray body;
    QHash<QString, QString> headers; // lower-cased names
  };

  struct Response {
    int status = 200;
    QByteArray body = "{}";
    QList<QPair<QByteArray, QByteArray>> headers;
    int delayMs = 0;
    bool dropConnection = false; // closes without replying
    bool neverRespond = false;   // keeps the socket open and silent
  };

  explicit FakeScheduleBackend(QObject *parent = nullptr) : QObject(parent) {
    connect(&mServer, &QTcpServer::newConnection, this, &FakeScheduleBackend::onNewConnection);
    mServer.listen(QHostAddress(QStringLiteral("127.0.0.1")), 0);
  }

  [[nodiscard]] QString baseUrl() const {
    return QStringLiteral("http://127.0.0.1:%1").arg(mServer.serverPort());
  }

  // Responses are consumed in order; once exhausted `fallback` answers.
  void enqueue(const Response &response) { mScript.append(response); }
  void setFallback(const Response &response) { mFallback = response; }
  // When set, takes precedence over the script.
  void setHandler(std::function<Response(const Request &)> handler) { mHandler = std::move(handler); }

  [[nodiscard]] const QList<Request> &requests() const { return mRequests; }
  [[nodiscard]] int requestCount(const QString &method, const QString &pathPrefix) const {
    int count = 0;
    for (const auto &request : mRequests) {
      if (request.method == method && request.path.startsWith(pathPrefix)) {
        ++count;
      }
    }
    return count;
  }

private slots:
  void onNewConnection() {
    while (auto *socket = mServer.nextPendingConnection()) {
      auto *buffer = new QByteArray;
      connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer]() {
        buffer->append(socket->readAll());
        const auto headerEnd = buffer->indexOf("\r\n\r\n");
        if (headerEnd < 0) {
          return;
        }
        const auto lines = QString::fromLatin1(buffer->left(headerEnd)).split("\r\n");
        Request request;
        const auto requestLine = lines.first().split(' ');
        request.method = requestLine.value(0);
        request.path = requestLine.value(1);
        qsizetype contentLength = 0;
        for (const auto &line : lines.mid(1)) {
          const auto name = line.section(':', 0, 0).trimmed().toLower();
          const auto value = line.section(':', 1).trimmed();
          request.headers.insert(name, value);
          if (name == "content-length") {
            contentLength = value.toLongLong();
          }
        }
        const auto bodyStart = headerEnd + 4;
        if (buffer->size() - bodyStart < contentLength) {
          return;
        }
        request.body = buffer->mid(bodyStart, contentLength);
        buffer->clear();
        mRequests.append(request);
        respond(socket, request);
      });
      connect(socket, &QTcpSocket::disconnected, this, [socket, buffer]() {
        delete buffer;
        socket->deleteLater();
      });
    }
  }

private:
  void respond(QTcpSocket *socket, const Request &request) {
    Response response = mFallback;
    if (mHandler) {
      response = mHandler(request);
    } else if (!mScript.isEmpty()) {
      response = mScript.takeFirst();
    }
    if (response.neverRespond) {
      return;
    }
    const QPointer<QTcpSocket> guard(socket);
    QTimer::singleShot(response.delayMs, this, [guard, response]() {
      if (!guard) {
        return;
      }
      if (response.dropConnection) {
        guard->abort();
        return;
      }
      QByteArray bytes = "HTTP/1.1 " + QByteArray::number(response.status) + " X\r\n";
      bytes += "Content-Type: application/json\r\n";
      for (const auto &header : response.headers) {
        bytes += header.first + ": " + header.second + "\r\n";
      }
      bytes += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
      bytes += "Connection: close\r\n\r\n" + response.body;
      guard->write(bytes);
      guard->disconnectFromHost();
    });
  }

  QTcpServer mServer;
  QList<Request> mRequests;
  QList<Response> mScript;
  Response mFallback;
  std::function<Response(const Request &)> mHandler;
};
