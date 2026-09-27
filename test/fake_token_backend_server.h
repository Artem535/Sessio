#pragma once

#include <QByteArray>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

// A minimal single-connection-at-a-time HTTP/1.1 server for testing
// TokenBackendClient without a real token-backend process. Records the last
// request's method/path/headers/body and replies with a pre-configured
// status+body to the next connection.
class FakeTokenBackendServer final : public QObject {
  Q_OBJECT

public:
  explicit FakeTokenBackendServer(QObject *parent = nullptr) : QObject(parent) {
    connect(&mServer, &QTcpServer::newConnection, this, &FakeTokenBackendServer::onNewConnection);
    mServer.listen(QHostAddress(QStringLiteral("127.0.0.1")), 0);
  }

  [[nodiscard]] QUrl baseUrl() const {
    return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(mServer.serverPort()));
  }

  void setNextResponse(int statusCode, const QByteArray &jsonBody) {
    mStatusCode = statusCode;
    mBody = jsonBody;
  }

  QString lastPath;
  QString lastMethod;
  QString lastAuthorizationHeader;
  QByteArray lastBody;

private slots:
  void onNewConnection() {
    auto *socket = mServer.nextPendingConnection();
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
      mBuffer.append(socket->readAll());
      const auto headerEnd = mBuffer.indexOf("\r\n\r\n");
      if (headerEnd < 0) {
        return;
      }
      const auto header = QString::fromLatin1(mBuffer.left(headerEnd));
      const auto lines = header.split("\r\n");
      const auto requestLine = lines.first().split(' ');
      lastMethod = requestLine.value(0);
      lastPath = requestLine.value(1);
      for (const auto &line : lines) {
        if (line.startsWith("Authorization:", Qt::CaseInsensitive)) {
          lastAuthorizationHeader = line.section(':', 1).trimmed();
        }
      }
      const auto bodyStart = headerEnd + 4;
      if (mBuffer.size() < bodyStart) {
        return;
      }
      lastBody = mBuffer.mid(bodyStart);

      const QByteArray statusLine =
          mStatusCode == 200 ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 " + QByteArray::number(mStatusCode) + " Error\r\n";
      QByteArray response = statusLine;
      response += "Content-Type: application/json\r\n";
      response += "Content-Length: " + QByteArray::number(mBody.size()) + "\r\n";
      response += "Connection: close\r\n\r\n";
      response += mBody;
      socket->write(response);
      socket->disconnectFromHost();
      mBuffer.clear();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
  }

private:
  QTcpServer mServer;
  QByteArray mBuffer;
  int mStatusCode = 200;
  QByteArray mBody = "{}";
};
