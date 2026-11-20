// Manual test tool: joins a Sessio call as one or more fake clients that publish a
// synthetic animation and a tone, so a specialist can be tested from a single device
// with no second camera or microphone.
//
//   Sessio_fake_client --invite 'sessio://join?code=...&passcode=...&backend=...' [--count 3]
//   Sessio_fake_client --code CODE --passcode 123456 --backend https://host [--name Anna]
//
// Each fake client redeems the invitation through the real token backend exactly like
// ClientModeWindow does (POST /v1/invitations/{code}/client-token), so it exercises the
// production token path, not a hand-signed JWT.
#include "token_backend_client.h"

#include <QColor>
#include <QCommandLineParser>
#include <QDateTime>
#include <QEventLoop>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <livekit/livekit.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace {

constexpr int kSampleRate = 48000;
constexpr int kSamplesPerChunk = 480; // 10 ms
std::atomic<bool> gStop{false};

void onSignal(int) { gStop = true; }

struct Options {
  QString code, passcode, backend, baseName;
  int count = 1, width = 640, height = 360, fps = 15;
};

// One synthetic participant: a bouncing ball over a drifting gradient, labelled with its name
// and a running clock (so frozen video is obvious), plus a pulsed tone with a per-participant
// pitch and cadence (so participants are distinguishable by ear).
class FakeParticipant {
public:
  FakeParticipant(int index, QString name, const Options &options)
      : mIndex(index), mName(std::move(name)), mWidth(options.width), mHeight(options.height),
        mFps(options.fps),
        mVideo(std::make_shared<livekit::VideoSource>(options.width, options.height)),
        mAudio(std::make_shared<livekit::AudioSource>(kSampleRate, 1)) {}
  ~FakeParticipant() { stop(); }

  bool start(const QString &url, const QString &token) {
    livekit::RoomOptions roomOptions;
    roomOptions.auto_subscribe = false; // publish only; we never render remote media
    if (!mRoom.connect(url.toStdString(), token.toStdString(), roomOptions)) return false;
    mVideoTrack = livekit::LocalVideoTrack::createLocalVideoTrack("animation", mVideo);
    mAudioTrack = livekit::LocalAudioTrack::createLocalAudioTrack("tone", mAudio);
    livekit::TrackPublishOptions videoOptions;
    videoOptions.source = livekit::TrackSource::SOURCE_CAMERA;
    videoOptions.simulcast = false;
    mRoom.localParticipant().lock()->publishTrack(mVideoTrack, videoOptions);
    livekit::TrackPublishOptions audioOptions;
    audioOptions.source = livekit::TrackSource::SOURCE_MICROPHONE;
    audioOptions.dtx = false;
    mRoom.localParticipant().lock()->publishTrack(mAudioTrack, audioOptions);
    mRunning = true;
    mAudioThread = std::thread([this] { audioLoop(); });
    mVideoThread = std::thread([this] { videoLoop(); });
    return true;
  }

  void stop() {
    mRunning = false;
    if (mAudioThread.joinable()) mAudioThread.join();
    if (mVideoThread.joinable()) mVideoThread.join();
    mRoom.disconnect();
    mVideoTrack.reset();
    mAudioTrack.reset();
  }

private:
  void audioLoop() {
    // Pitch: 330 Hz + 110 Hz per participant. Cadence: 0.3 s on, then silence, period grows
    // with the index, so two fakes never beep in lockstep.
    const double frequency = 330.0 + 110.0 * mIndex;
    const double periodSeconds = 1.0 + 0.25 * mIndex;
    const double twoPi = 2 * 3.141592653589793;
    int64_t sample = 0;
    auto next = std::chrono::steady_clock::now();
    while (mRunning.load()) {
      std::vector<int16_t> samples(kSamplesPerChunk);
      for (auto &value : samples) {
        const double t = static_cast<double>(sample) / kSampleRate;
        const bool on = std::fmod(t, periodSeconds) < 0.3;
        value = on ? static_cast<int16_t>(4000 * std::sin(twoPi * frequency * t)) : 0;
        ++sample;
      }
      mAudio->captureFrame(livekit::AudioFrame(std::move(samples), kSampleRate, 1, kSamplesPerChunk));
      next += std::chrono::milliseconds(10);
      std::this_thread::sleep_until(next);
    }
  }

