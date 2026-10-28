#include "prompter_panel.h"
#include "call_notes_controller.h"
#include "call_notes_page.h"
#include "notes_widget_frame.h"
#include <QLabel>
#include <QScrollArea>
#include <QApplication>
#include <QDir>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTranslator>
#include <QTest>
#include <QToolButton>
#include <QPushButton>
#include <QQuickWidget>
#include <QQuickItem>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <oclero/qlementine/style/QlementineStyle.hpp>

TEST(PrompterPanelTest, NoTranscriptShowsStartControlWithoutInventedCards) {
  QTemporaryDir dir;
  pcm::config::Config conf{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir.path().toStdString())}};
  auto db = std::make_shared<pcm::database::Database>(conf);
  CallPrompterController controller(db);
  PrompterPanel panel(&controller, db);
  EXPECT_TRUE(panel.findChild<QPushButton *>("prompterStart"));
  EXPECT_TRUE(controller.cards().isEmpty());
  EXPECT_TRUE(panel.sharedHintWidget()->isHidden());
}
TEST(PrompterPanelTest, RealPanelKeepsReviewActionsAndSourceLinksAtNarrowWidth) {
  QTemporaryDir dir;
  pcm::config::Config conf{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir.path().toStdString())}};
  auto db = std::make_shared<pcm::database::Database>(conf);
  const auto id = db->add_transcript(std::nullopt, "live_local_v1");
  CallPrompterController controller(db); controller.beginTranscript(id); controller.resume();
  QSignalSpy request(&controller, &CallPrompterController::requestReady);
  DuckTranscriptPhrase a; a.transcript_id = id; a.track_role = "participant"; a.speaker_name = "Анна";
  a.start_ms = 31000; a.end_ms = 34000; a.text = "Я отправлю вам документы."; a.id = db->add_transcript_phrase(a);
  auto b = a; b.speaker_name = "Борис"; b.text = "Хорошо, жду документы."; b.start_ms = 35000; b.end_ms = 38000; b.id = db->add_transcript_phrase(b);
  controller.addPhrase(a); controller.addPhrase(b); controller.flush();
  auto reply = request[0][0].toJsonObject();
  const QJsonArray evidence{QString::number(a.id), QString::number(b.id)};
  reply["events"] = QJsonArray{QJsonObject{{"kind", "accept"}, {"action", "Анна отправит документы Борису"},
      {"participants", QJsonArray{"Анна", "Борис"}}, {"evidence_ids", evidence}, {"target_id", ""}, {"unresolved", false}}};
  reply["suggestions"] = QJsonArray{QJsonObject{{"text", "К какому дню будут готовы документы?"}, {"evidence_ids", evidence}}};
  reply["rejected"] = QJsonArray{}; controller.handleResult(reply);
  CallNotesController notes(db);
  CallNotesPage side(db, &notes);
  auto *panel = new PrompterPanel(&controller, db);
  EXPECT_FALSE(panel->findChild<QPushButton *>("prompterStart")->isEnabled());
  controller.pause();
  EXPECT_TRUE(panel->findChild<QPushButton *>("prompterStart")->isEnabled());
  controller.resume();
  EXPECT_FALSE(panel->findChild<QPushButton *>("prompterStart")->isEnabled());
  side.setPrompterPanel(panel); side.resize(420, 800); side.show();
  QCoreApplication::processEvents();
  // The prompter is a card on the notes page: no scroll of its own, pending
  // count in the chip, accepted agreements left to the Agreements card.
  auto *card = side.findChild<NotesWidgetFrame *>("prompterNotesCard");
  ASSERT_NE(card, nullptr);
  EXPECT_TRUE(card->isAncestorOf(panel));
  EXPECT_EQ(card->findChild<QLabel *>("notesWidgetStatus")->text(), CallNotesPage::tr("%n new", nullptr, 1));
  for (auto *scroll : panel->findChildren<QScrollArea *>()) EXPECT_TRUE(scroll->isHidden());
  for (auto *label : panel->findChildren<QLabel *>())
    if (label->text() == PrompterPanel::tr("Accepted agreements")) EXPECT_FALSE(label->isVisibleTo(panel));
  EXPECT_TRUE(panel->isAncestorOf(panel->sharedHintWidget()));
  auto *quick = side.findChild<QQuickWidget *>("notesQuickWorkspace");
  ASSERT_NE(quick, nullptr);
  ASSERT_NE(quick->rootObject(), nullptr);
  auto *qmlPrompter = quick->rootObject()->findChild<QQuickItem *>("qmlPrompterSection");
  ASSERT_NE(qmlPrompter, nullptr);
  EXPECT_TRUE(qmlPrompter->isVisible());
  const auto info = quick->rootObject()->property("info").toMap();
  EXPECT_TRUE(info.value("prompterVisible").toBool());
  EXPECT_EQ(info.value("prompterHint").toString(), QStringLiteral("К какому дню будут готовы документы?"));
  EXPECT_EQ(info.value("prompterCards").toList().size(), 1);
  QSignalSpy sources(panel, &PrompterPanel::sourceRequested);
  const auto buttons = panel->findChildren<QPushButton *>();
  bool found = false;
  for (auto *button : buttons) if (button->text().contains("0:31")) { button->click(); found = true; break; }
  ASSERT_TRUE(found); EXPECT_EQ(sources.count(), 1);
  EXPECT_TRUE(panel->findChild<QPushButton *>("accept")->isEnabled());
  const auto screenshots = qEnvironmentVariable("SESSIO_PROMPTER_SCREENSHOT_DIR");
  if (!screenshots.isEmpty()) {
    QDir().mkpath(screenshots);
    auto *style = dynamic_cast<oclero::qlementine::QlementineStyle *>(QApplication::style());
    ASSERT_NE(style, nullptr);
    QTest::qWait(200);  // let the page lay out before grabbing
    ASSERT_TRUE(side.grab().save(QDir(screenshots).filePath("prompter-light.png")));
    style->setThemeJsonPath(QStringLiteral(SESSIO_SOURCE_DIR) + "/resources/themes/qlementine_dark.json");
    QTest::qWait(300);
    ASSERT_TRUE(side.grab().save(QDir(screenshots).filePath("prompter-dark.png")));
  }
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  // QApplication owns and deletes the style; the static marks that it is not leaked.
  static oclero::qlementine::QlementineStyle *style = nullptr;
  style = new oclero::qlementine::QlementineStyle(&app);
  app.setStyle(style);
  QTranslator ru;
  if (!qEnvironmentVariable("SESSIO_PROMPTER_SCREENSHOT_DIR").isEmpty() &&
      ru.load(QCoreApplication::applicationDirPath() + "/../app_ru.qm")) app.installTranslator(&ru);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
