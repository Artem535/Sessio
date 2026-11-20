#include "outline_connector.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace sessio::rag {

OutlineConnector::OutlineConnector(QObject *parent) : QObject(parent) {
  mNam = new QNetworkAccessManager(this);
}

void OutlineConnector::fetch(const OutlineConfig &config) {
  if (mBusy) return;
  mBusy = true;
  mConfig = config;
  mCollections.clear();
  mDocs.clear();
  mOffset = 0;
  mRetries = 0;
  const int generation = ++mGeneration;
  post(QStringLiteral("collections.list"), QByteArrayLiteral("{\"limit\":100,\"offset\":0}"), generation,
       [this, generation](const QJsonObject &response) {
    for (const auto &value : response.value(QStringLiteral("data")).toArray()) {
      const auto collection = value.toObject();
      mCollections.insert(collection.value(QStringLiteral("id")).toString(),
                          collection.value(QStringLiteral("name")).toString());
    }
    fetchDocumentsPage(generation);
  });
}

void OutlineConnector::cancel() {
  ++mGeneration;
  if (mReply) {
    mReply->abort();
    mReply = nullptr;
  }
  mBusy = false;
}

void OutlineConnector::fail(const QString &reason) {
  mBusy = false;
  mReply = nullptr;
  emit failed(reason);
}

void OutlineConnector::post(const QString &method, const QByteArray &payload, int generation,
                            const std::function<void(const QJsonObject &)> &onSuccess) {
  if (generation != mGeneration) return;
  QNetworkRequest request(mConfig.baseUrl.resolved(QUrl(QStringLiteral("api/") + method)));
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  request.setRawHeader(QByteArrayLiteral("Authorization"),
                       QStringLiteral("Bearer %1").arg(mConfig.apiToken).toUtf8());
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  auto *reply = mNam->post(request, payload);
  mReply = reply;
  connect(reply, &QNetworkReply::finished, this, [this, reply, generation, method, payload, onSuccess] {
    reply->deleteLater();
    if (mReply == reply) mReply = nullptr;
    if (generation != mGeneration) return;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 429) {
      const int retryAfter = reply->rawHeader(QByteArrayLiteral("Retry-After")).toInt();
      if (mRetries < 3) {
        ++mRetries;
        QTimer::singleShot(qMax(0, retryAfter) * 1000, this,
                           [this, method, payload, generation, onSuccess] {
          post(method, payload, generation, onSuccess);
        });
        return;
      }
    }
    if (reply->error() != QNetworkReply::NoError || status != 200) {
      fail(QStringLiteral("Outline HTTP %1").arg(status));
      return;
    }
    mRetries = 0;
    const auto document = QJsonDocument::fromJson(reply->readAll());
    if (!document.isObject()) {
      fail(QStringLiteral("Outline response is not a JSON object"));
      return;
    }
    onSuccess(document.object());
  });
}

void OutlineConnector::fetchDocumentsPage(int generation) {
  const QByteArray payload =
      QStringLiteral("{\"limit\":100,\"offset\":%1}").arg(mOffset).toUtf8();
  post(QStringLiteral("documents.list"), payload, generation, [this, generation](const QJsonObject &response) {
    const auto batch = response.value(QStringLiteral("data")).toArray();
    for (const auto &value : batch) {
      const auto doc = value.toObject();
      const auto archivedAt = doc.value(QStringLiteral("archivedAt"));
      if (!archivedAt.isNull() && !archivedAt.isUndefined()) continue;
      const QString text = doc.value(QStringLiteral("text")).toString();
      if (text.trimmed().isEmpty()) continue;
      const QString collectionId = doc.value(QStringLiteral("collectionId")).toString();
      if (!mConfig.collectionAllowlist.isEmpty() && !mConfig.collectionAllowlist.contains(collectionId))
        continue;
      OutlineDocument out;
      out.info.docId = doc.value(QStringLiteral("id")).toString().toStdString();
      out.info.title = doc.value(QStringLiteral("title")).toString().toStdString();
      out.info.url = mConfig.baseUrl.resolved(QUrl(doc.value(QStringLiteral("url")).toString()))
                         .toString()
                         .toStdString();
      out.info.collection = mCollections.value(collectionId).toStdString();
      out.info.revision = QString::number(doc.value(QStringLiteral("revision")).toInt()).toStdString();
      out.text = text;
      mDocs.append(std::move(out));
    }
    const int total = response.value(QStringLiteral("pagination")).toObject()
                          .value(QStringLiteral("total")).toInt();
    mOffset += batch.size();
    if (!batch.isEmpty() && mOffset < total) {
      fetchDocumentsPage(generation);
      return;
    }
    mBusy = false;
    emit finished(mDocs);
  });
}

} // namespace sessio::rag
