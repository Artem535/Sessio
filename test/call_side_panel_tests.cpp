#include "call_side_panel.h"
#include <QApplication>
#include <QPointer>
#include <QSignalSpy>
#include <QStackedWidget>
#include <gtest/gtest.h>
#include <oclero/qlementine/widgets/SegmentedControl.hpp>

class CallSidePanelTest : public ::testing::Test {
protected:
  CallSidePanel panel{new QWidget, new QWidget};
  QStackedWidget *stack() { return panel.findChild<QStackedWidget *>(); }
  oclero::qlementine::SegmentedControl *switcher() {
    return dynamic_cast<oclero::qlementine::SegmentedControl *>(
        panel.findChild<oclero::qlementine::AbstractItemListWidget *>());
  }
};

TEST_F(CallSidePanelTest, DefaultsToNotes) {
  EXPECT_FALSE(panel.transcriptShown());
  ASSERT_NE(stack(), nullptr);
  EXPECT_EQ(stack()->currentIndex(), 0);
  EXPECT_EQ(switcher()->currentIndex(), 0);
}
TEST_F(CallSidePanelTest, SwitcherSwitchesPages) {
  QSignalSpy changed(&panel, &CallSidePanel::pageChanged);
  switcher()->setCurrentIndex(1);
  EXPECT_TRUE(panel.transcriptShown());
  EXPECT_EQ(stack()->currentIndex(), 1);
  ASSERT_EQ(changed.size(), 1);
  EXPECT_TRUE(changed.at(0).at(0).toBool());
  switcher()->setCurrentIndex(0);
  EXPECT_EQ(stack()->currentIndex(), 0);
  ASSERT_EQ(changed.size(), 2);
  EXPECT_FALSE(changed.at(1).at(0).toBool());
}
TEST_F(CallSidePanelTest, ShowTranscriptSelectsThePage) {
  QSignalSpy changed(&panel, &CallSidePanel::pageChanged);
  panel.showTranscript();
  EXPECT_TRUE(panel.transcriptShown());
  EXPECT_EQ(switcher()->currentIndex(), 1);
  EXPECT_EQ(stack()->currentIndex(), 1);
  EXPECT_EQ(changed.size(), 1);
  panel.showTranscript();
  EXPECT_EQ(changed.size(), 1);
  panel.showNotes();
  EXPECT_FALSE(panel.transcriptShown());
  EXPECT_EQ(switcher()->currentIndex(), 0);
}
TEST_F(CallSidePanelTest, HidingTranscriptionHidesSwitcherAndForcesNotes) {
  panel.showTranscript();
  QSignalSpy changed(&panel, &CallSidePanel::pageChanged);
  panel.setTranscriptionAvailable(false);
  EXPECT_TRUE(switcher()->isHidden());
  EXPECT_FALSE(panel.transcriptShown());
  EXPECT_EQ(stack()->currentIndex(), 0);
  EXPECT_EQ(switcher()->currentIndex(), 0);
  ASSERT_EQ(changed.size(), 1);
  EXPECT_FALSE(changed.at(0).at(0).toBool());
  panel.showTranscript();
  switcher()->setCurrentIndex(1);
  EXPECT_FALSE(panel.transcriptShown());
  EXPECT_EQ(switcher()->currentIndex(), 0);
}
TEST_F(CallSidePanelTest, ReenablingKeepsNotesSelected) {
  panel.showTranscript();
  panel.setTranscriptionAvailable(false);
  panel.setTranscriptionAvailable(true);
  EXPECT_FALSE(switcher()->isHidden());
  EXPECT_FALSE(panel.transcriptShown());
  EXPECT_EQ(stack()->currentIndex(), 0);
  EXPECT_EQ(switcher()->currentIndex(), 0);
}
TEST(CallSidePanelOwnershipTest, BothWidgetsAreReparentedIntoThePanel) {
  QWidget originalParent;
  QPointer<QWidget> notes = new QWidget(&originalParent);
  QPointer<QWidget> transcript = new QWidget(&originalParent);
  {
    CallSidePanel panel(notes, transcript);
    auto *stack = panel.findChild<QStackedWidget *>();
    ASSERT_NE(stack, nullptr);
    EXPECT_EQ(notes->parentWidget(), stack);
    EXPECT_EQ(transcript->parentWidget(), stack);
    EXPECT_EQ(stack->widget(0), notes.data());
    EXPECT_EQ(stack->widget(1), transcript.data());
  }
  EXPECT_TRUE(notes.isNull());
  EXPECT_TRUE(transcript.isNull());
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
