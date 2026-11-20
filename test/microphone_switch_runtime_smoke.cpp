#include "audio_capture_adapter.h"
#include "transcription_engine.h"
#include "transcription_test_support.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMediaDevices>
#include <QTimer>
#include <atomic>
#include <iostream>
#include <livekit/livekit.h>
#include <thread>

using namespace pcm::transcription;
using namespace pcm::transcription::testing;
namespace {
class SyntheticSource final : public pcm::video::AudioCaptureSource {
public:
  SyntheticSource() {
    QObject::connect(&timer, &QTimer::timeout, &input, [this] {
      const auto samples = speech(10);
      input.buffer() = QByteArray(reinterpret_cast<const char *>(samples.data()),
                                 static_cast<int>(samples.size() * sizeof(int16_t)));
      input.seek(0);
      emit input.readyRead();
    });
  }
  QIODevice *start() override {
    input.open(QIODevice::ReadOnly);
    timer.start(10);
    return &input;
  }
  void stop() override { timer.stop(); }
private:
  QBuffer input;
  QTimer timer;
};
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  const bool hardware = app.arguments().contains(QStringLiteral("--hardware"));
  const auto device = hardware ? QMediaDevices::defaultAudioInput() : QAudioDevice{};
  if (hardware && device.isNull()) return 77;
  std::atomic<bool> done{false};
  std::jthread watchdog([&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (!done) {
      std::cerr << "microphone-switch watchdog: owner thread did not finish\n";
      std::quick_exit(1);
    }
  });
  livekit::initialize(livekit::LogLevel::Warn);
  int result = 0;
  {
    TranscriptionEngine engine([] { return std::make_unique<FakeVad>(); },
                               std::make_shared<FakeRecognizer>(), {});
    engine.addTrack({"local", TrackRole::Practitioner, {}}, 0);
    engine.addTrack({"remote", TrackRole::Participant, {}}, 0);
    std::unique_ptr<pcm::video::AudioCaptureAdapter> adapter;
    if (hardware)
      adapter = std::make_unique<pcm::video::AudioCaptureAdapter>();
    else
      adapter = std::make_unique<pcm::video::AudioCaptureAdapter>(
          [](const QAudioDevice &) { return std::make_unique<SyntheticSource>(); });
    const auto published = adapter->audioSource();
    adapter->tap().setCallback([&](const int16_t *samples, size_t count, int rate) {
      engine.pushAudio("local", samples, count, rate);
    });
    QElapsedTimer clock;
    clock.start();
    QObject::connect(adapter.get(), &pcm::video::AudioCaptureAdapter::captureResumed,
                     [&] { engine.resetTrack("local", clock.elapsed()); });
    QObject::connect(adapter.get(), &pcm::video::AudioCaptureAdapter::captureFailed,
                     [&](const QString &) { result = 1; });
    if (!adapter->start(device)) result = 1;
    QTimer remote;
    QObject::connect(&remote, &QTimer::timeout, [&] {
      const auto pcm = concat({speech(10), silence(40)});
      engine.pushAudio("remote", pcm.data(), pcm.size(), 48000);
    });
    remote.start(50);
    QTimer switches;
    int cycles = 0;
    QObject::connect(&switches, &QTimer::timeout, [&] {
      adapter->tap().setEnabled(cycles % 3 != 0);
      if (!adapter->start(device) || adapter->audioSource() != published) result = 1;
      if (++cycles == 25) {
        adapter->stop();
        remote.stop();
        app.quit();
      }
    });
    switches.start(100);
    app.exec();
    engine.stop();
    if (adapter->framesCaptured() == 0 || engine.stats().phrases == 0) result = 1;
    std::cout << "25 capture restarts with active ASR; source preserved; frames="
              << adapter->framesCaptured() << "; phrases=" << engine.stats().phrases
              << "; mode=" << (hardware ? "single hardware input" : "synthetic sources") << '\n';
  }
  livekit::shutdown();
  done = true;
  return result;
}
