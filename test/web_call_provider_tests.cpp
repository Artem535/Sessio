// Runs the real call page in QtWebEngine (offscreen) with the fake adapter, so
// the scheme handler, QWebChannel bridge, WebSocket audio path and AudioSink
// plumbing are exercised end to end without a Jitsi server.
#include "call_scheme_handler.h"
#include "web_call_provider.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QThread>

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <mutex>

using pcm::video::AudioSink;
using pcm::webcall::WebCallProvider;

namespace {

class RecordingSink final : public AudioSink {
public:
  void onAudio(const QString &id, const int16_t *samples, std::size_t count, int rate) override {
    std::lock_guard lock(mutex);
    EXPECT_EQ(rate, 16000);
    EXPECT_NE(samples, nullptr);
    perId[id] += count;
    ++calls;
  }
  std::size_t samplesFor(const QString &id) {
    std::lock_guard lock(mutex);
    auto it = perId.find(id);
    return it == perId.end() ? 0 : it->second;
  }
  std::size_t callCount() {
    std::lock_guard lock(mutex);
    return calls;
  }
  std::size_t distinctIds() {
    std::lock_guard lock(mutex);
    return perId.size();
  }
  std::mutex mutex;
  std::map<QString, std::size_t> perId;
  std::size_t calls = 0;
};

template <class Pred> bool waitFor(Pred pred, int ms) {
  QElapsedTimer t;
  t.start();
  while (!pred()) {
    if (t.elapsed() > ms) return false;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(5);
  }
  return true;
}

void pump(int ms) { waitFor([] { return false; }, ms); }

WebCallProvider::Options fakeOptions(int churnMs = 0) {
  WebCallProvider::Options o;
  o.adapter = QStringLiteral("fake");
  o.displayName = QStringLiteral("Tester");
  o.fakeParticipants = 2;
  o.fakeChurnMs = churnMs;
  return o;
}

constexpr std::size_t kOneSecond = 16000;

} // namespace

TEST(WebCallProvider, FakeCallDeliversPerParticipantAudio) {
  WebCallProvider provider(fakeOptions());
  auto sink = std::make_shared<RecordingSink>();
  QSignalSpy joined(&provider, &WebCallProvider::joined);
  provider.setAudioSink(sink);
  provider.join("https://jitsi.invalid/room-1", "jwt");
  ASSERT_TRUE(waitFor([&] { return sink->samplesFor("fake-1") >= kOneSecond && sink->samplesFor("fake-2") >= kOneSecond; },
                      10000));
  EXPECT_EQ(joined.count(), 1);
  EXPECT_NE(provider.callView(), nullptr);
  EXPECT_GE(provider.callElapsedMs(), 0);
  EXPECT_EQ(provider.participants()->remoteCount(), 2);
  provider.leave();
}

TEST(WebCallProvider, TrackChurnKeepsOneStream) {
  WebCallProvider provider(fakeOptions(300));
  auto sink = std::make_shared<RecordingSink>();
  QSignalSpy joinedSpy(&provider, &WebCallProvider::participantJoined);
  QSignalSpy leftSpy(&provider, &WebCallProvider::participantLeft);
  provider.setAudioSink(sink);
  provider.join("https://jitsi.invalid/room-1", "jwt");
  ASSERT_TRUE(waitFor([&] { return sink->samplesFor("fake-1") >= 2 * kOneSecond; }, 15000));
  pump(500);

  EXPECT_EQ(provider.participants()->remoteCount(), 2);
  EXPECT_EQ(leftSpy.count(), 0);
  QStringList remote;
  for (const auto &args : joinedSpy) {
    const QString id = args.at(0).toString();
    if (!id.endsWith("local")) remote << id;
  }
  remote.sort();
  EXPECT_EQ(remote, (QStringList{"fake-1", "fake-2"}));
  // local + two remotes, nothing else, each announced once
  EXPECT_EQ(joinedSpy.count(), 3);
  EXPECT_EQ(sink->distinctIds(), 3u);
  provider.leave();
}

TEST(WebCallProvider, NoAudioAfterSinkCleared) {
  WebCallProvider provider(fakeOptions());
  auto sink = std::make_shared<RecordingSink>();
  provider.setAudioSink(sink);
  provider.join("https://jitsi.invalid/room-1", "jwt");
  ASSERT_TRUE(waitFor([&] { return sink->samplesFor("fake-1") >= kOneSecond / 2; }, 10000));

  provider.setAudioSink(nullptr);
  const std::size_t calls = sink->callCount();
  pump(1000);
  EXPECT_EQ(sink->callCount(), calls);
  provider.leave();
}

TEST(WebCallProvider, RendererCrashEmitsConnectionLost) {
  WebCallProvider provider(fakeOptions());
  auto sink = std::make_shared<RecordingSink>();
  QSignalSpy lost(&provider, &WebCallProvider::connectionLost);
  provider.setAudioSink(sink);
  provider.join("https://jitsi.invalid/room-1", "jwt");
  ASSERT_TRUE(waitFor([&] { return sink->samplesFor("fake-1") >= kOneSecond / 2; }, 10000));

  provider.simulateRenderProcessTerminated();
  const std::size_t calls = sink->callCount();
  provider.simulateRenderProcessTerminated(); // a second notification must not repeat
  pump(1000);
  EXPECT_EQ(lost.count(), 1);
  EXPECT_EQ(sink->callCount(), calls);
}

TEST(WebCallProvider, BridgeReconnectsAfterDrop) {
  WebCallProvider provider(fakeOptions());
  auto sink = std::make_shared<RecordingSink>();
  provider.setAudioSink(sink);
  provider.join("https://jitsi.invalid/room-1", "jwt");
  ASSERT_TRUE(waitFor([&] { return sink->samplesFor("fake-1") >= kOneSecond / 2; }, 10000));

  provider.simulateAudioBridgeDrop();
  // Wait for the stall (best effort: a fast reconnect may hide it), then require growth.
  std::size_t last = sink->samplesFor("fake-1");
  waitFor(
      [&] {
        pump(300);
        const std::size_t now = sink->samplesFor("fake-1");
        const bool stalled = now == last;
        last = now;
        return stalled;
      },
      2000);
  const std::size_t base = sink->samplesFor("fake-1");
  EXPECT_TRUE(waitFor([&] { return sink->samplesFor("fake-1") >= base + kOneSecond / 2; }, 5000));
  provider.leave();
}

TEST(WebCallProvider, LeaveWithoutJoinEmitsLeft) {
  WebCallProvider provider(fakeOptions());
  QSignalSpy left(&provider, &WebCallProvider::left);
  provider.leave();
  ASSERT_TRUE(waitFor([&] { return left.count() == 1; }, 2000));
}

int main(int argc, char **argv) {
  pcm::webcall::registerCallScheme();
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
