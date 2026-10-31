#include "meeting_join.h"

#include <gtest/gtest.h>

using pcm::meeting::JoinAction;
using pcm::meeting::resolveJoinAction;

namespace {
DuckEvent makeEvent(const std::string &providerKind, const std::string &ref, const std::string &url) {
  DuckEvent e;
  e.provider_kind = providerKind;
  e.meeting_ref = ref;
  e.meeting_url = url;
  return e;
}
} // namespace

TEST(MeetingJoinTest, seriesTargetWins) {
  const auto e = makeEvent("LiveKit", "ref-1", "https://x.invalid/a");
  const JoinAction a = resolveJoinAction(e, QStringLiteral("series-room"));
  EXPECT_EQ(a.kind, JoinAction::Kind::LiveKit);
  EXPECT_EQ(a.target, QStringLiteral("series-room"));
}

TEST(MeetingJoinTest, liveKitUsesMeetingRef) {
  const auto e = makeEvent("LiveKit", "ref-1", "https://x.invalid/a");
  const JoinAction a = resolveJoinAction(e, std::nullopt);
  EXPECT_EQ(a.kind, JoinAction::Kind::LiveKit);
  EXPECT_EQ(a.target, QStringLiteral("ref-1"));
}

TEST(MeetingJoinTest, jitsiJoinsThroughBackendByMeetingRef) {
  const auto e = makeEvent("Jitsi", "ref-2", "https://x.invalid/a");
  const JoinAction a = resolveJoinAction(e, std::nullopt);
  EXPECT_EQ(a.kind, JoinAction::Kind::LiveKit);
  EXPECT_EQ(a.target, QStringLiteral("ref-2"));
}

TEST(MeetingJoinTest, externalUsesUrl) {
  const auto e = makeEvent("ExternalUrl", "", "https://x.invalid/a");
  const JoinAction a = resolveJoinAction(e, std::nullopt);
  EXPECT_EQ(a.kind, JoinAction::Kind::Url);
  EXPECT_EQ(a.target, QStringLiteral("https://x.invalid/a"));
}
TEST(MeetingJoinTest, ExternalJitsiUsesEmbeddedUrlWithoutBackendReference) {
  const auto e = makeEvent("ExternalJitsi", "", "https://other.test/prefix/room");
  const auto action = resolveJoinAction(e, std::nullopt);
  EXPECT_EQ(action.kind, JoinAction::Kind::JitsiUrl);
  EXPECT_EQ(action.target, "https://other.test/prefix/room");
}

TEST(MeetingJoinTest, offlineIsNone) {
  const auto e = makeEvent("", "", "");
  EXPECT_EQ(resolveJoinAction(e, std::nullopt).kind, JoinAction::Kind::None);
}

TEST(MeetingJoinTest, emptySeriesTargetIsNoneEvenWithRefAndUrl) {
  const auto e = makeEvent("LiveKit", "ref-1", "https://x.invalid/a");
  EXPECT_EQ(resolveJoinAction(e, QString()).kind, JoinAction::Kind::None);
}
