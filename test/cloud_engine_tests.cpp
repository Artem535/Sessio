#include "cloud_engine.h"
#include "fake_http_server.h"
#include "prompter_pipeline.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QtGlobal>
#include <gtest/gtest.h>
#include <thread>

using pcm::prompter::CloudConfig;
using pcm::prompter::PipelineError;

namespace {
QCoreApplication &app() {
  static int argc = 1;
  static char name[] = "cloud_engine_tests";
  static char *argv[] = {name, nullptr};
  static QCoreApplication instance(argc, argv);
  return instance;
}
struct Env : ::testing::Environment { void SetUp() override { app(); } };
const auto *const kEnv = ::testing::AddGlobalTestEnvironment(new Env);

QByteArray chat(const QByteArray &content, const QByteArray &finish = "stop") {
  return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{
             {"finish_reason", QString::fromUtf8(finish)},
             {"message", QJsonObject{{"role", "assistant"}, {"content", QString::fromUtf8(content)}}}}}}})
      .toJson(QJsonDocument::Compact);
}
const QByteArray kSchema = R"({"type":"object","properties":{"ok":{"type":"boolean"}},"required":["ok"],"additionalProperties":false})";

CloudConfig config(const FakeHttpServer &server, const QString &path = {}) {
  CloudConfig c;
  c.baseUrl = server.url(path).toString();
  c.model = "m1";
  c.apiKey = "k";
  c.timeoutMs = 2000;
  c.retryDelayMs = 20;
  return c;
}
std::string codeOf(const std::function<void()> &call) {
  try { call(); } catch (const PipelineError &e) { return e.what(); }
  return {};
}
} // namespace

TEST(CloudEngine, SendsStrictJsonSchemaRequest) {
  FakeHttpServer server;
  server.enqueue({200, chat(R"({"ok":true})")});
  auto engine = pcm::prompter::createCloudEngine(config(server, "/v1"));
  const auto value = engine->generate("SYS", "PAYLOAD", kSchema);
  EXPECT_TRUE(value.toObject()["ok"].toBool());
  ASSERT_EQ(server.requests().size(), 1);
  const auto &r = server.requests()[0];
  EXPECT_EQ(r.path, "/v1/chat/completions");
  EXPECT_EQ(r.headers.value("authorization"), "Bearer k");
  const auto body = QJsonDocument::fromJson(r.body).object();
  EXPECT_EQ(body["model"].toString(), "m1");
  EXPECT_EQ(body["messages"].toArray()[0].toObject()["role"].toString(), "system");
  EXPECT_EQ(body["messages"].toArray()[0].toObject()["content"].toString(), "SYS");
  EXPECT_EQ(body["messages"].toArray()[1].toObject()["content"].toString(), "PAYLOAD");
  const auto format = body["response_format"].toObject();
  EXPECT_EQ(format["type"].toString(), "json_schema");
  EXPECT_TRUE(format["json_schema"].toObject()["strict"].toBool());
  EXPECT_TRUE(r.body.contains(kSchema)); // verbatim, property order kept
  EXPECT_GT(body["max_tokens"].toInt(), 0);
}

TEST(CloudEngine, BaseUrlSlashNormalised) {
  FakeHttpServer server;
  server.enqueue({200, chat(R"({"ok":true})")});
  auto c = config(server, "/v1/");
  pcm::prompter::createCloudEngine(c)->generate("s", "p", kSchema);
  EXPECT_EQ(server.requests()[0].path, "/v1/chat/completions");
}

TEST(CloudEngine, ToleratesCodeFence) {
  FakeHttpServer server;
  server.enqueue({200, chat("```json\n{\"ok\":true}\n```")});
  EXPECT_TRUE(pcm::prompter::createCloudEngine(config(server))->generate("s", "p", kSchema).toObject()["ok"].toBool());
}