  void videoLoop() {
    auto frame = livekit::VideoFrame::create(mWidth, mHeight, livekit::VideoBufferType::RGBA);
    QImage image(mWidth, mHeight, QImage::Format_RGBA8888);
    const int baseHue = (mIndex * 67) % 360;
    const int ballRadius = mHeight / 10;
    const auto start = std::chrono::steady_clock::now();
    auto next = start;
    while (mRunning.load()) {
      const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      QPainter painter(&image);
      painter.setRenderHint(QPainter::Antialiasing);
      QLinearGradient gradient(0, 0, mWidth, mHeight);
      gradient.setColorAt(0, QColor::fromHsv((baseHue + static_cast<int>(t * 20)) % 360, 200, 90));
      gradient.setColorAt(1, QColor::fromHsv((baseHue + 60 + static_cast<int>(t * 20)) % 360, 200, 200));
      painter.fillRect(image.rect(), gradient);
      // Triangle-wave bounce on both axes with different periods, so it traces a Lissajous path.
      const auto bounce = [](double phase) {
        const double p = std::fmod(phase, 2.0);
        return p < 1.0 ? p : 2.0 - p;
      };
      const int x = ballRadius + static_cast<int>(bounce(t * 0.45) * (mWidth - 2 * ballRadius));
      const int y = ballRadius + static_cast<int>(bounce(t * 0.31) * (mHeight - 2 * ballRadius));
      painter.setPen(Qt::NoPen);
      painter.setBrush(Qt::white);
      painter.drawEllipse(QPoint(x, y), ballRadius, ballRadius);
      painter.setPen(Qt::white);
      QFont font = painter.font();
      font.setPixelSize(mHeight / 9);
      font.setBold(true);
      painter.setFont(font);
      painter.drawText(QRect(12, 8, mWidth - 24, mHeight / 5), Qt::AlignLeft | Qt::AlignVCenter,
                       mName);
      painter.drawText(QRect(12, mHeight - mHeight / 5 - 8, mWidth - 24, mHeight / 5),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QString::number(t, 'f', 1) + QStringLiteral(" s"));
      painter.end();
      std::copy(image.constBits(), image.constBits() + image.sizeInBytes(), frame.data());
      mVideo->captureFrame(frame);
      next += std::chrono::microseconds(1000000 / mFps);
      std::this_thread::sleep_until(next);
    }
  }

  int mIndex;
  QString mName;
  int mWidth, mHeight, mFps;
  livekit::Room mRoom;
  std::shared_ptr<livekit::VideoSource> mVideo;
  std::shared_ptr<livekit::AudioSource> mAudio;
  std::shared_ptr<livekit::LocalVideoTrack> mVideoTrack;
  std::shared_ptr<livekit::LocalAudioTrack> mAudioTrack;
  std::atomic<bool> mRunning{false};
  std::thread mAudioThread, mVideoThread;
};

// Blocks (spinning the event loop) until the backend answers; nullopt on failure.
std::optional<pcm::tokenclient::TokenResult> fetchToken(pcm::tokenclient::TokenBackendClient &client,
                                                       const Options &options, const QString &name) {
  std::optional<pcm::tokenclient::TokenResult> result;
  QString failure;
  QEventLoop loop;
  QObject::connect(&client, &pcm::tokenclient::TokenBackendClient::tokenReceived, &loop,
                   [&](pcm::tokenclient::TokenResult value) { result = value; loop.quit(); });
  QObject::connect(&client, &pcm::tokenclient::TokenBackendClient::tokenRequestFailed, &loop,
                   [&](QString reason) { failure = reason; loop.quit(); });
  client.requestClientToken(options.code, options.passcode, name);
  loop.exec();
  QObject::disconnect(&client, nullptr, &loop, nullptr);
  if (!result) std::cerr << "token request for \"" << name.toStdString() << "\" failed: "
                         << failure.toStdString() << '\n';
  return result;
}

} // namespace

