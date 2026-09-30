#include "livekit_video_provider.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <iostream>
#include <thread>

namespace pcm::video {

// Exercise the same copied-value handlers used by the SDK delegate without
// borrowing SDK callback pointers or requiring devices/network participants.
struct LiveKitVideoProviderTestAccess {
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
  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, [] {
    std::cerr << "provider watchdog: hang detected" << std::endl;
    std::exit(1);
  });
  watchdog.start(25000);
  {
    pcm::video::LiveKitVideoProvider provider;
    if (provider.remoteVideoWidget() || provider.localVideoWidget()) return 1;
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
