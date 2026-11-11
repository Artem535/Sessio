#include "portal_screen_cast.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>
#include <QLoggingCategory>
#include <QRandomGenerator>

#include <atomic>
#include <optional>
#include <utility>
#include <fcntl.h>

Q_LOGGING_CATEGORY(lcPortalScreenCast, "pcm.video.portal")

namespace pcm::video {

namespace {

const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kScreenCast = QStringLiteral("org.freedesktop.portal.ScreenCast");
const QString kRequest = QStringLiteral("org.freedesktop.portal.Request");
const QString kSession = QStringLiteral("org.freedesktop.portal.Session");

// The picker stays open as long as the user deliberates; some backends answer
// the method call only after it closes, so the D-Bus default of 25 s is too short.
constexpr int kPickerTimeoutMs = 10 * 60 * 1000;
constexpr int kQuickTimeoutMs = 5000;
constexpr uint kCursorEmbedded = 2;
constexpr uint kCursorHidden = 1;

QDBusMessage portalCall(const QString &method, const QVariantList &arguments) {
  auto message = QDBusMessage::createMethodCall(kService, kPath, kScreenCast, method);
  message.setArguments(arguments);
  return message;
}

std::optional<QVariant> screenCastProperty(const QString &name) {
  auto message = QDBusMessage::createMethodCall(kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                QStringLiteral("Get"));
  message << kScreenCast << name;
  const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, kQuickTimeoutMs);
  if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) return std::nullopt;
  return reply.arguments().constFirst().value<QDBusVariant>().variant();
}

// The first node id of a Start response's "streams" (a(ua{sv})).
std::optional<quint32> firstStreamNode(const QVariant &streams) {
  if (!streams.canConvert<QDBusArgument>()) return std::nullopt;
  const auto argument = streams.value<QDBusArgument>();
  std::optional<quint32> node;
  argument.beginArray();
  while (!argument.atEnd()) {
    quint32 id = 0;
    QVariantMap properties;
    argument.beginStructure();
    argument >> id >> properties;
    argument.endStructure();
    if (!node) node = id;
  }
  argument.endArray();
  return node;
}

} // namespace

PortalScreenCast::PortalScreenCast(QObject *parent) : QObject(parent) {}

PortalScreenCast::~PortalScreenCast() { close(); }

bool PortalScreenCast::isAvailable() {
  static const bool available = [] {
    const auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return false;
    return screenCastProperty(QStringLiteral("version")).has_value();
  }();
  return available;
}

QString PortalScreenCast::newToken() const {
  static std::atomic<quint32> counter{0};
  return QStringLiteral("sessio_%1_%2").arg(++counter).arg(QRandomGenerator::global()->generate());
}

QString PortalScreenCast::requestPathFor(const QString &token) const {
  // Per the portal spec: the caller's unique name without ':' and with '.' as '_'.
  QString sender = QDBusConnection::sessionBus().baseService();
  sender.remove(0, 1).replace(QLatin1Char('.'), QLatin1Char('_'));
  return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
}

void PortalScreenCast::start(uint sourceTypes) {
  close();
  mSourceTypes = sourceTypes;
  const QString sessionToken = newToken();
  const QString token = newToken();
  callWithRequest(Step::CreateSession, QStringLiteral("CreateSession"),
                  {QVariantMap{{QStringLiteral("handle_token"), token},
                               {QStringLiteral("session_handle_token"), sessionToken}}},
                  token);
}

void PortalScreenCast::callWithRequest(Step step, const QString &method, QVariantList arguments,
                                       const QString &token) {
  // Subscribe before calling: the response may arrive before the call returns.
  watchRequest(requestPathFor(token));
  mStep = step;
  const int timeout = step == Step::CreateSession ? kQuickTimeoutMs : kPickerTimeoutMs;
  auto pending = QDBusConnection::sessionBus().asyncCall(portalCall(method, arguments), timeout);
  mPendingCall = new QDBusPendingCallWatcher(pending, this);
  connect(mPendingCall, &QDBusPendingCallWatcher::finished, this, [this, method](QDBusPendingCallWatcher *watcher) {
    watcher->deleteLater();
    if (watcher != mPendingCall) return;
    mPendingCall = nullptr;
    const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
    if (reply.isError()) {
      fail(QStringLiteral("%1 failed: %2").arg(method, reply.error().message()));
      return;
    }
    // Portals older than version 0.9 pick the handle themselves.
    // An empty mRequestPath means the response already came in.
    const QString handle = reply.value().path();
    if (!mRequestPath.isEmpty() && handle != mRequestPath) watchRequest(handle);
  });
}

void PortalScreenCast::watchRequest(const QString &path) {
  unwatchRequest();
  mRequestPath = path;
  QDBusConnection::sessionBus().connect(kService, path, kRequest, QStringLiteral("Response"), this,
                                        SLOT(onResponse(uint, QVariantMap)));
}

void PortalScreenCast::unwatchRequest() {
  if (mRequestPath.isEmpty()) return;
  QDBusConnection::sessionBus().disconnect(kService, mRequestPath, kRequest, QStringLiteral("Response"), this,
                                           SLOT(onResponse(uint, QVariantMap)));
  mRequestPath.clear();
}

