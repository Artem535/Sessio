#include "web_call_provider.h"

#include "audio_bridge_server.h"
#include "call_scheme_handler.h"
#include "web_call_bridge.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEnginePermission>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <mutex>

Q_LOGGING_CATEGORY(lcCallPage, "sessio.webcall.page")

namespace pcm::webcall {

namespace {

const QString kOrigin = QStringLiteral("sessio-call://app");
constexpr int kSampleRate = 16000;
constexpr int kServerStopTimeoutMs = 3000;
constexpr int kLeaveFallbackMs = 3000;

class CallPage final : public QWebEnginePage {
public:
  using QWebEnginePage::QWebEnginePage;

protected:
  void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel, const QString &message, int line,
                                const QString &) override {
    qCDebug(lcCallPage) << line << message;
  }
};

bool isOurOrigin(const QUrl &origin) {
  return origin.scheme() == QLatin1String("sessio-call") && origin.host() == QLatin1String("app");
}

QString toJson(const QJsonObject &o) {
  return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

} // namespace

// Shared with the server thread's sink lambda, so it outlives the provider if
// the thread ever fails to stop in time.
struct WebCallProvider::SinkState {
  // Held around every onAudio call: setAudioSink(nullptr) takes it, which is
  // what guarantees no call is in flight (or starts) once it returns.
  std::mutex callMutex;
  pcm::video::AudioSinkSlot slot;
};

WebCallProvider::WebCallProvider(Options options, QObject *parent)
    : VideoProvider(parent), mOptions(std::move(options)), mSink(std::make_shared<SinkState>()) {
  mProfile = new QWebEngineProfile; // off the record: no cookies/cache on disk
  mProfile->installUrlSchemeHandler("sessio-call", new CallSchemeHandler(mProfile));

  mPage = new CallPage(mProfile);
  mPage->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
  connect(mPage, &QWebEnginePage::permissionRequested, this, [](QWebEnginePermission permission) {
    using Type = QWebEnginePermission::PermissionType;
    const Type type = permission.permissionType();
    const bool media = type == Type::MediaAudioCapture || type == Type::MediaVideoCapture ||
                       type == Type::MediaAudioVideoCapture;
    if (media && isOurOrigin(permission.origin())) {
      permission.grant();
    } else {
      permission.deny();
    }
  });
  connect(mPage, &QWebEnginePage::loadFinished, this, &WebCallProvider::onLoadFinished);
  connect(mPage, &QWebEnginePage::renderProcessTerminated, this,
          [this](QWebEnginePage::RenderProcessTerminationStatus, int) { onRenderProcessTerminated(); });

  mBridge = new WebCallBridge(participants(), this);
  mChannel = new QWebChannel(this);
  mChannel->registerObject(QStringLiteral("bridge"), mBridge);
  mPage->setWebChannel(mChannel);

  connect(mBridge, &WebCallBridge::joinedCall, this, [this] {
    if (mDead) return;
    mElapsed.start();
    mConnected = true;
    emit joined();
  });
  connect(mBridge, &WebCallBridge::joinFailedWith, this, [this](const QString &reason) {
    if (mDead) return;
    mJoinRequested = false;
    emit joinFailed(reason);
  });
  connect(mBridge, &WebCallBridge::leftCall, this, &WebCallProvider::onPageLeft);
  connect(mBridge, &WebCallBridge::connectionLostWith, this, [this](const QString &reason) {
    if (mDead) return;
    mConnected = false;
    mJoinRequested = false;
    emit connectionLost(reason);
  });
  connect(mBridge, &WebCallBridge::participantAdded, this, [this](const QString &id) {
    if (!mDead) emit participantJoined(id);
  });
  connect(mBridge, &WebCallBridge::participantRemoved, this, [this](const QString &id) {
    if (!mDead) emit participantLeft(id);
  });
  connect(mBridge, &WebCallBridge::mediaErrorWith, this, [this](const QString &reason) {
    if (!mDead) emit mediaError(reason);
  });
  connect(mBridge, &WebCallBridge::audioBridgeDropped, this, &WebCallProvider::onAudioBridgeLost);

  mView = new QWebEngineView;
  mView->setPage(mPage);
}

WebCallProvider::~WebCallProvider() {
  detachSink();
  stopServer();
  // The page must go before its profile; the view may have been reparented by
  // the host, deleting it here also removes it from there.
  delete mView.data();
  delete mPage;
  delete mProfile;
}

QWidget *WebCallProvider::callView() { return mView.data(); }

qint64 WebCallProvider::callElapsedMs() const { return mConnected ? mElapsed.elapsed() : 0; }

void WebCallProvider::join(const QString &url, const QString &token) {
  if (mDead) {
    QTimer::singleShot(0, this, [this] { emit joinFailed(tr("The call page stopped unexpectedly")); });
    return;
  }
  if (mJoinRequested) {
    qWarning() << "WebCallProvider::join ignored: already joining or joined";
    return;
  }

  // The backend may send JITSI_URL with a trailing slash.
  QString trimmed = url;
  while (trimmed.endsWith(QLatin1Char('/'))) trimmed.chop(1);
  const qsizetype slash = trimmed.lastIndexOf(QLatin1Char('/'));
  const qsizetype schemeEnd = trimmed.indexOf(QLatin1String("://"));
  const bool parsed = slash > 0 && (schemeEnd < 0 || slash > schemeEnd + 2) && slash < trimmed.size() - 1;
  const bool fake = mOptions.adapter == QLatin1String("fake");
  if (!parsed && !fake) {
    QTimer::singleShot(0, this, [this] { emit joinFailed(tr("Invalid call address")); });
    return;
  }

  if (!ensureServer()) {
    QTimer::singleShot(0, this, [this] { emit joinFailed(tr("Could not start the local audio bridge")); });
    return;
  }
  QString wsToken;
  QMetaObject::invokeMethod(mServer, "issueToken", Qt::BlockingQueuedConnection, qReturnArg(wsToken));

  QJsonObject opts{
      {"serverUrl", parsed ? trimmed.left(slash) : QString()},
      {"room", parsed ? trimmed.mid(slash + 1) : QStringLiteral("fake")},
      {"jwt", token},
      {"displayName", mOptions.displayName},
      {"wsPort", int(mWsPort)},
      {"wsToken", wsToken},
      {"adapter", mOptions.adapter},
      {"startWithMicrophone", mMicEnabled},
      {"startWithCamera", mCamEnabled},
  };
  if (fake) {
    opts["fake"] = QJsonObject{{"participants", mOptions.fakeParticipants}, {"churnMs", mOptions.fakeChurnMs}};
  }

  mJoinRequested = true;
  if (mSink->slot.hasSink()) callPage(QStringLiteral("setAudioCapture"), QStringLiteral("true"));
  callPage(QStringLiteral("join"), toJson(opts));
  if (!mPage->url().isValid() || mPage->url().isEmpty() || mPage->url().scheme() != QLatin1String("sessio-call")) {
    mPage->load(QUrl(QStringLiteral("sessio-call://app/jitsi-call/index.html")));
  }
}

void WebCallProvider::leave() {
  if (mLeaving) return;
  stopServer();
  if (!mJoinRequested || mDead || !mLoaded) {
    mPendingJs.clear();
    mJoinRequested = false;
    mConnected = false;
    participants()->clear();
    QTimer::singleShot(0, this, [this] { emit left(); });
    return;
  }
  mLeaving = true;
  callPage(QStringLiteral("leave"));
  // The page confirms with left(); don't hang the caller if it never does.
  QTimer::singleShot(kLeaveFallbackMs, this, [this] { onPageLeft(); });
}

void WebCallProvider::onPageLeft() {
  if (!mLeaving) return;
  mLeaving = false;
  mJoinRequested = false;
  mConnected = false;
  participants()->clear();
  emit left();
}

void WebCallProvider::setMicrophoneEnabled(bool enabled) {
  mMicEnabled = enabled;
  if (!mDead) callPage(QStringLiteral("setMicrophoneEnabled"), enabled ? QStringLiteral("true") : QStringLiteral("false"));
}

void WebCallProvider::setCameraEnabled(bool enabled) {
  mCamEnabled = enabled;
  if (!mDead) callPage(QStringLiteral("setCameraEnabled"), enabled ? QStringLiteral("true") : QStringLiteral("false"));
}

void WebCallProvider::setAudioSink(std::shared_ptr<pcm::video::AudioSink> sink) {
  if (mDead && sink) return;
  {
    std::lock_guard lock(mSink->callMutex);
    mSink->slot.set(std::move(sink));
  }
  if (!mDead) {
    callPage(QStringLiteral("setAudioCapture"), mSink->slot.hasSink() ? QStringLiteral("true") : QStringLiteral("false"));
  }
}

void WebCallProvider::detachSink() {
  std::lock_guard lock(mSink->callMutex);
  mSink->slot.set(nullptr);
}

void WebCallProvider::onLoadFinished(bool ok) {
  if (!ok) {
    mPendingJs.clear();
    if (mJoinRequested && !mDead) {
      mJoinRequested = false;
      emit joinFailed(tr("The call page failed to load"));
    }
    return;
  }
  mLoaded = true;
  const QStringList pending = std::exchange(mPendingJs, {});
  for (const QString &js : pending) mPage->runJavaScript(js);
}

void WebCallProvider::onRenderProcessTerminated() {
  if (mDead) return;
  mDead = true;
  mConnected = false;
  detachSink();
  stopServer();
  mPendingJs.clear();
  emit connectionLost(tr("The call page stopped unexpectedly"));
}

void WebCallProvider::simulateRenderProcessTerminated() { onRenderProcessTerminated(); }

void WebCallProvider::simulateAudioBridgeDrop() {
  if (mServer) QMetaObject::invokeMethod(mServer, "closeAll", Qt::QueuedConnection);
}

void WebCallProvider::onAudioBridgeLost() {
  if (mDead || mLeaving || !mServer || !mJoinRequested) return;
  // issueToken() lives on the server thread (the token set is not shared).
  QString wsToken;
  QMetaObject::invokeMethod(mServer, "issueToken", Qt::BlockingQueuedConnection, qReturnArg(wsToken));
  callPage(QStringLiteral("reconnectAudio"),
           toJson({{"wsPort", int(mWsPort)}, {"wsToken", wsToken}}));
}

bool WebCallProvider::ensureServer() {
  if (mServer) return true;
  std::shared_ptr<SinkState> state = mSink;
  mServerThread = new QThread;
  mServer = new AudioBridgeServer(kOrigin, [state](const AudioChunk &chunk) {
    std::lock_guard lock(state->callMutex);
    if (auto sink = state->slot.get()) {
      sink->onAudio(chunk.participantId, chunk.samples, chunk.count, kSampleRate);
    }
  });
  mServer->moveToThread(mServerThread);
  // Destroys the server (and its sockets) on its own thread.
  connect(mServerThread, &QThread::finished, mServer, &QObject::deleteLater);
  mServerThread->start();
  quint16 port = 0;
  QMetaObject::invokeMethod(mServer, "listen", Qt::BlockingQueuedConnection, qReturnArg(port));
  if (port == 0) {
    stopServer();
    return false;
  }
  mWsPort = port;
  return true;
}

void WebCallProvider::stopServer() {
  if (!mServerThread) return;
  QThread *thread = mServerThread;
  mServerThread = nullptr;
  mServer = nullptr;
  mWsPort = 0;
  thread->quit();
  if (thread->wait(kServerStopTimeoutMs)) {
    delete thread;
  } else {
    qWarning() << "WebCallProvider: audio server thread did not stop in time; abandoning it";
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  }
}

void WebCallProvider::callPage(const QString &function, const QString &jsonArgs) {
  const QString js = QStringLiteral("window.sessioCall.%1(%2);").arg(function, jsonArgs);
  if (mLoaded) {
    mPage->runJavaScript(js);
  } else {
    mPendingJs.append(js);
  }
}

} // namespace pcm::webcall
