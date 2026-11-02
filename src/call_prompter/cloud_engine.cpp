#include "cloud_engine.h"
#include "prompter_pipeline.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <atomic>

namespace pcm::prompter {
namespace {

constexpr qsizetype kMaxResponseBytes = 4 * 1024 * 1024;
constexpr int kPollMs = 20;

struct Attempt {
  int status = 0;  // 0: no HTTP response
  QByteArray body;
  bool timedOut = false;
  bool cancelled = false;
};

class CloudEngine final : public PrompterEngine {
public:
  explicit CloudEngine(CloudConfig config) : mConfig(std::move(config)) {
    while (mConfig.baseUrl.endsWith('/')) mConfig.baseUrl.chop(1);
  }

  QJsonValue generate(const QString &system, const QByteArray &payload, const QByteArray &schema) override {
    mCancel = false;
    if (mConfig.apiKey.isEmpty()) throw PipelineError("cloud no key");
    const auto body = requestBody(system, payload, schema);
    for (int attempt = 0;; ++attempt) {
      const auto result = post(body);
      if (result.cancelled || mCancel) throw PipelineError("cancelled");
      if (result.timedOut) throw PipelineError("cloud timeout");
      std::string failure;
      bool retryable = false;
      if (result.status == 200) return parseReply(result.body);
      if (result.status == 0) { failure = "cloud network"; retryable = true; }
      else if (result.status == 429) { failure = "cloud rate limit"; retryable = true; }
      else if (result.status >= 500) { failure = "cloud server"; retryable = true; }
      else failure = classify(result.status, result.body);
      if (!retryable || attempt >= 1) throw PipelineError(failure);
      if (!wait(mConfig.retryDelayMs)) throw PipelineError("cancelled");
    }
  }

  void cancel() override { mCancel = true; }

private:
  QByteArray requestBody(const QString &system, const QByteArray &payload, const QByteArray &schema) const {
    QJsonObject root{{"model", mConfig.model},
                     {"messages", QJsonArray{QJsonObject{{"role", "system"}, {"content", system}},
                                              QJsonObject{{"role", "user"}, {"content", QString::fromUtf8(payload)}}}},
                     {"temperature", 0},
                     {"max_tokens", mConfig.maxOutputTokens}};
    auto text = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (schema.isEmpty()) return text;
    if (!QJsonDocument::fromJson(schema).isObject()) throw PipelineError("cloud request");
    // The schema goes in verbatim: re-serialising through QJson would sort its
    // keys, and providers that decode in property order need reflect-cpp's.
    text.chop(1);
    return text + R"(,"response_format":{"type":"json_schema","json_schema":{"name":"reply","strict":true,"schema":)" +
           schema + "}}}";
  }

  Attempt post(const QByteArray &body) {
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(mConfig.baseUrl + QStringLiteral("/chat/completions"))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "Bearer " + mConfig.apiKey.toUtf8());
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    std::unique_ptr<QNetworkReply> reply(manager.post(request, body));
    QEventLoop loop;
    QTimer poll;
    QElapsedTimer elapsed;
    elapsed.start();
    Attempt result;
    QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
      if (mCancel) { result.cancelled = true; reply->abort(); }
      else if (elapsed.elapsed() > mConfig.timeoutMs) { result.timedOut = true; reply->abort(); }
    });
    poll.start(kPollMs);
    loop.exec();
    if (result.cancelled || result.timedOut) return result;
    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->read(kMaxResponseBytes + 1);
    if (result.body.size() > kMaxResponseBytes) throw PipelineError("output budget exceeded");
    return result;
  }

  // False when cancelled while waiting.
  bool wait(int ms) {
    QEventLoop loop;
    QTimer poll, done;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (mCancel) loop.quit(); });
    done.setSingleShot(true);
    QObject::connect(&done, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(kPollMs);
    done.start(ms);
    loop.exec();
    return !mCancel;
  }

  static std::string classify(int status, const QByteArray &body) {
    if (status == 401 || status == 403) return "cloud auth";
    if (status == 404) return "cloud model";
    const auto text = QString::fromUtf8(body).toLower();
    if (text.contains("response_format") || text.contains("json_schema") || text.contains("structured output"))
      return "cloud unsupported";
    if (text.contains("model") && (text.contains("not exist") || text.contains("not found") ||
                                   text.contains("unknown") || text.contains("invalid model")))
      return "cloud model";
    return "cloud request";
  }

  static QJsonValue parseReply(const QByteArray &body) {
    const auto document = QJsonDocument::fromJson(body);
    const auto choices = document.object()["choices"].toArray();
    if (choices.isEmpty()) throw PipelineError("cloud request");
    const auto choice = choices[0].toObject();
    if (choice["finish_reason"].toString() == QLatin1String("length")) throw PipelineError("output truncated");
    const auto content = choice["message"].toObject()["content"].toString();
    if (content.trimmed().isEmpty()) throw PipelineError("output truncated");
    return parseModelReply(content);
  }

  CloudConfig mConfig;
  std::atomic_bool mCancel = false;
};

} // namespace

std::unique_ptr<PrompterEngine> createCloudEngine(const CloudConfig &config) {
  return std::make_unique<CloudEngine>(config);
}

CloudProbe probeCloud(const CloudConfig &config) {
  static const QByteArray schema =
      R"({"type":"object","properties":{"ok":{"type":"boolean"}},"required":["ok"],"additionalProperties":false})";
  try {
    const auto reply = createCloudEngine(config)->generate(
        QStringLiteral("Reply with the JSON object {\"ok\": true} and nothing else."), R"({"ping":"ok"})", schema);
    if (reply.isObject() && reply.toObject().size() == 1 && reply.toObject()["ok"].isBool()) return {true, {}};
    return {false, QStringLiteral("unsupported")};
  } catch (const PipelineError &error) {
    const QString code = QString::fromLatin1(error.what());
    if (code.startsWith(QLatin1String("cloud "))) return {false, code.mid(6)};
    return {false, QStringLiteral("unsupported")};
  }
}

} // namespace pcm::prompter
