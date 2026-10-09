#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QScreen>
#include <QVBoxLayout>
#include <QWidget>
#include <gtest/gtest.h>
#include <oclero/qlementine/style/QlementineStyle.hpp>

// #144: on Windows a combo in the lower part of the screen opened a list a
// few pixels high, because qlementine bounded it by "screen height - 128 -
// view y". The list must hold its rows wherever the combo sits.
TEST(ComboPopupTest, ListNearTheBottomOfTheScreenStillShowsItsRows) {
  QWidget window;
  auto *layout = new QVBoxLayout(&window);
  auto *combo = new QComboBox(&window);
  for (const char *name : {"Speakers (Realtek(R) Audio)", "Headphones", "HDMI output", "USB headset"})
    combo->addItem(QString::fromUtf8(name));
  layout->addWidget(combo);
  const QRect screen = QApplication::primaryScreen()->geometry();
  // Low enough that the old bound came out small but positive (a negative one was
  // already skipped by the previous patch).
  window.setGeometry(screen.x() + 40, screen.bottom() - 220, 320, 60);
  window.show();
  QApplication::processEvents();

  combo->showPopup();
  QApplication::processEvents();

  auto *view = combo->view();
  int rows = 0;
  for (int i = 0; i < combo->count(); ++i) rows += view->sizeHintForRow(i);
  EXPECT_GE(view->height(), rows);
  combo->hidePopup();
}

TEST(ComboPopupTest, LongListStopsAtMaxVisibleItemsAndScrolls) {
  QComboBox combo;
  for (int i = 0; i < 40; ++i) combo.addItem(QStringLiteral("Device %1").arg(i));
  combo.setMaxVisibleItems(5);
  combo.show();
  combo.showPopup();
  QApplication::processEvents();

  auto *view = combo.view();
  EXPECT_LE(view->height(), view->sizeHintForRow(0) * 6);
  EXPECT_GE(view->height(), view->sizeHintForRow(0) * 4);
  combo.hidePopup();
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setStyle(new oclero::qlementine::QlementineStyle(&app));
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
