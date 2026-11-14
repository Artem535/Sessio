#include "scheme_handler.h"

#include "audio_stats.h"

#include <QBuffer>
#include <QFile>
#include <QWebEngineUrlRequestJob>

void SchemeHandler::requestStarted(QWebEngineUrlRequestJob *job) {
  const QString path = job->requestUrl().path();
  if (job->requestMethod() == "POST" && path == "/push") {
    if (job->initiator() != QUrl("sessio-audio://app")) {
      job->fail(QWebEngineUrlRequestJob::RequestDenied);
      return;
    }
    QIODevice *body = job->requestBody();
    if (body && !body->isOpen()) body->open(QIODevice::ReadOnly);
    stats_->onChunk(body ? body->readAll() : QByteArray());
    auto *reply = new QBuffer(job);
    reply->setData(QByteArray());
    job->reply("application/octet-stream", reply);
    return;
  }

  static const QHash<QString, QByteArray> kTypes{
      {"html", "text/html"}, {"js", "text/javascript"}};
  const QString file = path == "/qwebchannel.js" ? QStringLiteral(":/qtwebchannel/qwebchannel.js")
                                                 : QStringLiteral(":/web") + path;
  QFile f(file);
  if (!f.open(QIODevice::ReadOnly)) {
    job->fail(QWebEngineUrlRequestJob::UrlNotFound);
    return;
  }
  auto *reply = new QBuffer(job);
  reply->setData(f.readAll());
  job->reply(kTypes.value(path.section('.', -1), "application/octet-stream"), reply);
}
