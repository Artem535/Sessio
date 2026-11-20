#include "call_control_icons.h"

#include <QGuiApplication>
#include <gtest/gtest.h>

using namespace pcm::widgets;

TEST(CallControlIconsTest, EveryIconIsNonNull) {
  EXPECT_FALSE(microphoneIcon(true).isNull());
  EXPECT_FALSE(microphoneIcon(false).isNull());
  EXPECT_FALSE(cameraIcon(true).isNull());
  EXPECT_FALSE(cameraIcon(false).isNull());
  EXPECT_FALSE(fullscreenIcon(true).isNull());
  EXPECT_FALSE(fullscreenIcon(false).isNull());
}

TEST(CallControlIconsTest, NotesIconIsNonNull) {
  const QIcon icon = pcm::widgets::notesIcon();
  EXPECT_FALSE(icon.isNull());
}

int main(int argc, char **argv) {
  // QPixmap (used internally by renderIcon()) requires a QGuiApplication to
  // exist before construction — without one it aborts with "Must construct
  // a QGuiApplication before a QPixmap". A plain QGuiApplication is enough
  // here (no widgets are involved), unlike e.g. busy_spinner_tests.cpp which
  // needs the full QApplication for its widget under test.
  QGuiApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