int main(int argc, char **argv) {
  // Painting into a QImage needs fonts but no display; default to offscreen so the tool also
  // runs over SSH or on a headless box.
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);

  QCommandLineParser parser;
  parser.setApplicationDescription("Join a Sessio call as fake clients publishing animation + tone.");
  parser.addHelpOption();
  parser.addOption({"invite", "sessio://join?code=..&passcode=.. link (carries the backend too).", "url"});
  parser.addOption({"code", "Invitation code.", "code"});
  parser.addOption({"passcode", "Room passcode.", "digits"});
  parser.addOption({"backend", "Token backend base URL, e.g. https://livekit.sessio-pcm.ru.", "url"});
  parser.addOption({"name", "Display name; with --count > 1 a number is appended.", "name", "Fake client"});
  parser.addOption({"count", "Number of fake clients to join with.", "n", "1"});
  parser.addOption({"size", "Video size WxH.", "WxH", "640x360"});
  parser.addOption({"fps", "Video frame rate.", "n", "15"});
  parser.process(app);

  Options options;
  options.baseName = parser.value("name");
  options.count = parser.value("count").toInt();
  options.fps = parser.value("fps").toInt();
  const auto size = parser.value("size").split('x');
  if (size.size() != 2) { std::cerr << "--size must be WxH\n"; return 2; }
  options.width = size[0].toInt();
  options.height = size[1].toInt();

  if (parser.isSet("invite")) {
    const QUrl url(parser.value("invite"));
    if (url.scheme() != "sessio") { std::cerr << "--invite must be a sessio:// link\n"; return 2; }
    const QUrlQuery query(url);
    options.code = query.queryItemValue("code");
    options.passcode = query.queryItemValue("passcode");
    options.backend = query.queryItemValue("backend", QUrl::FullyDecoded);
  }
  if (parser.isSet("code")) options.code = parser.value("code");
  if (parser.isSet("passcode")) options.passcode = parser.value("passcode");
  if (parser.isSet("backend")) options.backend = parser.value("backend");
  if (options.code.isEmpty() || options.passcode.isEmpty() || options.backend.isEmpty() ||
      options.count < 1 || options.width < 64 || options.height < 64 || options.fps < 1) {
    std::cerr << "need an invitation (--invite, or --code/--passcode/--backend) and sane sizes\n";
    return 2;
  }

  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);
  livekit::initialize(livekit::LogLevel::Warn);
  int exitCode = 0;
  {
    pcm::tokenclient::TokenBackendClient client(options.backend);
    std::vector<std::unique_ptr<FakeParticipant>> participants;
    for (int index = 0; index < options.count && !gStop; ++index) {
      const QString name = options.count == 1 ? options.baseName
                                              : QStringLiteral("%1 %2").arg(options.baseName).arg(index + 1);
      const auto token = fetchToken(client, options, name);
      if (!token) { exitCode = 1; break; }
      auto participant = std::make_unique<FakeParticipant>(index, name, options);
      if (!participant->start(token->endpointUrl, token->token)) {
        std::cerr << "failed to connect \"" << name.toStdString() << "\" to " << token->endpointUrl.toStdString() << '\n';
        exitCode = 1;
        break;
      }
      std::cout << "joined: " << name.toStdString() << " (room " << token->roomName.toStdString() << ")\n";
      participants.push_back(std::move(participant));
    }
    if (exitCode == 0 && !participants.empty()) {
      std::cout << participants.size() << " fake client(s) publishing. Ctrl+C to leave.\n";
      QTimer poll;
      QObject::connect(&poll, &QTimer::timeout, &app, [&] { if (gStop) app.quit(); });
      poll.start(200);
      app.exec();
    }
    participants.clear(); // stop publishers and disconnect before shutting the SDK down
  }
  livekit::shutdown();
  return exitCode;
}
