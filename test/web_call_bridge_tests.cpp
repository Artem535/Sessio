#include "participant_model.h"
#include "web_call_bridge.h"

#include <QSignalSpy>

#include <gtest/gtest.h>

using pcm::video::ParticipantModel;
using pcm::webcall::WebCallBridge;

TEST(WebCallBridge, JoinedEmitsJoined) {
  ParticipantModel model;
  WebCallBridge bridge(&model);
  QSignalSpy spy(&bridge, &WebCallBridge::joinedCall);
  bridge.joined("local-1");
  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.at(0).at(0).toString(), "local-1");
}

TEST(WebCallBridge, ParticipantJoinedUpsertsModelOnce) {
  ParticipantModel model;
  WebCallBridge bridge(&model);
  QSignalSpy spy(&bridge, &WebCallBridge::participantAdded);
  bridge.participantJoined("p1", "Ann", false);
  bridge.participantJoined("p1", "Ann", false);
  bridge.participantJoined("me", "Me", true);
  EXPECT_EQ(model.rowCount(), 2);
  EXPECT_EQ(spy.count(), 2);
  EXPECT_FALSE(model.participant("p1")->isLocal);
  EXPECT_TRUE(model.participant("me")->isLocal);
  EXPECT_EQ(model.participant("p1")->displayName, "Ann");
}

TEST(WebCallBridge, ParticipantLeftRemoves) {
  ParticipantModel model;
  WebCallBridge bridge(&model);
  QSignalSpy spy(&bridge, &WebCallBridge::participantRemoved);
  bridge.participantJoined("p1", "Ann", false);
  bridge.participantLeft("p1");
  bridge.participantLeft("p1");
  bridge.participantLeft("ghost");
  EXPECT_EQ(model.rowCount(), 0);
  EXPECT_EQ(spy.count(), 1);
}

TEST(WebCallBridge, MediaErrorIsNotFatal) {
  ParticipantModel model;
  WebCallBridge bridge(&model);
  QSignalSpy media(&bridge, &WebCallBridge::mediaErrorWith);
  QSignalSpy failed(&bridge, &WebCallBridge::joinFailedWith);
  QSignalSpy lost(&bridge, &WebCallBridge::connectionLostWith);
  bridge.mediaError("no mic");
  EXPECT_EQ(media.count(), 1);
  EXPECT_EQ(failed.count(), 0);
  EXPECT_EQ(lost.count(), 0);
}

TEST(WebCallBridge, ConnectionLostRelays) {
  ParticipantModel model;
  WebCallBridge bridge(&model);
  QSignalSpy spy(&bridge, &WebCallBridge::connectionLostWith);
  bridge.connectionLost("kicked");
  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.at(0).at(0).toString(), "kicked");
}