TEST(CloudEngine, Retries429ThenSucceeds) {
  FakeHttpServer server;
  server.enqueue({429, "{}"});
  server.enqueue({200, chat(R"({"ok":true})")});
  EXPECT_TRUE(pcm::prompter::createCloudEngine(config(server))->generate("s", "p", kSchema).toObject()["ok"].toBool());
  EXPECT_EQ(server.requests().size(), 2);
}

TEST(CloudEngine, Persistent5xxThrowsCloudServerAfterOneRetry) {
  FakeHttpServer server;
  server.enqueue({503, "{}"});
  auto engine = pcm::prompter::createCloudEngine(config(server));
  EXPECT_EQ(codeOf([&] { engine->generate("s", "p", kSchema); }), "cloud server");
  EXPECT_EQ(server.requests().size(), 2);
}

TEST(CloudEngine, Persistent429ThrowsRateLimit) {
  FakeHttpServer server;
  server.enqueue({429, "{}"});
  auto engine = pcm::prompter::createCloudEngine(config(server));
  EXPECT_EQ(codeOf([&] { engine->generate("s", "p", kSchema); }), "cloud rate limit");
}

TEST(CloudEngine, Http401ThrowsCloudAuthWithoutRetry) {
  FakeHttpServer server;
  server.enqueue({401, "{}"});
  auto engine = pcm::prompter::createCloudEngine(config(server));
  EXPECT_EQ(codeOf([&] { engine->generate("s", "p", kSchema); }), "cloud auth");
  EXPECT_EQ(server.requests().size(), 1);
}

TEST(CloudEngine, Http404ThrowsCloudModel) {
  FakeHttpServer server;
  server.enqueue({404, "{}"});
  auto engine = pcm::prompter::createCloudEngine(config(server));
  EXPECT_EQ(codeOf([&] { engine->generate("s", "p", kSchema); }), "cloud model");
}

TEST(CloudEngine, UnknownModelBodyThrowsCloudModel) {
  FakeHttpServer server;
  server.enqueue({400, R"({"error":{"message":"The model `m1` does not exist","code":"model_not_found"}})"});
  auto engine = pcm::prompter::createCloudEngine(config(server));
  EXPECT_EQ(codeOf([&] { engine->generate("s", "p", kSchema); }), "cloud model");
}

TEST(CloudEngine, UnsupportedResponseFormatThrowsCloudUnsupported) {
  FakeHttpServer server;
  server.enqueue({400, R"({"error":{"message":"response_format json_schema is not supported"}})"});
  auto engine = pcm::prompter::createCloudEngine(config(server));
  EXPECT_EQ(codeOf([&] { engine->generate("s", "p", kSchema); }), "cloud unsupported");
}

TEST(CloudEngine, FinishReasonLengthThrowsTruncated) {
  FakeHttpServer server;
  server.enqueue({200, chat(R"({"ok":tr)", "length")});
  auto engine = pcm::prompter::createCloudEngine(config(server));
  EXPECT_EQ(codeOf([&] { engine->generate("s", "p", kSchema); }), "output truncated");
}

TEST(CloudEngine, EmptyContentThrowsTruncated) {
  FakeHttpServer server;
  server.enqueue({200, chat("")});
  auto engine = pcm::prompter::createCloudEngine(config(server));
  EXPECT_EQ(codeOf([&] { engine->generate("s", "p", kSchema); }), "output truncated");
}

TEST(CloudEngine, TimeoutThrowsCloudTimeout) {
  FakeHttpServer server;
  server.enqueue({200, chat(R"({"ok":true})"), 1500});
  auto c = config(server);
  c.timeoutMs = 200;
  auto engine = pcm::prompter::createCloudEngine(c);
  EXPECT_EQ(codeOf([&] { engine->generate("s", "p", kSchema); }), "cloud timeout");
}

TEST(CloudEngine, CancelDuringRequestThrowsCancelled) {
  FakeHttpServer server;
  server.enqueue({200, chat(R"({"ok":true})"), 1500});
  auto engine = pcm::prompter::createCloudEngine(config(server));
  std::thread canceller([&] { QThread::msleep(100); engine->cancel(); });
  EXPECT_EQ(codeOf([&] { engine->generate("s", "p", kSchema); }), "cancelled");
  canceller.join();
}

