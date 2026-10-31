#pragma once

#include "video_provider.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <memory>

class QThread;
class QWebChannel;
class QWebEngineProfile;
class QWebEnginePage;
class QWebEngineView;

namespace pcm::webcall {

class AudioBridgeServer;
class WebCallBridge;

// A call that runs inside our Jitsi page hosted in a QWebEngineView. Control
// events come back over QWebChannel (WebCallBridge); per-participant PCM comes
// over a loopback WebSocket (AudioBridgeServer on its own thread) and is
// handed to the AudioSink.
class WebCallProvider final : public pcm::video::VideoProvider {
  Q_OBJECT
public:
  struct Options {
    QString adapter = QStringLiteral("jitsi"); // "fake" is for tests
    QString displayName;
    // Only used when adapter == "fake".
    int fakeParticipants = 2;
    int fakeChurnMs = 0;
  };

  explicit WebCallProvider(Options options, QObject *parent = nullptr);
  ~WebCallProvider() override;

  // url = "<serverUrl>/<room>": the room is the last path segment.
  void join(const QString &url, const QString &token) override;
  void leave() override;
  void setMicrophoneEnabled(bool enabled) override;
  void setCameraEnabled(bool enabled) override;
  [[nodiscard]] bool isMicrophoneEnabled() const override { return mMicEnabled; }
  [[nodiscard]] bool isCameraEnabled() const override { return mCamEnabled; }
  // After this returns with nullptr, the sink receives no further onAudio.
  void setAudioSink(std::shared_ptr<pcm::video::AudioSink> sink) override;
  [[nodiscard]] qint64 callElapsedMs() const override;
  // Owned by the provider (deleted with it); a host may reparent it into a layout.
  [[nodiscard]] QWidget *callView() override;

  // Test seams.
  void simulateRenderProcessTerminated();
  void simulateAudioBridgeDrop();

private:
  struct SinkState;

  void onLoadFinished(bool ok);
  void onRenderProcessTerminated();
  void onAudioBridgeLost();
  void onPageLeft();
  bool ensureServer();
  void stopServer();
  void callPage(const QString &function, const QString &jsonArgs = {});
  void detachSink();

  Options mOptions;
  std::shared_ptr<SinkState> mSink;
  QWebEngineProfile *mProfile = nullptr;
  QWebEnginePage *mPage = nullptr;
  QWebChannel *mChannel = nullptr;
  WebCallBridge *mBridge = nullptr;
  QPointer<QWebEngineView> mView;
  QThread *mServerThread = nullptr;
  AudioBridgeServer *mServer = nullptr;
  quint16 mWsPort = 0;
  bool mLoaded = false;
  bool mDead = false;
  bool mJoinRequested = false;
  bool mLeaving = false;
  bool mMicEnabled = true;
  bool mCamEnabled = true;
  QStringList mPendingJs;
  QElapsedTimer mElapsed;
  bool mConnected = false;
};

} // namespace pcm::webcall
