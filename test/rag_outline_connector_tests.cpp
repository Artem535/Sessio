#include "outline_connector.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <functional>

namespace {

using sessio::rag::OutlineConfig;
using sessio::rag::OutlineConnector;
using sessio::rag::OutlineDocument;

class MockOutlineServer final : public QObject {
public:
  std::function<QByteArray(const QString &path, const QByteArray &body)> handler;

  bool start() {
    connect(&mServer, &QTcpServer::newConnection, this, &MockOutlineServer::onConnection);
    return mServer.listen(QHostAddress::LocalHost, 0);
  }
  QUrl baseUrl() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(mServer.serverPort())); }

private:
  void onConnection() {
    auto *socket = mServer.nextPendingConnection();
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::readyRead, this, [this, socket] { serve(socket); });
    if (socket->bytesAvailable() > 0) serve(socket);
  }

  void serve(QTcpSocket *socket) {
    if (socket->property("responded").toBool()) return;
    QByteArray request = socket->readAll();
    while (true) {
      const int headerEnd = request.indexOf("\r\n\r\n");
      if (headerEnd >= 0) {
        const QByteArray headers = request.left(headerEnd);
        int contentLength = 0;
        for (const auto &line : headers.split('\n')) {
          if (line.toLower().startsWith("content-length:"))
            contentLength = line.mid(15).trimmed().toInt();
        }
        if (request.size() >= headerEnd + 4 + contentLength) {
          const QByteArray body = request.mid(headerEnd + 4, contentLength);
          const QByteArray path = headers.left(headers.indexOf('\r')).split(' ').value(1);
          const QByteArray response = handler(QString::fromUtf8(path), body);
          socket->setProperty("responded", true);
          socket->write(response);
          socket->flush();
          socket->disconnectFromHost();
          return;
        }
      }
      // Qt6 returns false here when data is already buffered, so read first.
      if (!socket->waitForReadyRead(500)) break;
      request += socket->readAll();
    }
    socket->close();
  }

  QTcpServer mServer;
};

QByteArray httpResponse(int status, const QByteArray &body, const QByteArray &extraHeaders = {}) {
  QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + " OK\r\n";
  out += "Content-Type: application/json\r\n";
  out += extraHeaders;
  out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
  out += "Connection: close\r\n\r\n";
  out += body;
  return out;
}

QJsonObject doc(const QString &id, const QString &collection, const QString &text,
                int revision = 1, bool archived = false) {
  QJsonObject out;
  out["id"] = id;
  out["title"] = "Title " + id;
  out["url"] = "/doc/" + id;
  out["text"] = text;
  out["collectionId"] = collection;
  out["revision"] = revision;
  if (archived) out["archivedAt"] = "2026-01-01T00:00:00Z";
  return out;
}

QByteArray collectionsResponse() {
  QJsonArray data;
  data.append(QJsonObject{{"id", "c1"}, {"name", "Presale"}});
  data.append(QJsonObject{{"id", "c2"}, {"name", "Other"}});
  return QJsonDocument(QJsonObject{{"data", data}}).toJson(QJsonDocument::Compact);
}

QByteArray documentsResponse(const QJsonArray &data, int total) {
  return QJsonDocument(QJsonObject{{"data", data}, {"pagination", QJsonObject{{"total", total}}}})
      .toJson(QJsonDocument::Compact);
}

struct App {
  int argc = 1;
  char *argv[2] = {const_cast<char *>("test"), nullptr};
  QCoreApplication core{argc, argv};
};

OutlineConfig config(const QUrl &base, const QStringList &allowlist = {}) {
  OutlineConfig out;
  out.baseUrl = base;
  out.apiToken = QStringLiteral("secret-token-value");
  out.collectionAllowlist = allowlist;
  return out;
}

