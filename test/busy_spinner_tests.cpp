#include "busy_spinner.h"

#include <QApplication>
#include <gtest/gtest.h>

using pcm::widgets::BusySpinner;

TEST(BusySpinnerTest, NotAnimatingBeforeShown) {
  BusySpinner spinner;
  EXPECT_FALSE(spinner.isAnimating());
}

TEST(BusySpinnerTest, AnimatesWhileShownAndStopsWhenHidden) {
  BusySpinner spinner;
  spinner.show();
  EXPECT_TRUE(spinner.isAnimating());

  spinner.hide();
  EXPECT_FALSE(spinner.isAnimating());
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
