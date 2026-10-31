#include "participant_model.h"
#include <QCoreApplication>
#include <QSignalSpy>
#include <gtest/gtest.h>

using namespace pcm::video;

TEST(ParticipantModelTest, UpsertsKeepIdentityOrderAndUpdateEveryRole) {
  ParticipantModel model;
  QSignalSpy count(&model, &ParticipantModel::remoteCountChanged);
  QSignalSpy changes(&model, &ParticipantModel::dataChanged);
  model.upsert({"local", "Me", "practitioner", true});
  model.upsert({"a", "First", "client"});
  model.upsert({"b", "Second", "client"});
  model.upsert({"a", "Updated", "guest", false, false, false});
  model.upsert({"a", "Updated", "guest", false, false, false});
  EXPECT_EQ(model.rowCount(), 3);
  EXPECT_EQ(model.remoteCount(), 2);
  EXPECT_EQ(count.count(), 2);
  EXPECT_EQ(changes.count(), 1);
  const auto row = model.index(1);
  EXPECT_EQ(model.data(row, ParticipantModel::IdRole).toString(), "a");
  EXPECT_EQ(model.data(row, ParticipantModel::DisplayNameRole).toString(), "Updated");
  EXPECT_EQ(model.data(row, ParticipantModel::ParticipantRole).toString(), "guest");
  EXPECT_FALSE(model.data(row, ParticipantModel::IsLocalRole).toBool());
  EXPECT_FALSE(model.data(row, ParticipantModel::MicrophoneEnabledRole).toBool());
  EXPECT_FALSE(model.data(row, ParticipantModel::CameraEnabledRole).toBool());
  ASSERT_TRUE(model.participant("a"));
  EXPECT_EQ(model.participant("a")->displayName, "Updated");
  EXPECT_FALSE(model.participant("unknown"));
}

TEST(ParticipantModelTest, RowObserversSeeCurrentPresenceCounts) {
  ParticipantModel model;
  QObject::connect(&model, &ParticipantModel::rowsInserted, &model,
                   [&]() { EXPECT_EQ(model.remoteCount(), 1); });
  QObject::connect(&model, &ParticipantModel::rowsRemoved, &model,
                   [&]() { EXPECT_EQ(model.remoteCount(), 0); });
  model.upsert({"a"});
  model.remove("a");
  model.upsert({"a"});
  QObject::connect(&model, &ParticipantModel::modelReset, &model,
                   [&]() { EXPECT_EQ(model.remoteCount(), 0); });
  model.clear();
}

TEST(ParticipantModelTest, RemovalAndClearNotifyOnlyChangedRemoteCounts) {
  ParticipantModel model;
  QSignalSpy count(&model, &ParticipantModel::remoteCountChanged);
  model.upsert({"local", {}, {}, true});
  model.upsert({"a"});
  model.remove("unknown");
  EXPECT_EQ(model.rowCount(), 2);
  model.upsert({"a", {}, {}, true});
  EXPECT_EQ(model.remoteCount(), 0);
  model.upsert({"b"});
  model.remove("b");
  model.upsert({"c"});
  model.clear();
  model.clear();
  EXPECT_EQ(model.rowCount(), 0);
  EXPECT_EQ(model.remoteCount(), 0);
  EXPECT_EQ(count.count(), 6);
  EXPECT_FALSE(model.data(model.index(0), ParticipantModel::IdRole).isValid());
  model.upsert({"local", {}, {}, true});
  EXPECT_EQ(model.rowCount(model.index(0)), 0);
}
