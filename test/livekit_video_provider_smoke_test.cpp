#include "livekit_video_provider.h"
#include "audio_capture_adapter.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <iostream>
#include <thread>

namespace pcm::video {

// Exercise the same copied-value handlers used by the SDK delegate without
// borrowing SDK callback pointers or requiring devices/network participants.
struct LiveKitVideoProviderTestAccess {
  static bool failureMuteNotificationCancelsCompletion(bool destroy) {
    class Source final : public AudioCaptureSource {
    public:
      bool fails = false;
      QIODevice *start() override {
        buffer.open(QIODevice::ReadOnly);
        return fails ? nullptr : &buffer;
      }
      void stop() override { buffer.close(); }
      QBuffer buffer;
    };
    auto provider = std::make_unique<LiveKitVideoProvider>();
    int opens = 0;
    provider->mAudioCapture = std::make_unique<AudioCaptureAdapter>([&](const QAudioDevice &) {
      auto source = std::make_unique<Source>();
      source->fails = ++opens > 1;
      return source;
    });
    if (!provider->mAudioCapture->start({})) return false;
    provider->mRoom = std::make_unique<livekit::Room>();
    provider->mLocalIdentity = QStringLiteral("local");
    provider->participants()->upsert({QStringLiteral("local"), {}, {}, true});
    int completions = 0, notifications = 0;
    QObject::connect(provider.get(), &VideoProvider::microphoneChanged,
                     [&](const QAudioDevice &) { ++completions; });
    QObject::connect(provider->participants(), &QAbstractItemModel::dataChanged, [&] {
      ++notifications;
      if (destroy) provider.reset();
      else provider->leave();
    });
    provider->switchMicrophone({});
    QCoreApplication::processEvents();
    return opens == 3 && notifications == 1 && completions == 0 &&
           (destroy ? !provider : !provider->mRoom);
  }
  static bool destructionDuringMicrophoneResumeCancelsTransition() {
    class Source final : public AudioCaptureSource {
    public:
      QIODevice *start() override { buffer.open(QIODevice::ReadOnly); return &buffer; }
      void stop() override { buffer.close(); }
      QBuffer buffer;
    };
    auto provider = std::make_unique<LiveKitVideoProvider>();
    provider->mAudioCapture = std::make_unique<AudioCaptureAdapter>([](const QAudioDevice &) {
      return std::make_unique<Source>();
    });
    provider->mRoom = std::make_unique<livekit::Room>();
    QObject::connect(provider->mAudioCapture.get(), &AudioCaptureAdapter::captureResumed,
                     [&] { provider.reset(); });
    provider->switchMicrophone({});
    QCoreApplication::processEvents();
    return !provider;
  }
  static bool microphoneSwitchesCoalesceAndLeaveCancelsPending() {
    class Source final : public AudioCaptureSource {
    public:
      QIODevice *start() override { buffer.open(QIODevice::ReadOnly); return &buffer; }
      void stop() override { buffer.close(); }
      QBuffer buffer;
    };
    LiveKitVideoProvider provider;
    int opens = 0;
    provider.mAudioCapture = std::make_unique<AudioCaptureAdapter>([&](const QAudioDevice &) {
      ++opens;
      return std::make_unique<Source>();
    });
    provider.mRoom = std::make_unique<livekit::Room>();
    provider.mLocalIdentity = QStringLiteral("local");
    const auto published = provider.mAudioCapture->audioSource();
    provider.setMicrophoneEnabled(false);
    provider.switchMicrophone({});
    provider.switchMicrophone({});
    provider.switchMicrophone({});
    QCoreApplication::processEvents();
    if (opens != 1 || provider.mAudioCapture->audioSource() != published ||
        provider.isMicrophoneEnabled() || provider.mLocalIdentity != QStringLiteral("local"))
      return false;
    provider.switchMicrophone({});
    provider.leave();
    QCoreApplication::processEvents();
    if (opens != 1 || provider.mRoom) return false;
    provider.mRoom = std::make_unique<livekit::Room>();
    QObject::connect(provider.mAudioCapture.get(), &AudioCaptureAdapter::captureResumed,
                     &provider, [&] { provider.leave(); });
    provider.switchMicrophone({});
    QCoreApplication::processEvents();
    return opens == 2 && !provider.mRoom && !provider.mAudioCapture->activeDevice();
  }

