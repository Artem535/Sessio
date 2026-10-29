#pragma once

#include <QWebEngineUrlSchemeHandler>

class AudioStats;

// sessio-audio://app/<file> serves the page from resources (a secure origin,
// needed for AudioWorklet and getUserMedia); POST sessio-audio://app/push
// accepts one binary chunk per request, only from the page's own origin.
class SchemeHandler final : public QWebEngineUrlSchemeHandler {
  Q_OBJECT
public:
  explicit SchemeHandler(AudioStats *stats, QObject *parent = nullptr)
      : QWebEngineUrlSchemeHandler(parent), stats_(stats) {}
  void requestStarted(QWebEngineUrlRequestJob *job) override;

private:
  AudioStats *stats_;
};
