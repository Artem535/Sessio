// Spike: which transport carries per-participant PCM from a QtWebEngine page
// to C++ best? See README.md for the matrix and findings.
#include "audio_stats.h"
#include "scheme_handler.h"
#include "ws_server.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEnginePermission>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlScheme>
#include <QWebEngineView>
#include <chrono>
#include <cstdio>
#include <thread>
#include <unistd.h>

namespace {

class ChannelBridge final : public QObject {
  Q_OBJECT
public:
  explicit ChannelBridge(AudioStats *stats) : stats_(stats) {}
public slots:
  void push(const QString &b64) { stats_->onChunk(QByteArray::fromBase64(b64.toLatin1())); }

private:
  AudioStats *stats_;
};

class SpikePage final : public QWebEnginePage {
public:
  SpikePage(QWebEngineProfile *profile, QObject *parent) : QWebEnginePage(profile, parent) {
    connect(this, &QWebEnginePage::permissionRequested, this,
            [](QWebEnginePermission permission) { permission.grant(); });
  }

protected:
  void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel, const QString &message, int line,
                                const QString &) override {
    std::fprintf(stderr, "js:%d %s\n", line, qPrintable(message));
  }
};

// utime+stime clock ticks of `pid` and every descendant (WebEngine's zygote,
// renderer, GPU and utility processes).
uint64_t treeTicks(pid_t root) {
  QHash<pid_t, pid_t> parent;
  QHash<pid_t, uint64_t> ticks;
  for (const QString &entry : QDir("/proc").entryList(QDir::Dirs)) {
    bool ok = false;
    const pid_t pid = entry.toInt(&ok);
    if (!ok) continue;
    QFile f("/proc/" + entry + "/stat");
    if (!f.open(QIODevice::ReadOnly)) continue;
    const QByteArray stat = f.readAll();
    const QList<QByteArray> fields = stat.mid(stat.lastIndexOf(')') + 2).split(' ');
    if (fields.size() < 13) continue;
    parent[pid] = fields[1].toInt();             // field 4: ppid
    ticks[pid] = fields[11].toULongLong() + fields[12].toULongLong();  // 14, 15
  }
  uint64_t sum = 0;
  for (auto it = ticks.cbegin(); it != ticks.cend(); ++it) {
    for (pid_t p = it.key(); p > 1; p = parent.value(p, 0)) {
      if (p == root) {
        sum += it.value();
        break;
      }
    }
  }
  return sum;
}

}  // namespace