  static bool terminalDisconnectCancelsQueuedJoined() {
    LiveKitVideoProvider provider;
    provider.mRoom = std::make_unique<livekit::Room>();
    provider.applyParticipant({{"joining", {}, {}, false}, "P"}, true);
    const auto generation = provider.mGeneration;
    int joins = 0, losses = 0, leaves = 0;
    QString reason;
    QObject::connect(&provider, &VideoProvider::joined, &provider, [&] { ++joins; });
    QObject::connect(&provider, &VideoProvider::connectionLost, &provider,
                     [&](const QString &value) { ++losses; reason = value; });
    QObject::connect(&provider, &VideoProvider::left, &provider, [&] { ++leaves; });
    // Same copied terminal handler as onDisconnected, before the success
    // queued by join after publication. No live room or device is required.
    std::thread worker([&] {
      provider.queueCallback(generation, [](LiveKitVideoProvider &target) {
        target.teardown();
        emit target.connectionLost(QStringLiteral("Room disconnected (reason code %1).").arg(1));
      });
      provider.queueCallback(generation, [](LiveKitVideoProvider &target) { emit target.joined(); });
    });
    worker.join();
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    const bool terminal = losses == 1 && joins == 0 && !provider.mRoom &&
        provider.mPendingCallbacks.empty() && provider.participants()->rowCount() == 0 &&
        !provider.frameSource("joining") && reason == QStringLiteral("Room disconnected (reason code 1).");
    provider.leave(); // Failed entry performs another teardown after the callback.
    provider.leave();
    QCoreApplication::processEvents();
    const bool ok = terminal && joins == 0 && losses == 1 && leaves == 2 &&
        !provider.mRoom && provider.participants()->rowCount() == 0;
    if (!ok) std::cerr << "terminal disconnect failed to cancel queued joined or repeated teardown" << std::endl;
    return ok;
  }

  static bool replacementParticipantSidResetsMedia() {
    LiveKitVideoProvider provider;
    ParticipantSnapshot participant{{"replace", {}, {}, false, true, true}, "P-old"};
    provider.applyParticipant(participant, true);
    auto *source = provider.frameSource("replace");
    QImage frame(4, 4, QImage::Format_RGBA8888);
    frame.fill(Qt::red);
    source->submitFrame(frame);
    participant.sid = "P-new";
    participant.value.microphoneEnabled = false;
    participant.value.cameraEnabled = false;
    provider.applyParticipant(participant, true); // replacement without departure
    const auto current = provider.participants()->participant("replace");
    const bool ok = current && !current->microphoneEnabled && !current->cameraEnabled &&
        source == provider.frameSource("replace") && source->latestFrame().isNull() &&
        provider.participants()->remoteCount() == 1;
    if (!ok) std::cerr << "new participant SID inherited predecessor media flags" << std::endl;
    return ok;
  }

  static bool unsubscribeHistoryRejectsOldSubscribe(livekit::TrackKind kind) {
    LiveKitVideoProvider provider;
    provider.applyParticipant({{"history", {}, {}, false, false, false}, "P"}, true);
    const auto enabled = [&] {
      const auto participant = provider.participants()->participant("history");
      return kind == livekit::TrackKind::KIND_VIDEO ? participant->cameraEnabled
                                                   : participant->microphoneEnabled;
    };
    TrackSnapshot first{"history", "P", "T1", kind, false, {}};
    provider.applySubscribed(first);
    provider.applyUnsubscribed(first);
    provider.applySubscribed(first); // same publication resubscription is legitimate
    if (!enabled()) {
      std::cerr << "same publication resubscription rejected" << std::endl;
      return false;
    }
    provider.applyUnsubscribed(first);
    auto successor = first;
    successor.sid = "T2";
    successor.muted = true;
    provider.applySubscribed(successor);
    QImage frame(4, 4, QImage::Format_RGBA8888);
    frame.fill(Qt::blue);
    auto *source = provider.frameSource("history");
    source->submitFrame(frame);
    provider.applySubscribed(first); // T1 was unsubscribed before T2 arrived
    if (enabled() || source->latestFrame().isNull()) {
      std::cerr << "old " << (kind == livekit::TrackKind::KIND_VIDEO ? "video" : "audio")
                << " subscription replaced successor after unsubscribe" << std::endl;
      return false;
    }
    provider.applyMuted(successor, false);
    provider.applyUnsubscribed(successor);
    if (enabled()) {
      std::cerr << "successor SID no longer handles unsubscribe" << std::endl;
      return false;
    }
    successor.muted = false;
    provider.applySubscribed(successor);
    if (!enabled()) {
      std::cerr << "successor same publication resubscription rejected" << std::endl;
      return false;
    }
    return provider.participants()->remoteCount() == 1;
  }