TEST(CloudEngine, CancelDuringBackoffSendsNoSecondRequest) {
  FakeHttpServer server;
  server.enqueue({503, "{}"});
  auto c = config(server);
  c.retryDelayMs = 800;
  auto engine = pcm::prompter::createCloudEngine(c);
  std::thread canceller([&] { QThread::msleep(300); engine->cancel(); });
  EXPECT_EQ(codeOf([&] { engine->generate("s", "p", kSchema); }), "cancelled");
  canceller.join();
  EXPECT_EQ(server.requests().size(), 1);
}

TEST(CloudEngine, GenerateWorksAgainAfterCancel) {
  FakeHttpServer server;
  server.enqueue({200, chat(R"({"ok":true})")});
  auto engine = pcm::prompter::createCloudEngine(config(server));
  engine->cancel();
  EXPECT_TRUE(engine->generate("s", "p", kSchema).toObject()["ok"].toBool());
}

TEST(CloudEngine, ConnectionRefusedThrowsCloudNetworkAfterOneRetry) {
  quint16 port;
  { FakeHttpServer closed; port = closed.serverPort(); }
  CloudConfig c;
  c.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
  c.model = "m"; c.apiKey = "k"; c.timeoutMs = 1000; c.retryDelayMs = 20;
  auto engine = pcm::prompter::createCloudEngine(c);
  EXPECT_EQ(codeOf([&] { engine->generate("s", "p", kSchema); }), "cloud network");
}

TEST(CloudEngine, MissingKeyThrowsWithoutRequest) {
  FakeHttpServer server;
  server.enqueue({200, chat("{}")});
  auto c = config(server);
  c.apiKey.clear();
  auto engine = pcm::prompter::createCloudEngine(c);
  EXPECT_EQ(codeOf([&] { engine->generate("s", "p", kSchema); }), "cloud no key");
  EXPECT_EQ(server.requests().size(), 0);
}

TEST(CloudProbe, AcceptsConformingReply) {
  FakeHttpServer server;
  server.enqueue({200, chat(R"({"ok":true})")});
  const auto probe = pcm::prompter::probeCloud(config(server));
  EXPECT_TRUE(probe.ok) << probe.reason.toStdString();
}

TEST(CloudProbe, RejectsServerWithoutJsonSchema) {
  FakeHttpServer server;
  server.enqueue({400, R"({"error":{"message":"response_format type json_schema is not supported"}})"});
  const auto probe = pcm::prompter::probeCloud(config(server));
  EXPECT_FALSE(probe.ok);
  EXPECT_EQ(probe.reason, "unsupported");
}

TEST(CloudProbe, RejectsOffSchemaReply) {
  FakeHttpServer server;
  server.enqueue({200, chat(R"({"answer":"yes"})")});
  const auto probe = pcm::prompter::probeCloud(config(server));
  EXPECT_FALSE(probe.ok);
  EXPECT_EQ(probe.reason, "unsupported");
}

TEST(CloudEngine, LogsNeverContainPayloadOrKey) {
  static QStringList logged;
  logged.clear();
  const auto previous = qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &m) { logged << m; });
  FakeHttpServer server;
  server.enqueue({401, "{}"});
  auto c = config(server);
  c.apiKey = "SECRETKEY";
  auto engine = pcm::prompter::createCloudEngine(c);
  codeOf([&] { engine->generate("SYSTEMTEXT", "PAYLOADTEXT", kSchema); });
  qInstallMessageHandler(previous);
  for (const auto &line : logged) {
    EXPECT_FALSE(line.contains("SECRETKEY"));
    EXPECT_FALSE(line.contains("PAYLOADTEXT"));
    EXPECT_FALSE(line.contains("SYSTEMTEXT"));
  }
}
