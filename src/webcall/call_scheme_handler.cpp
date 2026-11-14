#include "call_scheme_handler.h"

#include <QBuffer>
#include <QFile>
#include <QHash>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>

namespace pcm::webcall {

void registerCallScheme() {
  QWebEngineUrlScheme scheme("sessio-call");
  scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
  scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::CorsEnabled |
                  QWebEngineUrlScheme::FetchApiAllowed | QWebEngineUrlScheme::LocalAccessAllowed);
  QWebEngineUrlScheme::registerScheme(scheme);
}

void CallSchemeHandler::requestStarted(QWebEngineUrlRequestJob *job) {
  const QString path = job->requestUrl().path();
  QString file;
  if (path == QLatin1String("/qwebchannel.js")) {
    file = QStringLiteral(":/qtwebchannel/qwebchannel.js");
  } else if (path.startsWith(QLatin1String("/jitsi-call/")) && !path.contains(QLatin1String(".."))) {
    file = QLatin1Char(':') + path;
  } else {
    job->fail(QWebEngineUrlRequestJob::UrlNotFound);
    return;
  }
  QFile f(file);
  if (!f.open(QIODevice::ReadOnly)) {
    job->fail(QWebEngineUrlRequestJob::UrlNotFound);
    return;
  }
  static const QHash<QString, QByteArray> kTypes{
      {"html", "text/html"}, {"js", "text/javascript"}, {"css", "text/css"}};
  auto *reply = new QBuffer(job);
  reply->setData(f.readAll());
  job->reply(kTypes.value(path.section('.', -1), "application/octet-stream"), reply);
}

} // namespace pcm::webcall