  static bool run(LiveKitVideoProvider &provider) {
    const auto check = [](bool ok, const char *message) {
      if (!ok) std::cerr << message << std::endl;
      return ok;
    };
    int joins = 0, leaves = 0;
    QObject::connect(&provider, &VideoProvider::participantJoined, &provider,
                     [&joins] { ++joins; });
    QObject::connect(&provider, &VideoProvider::participantLeft, &provider,
                     [&leaves] { ++leaves; });
    bool sourceBeforeRow = false;
    QObject::connect(provider.participants(), &QAbstractItemModel::rowsInserted,
                     &provider, [&] {
      sourceBeforeRow = provider.frameSource("a") != nullptr;
    });
    ParticipantSnapshot a{{"a", "Synthetic A", {}, false, false, false}, "PA"};
    ParticipantSnapshot b{{"b", "Synthetic B", "client", false, false, false}, "PB"};
    provider.applyParticipant(a, true);
    auto *sourceA = provider.frameSource("a");
    provider.applyParticipant(a, true);
    provider.applyParticipant(b, true);
    if (!check(sourceBeforeRow && sourceA && sourceA != provider.frameSource("b"),
               "sources must exist before rows, be stable and independent")) return false;
    if (!check(joins == 2 && provider.participants()->remoteCount() == 2,
               "duplicate presence must be ignored; audio-only remotes counted")) return false;
    if (!check(provider.participants()->participant("a")->role.isEmpty(),
               "absent legacy role metadata must be accepted")) return false;
    a.value.displayName = "Synthetic updated";
    a.value.role = "practitioner";
    provider.applyParticipant(a, false);
    if (!check(provider.participants()->participant("a")->displayName == a.value.displayName,
               "copied metadata update missing")) return false;

    TrackSnapshot video{"a", "PA", "V1", livekit::TrackKind::KIND_VIDEO, false, {}};
    provider.applySubscribed(video);
    QImage red(4, 4, QImage::Format_RGBA8888);
    red.fill(Qt::red);
    sourceA->submitFrame(red);
    auto replacement = video;
    replacement.sid = "V2";
    provider.applySubscribed(replacement);
    if (!check(sourceA == provider.frameSource("a") && sourceA->latestFrame().isNull(),
               "replacement must clear frame and preserve source")) return false;
    sourceA->submitFrame(red);
    provider.applySubscribed(video); // delayed old subscribe cannot replace V2
    if (!check(!sourceA->latestFrame().isNull(),
               "old subscribed SID replaced successor stream")) return false;
    provider.applyMuted(video, true);
    provider.applyUnsubscribed(video);
    if (!check(provider.participants()->participant("a")->cameraEnabled,
               "old SID changed replacement camera state")) return false;
    provider.applyMuted(replacement, true);
    if (!check(!provider.participants()->participant("a")->cameraEnabled &&
               provider.participants()->remoteCount() == 2, "mute removed presence")) return false;
    provider.applyMuted(replacement, false);
    provider.applyUnsubscribed(replacement);
    provider.applyParticipant(a, false); // display metadata cannot undo unsubscribe
    if (!check(!provider.participants()->participant("a")->cameraEnabled &&
               provider.participants()->remoteCount() == 2, "unsubscribe removed presence")) return false;
    TrackSnapshot audioA{"a", "PA", "A1", livekit::TrackKind::KIND_AUDIO, false, {}};
    TrackSnapshot audioB{"b", "PB", "B1", livekit::TrackKind::KIND_AUDIO, false, {}};
    provider.applySubscribed(audioA);
    provider.applySubscribed(audioB);
    provider.applyUnsubscribed(audioA);
    if (!check(!provider.participants()->participant("a")->microphoneEnabled &&
               provider.participants()->participant("b")->microphoneEnabled,
               "one audio unsubscribe affected another participant")) return false;

    provider.applyDeparture("a", "PA");
    provider.applySubscribed(replacement);
    provider.applyParticipant(a, false);
    if (!check(!provider.participants()->participant("a") &&
               provider.participants()->remoteCount() == 1, "late track resurrected departure")) return false;
    a.sid = "PA-new";
    provider.applyParticipant(a, true);
    provider.applyDeparture("a", "PA");
    provider.applySubscribed(replacement);
    if (!check(provider.participants()->remoteCount() == 2 &&
               !provider.participants()->participant("a")->cameraEnabled,
               "old participant SID affected rejoin")) return false;
    provider.applyDeparture("unknown", "unknown");
    if (!check(leaves == 1, "unknown departure emitted signal")) return false;

    provider.mRoom = std::make_unique<livekit::Room>();
    const auto oldGeneration = provider.mGeneration;
    auto captured = std::make_shared<int>(42);
    std::weak_ptr<int> capturedLifetime = captured;
    std::thread worker([&] {
      provider.queueCallback(oldGeneration, [a, captured](LiveKitVideoProvider &target) {
        target.applyParticipant(a, true);
      });
    });
    worker.join();
    captured.reset();
    provider.leave();
    if (!check(capturedLifetime.expired(),
               "teardown retained callback payload until Qt event delivery")) return false;
    provider.mRoom = std::make_unique<livekit::Room>();
    QCoreApplication::processEvents();
    if (!check(provider.participants()->rowCount() == 0,
               "old queued generation populated a new room")) return false;
    provider.leave();
    return check(provider.frameSource("a") == nullptr, "teardown retained sources");
  }
};

} // namespace pcm::video

