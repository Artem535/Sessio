#include <ComboboxItemViewFilter.hpp>
#include <QApplication>
#include <QComboBox>
#include <QListView>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QScreen>
#include <QEventLoop>
#include <QTimer>
#include <gtest/gtest.h>

TEST(QlementineComboFilter, ChildAddedDoesNotLazilyCreateAnotherPopup) {
  QComboBox combo;
  QWidget popup;
  auto *view = new QListView(&popup);
  oclero::qlementine::ComboboxItemViewFilter filter(&combo, view);
  // Mirrors Qt constructing a child before the combobox popup is assigned.
  // Calling combo.view() from this event recursively creates another popup.
  QObject child(&combo);
  EXPECT_TRUE(combo.findChildren<QAbstractItemView *>().isEmpty());
}

TEST(QlementineComboFilter, StyledComboKeepsItsListHiddenUntilOpened) {
  QWidget window;
  window.setStyleSheet("QFrame#joinGroup { background-color: rgba(255,255,255,0.04); }");
  auto *layout = new QVBoxLayout(&window);
  auto *combo = new QComboBox(&window);
  combo->addItems({"Sessio invitation", "Jitsi link"});
  layout->addWidget(combo);
  window.resize(800, 180);
  window.show();
  QApplication::processEvents();
  EXPECT_FALSE(combo->view()->isVisible());
  EXPECT_TRUE(combo->view()->parentWidget()->isWindow());
  combo->showPopup();
  QApplication::processEvents();
  EXPECT_TRUE(combo->view()->isVisible());
  EXPECT_GE(combo->view()->height(), combo->view()->sizeHintForRow(0) * 2);
  if (qEnvironmentVariableIsSet("SESSIO_COMBO_CAPTURE")) {
    combo->view()->parentWidget()->grab().save(qEnvironmentVariable("SESSIO_COMBO_CAPTURE"));
  }
  combo->hidePopup();
}

TEST(QlementineComboFilter, PopupRowsAndSelectionSurviveStylesheetsAndReopening) {
  for (const bool styled : {false, true}) {
    for (const bool editable : {false, true}) {
      for (const int count : {2, 40}) {
        SCOPED_TRACE(::testing::Message() << "styled=" << styled
                     << " editable=" << editable << " count=" << count);
        QWidget window;
        if (styled) window.setStyleSheet("QWidget#card { background: #30343b; }");
        window.setObjectName("card");
        auto *layout = new QVBoxLayout(&window);
        auto *combo = new QComboBox(&window);
        combo->setEditable(editable);
        combo->setMaxVisibleItems(8);
        for (int i = 0; i < count; ++i) combo->addItem(QString("Device / provider %1").arg(i));
        layout->addWidget(combo);
        window.resize(500, 100);
        const QRect screen = window.screen()->availableGeometry();
        window.move(screen.left() + 20, screen.bottom() - 105);
        window.show();
        QApplication::processEvents();
        for (int opening = 0; opening < 2; ++opening) {
          combo->showPopup();
          QApplication::processEvents();
          EXPECT_GE(combo->view()->height(), combo->view()->sizeHintForRow(0) * std::min(count, 8));
          combo->hidePopup();
          QEventLoop animation;
          QTimer::singleShot(300, &animation, &QEventLoop::quit);
          animation.exec();
          EXPECT_FALSE(combo->view()->isVisible());
        }
        combo->setCurrentIndex(0);
        QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
        QApplication::sendEvent(combo, &down);
        EXPECT_EQ(combo->currentIndex(), 1);
      }
    }
  }
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setStyle(new oclero::qlementine::QlementineStyle(&app));
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
