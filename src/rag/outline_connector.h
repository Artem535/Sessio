#pragma once

#include "chunk.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVector>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace sessio::rag {

struct OutlineConfig {
  QUrl baseUrl;
  QString apiToken;              // from settings; never logged or echoed in errors
  QStringList collectionAllowlist; // collection ids; empty allows every collection
};

struct OutlineDocument {
  DocumentInfo info; // url resolved absolute against baseUrl, revision as decimal string
  QString text;
};

// Read-only Outline wiki fetch: collections.list + documents.list with
// offset pagination, 429 retries honoring Retry-After. Skips archived and
// empty documents and collections outside the allowlist. One fetch at a
// time; cancel() aborts it. All work is async on the calling thread's event
// loop (the RagWorker thread owns the connector).
class OutlineConnector final : public QObject {
  Q_OBJECT
public:
  explicit OutlineConnector(QObject *parent = nullptr);
  void fetch(const OutlineConfig &config);
  void cancel();
signals:
  void finished(const QVector<OutlineDocument> &documents);
  void failed(const QString &reason);
private:
  void post(const QString &method, const QByteArray &payload, int generation,
            const std::function<void(const QJsonObject &)> &onSuccess);
  void fetchDocumentsPage(int generation);
  void fail(const QString &reason);

  QNetworkAccessManager *mNam = nullptr;
  QNetworkReply *mReply = nullptr;
  OutlineConfig mConfig;
  QHash<QString, QString> mCollections;
  QVector<OutlineDocument> mDocs;
  int mOffset = 0;
  int mGeneration = 0;
  int mRetries = 0;
  bool mBusy = false;
};

} // namespace sessio::rag