int main(int argc, char *argv[]) {
  // No QApplication: constructing a QWidget here aborts, proving the production
  // provider no longer owns either local or remote presentation.
  QCoreApplication app(argc, argv);
  if (!pcm::video::LiveKitVideoProviderTestAccess::failureMuteNotificationCancelsCompletion(false)) {
    std::cerr << "failure-path mute notification emitted stale completion after leave\n";
    return 1;
  }
  if (!pcm::video::LiveKitVideoProviderTestAccess::failureMuteNotificationCancelsCompletion(true)) {
    std::cerr << "failure-path mute notification did not cancel provider destruction\n";
    return 1;
  }
  if (app.arguments().contains(QStringLiteral("--microphone-failure-only"))) {
    std::cout << "failure-path leave and destruction passed\n";
    return 0;
  }
  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, [] {
    std::cerr << "provider watchdog: hang detected" << std::endl;
    std::exit(1);
  });
  watchdog.start(25000);
  if (!pcm::video::LiveKitVideoProviderTestAccess::destructionDuringMicrophoneResumeCancelsTransition())
    return 1;
  if (!pcm::video::LiveKitVideoProviderTestAccess::microphoneSwitchesCoalesceAndLeaveCancelsPending()) {
    std::cerr << "microphone transition did not coalesce/preserve mute/cancel on leave\n";
    return 1;
  }
  if (!pcm::video::LiveKitVideoProviderTestAccess::terminalDisconnectCancelsQueuedJoined()) return 1;
  const bool participantReplacement =
      pcm::video::LiveKitVideoProviderTestAccess::replacementParticipantSidResetsMedia();
  const bool videoHistory =
      pcm::video::LiveKitVideoProviderTestAccess::unsubscribeHistoryRejectsOldSubscribe(
          livekit::TrackKind::KIND_VIDEO);
  const bool audioHistory =
      pcm::video::LiveKitVideoProviderTestAccess::unsubscribeHistoryRejectsOldSubscribe(
          livekit::TrackKind::KIND_AUDIO);
  if (!participantReplacement || !videoHistory || !audioHistory) return 1;
  {
    pcm::video::LiveKitVideoProvider provider;
    if (!pcm::video::LiveKitVideoProviderTestAccess::run(provider)) return 1;
    provider.setMicrophoneEnabled(false);
    provider.setCameraEnabled(false);
    if (provider.isMicrophoneEnabled() || provider.isCameraEnabled()) return 1;
    provider.setMicrophoneEnabled(true);
    provider.setCameraEnabled(true);
    if (!provider.isMicrophoneEnabled() || !provider.isCameraEnabled()) return 1;
  }
  for (int cycle = 0; cycle < 5; ++cycle) {
    pcm::video::LiveKitVideoProvider provider;
    QEventLoop loop;
    QObject::connect(&provider, &pcm::video::VideoProvider::joinFailed, &loop, &QEventLoop::quit);
    provider.join("wss://127.0.0.1:1", "not-a-real-token");
    loop.exec();
    provider.leave();
  }
  std::cout << "provider copied handlers, generation/SID guards, zero widgets and 5 failed joins passed"
            << std::endl;
}
