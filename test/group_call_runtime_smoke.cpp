#include "call_page.h"
#include "fake_video_provider.h"
#include "livekit_video_provider.h"
#include "livekit_video_frame_source.h"
#include "remote_audio_player.h"
#include "remote_video_renderer.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMediaDevices>
#include <QMessageAuthenticationCode>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPainter>
#include <QPointer>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

// Test access to real subscribed tracks and production audio sink telemetry.
// No copied callback handler or synthetic local track is injected into provider.
namespace pcm::video {
struct RemoteAudioPlayerTestAccess {
  static qint64 processed(RemoteAudioPlayer &player) {
    if (!player.mSink) return 0;
    player.mSink->setVolume(0); // Automated gate is deliberately inaudible.
    return player.mSink->processedUSecs();
  }
};
struct LiveKitVideoProviderTestAccess {
  static std::shared_ptr<livekit::Track> audio(LiveKitVideoProvider &provider, const QString &id) {
    const auto it = provider.mMedia.find(id);
    return it == provider.mMedia.end() ? nullptr : it->second->audioTrack;
  }
  static qint64 processed(LiveKitVideoProvider &provider, const QString &id) {
    const auto it = provider.mMedia.find(id);
    return it == provider.mMedia.end() ? 0 : RemoteAudioPlayerTestAccess::processed(*it->second->audio);
  }
};
} // namespace pcm::video

