#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

class QDBusPendingCallWatcher;

namespace pcm::video {

// Client of org.freedesktop.portal.ScreenCast: asks the desktop which screen or
// window to share and hands back a PipeWire remote for it.
//
// Unlike Qt 6.10's own helper this gives every request and session a unique
// token, listens only to its own request, and closes the session when done.
// Qt's fixed request token collided with backends that keep request objects
// alive (xdg-desktop-portal-hyprland), so only the first share of a process
// ever worked.
class PortalScreenCast final : public QObject {
  Q_OBJECT
public:
  enum SourceType : uint { Monitor = 1, Window = 2 };

  explicit PortalScreenCast(QObject *parent = nullptr);
  ~PortalScreenCast() override;

  // True when the session bus has a desktop portal with the ScreenCast interface.
  [[nodiscard]] static bool isAvailable();

  // Runs CreateSession, SelectSources (the desktop's picker), Start and
  // OpenPipeWireRemote. Ends in exactly one of streamReady() or failed().
  void start(uint sourceTypes);
  // Cancels a pending request and closes the session. Safe to call repeatedly.
  void close();

signals:
  // The receiver owns pipeWireFd and must close it.
  void streamReady(int pipeWireFd, quint32 nodeId);
  void failed(QString reason);
  // The desktop ended a running session (e.g. its "stop sharing" control).
  void closed();

private slots:
  void onResponse(uint response, const QVariantMap &results);
  void onSessionClosed(const QVariantMap &details);

private:
  enum class Step { Idle, CreateSession, SelectSources, Start, OpenRemote, Streaming, Failed };

  QString newToken() const;
  QString requestPathFor(const QString &token) const;
  // Sends a portal method whose result arrives as a Request::Response signal.
  void callWithRequest(Step step, const QString &method, QVariantList arguments, const QString &token);
  void watchRequest(const QString &path);
  void unwatchRequest();
  void selectSources();
  void startStream();
  // Returns a PipeWire remote descriptor the caller owns, or -1 after fail().
  int openRemote();
  void fail(const QString &reason);

  Step mStep{Step::Idle};
  uint mSourceTypes{Monitor};
  QString mSessionHandle;
  QString mRequestPath;
  QDBusPendingCallWatcher *mPendingCall{nullptr};
};

} // namespace pcm::video