int main(int argc, char **argv) {
  QWebEngineUrlScheme scheme("sessio-audio");
  scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
  scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::CorsEnabled |
                  QWebEngineUrlScheme::FetchApiAllowed | QWebEngineUrlScheme::LocalAccessAllowed);
  QWebEngineUrlScheme::registerScheme(scheme);

  QApplication app(argc, argv);
  QCommandLineParser cli;
  cli.addHelpOption();
  const QCommandLineOption transportOpt("transport", "channel | ws | scheme | none (CPU baseline)", "t", "channel");
  const QCommandLineOption modeOpt("mode", "synthetic | loopback | jitsi", "m", "loopback");
  const QCommandLineOption tracksOpt("tracks", "synthetic/loopback track count", "n", "3");
  const QCommandLineOption chunkOpt("chunk-ms", "PCM chunk length", "ms", "20");
  const QCommandLineOption secondsOpt("seconds", "run length", "s", "60");
  const QCommandLineOption warmupOpt("warmup", "seconds excluded from latency", "s", "3");
  const QCommandLineOption wsThreadOpt("ws-thread", "run the WebSocket server on its own thread");
  const QCommandLineOption jankOpt("gui-jank-ms", "block the GUI thread this long every 2 s", "ms", "0");
  const QCommandLineOption domainOpt("jitsi-domain", "Jitsi server", "host", "meet.jit.si");
  const QCommandLineOption roomOpt("room", "Jitsi room", "name", "");
  const QCommandLineOption jwtOpt("jwt-file", "file with a Jitsi JWT", "path", "");
  const QCommandLineOption micOpt("mic", "also tap the local microphone as 'local'");
  const QCommandLineOption showOpt("show", "show the page window");
  cli.addOptions({transportOpt, modeOpt, tracksOpt, chunkOpt, secondsOpt, warmupOpt, wsThreadOpt, jankOpt,
                  domainOpt, roomOpt, jwtOpt, micOpt, showOpt});
  // Unknown options are Chromium flags (e.g. --host-resolver-rules=...), which
  // QtWebEngine reads from argv itself.
  cli.parse(app.arguments());
  if (cli.isSet("help")) cli.showHelp();

  const QString transport = cli.value(transportOpt);
  const int seconds = cli.value(secondsOpt).toInt();
  AudioStats stats(cli.value(warmupOpt).toDouble() * 1000.0);

  auto *profile = new QWebEngineProfile(&app);  // off-the-record
  auto *handler = new SchemeHandler(&stats, &app);
  profile->installUrlSchemeHandler("sessio-audio", handler);

  QWebEngineView view;
  auto *page = new SpikePage(profile, &view);
  page->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
  view.setPage(page);

  QUrlQuery query;
  query.addQueryItem("transport", transport);
  query.addQueryItem("mode", cli.value(modeOpt));
  query.addQueryItem("tracks", cli.value(tracksOpt));
  query.addQueryItem("chunkMs", cli.value(chunkOpt));
  query.addQueryItem("domain", cli.value(domainOpt));
  query.addQueryItem("room", cli.value(roomOpt));
  query.addQueryItem("mic", cli.isSet(micOpt) ? "1" : "0");
  if (const QString path = cli.value(jwtOpt); !path.isEmpty()) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
      std::fprintf(stderr, "cannot read %s\n", qPrintable(path));
      return 1;
    }
    query.addQueryItem("jwt", QString::fromLatin1(f.readAll().trimmed()));
  }

  QWebChannel channel;
  ChannelBridge bridge(&stats);
  QThread wsThread;
  WsServer *ws = nullptr;
  if (transport == "channel") {
    channel.registerObject("bridge", &bridge);
    page->setWebChannel(&channel);
  } else if (transport == "ws") {
    QByteArray raw(16, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(raw.data()), 4);
    const QString token = QString::fromLatin1(raw.toHex());
    ws = new WsServer(&stats, token, "sessio-audio://app");
    quint16 port = 0;
    if (cli.isSet(wsThreadOpt)) {
      ws->moveToThread(&wsThread);
      QObject::connect(&wsThread, &QThread::finished, ws, &QObject::deleteLater);
      wsThread.start();
      QMetaObject::invokeMethod(ws, &WsServer::listen, Qt::BlockingQueuedConnection, &port);
    } else {
      ws->setParent(&app);
      port = ws->listen();
    }
    if (port == 0) {
      std::fprintf(stderr, "ws: listen failed\n");
      return 1;
    }
    query.addQueryItem("wsPort", QString::number(port));
    query.addQueryItem("token", token);
  } else if (transport != "scheme" && transport != "none") {
    std::fprintf(stderr, "unknown transport %s\n", qPrintable(transport));
    return 1;
  }

  QTimer jank;
  if (const int ms = cli.value(jankOpt).toInt(); ms > 0) {
    QObject::connect(&jank, &QTimer::timeout, [ms] { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); });
    jank.start(2000);
  }

  QUrl url("sessio-audio://app/index.html");
  url.setQuery(query);
  page->load(url);
  if (cli.isSet(showOpt)) view.show();

  const pid_t self = getpid();
  const long hz = sysconf(_SC_CLK_TCK);
  uint64_t startTicks = 0;
  double startMs = 0;
  // Measure CPU after page load so model/renderer start-up is excluded.
  QTimer::singleShot(cli.value(warmupOpt).toInt() * 1000, [&] {
    startTicks = treeTicks(self);
    startMs = nowEpochMs();
  });

  QTimer::singleShot(seconds * 1000, [&] {
    const double cpuPercent = 100.0 * double(treeTicks(self) - startTicks) / hz / ((nowEpochMs() - startMs) / 1000.0);
    page->runJavaScript("JSON.stringify(window.spikeProduced ? window.spikeProduced() : {})",
                        [&, cpuPercent](const QVariant &v) {
                          const QJsonObject produced = QJsonDocument::fromJson(v.toString().toUtf8()).object();
                          QJsonObject out = stats.report(produced.value("tracks").toObject());
                          out.insert("transport", transport + (cli.isSet(wsThreadOpt) ? "-thread" : ""));
                          out.insert("mode", cli.value(modeOpt));
                          out.insert("chunkMs", cli.value(chunkOpt).toInt());
                          out.insert("guiJankMs", cli.value(jankOpt).toInt());
                          out.insert("seconds", seconds);
                          out.insert("cpuPercentTree", cpuPercent);
                          out.insert("page", produced.value("page"));
                          std::printf("%s\n", QJsonDocument(out).toJson(QJsonDocument::Compact).constData());
                          std::fflush(stdout);
                          app.quit();
                        });
  });

  const int rc = app.exec();
  wsThread.quit();
  wsThread.wait();
  return rc;
}

#include "main.moc"