void PortalScreenCast::onResponse(uint response, const QVariantMap &results) {
  unwatchRequest();
  if (response == 1) {
    fail(QStringLiteral("Screen selection was cancelled."));
    return;
  }
  if (response != 0) {
    fail(QStringLiteral("The desktop portal refused screen sharing."));
    return;
  }
  switch (mStep) {
  case Step::CreateSession:
    mSessionHandle = results.value(QStringLiteral("session_handle")).toString();
    if (mSessionHandle.isEmpty()) {
      fail(QStringLiteral("The desktop portal returned no session."));
      return;
    }
    QDBusConnection::sessionBus().connect(kService, mSessionHandle, kSession, QStringLiteral("Closed"), this,
                                          SLOT(onSessionClosed(QVariantMap)));
    selectSources();
    break;
  case Step::SelectSources:
    startStream();
    break;
  case Step::Start: {
    const auto node = firstStreamNode(results.value(QStringLiteral("streams")));
    if (!node) {
      fail(QStringLiteral("Nothing was selected to share."));
      return;
    }
    const int fd = openRemote();
    if (fd < 0) return;
    mStep = Step::Streaming;
    emit streamReady(fd, *node);
    break;
  }
  default:
    break;
  }
}

void PortalScreenCast::selectSources() {
  uint cursorMode = kCursorHidden;
  if (const auto modes = screenCastProperty(QStringLiteral("AvailableCursorModes")))
    if (modes->toUInt() & kCursorEmbedded) cursorMode = kCursorEmbedded;
  const QString token = newToken();
  callWithRequest(Step::SelectSources, QStringLiteral("SelectSources"),
                  {QVariant::fromValue(QDBusObjectPath(mSessionHandle)),
                   QVariantMap{{QStringLiteral("handle_token"), token},
                               {QStringLiteral("types"), mSourceTypes},
                               {QStringLiteral("multiple"), false},
                               {QStringLiteral("cursor_mode"), cursorMode},
                               {QStringLiteral("persist_mode"), 0u}}},
                  token);
}

void PortalScreenCast::startStream() {
  const QString token = newToken();
  callWithRequest(Step::Start, QStringLiteral("Start"),
                  {QVariant::fromValue(QDBusObjectPath(mSessionHandle)), QString(),
                   QVariantMap{{QStringLiteral("handle_token"), token}}},
                  token);
}

int PortalScreenCast::openRemote() {
  mStep = Step::OpenRemote;
  const QDBusMessage reply = QDBusConnection::sessionBus().call(
      portalCall(QStringLiteral("OpenPipeWireRemote"),
                 {QVariant::fromValue(QDBusObjectPath(mSessionHandle)), QVariantMap{}}),
      QDBus::Block, kQuickTimeoutMs);
  if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
    fail(QStringLiteral("Could not open the screen stream: %1").arg(reply.errorMessage()));
    return -1;
  }
  const auto descriptor = reply.arguments().constFirst().value<QDBusUnixFileDescriptor>();
  // QDBusUnixFileDescriptor closes its own copy; keep a duplicate.
  const int fd = descriptor.isValid() ? fcntl(descriptor.fileDescriptor(), F_DUPFD_CLOEXEC, 3) : -1;
  if (fd < 0) {
    fail(QStringLiteral("The desktop portal returned no screen stream."));
    return -1;
  }
  return fd;
}

void PortalScreenCast::onSessionClosed(const QVariantMap &details) {
  Q_UNUSED(details);
  const bool wasStreaming = mStep == Step::Streaming;
  mSessionHandle.clear(); // already closed by the desktop
  close();
  if (wasStreaming)
    emit closed();
  else
    fail(QStringLiteral("The desktop portal closed the screen sharing session."));
}

void PortalScreenCast::fail(const QString &reason) {
  qCWarning(lcPortalScreenCast) << "screen cast:" << reason;
  close();
  mStep = Step::Failed;
  emit failed(reason);
}

void PortalScreenCast::close() {
  auto bus = QDBusConnection::sessionBus();
  if (mPendingCall) {
    mPendingCall->deleteLater();
    mPendingCall = nullptr;
  }
  if (!mRequestPath.isEmpty()) {
    // Cancels the picker or whatever the pending request is waiting for.
    bus.send(QDBusMessage::createMethodCall(kService, mRequestPath, kRequest, QStringLiteral("Close")));
    unwatchRequest();
  }
  if (!mSessionHandle.isEmpty()) {
    bus.disconnect(kService, mSessionHandle, kSession, QStringLiteral("Closed"), this,
                   SLOT(onSessionClosed(QVariantMap)));
    bus.send(QDBusMessage::createMethodCall(kService, mSessionHandle, kSession, QStringLiteral("Close")));
    qCDebug(lcPortalScreenCast) << "closed session" << mSessionHandle;
    mSessionHandle.clear();
  }
  mStep = Step::Idle;
}

} // namespace pcm::video