TEST(RagOutlineConnector, PaginatesAndSkipsEmptyAndArchivedDocuments) {
  App app;
  MockOutlineServer server;
  ASSERT_TRUE(server.start());
  server.handler = [](const QString &path, const QByteArray &body) {
    if (path.endsWith("collections.list")) return httpResponse(200, collectionsResponse());
    const int offset = QJsonDocument::fromJson(body).object().value("offset").toInt();
    if (offset == 0) {
      QJsonArray data;
      data.append(doc("d1", "c1", "alpha", 2));
      data.append(doc("d2", "c1", "   "));
      data.append(doc("d3", "c1", "archived", 1, true));
      return httpResponse(200, documentsResponse(data, 4));
    }
    QJsonArray data;
    data.append(doc("d4", "c1", "beta", 7));
    return httpResponse(200, documentsResponse(data, 4));
  };
  OutlineConnector connector;
  QSignalSpy finished(&connector, &OutlineConnector::finished);
  QSignalSpy failed(&connector, &OutlineConnector::failed);
  connector.fetch(config(server.baseUrl()));
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 5000);
  EXPECT_EQ(failed.count(), 0);
  const auto docs = finished.at(0).at(0).value<QVector<OutlineDocument>>();
  ASSERT_EQ(docs.size(), 2);
  EXPECT_EQ(docs[0].info.docId, "d1");
  EXPECT_EQ(docs[0].info.revision, "2");
  EXPECT_EQ(docs[0].info.collection, "Presale");
  EXPECT_EQ(docs[0].info.url, server.baseUrl().toString().toStdString() + "/doc/d1");
  EXPECT_EQ(docs[0].text.toStdString(), "alpha");
  EXPECT_EQ(docs[1].info.docId, "d4");
  EXPECT_EQ(docs[1].info.revision, "7");
}

TEST(RagOutlineConnector, AllowlistFiltersCollections) {
  App app;
  MockOutlineServer server;
  ASSERT_TRUE(server.start());
  server.handler = [](const QString &path, const QByteArray &) {
    if (path.endsWith("collections.list")) return httpResponse(200, collectionsResponse());
    QJsonArray data;
    data.append(doc("d1", "c1", "alpha"));
    data.append(doc("d2", "c2", "beta"));
    return httpResponse(200, documentsResponse(data, 2));
  };
  OutlineConnector connector;
  QSignalSpy finished(&connector, &OutlineConnector::finished);
  connector.fetch(config(server.baseUrl(), {"c1"}));
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 5000);
  const auto docs = finished.at(0).at(0).value<QVector<OutlineDocument>>();
  ASSERT_EQ(docs.size(), 1);
  EXPECT_EQ(docs[0].info.docId, "d1");
}

TEST(RagOutlineConnector, RetriesOn429HonoringRetryAfter) {
  App app;
  MockOutlineServer server;
  ASSERT_TRUE(server.start());
  int documentCalls = 0;
  server.handler = [&](const QString &path, const QByteArray &) {
    if (path.endsWith("collections.list")) return httpResponse(200, collectionsResponse());
    if (++documentCalls == 1) return httpResponse(429, QByteArray(), "Retry-After: 0\r\n");
    QJsonArray data;
    data.append(doc("d1", "c1", "alpha"));
    return httpResponse(200, documentsResponse(data, 1));
  };
  OutlineConnector connector;
  QSignalSpy finished(&connector, &OutlineConnector::finished);
  QSignalSpy failed(&connector, &OutlineConnector::failed);
  connector.fetch(config(server.baseUrl()));
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 5000);
  EXPECT_EQ(failed.count(), 0);
  EXPECT_EQ(documentCalls, 2);
  const auto docs = finished.at(0).at(0).value<QVector<OutlineDocument>>();
  ASSERT_EQ(docs.size(), 1);
  EXPECT_EQ(docs[0].info.docId, "d1");
}

TEST(RagOutlineConnector, FailureReasonDoesNotLeakToken) {
  App app;
  MockOutlineServer server;
  ASSERT_TRUE(server.start());
  server.handler = [](const QString &, const QByteArray &) { return httpResponse(401, QByteArray()); };
  OutlineConnector connector;
  QSignalSpy failed(&connector, &OutlineConnector::failed);
  connector.fetch(config(server.baseUrl()));
  QTRY_VERIFY_WITH_TIMEOUT(failed.count() == 1, 5000);
  const QString reason = failed.at(0).at(0).toString();
  EXPECT_TRUE(reason.contains("401")) << reason.toStdString();
  EXPECT_FALSE(reason.contains("secret-token-value"));
}

TEST(RagOutlineConnector, CancelStopsFetch) {
  App app;
  MockOutlineServer server;
  ASSERT_TRUE(server.start());
  server.handler = [](const QString &path, const QByteArray &) {
    if (path.endsWith("collections.list")) return httpResponse(200, collectionsResponse());
    QJsonArray data;
    data.append(doc("d1", "c1", "alpha"));
    return httpResponse(200, documentsResponse(data, 1));
  };
  OutlineConnector connector;
  QSignalSpy finished(&connector, &OutlineConnector::finished);
  QSignalSpy failed(&connector, &OutlineConnector::failed);
  connector.fetch(config(server.baseUrl()));
  connector.cancel();
  QTest::qWait(200);
  EXPECT_EQ(finished.count(), 0);
  EXPECT_EQ(failed.count(), 0);
}

} // namespace
