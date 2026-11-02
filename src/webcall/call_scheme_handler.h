#pragma once

#include <QWebEngineUrlSchemeHandler>

namespace pcm::webcall {

// Must run before the QApplication is constructed (Chromium reads the scheme
// table at start-up). Secure + CORS + fetch so the page can use getUserMedia,
// AudioWorklet, ES modules and cross-origin loads from the Jitsi server.
void registerCallScheme();

// Serves the call page from the Qt resources:
//   sessio-call://app/qwebchannel.js  -> :/qtwebchannel/qwebchannel.js
//   sessio-call://app/jitsi-call/<f>  -> :/jitsi-call/<f>
class CallSchemeHandler final : public QWebEngineUrlSchemeHandler {
  Q_OBJECT
public:
  using QWebEngineUrlSchemeHandler::QWebEngineUrlSchemeHandler;
  void requestStarted(QWebEngineUrlRequestJob *job) override;
};

} // namespace pcm::webcall