namespace {
using namespace pcm::video;
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
bool waitUntil(const std::function<bool()> &predicate, int timeout = 8000) {
  QElapsedTimer clock;
  clock.start();
  do {
    QApplication::processEvents();
    if (predicate()) return true;
    QThread::msleep(10);
  } while (clock.elapsed() < timeout);
  return false;
}
QByteArray base64(const QByteArray &bytes) {
  return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
QString token(const QString &room, const QString &id) {
  const auto now = QDateTime::currentSecsSinceEpoch();
  const QJsonObject claims{{"iss", "devkey"}, {"sub", id}, {"name", id},
                          {"nbf", now - 10}, {"exp", now + 600},
                          {"metadata", "{\"role\":\"client\"}"},
                          {"video", QJsonObject{{"room", room}, {"roomJoin", true},
                                                {"canPublish", true}, {"canSubscribe", true}}}};
  const auto payload = base64("{\"alg\":\"HS256\",\"typ\":\"JWT\"}") + "." +
                       base64(QJsonDocument(claims).toJson(QJsonDocument::Compact));
  return QString::fromLatin1(payload + "." + base64(QMessageAuthenticationCode::hash(
      payload, "secret", QCryptographicHash::Sha256)));
}
QImage pattern(bool red) {
  QImage image(320, 180, QImage::Format_RGBA8888);
  image.fill(red ? QColor(255, 0, 0) : QColor(0, 0, 255));
  return image;
}
bool hasPattern(VideoFrameSource *source, bool red) {
  if (!source || source->latestFrame().isNull()) return false;
  const auto image = source->latestFrame();
  const auto color = image.pixelColor(image.width() / 2, image.height() / 2);
  return red ? color.red() > 220 && color.blue() < 30
             : color.blue() > 220 && color.red() < 30;
}

struct Publisher {
  livekit::Room room;
  std::shared_ptr<livekit::VideoSource> video = std::make_shared<livekit::VideoSource>(320, 180);
  std::shared_ptr<livekit::AudioSource> audio = std::make_shared<livekit::AudioSource>(48000, 1);
  std::shared_ptr<livekit::LocalVideoTrack> videoTrack;
  std::shared_ptr<livekit::LocalAudioTrack> audioTrack;
  std::atomic<bool> running{false};
  std::thread producer;
  ~Publisher() { stop(); }
  void start(const QString &roomName, const QString &id, bool red, double frequency) {
    livekit::RoomOptions options;
    options.auto_subscribe = false;
    options.join_retries = 0;
    options.connect_timeout = std::chrono::seconds(5);
    require(room.connect("ws://127.0.0.1:17980", token(roomName, id).toStdString(), options),
            "synthetic publisher failed to connect");
    videoTrack = livekit::LocalVideoTrack::createLocalVideoTrack("pattern", video);
    audioTrack = livekit::LocalAudioTrack::createLocalAudioTrack("tone", audio);
    livekit::TrackPublishOptions videoOptions;
    videoOptions.source = livekit::TrackSource::SOURCE_CAMERA;
    videoOptions.simulcast = false;
    room.localParticipant().lock()->publishTrack(videoTrack, videoOptions);
    livekit::TrackPublishOptions audioOptions;
    audioOptions.source = livekit::TrackSource::SOURCE_MICROPHONE;
    audioOptions.dtx = false;
    room.localParticipant().lock()->publishTrack(audioTrack, audioOptions);
    running = true;
    producer = std::thread([this, red, frequency] {
      auto frame = livekit::VideoFrame::create(320, 180, livekit::VideoBufferType::RGBA);
      const auto image = pattern(red);
      std::copy(image.constBits(), image.constBits() + image.sizeInBytes(), frame.data());
      int64_t sample = 0;
      int tick = 0;
      while (running.load()) {
        if (tick++ % 3 == 0) video->captureFrame(frame);
        std::vector<int16_t> samples(480);
        for (auto &value : samples)
          value = static_cast<int16_t>(2000 * std::sin(2 * 3.141592653589793 * frequency * sample++ / 48000));
        try { audio->captureFrame(livekit::AudioFrame(std::move(samples), 48000, 1, 480)); }
        catch (const std::exception &) { running = false; }
        QThread::msleep(10);
      }
    });
  }
  void stop() {
    running = false;
    if (producer.joinable()) producer.join();
    room.disconnect();
    videoTrack.reset();
    audioTrack.reset();
  }
};

// Independent decoded-tone inspection from the real remote tracks also used
// by the production players. Reader is closed before destruction on all paths.
struct ToneProbe {
  std::shared_ptr<livekit::AudioStream> stream;
  std::thread reader;
  std::atomic<double> frequency{0};
  explicit ToneProbe(const std::shared_ptr<livekit::Track> &track) {
    livekit::AudioStream::Options options;
    options.capacity = 2;
    stream = livekit::AudioStream::fromTrack(track, options);
    reader = std::thread([this] {
      livekit::AudioFrameEvent event;
      while (stream->read(event)) {
        const auto &frame = event.frame;
        const auto &samples = frame.data();
        int crossings = 0;
        int peak = 0;
        const int channels = frame.numChannels();
        for (size_t i = channels; i < samples.size(); i += channels) {
          peak = std::max(peak, std::abs(static_cast<int>(samples[i])));
          if (samples[i - channels] <= 0 && samples[i] > 0) ++crossings;
        }
        if (peak > 300 && !samples.empty())
          frequency = static_cast<double>(crossings) * frame.sampleRate() * channels / samples.size();
      }
    });
  }
  ~ToneProbe() { stream->close(); reader.join(); }
};

void roomSmoke() {
  LiveKitVideoProvider provider; // Initializes SDK; outlives every SDK object.
  provider.setCameraEnabled(false);
  provider.setMicrophoneEnabled(false);
  const auto roomName = "sessio-synthetic-" + QString::number(QDateTime::currentMSecsSinceEpoch());
  bool joined = false;
  QObject::connect(&provider, &VideoProvider::joined, [&] { joined = true; });
  provider.join("ws://127.0.0.1:17980", token(roomName, "synthetic-observer"));
  require(waitUntil([&] { return joined; }), "production provider did not join");
  Publisher a, b;
  a.start(roomName, "synthetic-red", true, 440);
  b.start(roomName, "synthetic-blue", false, 660);
  require(waitUntil([&] { return provider.participants()->remoteCount() == 2 &&
      hasPattern(provider.frameSource("synthetic-red"), true) &&
      hasPattern(provider.frameSource("synthetic-blue"), false); }), "independent remote patterns missing");
  require(waitUntil([&] { return LiveKitVideoProviderTestAccess::audio(provider, "synthetic-red") &&
      LiveKitVideoProviderTestAccess::audio(provider, "synthetic-blue"); }), "remote audio subscriptions missing");
  {
    ToneProbe red(LiveKitVideoProviderTestAccess::audio(provider, "synthetic-red"));
    ToneProbe blue(LiveKitVideoProviderTestAccess::audio(provider, "synthetic-blue"));
    require(waitUntil([&] { return std::abs(red.frequency.load() - 440) < 100 &&
        std::abs(blue.frequency.load() - 660) < 100; }), "independent decoded remote tones missing");
    std::cout << "remote patterns and decoded 440/660 Hz tones passed\n";
  }
  const auto outputs = QMediaDevices::audioOutputs();
  if (!outputs.isEmpty()) {
    require(waitUntil([&] { return LiveKitVideoProviderTestAccess::processed(provider, "synthetic-red") > 20000 &&
        LiveKitVideoProviderTestAccess::processed(provider, "synthetic-blue") > 20000; }),
        "production audio sinks did not both advance");
    // Exercise every available speaker plus a same-device reattachment. This
    // covers reattachment of both tracks even on hosts with one output device.
    for (const auto &output : outputs) {
      provider.switchSpeaker(output);
      require(waitUntil([&] { return LiveKitVideoProviderTestAccess::processed(provider, "synthetic-red") > 20000 &&
          LiveKitVideoProviderTestAccess::processed(provider, "synthetic-blue") > 20000; }),
          "speaker switch did not resume both production sinks");
    }
    std::cout << "both production sinks advanced across " << outputs.size() << " speaker reattachments\n";
  } else std::cout << "DEVICE GATE UNAVAILABLE: no audio output; decoded tones verified\n";
  auto *survivor = provider.frameSource("synthetic-blue");
  a.videoTrack->mute();
  a.audioTrack->mute();
  require(waitUntil([&] { const auto row = provider.participants()->participant("synthetic-red");
    return row && !row->cameraEnabled && !row->microphoneEnabled; }), "mute states not received");
  require(provider.participants()->remoteCount() == 2 && provider.frameSource("synthetic-blue") == survivor,
          "muting changed presence or survivor source");
  a.videoTrack->unmute();
  a.audioTrack->unmute();
  require(waitUntil([&] { const auto row = provider.participants()->participant("synthetic-red");
    return row && row->cameraEnabled && row->microphoneEnabled && hasPattern(provider.frameSource("synthetic-red"), true);
  }), "unmute did not recover media");
  QPointer<VideoFrameSource> departed = provider.frameSource("synthetic-red");
  a.stop();
  require(waitUntil([&] { return provider.participants()->remoteCount() == 1 && !departed; }),
          "departure did not destroy its reader/source");
  require(provider.frameSource("synthetic-blue") == survivor && hasPattern(survivor, false),
          "departure disturbed survivor");
  a.start(roomName, "synthetic-red", true, 440);
  require(waitUntil([&] { return provider.participants()->remoteCount() == 2 &&
      hasPattern(provider.frameSource("synthetic-red"), true); }), "same-identity rejoin did not recover");
  a.stop();
  b.stop();
  require(waitUntil([&] { return provider.participants()->remoteCount() == 0; }), "all departures not received");
  provider.leave();
  require(provider.participants()->rowCount() == 0 && !provider.frameSource("synthetic-blue"),
          "provider leave retained media");
  std::cout << "join, mute/unmute, leave/rejoin, survivor identity and terminal teardown passed\n"
               "MANUAL GATE: audible mixing and physical camera/microphone remain unverified\n";
}

void nativeUi(const QString &directory) {
  require(QGuiApplication::platformName() != "offscreen", "native UI gate requires a display");
  QDir().mkpath(directory);
  DeviceManager devices;
  CallPage page(&devices);
  auto *provider = new test::FakeVideoProvider;
  VideoSession session(provider);
  page.attachSession(&session);
  page.setSidePanelToggleVisible(false); // Client composition: display role cannot enable notes.
  page.setFixedSize(1280, 800);
  page.setWindowTitle("Sessio synthetic call verification");
  session.join("synthetic", "synthetic");
  require(waitUntil([&] { return session.state() == VideoSessionState::Joining; }), "fake session not joining");
  provider->simulateJoined();
  require(waitUntil([&] { return session.state() == VideoSessionState::WaitingForParticipants; }), "fake session not waiting");
  provider->simulateParticipantJoined({"local", "Synthetic local", "client", true, true, true});
  page.show();
  for (const int count : {2, 3, 10}) {
    for (int index = provider->participants()->rowCount(); index < count; ++index)
      provider->simulateParticipantJoined({QString("remote-%1").arg(index), QString("Synthetic %1").arg(index),
                                           "practitioner", false, true, true});
    for (int index = 0; index < provider->participants()->rowCount(); ++index) {
      const auto id = provider->participants()->data(provider->participants()->index(index), ParticipantModel::IdRole).toString();
      provider->frameSource(id)->submitFrame(pattern(index % 2 == 0));
    }
    require(waitUntil([&] { const auto renderers = page.findChildren<RemoteVideoRenderer *>();
      return renderers.size() == count && std::all_of(renderers.begin(), renderers.end(),
          [](auto *renderer) { return renderer->isVisible() && renderer->isValid(); }); }), "populated hardware GL renderers missing");
    const auto renderers = page.findChildren<RemoteVideoRenderer *>();
    for (auto *renderer : renderers) {
      require(renderer->context() && renderer->context()->isValid(), "invalid GL context");
      renderer->makeCurrent();
      const auto *hardware = renderer->context()->functions()->glGetString(GL_RENDERER);
      require(hardware, "GL_RENDERER unavailable");
      const QString name = QString::fromLatin1(reinterpret_cast<const char *>(hardware));
      require(!name.contains("llvmpipe", Qt::CaseInsensitive) && !name.contains("softpipe", Qt::CaseInsensitive),
              "hardware gate used software GL");
      renderer->doneCurrent();
      const auto image = renderer->grabFramebuffer();
      require(!image.isNull(), "GL framebuffer capture empty");
      const auto pixel = image.pixelColor(image.width() / 2, image.height() / 2);
      require(pixel.red() > 220 || pixel.blue() > 220, "GL framebuffer did not contain source pattern");
      std::cout << "tiles=" << count << " hardware=" << name.toStdString() << " framebuffer="
                << image.width() << "x" << image.height() << '\n';
    }
    const auto tiles = page.findChildren<ParticipantTile *>();
    require(page.findChild<QWidget *>("notesToggleButton") == nullptr,
            "participant display role exposed client notes toggle");
    auto *stage = page.findChild<QWidget *>("videoStage");
    auto *controls = page.findChild<QWidget *>("controlBar");
    for (auto *tile : tiles) {
      const QRect stageGeometry(tile->mapTo(stage, QPoint()), tile->size());
      auto *renderer = tile->findChild<RemoteVideoRenderer *>();
      std::cout << "identity=" << tile->identity().toStdString() << " stage=" << stage->width() << 'x'
                << stage->height() << " tile=" << stageGeometry.x() << ',' << stageGeometry.y() << ','
                << stageGeometry.width() << ',' << stageGeometry.height() << " renderer="
                << renderer->x() << ',' << renderer->y() << ',' << renderer->width() << ',' << renderer->height() << '\n';
      require(stage->rect().contains(stageGeometry), "tile escaped full stage bounds");
      require(!stageGeometry.intersects(controls->geometry()), "tile covered controls");
      require(renderer->geometry() == tile->rect(), "renderer did not resize with its tile");
      require(tile->parentWidget()->rect().contains(tile->geometry()), "tile escaped stage");
      for (auto *other : tiles)
        if (tile != other && count > 2) require(!tile->geometry().intersects(other->geometry()), "group tiles overlap");
    }
    // Let the exposed window composite after per-widget FBO inspection.
    int swapped = 0;
    const auto connection = QObject::connect(renderers.front(), &QOpenGLWidget::frameSwapped,
                                             [&] { ++swapped; });
    page.update();
    for (auto *renderer : renderers) renderer->update();
    require(waitUntil([&] { return swapped > 0; }), "native window never composed a GL frame");
    QObject::disconnect(connection);
    // This is an explicitly labeled reconstruction, not a compositor screenshot.
    // QWidget::grab on this Qt/Wayland backend misplaces GL textures belonging
    // to different QWidget parents. Native geometry and GL pixels above are
    // verified independently; visible desktop composition is a manual gate.
    QImage artifact(page.size(), QImage::Format_ARGB32);
    artifact.fill(page.palette().color(QPalette::Window));
    {
      QPainter painter(&artifact);
      // Paint the PiP last, matching its raised position above the main tile.
      auto ordered = tiles;
      std::stable_sort(ordered.begin(), ordered.end(), [](auto *a, auto *b) {
        return !a->isLocal() && b->isLocal();
      });
      for (auto *tile : ordered) {
        auto *renderer = tile->findChild<RemoteVideoRenderer *>();
        std::cout << "artifact " << tile->identity().toStdString() << " renderer="
                  << renderer->mapTo(&page, QPoint()).x() << ',' << renderer->mapTo(&page, QPoint()).y()
                  << ',' << renderer->width() << ',' << renderer->height() << '\n';
        painter.drawImage(QRect(renderer->mapTo(&page, QPoint()), renderer->size()), renderer->grabFramebuffer());
        auto *label = tile->findChild<QLabel *>("participantName");
        std::cout << "label=" << label->width() << ',' << label->height() << " origin="
                  << label->mapTo(&page, QPoint()).x() << ',' << label->mapTo(&page, QPoint()).y() << '\n';
        painter.drawPixmap(label->mapTo(&page, QPoint()), label->grab());
      }
      painter.drawPixmap(controls->mapTo(&page, QPoint()), controls->grab());
    }
    require(artifact.save(directory + QString("/participants-%1-reconstruction.png").arg(count)),
            "native framebuffer reconstruction save failed");
  }
  std::cout << "populated native 1:1, 1:2 and ten-tile hardware GL/geometry passed\n"
               "ARTIFACTS: widget/framebuffer reconstructions, not desktop compositor screenshots\n";
}
} // namespace

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  try {
    if (app.arguments().contains("--room")) roomSmoke();
    else if (app.arguments().contains("--native-ui") && app.arguments().size() == 3) nativeUi(app.arguments().last());
    else throw std::runtime_error("usage: --room (disposable loopback server only) | --native-ui <capture-directory>");
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "synthetic runtime gate failed: " << error.what() << '\n';
    return 1;
  }
}
