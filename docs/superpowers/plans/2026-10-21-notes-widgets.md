# Notes Widgets Page Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the call side panel's Notes tab (ClientNotesPage with an embedded CallNotesPanel) with a single-column page of self-contained widgets plus a «Эта встреча | История» switch, per `docs/superpowers/specs/2026-10-21-notes-widgets-design.md`.

**Architecture:** A new `CallNotesPage` owns the page scroll and stacks widgets built on one `NotesWidgetFrame` card (title, status chip, body, labelled actions, «⋯» menu). The AI summary, key points and agreements become separate widgets over the existing `CallNotesController` / `CallPrompterController` data. «Мои заметки» reuses `ClientNotesPage` in a new embedded mode. History reads past call materials through one new read-only DB query. No schema change.

**Tech Stack:** C++20, Qt 6 Widgets, qlementine style, DuckDB (`pcm::database::Database`), GoogleTest + QtTest.

## Global Constraints

- No schema change, no migration. Only a new read query `Database::get_call_materials_for_client`.
- Two-space indentation, `PascalCase` classes, `camelCase` Qt methods, `snake_case` database APIs.
- Every user-visible string goes through `tr()`; English source strings, Russian in `translation/app_ru.ts`.
- Build and test with at most 4 jobs: `--parallel 4`, `ctest -j4`.
- Build tree: `build-release` inside the Ubuntu distrobox (already configured with `PCM_BUILD_TESTS=ON`). Configure once more after adding new files/targets: `cmake build-release`.
- GUI tests run offscreen: `QT_QPA_PLATFORM=offscreen`.
- Logs must not contain note text, names or meeting URLs.
- Run `git diff --check` before every commit. End commit messages with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Reference `(#151)` in titles.
- Do not kill or restart a running Sessio.

## File Structure

Created in `src/pages/transcript_page/` (all added to `Sessio_transcription_ui` in its `CMakeLists.txt`):

- `notes_widget_frame.h/.cpp` — `NotesWidgetFrame`, the shared card.
- `call_notes_sources.h/.cpp` — source-link button builder moved out of `CallNotesPanel`.
- `ai_summary_widget.h/.cpp` — «Итог ИИ».
- `key_points_widget.h/.cpp` — «Ключевые пункты».
- `agreements_widget.h/.cpp` — «Договорённости».
- `meeting_history.h/.cpp` — `MeetingRecord` + loaders (no widgets).
- `meeting_history_widgets.h/.cpp` — `PreviousMeetingWidget`, `MeetingHistoryList`.
- `call_notes_page.h/.cpp` — `CallNotesPage` (switch, scroll, widget column, «Встреча» card).

Modified:

- `src/database/database.h`, `src/database/call_material_database.cpp` — new query.
- `src/pages/transcript_page/call_notes_controller.h/.cpp` — no accept step; agreements no longer touch the editor text.
- `src/pages/client_notes_page/client_notes_page.h/.cpp` — `setEmbedded(bool)`.
- `src/app/application.cpp` — wire `CallNotesPage` for the live call and the saved-materials dialog.
- Deleted at the end: `call_notes_panel.h/.cpp`, `test/call_notes_panel_tests.cpp`.

Tests (new executables registered in `test/CMakeLists.txt`):

- `test/notes_widgets_tests.cpp` — frame, AI summary, key points, agreements.
- `test/meeting_history_tests.cpp` — loaders and history widgets.
- `test/call_notes_page_tests.cpp` — page assembly and embedded editor.
- Existing: `test/call_notes_controller_tests.cpp`, `test/call_material_database_tests.cpp` (included by `database_transcript_tests.cpp`), `test/client_notes_page_ui_tests.cpp`.

Shared test fixture used by Tasks 4–5 and 9 (copy it into each new test file; it mirrors the existing `CallNotesPanelTest::generate`):

```cpp
// Produces a controller with one ready live draft and one key point.
struct NotesFixture {
  QTemporaryDir dir;
  pcm::config::Config conf{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir.path().toStdString())}};
  std::shared_ptr<pcm::database::Database> db = std::make_shared<pcm::database::Database>(conf);
  CallNotesController controller{db};
  DuckTranscriptPhrase phrase;
  void generate(const QString &text = QStringLiteral("Prepare a short outline."),
                const QString &pointText = QStringLiteral("Outline is due")) {
    const auto id = db->add_transcript(std::nullopt, "live_local_v1");
    controller.beginCall(); controller.attachTranscript(id); controller.setGenerationEnabled(true);
    phrase.transcript_id = id; phrase.track_role = "participant";
    phrase.speaker_name = "Participant"; phrase.text = "I will prepare a short outline.";
    phrase.start_ms = 73000; phrase.end_ms = 76000;
    phrase.id = db->add_transcript_phrase(phrase);
    QSignalSpy requests(&controller, &CallNotesController::requestReady);
    controller.addPhrase(phrase); controller.flush();
    ASSERT_FALSE(requests.isEmpty());
    auto reply = requests.last().first().toJsonObject();
    const QJsonObject note{{"text", text}, {"evidence_ids", QJsonArray{QString::number(phrase.id)}}};
    auto point = note; point["text"] = pointText;
    reply["notes"] = QJsonObject{{"note", note}, {"key_points", QJsonArray{point}}};
    controller.handleResult(reply);
    ASSERT_EQ(controller.draft()["text"].toString(), text);
  }
};
```

Every GUI test file ends with:

```cpp
int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  app.setStyle(new oclero::qlementine::QlementineStyle(&app));
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

---

### Task 1: Call materials of a client

**Files:**
- Modify: `src/database/database.h:131` (next to `get_call_materials()`)
- Modify: `src/database/call_material_database.cpp:160` (after `get_call_materials()`)
- Test: `test/call_material_database_tests.cpp`

**Interfaces:**
- Produces: `std::vector<CallMaterialState> Database::get_call_materials_for_client(int64_t client_id);` — materials linked to the client through `EventClient` (via `CallMaterial.event_id`) or `TranscriptClient` (via `CallMaterialTranscript`), each once, ordered `created_at DESC, id DESC`. Empty for `client_id <= 0`.

- [ ] **Step 1: Write the failing test** (append to `test/call_material_database_tests.cpp`)

```cpp
TEST_F(TranscriptDbTest, CallMaterialsForClientIncludeEventAndTranscriptLinks) {
  const auto clients = db_->get_clients();
  ASSERT_GE(clients.size(), 2u);
  const auto client = clients[0]->id, other = clients[1]->id;
  const auto event = makeEvent();
  DuckEvent stored = *db_->get_event(event);
  ASSERT_TRUE(db_->update_event_clients(event, {client}));
  const auto byEvent = db_->create_call_material(event);
  const auto byTranscript = db_->create_call_material(std::nullopt);
  const auto transcript = db_->add_transcript(std::nullopt, "live_local_v1");
  ASSERT_TRUE(db_->link_call_material_transcript(byTranscript, transcript));
  ASSERT_TRUE(db_->set_transcript_clients(transcript, {client}));
  const auto both = db_->create_call_material(event);
  const auto t2 = db_->add_transcript(event, "live_local_v1");
  ASSERT_TRUE(db_->link_call_material_transcript(both, t2));
  ASSERT_TRUE(db_->set_transcript_clients(t2, {client}));
  db_->create_call_material(std::nullopt);  // unrelated
  const auto foreign = db_->create_call_material(std::nullopt);
  const auto t3 = db_->add_transcript(std::nullopt, "live_local_v1");
  ASSERT_TRUE(db_->link_call_material_transcript(foreign, t3));
  ASSERT_TRUE(db_->set_transcript_clients(t3, {other}));

  const auto found = db_->get_call_materials_for_client(client);
  std::vector<int64_t> ids;
  for (const auto &m : found) ids.push_back(m.id);
  EXPECT_EQ(ids, (std::vector<int64_t>{both, byTranscript, byEvent}));
  EXPECT_TRUE(db_->get_call_materials_for_client(0).empty());
}
```

Before writing, check how the fixture links a client to an event: `grep -n "EventClient\|update_event\|client_ids" src/database/database.h`. If there is no `update_event_clients`, set `stored.client_ids`/the equivalent field used by `add_event` and create the event with it instead (see `makeEvent` in `test/database_transcript_tests.cpp:44`); keep the rest of the test unchanged.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-release --target Sessio_database_transcript_tests --parallel 4`
Expected: compile error `no member named 'get_call_materials_for_client'`.

- [ ] **Step 3: Implement**

`database.h`, after `get_call_materials();`:

```cpp
  // Call materials of one client, via the call's event or a linked transcript.
  std::vector<CallMaterialState> get_call_materials_for_client(int64_t client_id);
```

`call_material_database.cpp`, after `get_call_materials()`:

```cpp
std::vector<CallMaterialState> Database::get_call_materials_for_client(int64_t client_id) {
  if (client_id <= 0) return {};
  duckdb::Connection conn(*mDb);
  auto result = query(conn,
      "SELECT m.id,m.revision,m.event_id,m.state_json,m.created_at FROM CallMaterial m "
      "WHERE m.event_id IN (SELECT event_id FROM EventClient WHERE client_id=$1) "
      "OR m.id IN (SELECT t.material_id FROM CallMaterialTranscript t "
      "JOIN TranscriptClient c ON c.transcript_id=t.transcript_id WHERE c.client_id=$1) "
      "ORDER BY m.created_at DESC,m.id DESC", {duckdb::Value::BIGINT(client_id)});
  std::vector<CallMaterialState> states;
  while (auto chunk = result->Fetch())
    for (duckdb::idx_t row = 0; row < chunk->size(); ++row) states.push_back(readMaterial(*chunk, row));
  return states;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-release --target Sessio_database_transcript_tests --parallel 4 && QT_QPA_PLATFORM=offscreen ctest --test-dir build-release -R CallMaterial --output-on-failure -j4`
Expected: all `CallMaterial*` tests PASS. (`created_at` ties are broken by `id DESC`, which gives the expected order.)

- [ ] **Step 5: Commit**

```bash
git add src/database/database.h src/database/call_material_database.cpp test/call_material_database_tests.cpp
git commit -m "Add call materials lookup by client (#151)"
```

---

### Task 2: AI summary without an accept step

**Files:**
- Modify: `src/pages/transcript_page/call_notes_controller.h:42-45`
- Modify: `src/pages/transcript_page/call_notes_controller.cpp:442-520`
- Test: `test/call_notes_controller_tests.cpp`

**Interfaces:**
- Removes: `bool CallNotesController::acceptDraft(const QString &)`; `draft()` no longer returns an `accepted` key.
- Changes: `appendAgreement(id, text)` records the id only; it no longer modifies `manualText()`.
- Keeps (used by later tasks): `draft()`, `keyPoints()`, `source()`, `sourcesCurrent()`, `editDraft()`, `dismissDraft()`, `requestFinal()`, `liveStatus()`, `finalStatus()`, `statusText()`, `ended()`, `busy()`, `materialId()`, `transcriptId()`, signal `changed()`.

- [ ] **Step 1: Rewrite the tests that used acceptance**

Replace `ManualNotesPersistWithoutAClientAndAcceptIsIdempotent` with:

```cpp
TEST_F(CallNotesTest, AiSummaryIsStoredWithCallWithoutAcceptStep) {
  ASSERT_TRUE(c->setManualText("My own text"));
  const auto p = phrase(); c->setGenerationEnabled(true);
  QSignalSpy requests(c.get(), &CallNotesController::requestReady); c->flush();
  ASSERT_EQ(requests.count(), 1); c->handleResult(reply(requests[0][0].toJsonObject(), p));
  EXPECT_EQ(c->manualText(), "My own text");
  EXPECT_FALSE(c->draft().contains("accepted"));
  CallNotesController reopened(db); reopened.openTranscript(transcript);
  EXPECT_EQ(reopened.draft()["text"].toString(), "Rehearsal discussed");
  EXPECT_EQ(reopened.manualText(), "My own text");
}
```

In `EditingEvidenceMakesDraftStaleAndPreventsAcceptance` rename to `EditingEvidenceMakesSummaryStale` and replace `EXPECT_FALSE(c->acceptDraft());` with `EXPECT_EQ(c->finalStatus(), "idle"); EXPECT_FALSE(c->editDraft("Changed"));`.

Replace `AgreementCanBeSavedWithoutAClientOnlyOnce` with:

```cpp
TEST_F(CallNotesTest, AcceptedAgreementDoesNotChangeMyNotes) {
  ASSERT_TRUE(c->setManualText("Mine"));
  ASSERT_TRUE(c->appendAgreement("a1", "Send documents"));
  ASSERT_TRUE(c->appendAgreement("a1", "Send documents"));
  EXPECT_EQ(c->manualText(), "Mine");
}
```

Add:

```cpp
TEST_F(CallNotesTest, FinalSummaryStartsAutomaticallyWhenCallEnds) {
  phrase(); c->setGenerationEnabled(true);
  QSignalSpy requests(c.get(), &CallNotesController::requestReady);
  c->finishCall(); c->flush();
  ASSERT_EQ(requests.count(), 1);
  EXPECT_TRUE(requests[0][0].toJsonObject()["final"].toBool());
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build-release --target Sessio_call_notes_controller_tests --parallel 4 && QT_QPA_PLATFORM=offscreen ./build-release/test/Sessio_call_notes_controller_tests`
Expected: `AiSummaryIsStoredWithCallWithoutAcceptStep` fails on `contains("accepted")`; `AcceptedAgreementDoesNotChangeMyNotes` fails (`"Mine\n\nSend documents"`). `FinalSummaryStartsAutomaticallyWhenCallEnds` passes already (it guards existing behaviour).

- [ ] **Step 3: Implement**

In `call_notes_controller.h` delete the `acceptDraft` declaration.

In `call_notes_controller.cpp`:
- delete `CallNotesController::acceptDraft` entirely;
- in `draft()` delete the line `if (!note.isEmpty()) note["accepted"] = packet["accepted"].toBool();`;
- in `editDraft()` delete `packet["accepted"] = false;`;
- replace `appendAgreement` with:

```cpp
// Agreements live in the prompter state and are shown by their own widget;
// only remember which were saved so the prompter does not offer them twice.
bool CallNotesController::appendAgreement(const QString &id, const QString &text) {
  if (id.isEmpty() || text.trimmed().isEmpty() || text.size() > 16000) return false;
  auto saved = mState["agreement_notes"].toObject();
  if (saved.contains(id)) return true;
  const auto before = mState;
  saved[id] = true; mState["agreement_notes"] = saved;
  if (!persist()) { mState = before; return false; }
  emit changed(); return true;
}
```

`call_notes_panel.cpp` still calls `acceptDraft` and reads `draft()["accepted"]`; it is deleted in Task 10. To keep the tree building until then, change its accept handler body to `return;` guarded by a comment `// Removed with CallNotesPanel in Task 10.` and replace `draft["accepted"].toBool()` reads with `false`. Mark `AcceptPreservesManualNotesAndCannotAppendTheSameDraftTwice` in `test/call_notes_panel_tests.cpp` as `DISABLED_` (the file is deleted in Task 10).

- [ ] **Step 4: Run tests**

Run: `cmake --build build-release --parallel 4 && QT_QPA_PLATFORM=offscreen ctest --test-dir build-release -R "call_notes|CallNotes" --output-on-failure -j4`
Expected: PASS (the disabled panel test is reported as disabled).

- [ ] **Step 5: Commit**

```bash
git add src/pages/transcript_page/call_notes_controller.h src/pages/transcript_page/call_notes_controller.cpp src/pages/transcript_page/call_notes_panel.cpp test/call_notes_controller_tests.cpp test/call_notes_panel_tests.cpp
git commit -m "Store the AI summary with the call without an accept step (#151)"
```

---

### Task 3: NotesWidgetFrame

**Files:**
- Create: `src/pages/transcript_page/notes_widget_frame.h`, `notes_widget_frame.cpp`
- Modify: `src/pages/transcript_page/CMakeLists.txt` (add both files to `qt_add_library`)
- Create: `test/notes_widgets_tests.cpp`; Modify: `test/CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
class NotesWidgetFrame : public QFrame {
  Q_OBJECT
public:
  explicit NotesWidgetFrame(const QString &title, QWidget *parent = nullptr);
  void setTitle(const QString &title);
  // Empty text hides the chip.
  void setStatus(const QString &text);
  QVBoxLayout *body() const;
  // Labelled button in the action row (left to right, row hidden while empty/all hidden).
  QPushButton *addAction(const QString &text, const char *objectName);
  // Entry in the «⋯» menu; the «⋯» button exists only once a menu action is added.
  QAction *addMenuAction(const QString &text, const char *objectName);
};
```

Object names: `notesWidgetTitle` (QLabel), `notesWidgetStatus` (QLabel), `notesWidgetActions` (QWidget row), `notesWidgetMore` (QToolButton).

- [ ] **Step 1: Write the failing test** (`test/notes_widgets_tests.cpp`, with the includes below and the shared `main`)

```cpp
#include "notes_widget_frame.h"
#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <oclero/qlementine/style/QlementineStyle.hpp>
#include <gtest/gtest.h>

TEST(NotesWidgetFrameTest, ShowsTitleStatusChipAndLabelledActions) {
  NotesWidgetFrame frame(QStringLiteral("AI summary"));
  EXPECT_EQ(frame.findChild<QLabel *>("notesWidgetTitle")->text(), "AI summary");
  auto *chip = frame.findChild<QLabel *>("notesWidgetStatus");
  EXPECT_TRUE(chip->isHidden());
  frame.setStatus(QStringLiteral("ready"));
  EXPECT_FALSE(chip->isHidden()); EXPECT_EQ(chip->text(), "ready");
  EXPECT_EQ(frame.findChild<QToolButton *>("notesWidgetMore"), nullptr);
  auto *edit = frame.addAction(QStringLiteral("Edit"), "aiSummaryEdit");
  EXPECT_EQ(edit->text(), "Edit");
  auto *remove = frame.addMenuAction(QStringLiteral("Delete"), "aiSummaryDelete");
  auto *more = frame.findChild<QToolButton *>("notesWidgetMore");
  ASSERT_NE(more, nullptr);
  EXPECT_FALSE(more->accessibleName().isEmpty());
  EXPECT_TRUE(more->menu()->actions().contains(remove));
  frame.setStatus({}); EXPECT_TRUE(chip->isHidden());
}
```

`test/CMakeLists.txt`, next to the call-notes tests:

```cmake
add_executable(Sessio_notes_widgets_tests notes_widgets_tests.cpp)
target_link_libraries(Sessio_notes_widgets_tests PRIVATE GTest::gtest Qt6::Test Sessio_transcription_ui)
target_include_directories(Sessio_notes_widgets_tests PRIVATE ${qlementine_SOURCE_DIR}/lib/include)
gtest_discover_tests(Sessio_notes_widgets_tests)
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake build-release && cmake --build build-release --target Sessio_notes_widgets_tests --parallel 4`
Expected: `notes_widget_frame.h: No such file`.

- [ ] **Step 3: Implement**

`notes_widget_frame.h`:

```cpp
#pragma once

#include <QFrame>

class QAction;
class QHBoxLayout;
class QLabel;
class QMenu;
class QPushButton;
class QToolButton;
class QVBoxLayout;

// One card of the call notes page: title, status chip, body, labelled
// actions and an optional «⋯» menu for rare or destructive actions.
class NotesWidgetFrame : public QFrame {
  Q_OBJECT
public:
  explicit NotesWidgetFrame(const QString &title, QWidget *parent = nullptr);
  void setTitle(const QString &title);
  void setStatus(const QString &text);
  QVBoxLayout *body() const { return mBody; }
  QPushButton *addAction(const QString &text, const char *objectName);
  QAction *addMenuAction(const QString &text, const char *objectName);
private:
  QLabel *mTitle;
  QLabel *mStatus;
  QVBoxLayout *mBody;
  QWidget *mActionRow;
  QHBoxLayout *mActions;
  QToolButton *mMore = nullptr;
  QMenu *mMenu = nullptr;
};
```

`notes_widget_frame.cpp`:

```cpp
#include "notes_widget_frame.h"
#include "surface_paint_filter.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

NotesWidgetFrame::NotesWidgetFrame(const QString &title, QWidget *parent) : QFrame(parent) {
  installEventFilter(new pcm::widgets::SurfacePaintFilter(this));
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(12, 10, 12, 10); outer->setSpacing(8);
  auto *header = new QHBoxLayout; header->setSpacing(6);
  mTitle = new QLabel(title, this); mTitle->setObjectName("notesWidgetTitle");
  auto font = mTitle->font(); font.setBold(true); mTitle->setFont(font);
  mStatus = new QLabel(this); mStatus->setObjectName("notesWidgetStatus");
  mStatus->setForegroundRole(QPalette::PlaceholderText); mStatus->hide();
  header->addWidget(mTitle, 1); header->addWidget(mStatus);
  outer->addLayout(header);
  mBody = new QVBoxLayout; mBody->setSpacing(6); outer->addLayout(mBody);
  mActionRow = new QWidget(this); mActionRow->setObjectName("notesWidgetActions");
  mActions = new QHBoxLayout(mActionRow); mActions->setContentsMargins(0, 0, 0, 0); mActions->setSpacing(6);
  mActions->addStretch();
  outer->addWidget(mActionRow); mActionRow->hide();
}

void NotesWidgetFrame::setTitle(const QString &title) { mTitle->setText(title); }

void NotesWidgetFrame::setStatus(const QString &text) {
  mStatus->setText(text); mStatus->setVisible(!text.isEmpty());
}

QPushButton *NotesWidgetFrame::addAction(const QString &text, const char *objectName) {
  auto *button = new QPushButton(text, mActionRow);
  button->setObjectName(QString::fromLatin1(objectName)); button->setAutoDefault(false);
  mActions->insertWidget(mActions->count() - 1, button);
  mActionRow->show();
  return button;
}

QAction *NotesWidgetFrame::addMenuAction(const QString &text, const char *objectName) {
  if (!mMore) {
    mMenu = new QMenu(this);
    mMore = new QToolButton(mActionRow); mMore->setObjectName("notesWidgetMore");
    mMore->setText(QStringLiteral("⋯")); mMore->setAccessibleName(tr("More actions"));
    mMore->setToolTip(tr("More actions"));
    mMore->setPopupMode(QToolButton::InstantPopup); mMore->setMenu(mMenu);
    mActions->addWidget(mMore); mActionRow->show();
  }
  auto *action = mMenu->addAction(text); action->setObjectName(QString::fromLatin1(objectName));
  return action;
}
```

Check the include name with `grep -rn "class SurfacePaintFilter" src/widgets`; use that header. Add both files to `src/pages/transcript_page/CMakeLists.txt` after `call_notes_panel.h call_notes_panel.cpp`.

- [ ] **Step 4: Run test**

Run: `cmake --build build-release --target Sessio_notes_widgets_tests --parallel 4 && ./build-release/test/Sessio_notes_widgets_tests`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/pages/transcript_page/notes_widget_frame.* src/pages/transcript_page/CMakeLists.txt test/notes_widgets_tests.cpp test/CMakeLists.txt
git commit -m "Add the notes widget card frame (#151)"
```

---

### Task 4: AI summary widget

**Files:**
- Create: `src/pages/transcript_page/call_notes_sources.h/.cpp` (moved from `CallNotesPanel::populateSources`)
- Create: `src/pages/transcript_page/ai_summary_widget.h/.cpp`
- Modify: `src/pages/transcript_page/CMakeLists.txt`
- Test: `test/notes_widgets_tests.cpp`

**Interfaces:**
- Consumes: `NotesWidgetFrame` (Task 3); controller API from Task 2.
- Produces:

```cpp
// call_notes_sources.h
// Source link for a note or key point: «0:04–0:05 · 1 phrase» (compact: «0:04»).
// Returns nullptr when the item has no valid evidence. Clicking calls onClick(phraseId).
QPushButton *makeSourceLink(CallNotesController *controller, const QJsonObject &item, bool compact,
                            QWidget *parent, std::function<void(qint64)> onClick);
// «вся встреча · N фраз» / «до 12:30 · N фраз» caption for the summary.
QString summaryCoverage(CallNotesController *controller);

// ai_summary_widget.h
class AiSummaryWidget final : public NotesWidgetFrame {
  Q_OBJECT
public:
  explicit AiSummaryWidget(CallNotesController *controller, QWidget *parent = nullptr);
  // Replaces the delete confirmation dialog (tests). Returns true to delete.
  void setDeleteConfirmation(std::function<bool(QWidget *)> confirm);
  // False when there is nothing to show: no summary and an idle status.
  bool hasContent() const;
signals:
  void sourceRequested(qint64 phraseId);
  void retryRequested();      // during the call: host restarts live notes
  void contentChanged();      // visibility may need updating
};
```

Object names: `aiSummaryText` (QLabel), `aiSummaryCoverage` (QLabel), `aiSummaryEditor` (QPlainTextEdit), buttons `aiSummaryEdit`, `aiSummarySave`, `aiSummaryCancel`, `aiSummaryRetry`; label `aiSummaryProblem`; menu actions `aiSummaryRegenerate`, `aiSummaryDelete`.

Status chip mapping (state = `ended() ? finalStatus() : liveStatus()`):
`running`/`queued` → «обновляется…» (`tr("updating…")`); `ready` → `tr("ready")`; `stale` → `tr("outdated")`; `error` → `tr("failed")`; otherwise empty.
Problem line (`aiSummaryProblem`, hidden otherwise): state `error`/`cancelled`/`interrupted` → `controller->statusText()` with «Повторить» (`aiSummaryRetry`) visible; `stale` → `tr("The transcript changed after this summary.")` with the retry button labelled `tr("Update")`; `unavailable` → `tr("Turn on transcription to get a summary")`, no retry.
Retry/Update/Regenerate: `controller->ended() ? controller->requestFinal() : emit retryRequested()`.
Edit: «Изменить» (`aiSummaryEdit`) shows the editor with «Сохранить»/«Отмена»; Save calls `editDraft(text)`; edit enabled only when `sourcesCurrent() && !busy()`.
Delete: menu `aiSummaryDelete` → `confirm(this)` → `dismissDraft()`.

- [ ] **Step 1: Write the failing tests** (append to `test/notes_widgets_tests.cpp`; add the shared `NotesFixture`, includes `ai_summary_widget.h`, `call_notes_controller.h`, `<QPlainTextEdit>`, `<QSignalSpy>`, `<QTemporaryDir>`)

```cpp
class AiSummaryTest : public ::testing::Test, public NotesFixture {};

TEST_F(AiSummaryTest, ShowsSummaryWithoutAcceptOrEditorButtons) {
  generate();
  AiSummaryWidget widget(&controller);
  EXPECT_TRUE(widget.hasContent());
  EXPECT_EQ(widget.findChild<QLabel *>("aiSummaryText")->text(), "Prepare a short outline.");
  for (auto *button : widget.findChildren<QPushButton *>()) {
    EXPECT_FALSE(button->text().isEmpty()) << "every button is labelled";
    EXPECT_FALSE(button->text().contains("editor", Qt::CaseInsensitive));
  }
  EXPECT_EQ(widget.findChild<QLabel *>("notesWidgetStatus")->text(), "ready");
}

TEST_F(AiSummaryTest, EditSavesTextInPlace) {
  generate();
  AiSummaryWidget widget(&controller);
  widget.findChild<QPushButton *>("aiSummaryEdit")->click();
  auto *editor = widget.findChild<QPlainTextEdit *>("aiSummaryEditor");
  ASSERT_FALSE(editor->isHidden());
  editor->setPlainText("Revised summary");
  widget.findChild<QPushButton *>("aiSummarySave")->click();
  EXPECT_EQ(controller.draft()["text"].toString(), "Revised summary");
  EXPECT_TRUE(editor->isHidden());
}

TEST_F(AiSummaryTest, DeletingSummaryAsksForConfirmation) {
  generate();
  AiSummaryWidget widget(&controller);
  int asked = 0; bool answer = false;
  widget.setDeleteConfirmation([&](QWidget *) { ++asked; return answer; });
  auto *remove = widget.findChild<QAction *>("aiSummaryDelete");
  remove->trigger();
  EXPECT_EQ(asked, 1); EXPECT_FALSE(controller.draft().isEmpty());
  answer = true; remove->trigger();
  EXPECT_TRUE(controller.draft().isEmpty());
  EXPECT_FALSE(widget.hasContent());
}

TEST_F(AiSummaryTest, AiSummaryErrorShowsReasonAndRetryInsideCard) {
  generate();
  QSignalSpy requests(&controller, &CallNotesController::requestReady);
  controller.addPhrase(phrase); controller.flush();
  ASSERT_FALSE(requests.isEmpty());
  auto failed = requests.last().first().toJsonObject();
  failed["error"] = "x"; failed["reason"] = "invalid_output";
  controller.handleResult(failed);
  AiSummaryWidget widget(&controller);
  QSignalSpy retry(&widget, &AiSummaryWidget::retryRequested);
  auto *problem = widget.findChild<QLabel *>("aiSummaryProblem");
  EXPECT_FALSE(problem->isHidden());
  EXPECT_TRUE(problem->text().contains("could not be used"));
  widget.findChild<QPushButton *>("aiSummaryRetry")->click();
  EXPECT_EQ(retry.count(), 1);
}

TEST_F(AiSummaryTest, SourceLinkRequestsThePhrase) {
  generate();
  AiSummaryWidget widget(&controller);
  QSignalSpy sources(&widget, &AiSummaryWidget::sourceRequested);
  auto links = widget.findChildren<QPushButton *>("callNotesSource");
  ASSERT_FALSE(links.isEmpty()); links.first()->click();
  ASSERT_EQ(sources.count(), 1);
  EXPECT_EQ(sources.first().first().toLongLong(), phrase.id);
}

TEST_F(AiSummaryTest, EmptyIdleControllerHasNoContent) {
  AiSummaryWidget widget(&controller);
  EXPECT_FALSE(widget.hasContent());
}
```

Chip text in tests is English because no translator is installed.

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build-release --target Sessio_notes_widgets_tests --parallel 4`
Expected: `ai_summary_widget.h: No such file`.

- [ ] **Step 3: Implement `call_notes_sources`**

Move the body of `CallNotesPanel::populateSources` (`call_notes_panel.cpp:376-447`) into `makeSourceLink`, with these changes: take `controller` instead of `mController`; create the button with `parent`; replace `emit sourceRequested(id)` by `onClick(id)` (in both the click handler and the context-menu actions); return the button instead of `layout->addWidget(jump)`; return `nullptr` where the original returned early. Keep the object name `callNotesSource`, the tooltip, the accessible name/description, and the quote icon: move `NoteIconEngine`/`noteIcon` (`call_notes_panel.cpp:48-108`) into the anonymous namespace of `call_notes_sources.cpp` and expose nothing else.

`summaryCoverage`:

```cpp
QString summaryCoverage(CallNotesController *controller) {
  const auto count = static_cast<int>(controller->draft()["evidence_ids"].toArray().size());
  if (count == 0) return {};
  if (controller->ended() && controller->finalStatus() == "ready")
    return QCoreApplication::translate("CallNotesSources", "whole meeting · %n phrases", nullptr, count);
  const auto ms = controller->processedThroughMs();
  const auto time = QStringLiteral("%1:%2").arg(ms / 60000).arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'));
  return QCoreApplication::translate("CallNotesSources", "through %1 · %n phrases", nullptr, count).arg(time);
}
```

Until Task 10, make `CallNotesPanel::populateSources` call `makeSourceLink(mController, note, compact, this, [this](qint64 id) { emit sourceRequested(id); })` and `if (link) layout->addWidget(link);` so there is one copy of the code.

- [ ] **Step 4: Implement `AiSummaryWidget`**

`ai_summary_widget.h` — the interface above, plus private members:

```cpp
private:
  void refresh();
  void retry();
  QPointer<CallNotesController> mController;
  std::function<bool(QWidget *)> mConfirm;
  QLabel *mText, *mCoverage, *mProblem;
  QPlainTextEdit *mEditor;
  QVBoxLayout *mSources;
  QPushButton *mEdit, *mSave, *mCancel, *mRetry;
  QJsonObject mRenderedDraft;
  bool mEditing = false;
```

`ai_summary_widget.cpp`:

```cpp
#include "ai_summary_widget.h"
#include "call_notes_controller.h"
#include "call_notes_sources.h"
#include <QAction>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QLabel *textLabel(const char *name, QWidget *parent) {
  auto *label = new QLabel(parent); label->setObjectName(QString::fromLatin1(name));
  label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  return label;
}
}

AiSummaryWidget::AiSummaryWidget(CallNotesController *controller, QWidget *parent)
    : NotesWidgetFrame(tr("AI summary"), parent), mController(controller) {
  mConfirm = [](QWidget *owner) {
    return QMessageBox::question(owner, tr("Delete AI summary"),
        tr("Delete the AI summary of this meeting? Your own notes are kept.")) == QMessageBox::Yes;
  };
  mText = textLabel("aiSummaryText", this); body()->addWidget(mText);
  mEditor = new QPlainTextEdit(this); mEditor->setObjectName("aiSummaryEditor");
  mEditor->setAccessibleName(tr("Edit AI summary")); mEditor->setMinimumHeight(110);
  mEditor->setTabChangesFocus(true); mEditor->hide(); body()->addWidget(mEditor);
  mCoverage = textLabel("aiSummaryCoverage", this); mCoverage->setForegroundRole(QPalette::PlaceholderText);
  body()->addWidget(mCoverage);
  mSources = new QVBoxLayout; mSources->setSpacing(2); body()->addLayout(mSources);
  mProblem = textLabel("aiSummaryProblem", this); body()->addWidget(mProblem);
  mEdit = addAction(tr("Edit"), "aiSummaryEdit");
  mSave = addAction(tr("Save"), "aiSummarySave");
  mCancel = addAction(tr("Cancel"), "aiSummaryCancel");
  mRetry = addAction(tr("Retry"), "aiSummaryRetry");
  auto *regenerate = addMenuAction(tr("Create again"), "aiSummaryRegenerate");
  auto *remove = addMenuAction(tr("Delete"), "aiSummaryDelete");
  connect(mEdit, &QPushButton::clicked, this, [this] {
    if (!mController) return;
    mEditor->setPlainText(mController->draft()["text"].toString());
    mEditing = true; refresh(); mEditor->setFocus();
  });
  connect(mSave, &QPushButton::clicked, this, [this] {
    if (!mController) return;
    if (mController->editDraft(mEditor->toPlainText())) { mEditing = false; refresh(); return; }
    mProblem->setText(tr("Could not save — keep this page open")); mProblem->show();
  });
  connect(mCancel, &QPushButton::clicked, this, [this] { mEditing = false; refresh(); });
  connect(mRetry, &QPushButton::clicked, this, &AiSummaryWidget::retry);
  connect(regenerate, &QAction::triggered, this, &AiSummaryWidget::retry);
  connect(remove, &QAction::triggered, this, [this] {
    if (mController && mConfirm(this) && mController->dismissDraft()) { mEditing = false; refresh(); }
  });
  if (controller) connect(controller, &CallNotesController::changed, this, &AiSummaryWidget::refresh);
  refresh();
}

void AiSummaryWidget::setDeleteConfirmation(std::function<bool(QWidget *)> confirm) { mConfirm = std::move(confirm); }

void AiSummaryWidget::retry() {
  if (!mController) return;
  if (mController->ended()) mController->requestFinal(); else emit retryRequested();
}

bool AiSummaryWidget::hasContent() const {
  if (!mController || mController->materialId() <= 0) return false;
  const auto state = mController->ended() ? mController->finalStatus() : mController->liveStatus();
  return !mController->draft()["text"].toString().trimmed().isEmpty() || state != "idle";
}

void AiSummaryWidget::refresh() {
  if (!mController) { setEnabled(false); return; }
  const auto draft = mController->draft();
  const bool hasText = !draft["text"].toString().trimmed().isEmpty();
  const auto state = mController->ended() ? mController->finalStatus() : mController->liveStatus();
  const bool busy = mController->busy() || state == "running" || state == "queued";
  setStatus(busy ? tr("updating…") : state == "ready" && hasText ? tr("ready")
            : state == "stale" ? tr("outdated") : state == "error" ? tr("failed") : QString{});
  mText->setText(draft["text"].toString()); mText->setVisible(hasText && !mEditing);
  mEditor->setVisible(mEditing);
  mCoverage->setText(summaryCoverage(mController)); mCoverage->setVisible(hasText && !mEditing);
  if (draft != mRenderedDraft) {
    while (auto *item = mSources->takeAt(0)) { delete item->widget(); delete item; }
    if (auto *link = makeSourceLink(mController, draft, false, this, [this](qint64 id) { emit sourceRequested(id); }))
      mSources->addWidget(link);
    mRenderedDraft = draft;
  }
  QString problem;
  bool canRetry = false;
  if (state == "error" || state == "cancelled" || state == "interrupted") {
    problem = mController->statusText(); canRetry = true; mRetry->setText(tr("Retry"));
  } else if (state == "stale") {
    problem = tr("The transcript changed after this summary."); canRetry = true; mRetry->setText(tr("Update"));
  } else if (state == "unavailable") {
    problem = tr("Turn on transcription to get a summary");
  }
  mProblem->setText(problem); mProblem->setVisible(!problem.isEmpty());
  mRetry->setVisible(canRetry && !mEditing); mRetry->setEnabled(!busy);
  const bool editable = hasText && mController->sourcesCurrent() && !busy;
  mEdit->setVisible(!mEditing && hasText); mEdit->setEnabled(editable);
  mSave->setVisible(mEditing); mCancel->setVisible(mEditing);
  if (auto *more = findChild<QWidget *>("notesWidgetMore")) more->setVisible(hasText && !mEditing && !busy);
  emit contentChanged();
}
```

Note: the menu's «Create again» after the call calls `requestFinal()`; during the call it emits `retryRequested()` (host restarts live notes). Add `ai_summary_widget.*` and `call_notes_sources.*` to the CMake library.

- [ ] **Step 5: Run tests**

Run: `cmake build-release && cmake --build build-release --target Sessio_notes_widgets_tests Sessio_call_notes_panel_tests --parallel 4 && ./build-release/test/Sessio_notes_widgets_tests && QT_QPA_PLATFORM=offscreen ./build-release/test/Sessio_call_notes_panel_tests`
Expected: PASS (the panel still works through `makeSourceLink`).

- [ ] **Step 6: Commit**

```bash
git add src/pages/transcript_page/call_notes_sources.* src/pages/transcript_page/ai_summary_widget.* src/pages/transcript_page/call_notes_panel.cpp src/pages/transcript_page/CMakeLists.txt test/notes_widgets_tests.cpp
git commit -m "Add the AI summary widget (#151)"
```

---

### Task 5: Key points and agreements widgets

**Files:**
- Create: `src/pages/transcript_page/key_points_widget.h/.cpp`, `agreements_widget.h/.cpp`
- Modify: `src/pages/transcript_page/CMakeLists.txt`
- Test: `test/notes_widgets_tests.cpp`

**Interfaces:**
- Consumes: `NotesWidgetFrame`, `makeSourceLink` (Task 4).
- Produces:

```cpp
class KeyPointsWidget final : public NotesWidgetFrame {
  Q_OBJECT
public:
  explicit KeyPointsWidget(CallNotesController *controller, QWidget *parent = nullptr);
  bool hasContent() const;   // at least one visible point
signals:
  void sourceRequested(qint64 phraseId);
  void contentChanged();
};

class AgreementsWidget final : public NotesWidgetFrame {
  Q_OBJECT
public:
  explicit AgreementsWidget(QWidget *parent = nullptr);
  // Prompter records: {"id","text","active"}; inactive ones are skipped.
  void setRecords(const QJsonArray &records);
  bool hasContent() const;
};
```

Rows: `keyPointRow` (QWidget containing QLabel `keyPointText` and a compact source link), `agreementRow` (QLabel). Status chip: count (`QString::number(n)`).

- [ ] **Step 1: Write the failing tests**

```cpp
class KeyPointsTest : public ::testing::Test, public NotesFixture {};

TEST_F(KeyPointsTest, ListsPointsWithCompactSource) {
  generate();
  KeyPointsWidget widget(&controller);
  ASSERT_TRUE(widget.hasContent());
  const auto rows = widget.findChildren<QWidget *>("keyPointRow");
  ASSERT_EQ(rows.size(), 1);
  EXPECT_EQ(rows[0]->findChild<QLabel *>("keyPointText")->text(), "Outline is due");
  EXPECT_EQ(rows[0]->findChild<QPushButton *>("callNotesSource")->text(), "1:13");
  EXPECT_EQ(widget.findChild<QLabel *>("notesWidgetStatus")->text(), "1");
}

TEST_F(KeyPointsTest, KeyPointMatchingSummaryTextIsHidden) {
  generate(QStringLiteral("Same text."), QStringLiteral("Same text."));
  KeyPointsWidget widget(&controller);
  EXPECT_TRUE(widget.findChildren<QWidget *>("keyPointRow").isEmpty());
  EXPECT_FALSE(widget.hasContent());
}

TEST(AgreementsWidgetTest, ShowsActiveAgreementsOnly) {
  AgreementsWidget widget;
  EXPECT_FALSE(widget.hasContent());
  widget.setRecords(QJsonArray{
      QJsonObject{{"id", "a"}, {"text", "Keep a sleep diary"}, {"active", true}},
      QJsonObject{{"id", "b"}, {"text", "Cancelled call"}, {"active", false}}});
  const auto rows = widget.findChildren<QLabel *>("agreementRow");
  ASSERT_EQ(rows.size(), 1);
  EXPECT_EQ(rows[0]->text(), "Keep a sleep diary");
  EXPECT_TRUE(widget.hasContent());
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build-release --target Sessio_notes_widgets_tests --parallel 4`
Expected: missing headers.

- [ ] **Step 3: Implement**

`key_points_widget.cpp` (header per the interface, private `void refresh(); QPointer<CallNotesController> mController; QVBoxLayout *mRows; QJsonArray mRendered; QString mRenderedSummary; int mCount = 0;`):

```cpp
#include "key_points_widget.h"
#include "call_notes_controller.h"
#include "call_notes_sources.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

KeyPointsWidget::KeyPointsWidget(CallNotesController *controller, QWidget *parent)
    : NotesWidgetFrame(tr("Key points"), parent), mController(controller) {
  mRows = new QVBoxLayout; mRows->setSpacing(4); body()->addLayout(mRows);
  if (controller) connect(controller, &CallNotesController::changed, this, &KeyPointsWidget::refresh);
  refresh();
}

bool KeyPointsWidget::hasContent() const { return mCount > 0; }

void KeyPointsWidget::refresh() {
  if (!mController) return;
  const auto points = mController->keyPoints();
  const auto summary = mController->draft()["text"].toString().trimmed();
  if (points == mRendered && summary == mRenderedSummary) return;
  mRendered = points; mRenderedSummary = summary;
  while (auto *item = mRows->takeAt(0)) { delete item->widget(); delete item; }
  mCount = 0;
  for (const auto &value : points) {
    const auto point = value.toObject();
    const auto text = point["text"].toString().trimmed();
    if (text.isEmpty() || text == summary) continue;
    auto *row = new QWidget(this); row->setObjectName("keyPointRow");
    auto *layout = new QHBoxLayout(row); layout->setContentsMargins(0, 2, 0, 2); layout->setSpacing(8);
    auto *label = new QLabel(QStringLiteral("• ") + text, row); label->setObjectName("keyPointText");
    label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
    layout->addWidget(label, 1);
    if (auto *link = makeSourceLink(mController, point, true, row, [this](qint64 id) { emit sourceRequested(id); }))
      layout->addWidget(link);
    mRows->addWidget(row); ++mCount;
  }
  setStatus(mCount > 0 ? QString::number(mCount) : QString{});
  emit contentChanged();
}
```

The test expects `keyPointText` to be `"Outline is due"`: set the label text to `text` and draw the bullet with a separate fixed-width `QLabel("•")` placed before it in the row, instead of prefixing.

`agreements_widget.cpp` (private `QVBoxLayout *mRows; int mCount = 0;`):

```cpp
#include "agreements_widget.h"
#include <QJsonObject>
#include <QLabel>
#include <QVBoxLayout>

AgreementsWidget::AgreementsWidget(QWidget *parent) : NotesWidgetFrame(tr("Agreements"), parent) {
  mRows = new QVBoxLayout; mRows->setSpacing(4); body()->addLayout(mRows);
}

bool AgreementsWidget::hasContent() const { return mCount > 0; }

void AgreementsWidget::setRecords(const QJsonArray &records) {
  while (auto *item = mRows->takeAt(0)) { delete item->widget(); delete item; }
  mCount = 0;
  for (const auto &value : records) {
    const auto record = value.toObject();
    const auto text = record["text"].toString().trimmed();
    if (!record["active"].toBool(true) || text.isEmpty()) continue;
    auto *row = new QLabel(text, this); row->setObjectName("agreementRow");
    row->setTextFormat(Qt::PlainText); row->setWordWrap(true);
    row->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mRows->addWidget(row); ++mCount;
  }
  setStatus(mCount > 0 ? QString::number(mCount) : QString{});
}
```

- [ ] **Step 4: Run tests**

Run: `cmake build-release && cmake --build build-release --target Sessio_notes_widgets_tests --parallel 4 && ./build-release/test/Sessio_notes_widgets_tests`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/pages/transcript_page/key_points_widget.* src/pages/transcript_page/agreements_widget.* src/pages/transcript_page/CMakeLists.txt test/notes_widgets_tests.cpp
git commit -m "Add key points and agreements widgets (#151)"
```

---

### Task 6: Meeting history data

**Files:**
- Create: `src/pages/transcript_page/meeting_history.h/.cpp`
- Modify: `src/pages/transcript_page/CMakeLists.txt`
- Create: `test/meeting_history_tests.cpp`; Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `Database::get_call_materials_for_client` (Task 1), `get_call_material_transcript_ids`, `get_prompter_state`, `get_event`, `get_client_notes`.
- Produces:

```cpp
struct MeetingRecord {
  qint64 materialId = 0;
  qint64 startMs = 0;        // event start, else material created_at
  qint64 durationMs = 0;     // event duration, 0 when unknown
  QString summary;           // AI summary text (final, else live; dismissed → empty)
  QString firstNote;         // first own note entry, Markdown source
  int noteCount = 0;         // own note entries
  QStringList agreements;    // active prompter records of linked transcripts
  QJsonArray keyPoints;      // key points of the shown summary
};
// Ended meetings of the client, newest first, without excludeMaterialId.
std::vector<MeetingRecord> loadMeetingHistory(pcm::database::Database &db, int64_t clientId,
                                              qint64 excludeMaterialId);
// Client notes without a linked event or series, newest first (Markdown).
QStringList loadNotesWithoutMeeting(pcm::database::Database &db, int64_t clientId);
// Two-line preview: summary, else first note; trimmed to 2 lines / 160 chars.
QString meetingPreview(const MeetingRecord &record);
```

- [ ] **Step 1: Write the failing tests** (`test/meeting_history_tests.cpp`)

```cpp
#include "meeting_history.h"
#include <QApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <oclero/qlementine/style/QlementineStyle.hpp>
#include <gtest/gtest.h>

class MeetingHistoryTest : public ::testing::Test {
protected:
  QTemporaryDir dir;
  pcm::config::Config conf{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir.path().toStdString())}};
  std::shared_ptr<pcm::database::Database> db = std::make_shared<pcm::database::Database>(conf);
  int64_t client = 0;
  void SetUp() override { client = db->get_clients().at(0)->id; }
  // Ended material linked to the client through a transcript.
  qint64 meeting(const QJsonObject &state, bool ended = true) {
    const auto id = db->create_call_material(std::nullopt);
    const auto transcript = db->add_transcript(std::nullopt, "live_local_v1");
    EXPECT_TRUE(db->link_call_material_transcript(id, transcript));
    EXPECT_TRUE(db->set_transcript_clients(transcript, {client}));
    auto json = state; json["ended"] = ended;
    const auto revision = db->get_call_material(id).revision;
    EXPECT_TRUE(db->save_call_material(id, revision, QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString()));
    return id;
  }
};

TEST_F(MeetingHistoryTest, HistoryListsClientMeetingsNewestFirst) {
  const auto older = meeting({{"final", QJsonObject{{"note", QJsonObject{{"text", "Older summary"}}}}}});
  const auto newer = meeting({{"live", QJsonObject{{"note", QJsonObject{{"text", "Newer summary"}}}}},
                              {"manual_entries", QJsonArray{QJsonObject{{"markdown", "My note"}}}}});
  meeting({}, false);  // the current, unfinished call is not history
  const auto history = loadMeetingHistory(*db, client, 0);
  ASSERT_EQ(history.size(), 2u);
  EXPECT_EQ(history[0].materialId, newer);
  EXPECT_EQ(history[0].summary, "Newer summary");
  EXPECT_EQ(history[0].noteCount, 1);
  EXPECT_EQ(history[0].firstNote, "My note");
  EXPECT_EQ(history[1].materialId, older);
  EXPECT_EQ(loadMeetingHistory(*db, client, newer).size(), 1u);
}

TEST_F(MeetingHistoryTest, DismissedSummaryFallsBackToFirstNoteInPreview) {
  meeting({{"final", QJsonObject{{"dismissed", true}, {"note", QJsonObject{{"text", "Gone"}}}}},
           {"manual_entries", QJsonArray{QJsonObject{{"markdown", "Line one\nLine two\nLine three"}}}}});
  const auto history = loadMeetingHistory(*db, client, 0);
  ASSERT_EQ(history.size(), 1u);
  EXPECT_TRUE(history[0].summary.isEmpty());
  EXPECT_EQ(meetingPreview(history[0]), "Line one\nLine two");
}

TEST_F(MeetingHistoryTest, HistoryGroupsNotesWithoutMeetingSeparately) {
  DuckClientNote loose; loose.client_id = client; loose.body_markdown = std::string{"Loose note"};
  ASSERT_GT(db->add_client_note(loose), 0);
  const auto notes = loadNotesWithoutMeeting(*db, client);
  ASSERT_EQ(notes.size(), 1);
  EXPECT_EQ(notes[0], "Loose note");
}
```

Before Step 3, confirm the manual-entry key with `grep -n "manual_entries" -A8 src/pages/transcript_page/call_notes_controller.cpp | grep '"markdown"\|\["'` and use the real key in both the test and the loader if it is not `markdown`.

Register in `test/CMakeLists.txt`:

```cmake
add_executable(Sessio_meeting_history_tests meeting_history_tests.cpp)
target_link_libraries(Sessio_meeting_history_tests PRIVATE GTest::gtest Qt6::Test Sessio_transcription_ui)
target_include_directories(Sessio_meeting_history_tests PRIVATE ${qlementine_SOURCE_DIR}/lib/include)
gtest_discover_tests(Sessio_meeting_history_tests)
```

(add the shared `main`).

- [ ] **Step 2: Run to verify they fail**

Run: `cmake build-release && cmake --build build-release --target Sessio_meeting_history_tests --parallel 4`
Expected: `meeting_history.h: No such file`.

- [ ] **Step 3: Implement** (`meeting_history.cpp`; header per the interface, includes `database.h`, `<QJsonArray>`, `<QStringList>`, `<vector>`)

```cpp
#include "meeting_history.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace {
QJsonObject parse(const std::string &json) {
  const auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(json));
  return doc.isObject() ? doc.object() : QJsonObject{};
}
QJsonObject shownPacket(const QJsonObject &state) {
  return state[state["final"].isObject() ? "final" : "live"].toObject();
}
}

std::vector<MeetingRecord> loadMeetingHistory(pcm::database::Database &db, int64_t clientId,
                                              qint64 excludeMaterialId) {
  std::vector<MeetingRecord> result;
  for (const auto &material : db.get_call_materials_for_client(clientId)) {
    if (material.id == excludeMaterialId) continue;
    const auto state = parse(material.json);
    if (!state["ended"].toBool()) continue;
    MeetingRecord record;
    record.materialId = material.id;
    record.startMs = material.created_at;
    if (material.event_id) {
      if (const auto event = db.get_event(*material.event_id)) {
        if (event->start_date) record.startMs = *event->start_date;
        if (event->start_date && event->end_date) record.durationMs = *event->end_date - *event->start_date;
      }
    }
    const auto packet = shownPacket(state);
    if (!packet["dismissed"].toBool()) {
      record.summary = packet["note"].toObject()["text"].toString().trimmed();
      record.keyPoints = packet["key_points"].toArray();
    }
    const auto entries = state["manual_entries"].toArray();
    record.noteCount = static_cast<int>(entries.size());
    if (!entries.isEmpty()) record.firstNote = entries.first().toObject()["markdown"].toString().trimmed();
    for (const auto transcript : db.get_call_material_transcript_ids(material.id)) {
      const auto prompter = parse(db.get_prompter_state(transcript).json);
      for (const auto &value : prompter["records"].toArray()) {
        const auto r = value.toObject();
        if (r["active"].toBool(true) && !r["text"].toString().trimmed().isEmpty())
          record.agreements.append(r["text"].toString().trimmed());
      }
    }
    result.push_back(std::move(record));
  }
  std::stable_sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.startMs > b.startMs; });
  return result;
}

QStringList loadNotesWithoutMeeting(pcm::database::Database &db, int64_t clientId) {
  auto notes = db.get_client_notes(clientId);
  std::stable_sort(notes.begin(), notes.end(), [](const auto &a, const auto &b) {
    return a.created_at.value_or(0) > b.created_at.value_or(0);
  });
  QStringList result;
  for (const auto &note : notes)
    if (!note.linked_event_id && !note.linked_series_id && note.body_markdown)
      result.append(QString::fromStdString(*note.body_markdown).trimmed());
  return result;
}

QString meetingPreview(const MeetingRecord &record) {
  const auto text = record.summary.isEmpty() ? record.firstNote : record.summary;
  auto lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  while (lines.size() > 2) lines.removeLast();
  auto preview = lines.join(QLatin1Char('\n'));
  if (preview.size() > 160) preview = preview.left(159) + QStringLiteral("…");
  return preview;
}
```

`get_prompter_state` returns a struct with `.json` (see `src/database/prompter_database.cpp:39`); check the field name with `grep -n "struct PrompterState" -A4 src/database/*.h`. The `stable_sort` by `startMs` keeps the DB order (newest `created_at`) on ties; in the test both meetings have no event, so `created_at` order decides.

- [ ] **Step 4: Run tests**

Run: `cmake --build build-release --target Sessio_meeting_history_tests --parallel 4 && ./build-release/test/Sessio_meeting_history_tests`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/pages/transcript_page/meeting_history.* src/pages/transcript_page/CMakeLists.txt test/meeting_history_tests.cpp test/CMakeLists.txt
git commit -m "Load a client's past meetings for the notes page (#151)"
```

---

### Task 7: Previous meeting and history widgets

**Files:**
- Create: `src/pages/transcript_page/meeting_history_widgets.h/.cpp`
- Modify: `src/pages/transcript_page/CMakeLists.txt`
- Test: `test/meeting_history_tests.cpp`

**Interfaces:**
- Consumes: `MeetingRecord`, `meetingPreview` (Task 6); `NotesWidgetFrame` (3); `AiSummaryWidget` (4); `KeyPointsWidget`, `AgreementsWidget` (5).
- Produces:

```cpp
class PreviousMeetingWidget final : public NotesWidgetFrame {
  Q_OBJECT
public:
  explicit PreviousMeetingWidget(QWidget *parent = nullptr);
  void setMeeting(const std::optional<MeetingRecord> &meeting);  // nullopt hides
signals:
  void openRequested(qint64 materialId);
};

// Factory for a controller of a past meeting (host wires the finalizer queue).
using MeetingControllerFactory = std::function<CallNotesController *(qint64 materialId, QObject *parent)>;

class MeetingHistoryList final : public QWidget {
  Q_OBJECT
public:
  explicit MeetingHistoryList(QWidget *parent = nullptr);
  void setControllerFactory(MeetingControllerFactory factory);
  void setMeetings(const std::vector<MeetingRecord> &meetings, const QStringList &notesWithoutMeeting);
  void expand(qint64 materialId);   // collapses others
signals:
  void sourceRequested(qint64 phraseId);
};
```

Object names: `previousMeetingDate`, `previousMeetingPreview`, `previousMeetingAgreement` (≤3 labels), button `previousMeetingOpen`; history header buttons `historyRow_<materialId>` (checkable QToolButton, text «dd.MM · N мин»), expanded body `historyBody_<materialId>`, group label `historyLooseTitle`, loose notes `historyLooseNote`.

Expanded body content: `AiSummaryWidget` over `factory(materialId, body)` (if a factory is set), `KeyPointsWidget` over the same controller, a read-only list of own notes (`QLabel` `historyNote` per entry, Markdown shown as plain text), `AgreementsWidget` fed with `{"text", s, "active", true}` objects from `record.agreements`. The controller is created lazily on first expand and owned by the body.

- [ ] **Step 1: Write the failing tests** (append; include `meeting_history_widgets.h`, `call_notes_controller.h`, `<QDateTime>`, `<QLabel>`, `<QPushButton>`, `<QRegularExpression>`, `<QToolButton>`, `<QSignalSpy>`)

```cpp
TEST(PreviousMeetingWidgetTest, PreviousMeetingShowsLatestCompletedMeetingOfClient) {
  PreviousMeetingWidget widget;
  EXPECT_TRUE(widget.isHidden());
  MeetingRecord record;
  record.materialId = 7; record.startMs = QDateTime(QDate(2026, 10, 14), QTime(15, 30)).toMSecsSinceEpoch();
  record.durationMs = 50 * 60000; record.summary = "Anxiety before meetings";
  record.agreements = {"Breathing practice", "Sleep diary", "Walk", "Fourth"};
  widget.setMeeting(record);
  EXPECT_FALSE(widget.isHidden());
  EXPECT_TRUE(widget.findChild<QLabel *>("previousMeetingDate")->text().startsWith("14.10"));
  EXPECT_EQ(widget.findChild<QLabel *>("previousMeetingPreview")->text(), "Anxiety before meetings");
  EXPECT_EQ(widget.findChildren<QLabel *>("previousMeetingAgreement").size(), 3);
  QSignalSpy open(&widget, &PreviousMeetingWidget::openRequested);
  widget.findChild<QPushButton *>("previousMeetingOpen")->click();
  ASSERT_EQ(open.count(), 1); EXPECT_EQ(open[0][0].toLongLong(), 7);
  widget.setMeeting(std::nullopt); EXPECT_TRUE(widget.isHidden());
}

TEST_F(MeetingHistoryTest, ExpandingAMeetingShowsItsWidgetsAndCollapsesOthers) {
  const auto first = meeting({{"final", QJsonObject{{"note", QJsonObject{{"text", "First"}}}}}});
  const auto second = meeting({{"final", QJsonObject{{"note", QJsonObject{{"text", "Second"}}}}}});
  MeetingHistoryList list;
  int created = 0;
  list.setControllerFactory([&](qint64 id, QObject *parent) {
    ++created; auto *c = new CallNotesController(db, parent); c->openMaterial(id); return c;
  });
  list.setMeetings(loadMeetingHistory(*db, client, 0), {"Loose"});
  EXPECT_EQ(list.findChildren<QToolButton *>(QRegularExpression("^historyRow_")).size(), 2);
  EXPECT_EQ(list.findChild<QLabel *>("historyLooseNote")->text(), "Loose");
  list.expand(first);
  auto *firstBody = list.findChild<QWidget *>(QStringLiteral("historyBody_%1").arg(first));
  ASSERT_NE(firstBody, nullptr); EXPECT_FALSE(firstBody->isHidden());
  EXPECT_EQ(firstBody->findChild<QLabel *>("aiSummaryText")->text(), "First");
  list.expand(second);
  EXPECT_TRUE(firstBody->isHidden());
  EXPECT_EQ(created, 2);
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build-release --target Sessio_meeting_history_tests --parallel 4`
Expected: missing header.

- [ ] **Step 3: Implement** (`meeting_history_widgets.cpp`)

```cpp
#include "meeting_history_widgets.h"
#include "agreements_widget.h"
#include "ai_summary_widget.h"
#include "call_notes_controller.h"
#include "key_points_widget.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QString meetingTitle(const MeetingRecord &m) {
  const auto date = QDateTime::fromMSecsSinceEpoch(m.startMs).toString(QStringLiteral("dd.MM.yyyy HH:mm"));
  if (m.durationMs <= 0) return date;
  return QCoreApplication::translate("MeetingHistory", "%1 · %2 min").arg(date).arg(m.durationMs / 60000);
}
QLabel *plain(const QString &text, const char *name, QWidget *parent) {
  auto *label = new QLabel(text, parent); label->setObjectName(QString::fromLatin1(name));
  label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  return label;
}
}

PreviousMeetingWidget::PreviousMeetingWidget(QWidget *parent) : NotesWidgetFrame(tr("Last time"), parent) {
  hide();
  connect(addAction(tr("Open"), "previousMeetingOpen"), &QPushButton::clicked, this, [this] {
    emit openRequested(property("materialId").toLongLong());
  });
}

void PreviousMeetingWidget::setMeeting(const std::optional<MeetingRecord> &meeting) {
  while (auto *item = body()->takeAt(0)) { delete item->widget(); delete item; }
  setVisible(meeting.has_value());
  if (!meeting) return;
  setProperty("materialId", meeting->materialId);
  body()->addWidget(plain(meetingTitle(*meeting), "previousMeetingDate", this));
  body()->addWidget(plain(meetingPreview(*meeting), "previousMeetingPreview", this));
  for (const auto &agreement : meeting->agreements.mid(0, 3))
    body()->addWidget(plain(QStringLiteral("• ") + agreement, "previousMeetingAgreement", this));
}

MeetingHistoryList::MeetingHistoryList(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(8);
}

void MeetingHistoryList::setControllerFactory(MeetingControllerFactory factory) { mFactory = std::move(factory); }

void MeetingHistoryList::setMeetings(const std::vector<MeetingRecord> &meetings, const QStringList &loose) {
  auto *column = qobject_cast<QVBoxLayout *>(layout());
  while (auto *item = column->takeAt(0)) { delete item->widget(); delete item; }
  mMeetings.clear();
  for (const auto &meeting : meetings) {
    mMeetings.insert(meeting.materialId, meeting);
    auto *card = new NotesWidgetFrame(meetingTitle(meeting), this);
    auto *toggle = new QToolButton(card); toggle->setObjectName(QStringLiteral("historyRow_%1").arg(meeting.materialId));
    toggle->setCheckable(true); toggle->setText(tr("Show")); toggle->setAccessibleName(meetingTitle(meeting));
    card->body()->addWidget(plain(meetingPreview(meeting), "historyPreview", card));
    card->body()->addWidget(toggle);
    auto *details = new QWidget(card); details->setObjectName(QStringLiteral("historyBody_%1").arg(meeting.materialId));
    new QVBoxLayout(details); details->layout()->setContentsMargins(0, 0, 0, 0); details->hide();
    card->body()->addWidget(details);
    connect(toggle, &QToolButton::toggled, this, [this, id = meeting.materialId](bool on) {
      if (on) expand(id); else if (auto *d = findChild<QWidget *>(QStringLiteral("historyBody_%1").arg(id))) d->hide();
    });
    column->addWidget(card);
  }
  if (!loose.isEmpty()) {
    column->addWidget(plain(tr("Without a meeting"), "historyLooseTitle", this));
    for (const auto &note : loose) column->addWidget(plain(note, "historyLooseNote", this));
  }
}

void MeetingHistoryList::expand(qint64 materialId) {
  for (auto *details : findChildren<QWidget *>(QRegularExpression(QStringLiteral("^historyBody_")))) {
    const bool target = details->objectName() == QStringLiteral("historyBody_%1").arg(materialId);
    if (target && details->layout()->count() == 0) populate(details, mMeetings.value(materialId));
    details->setVisible(target);
    if (auto *toggle = findChild<QToolButton *>(QStringLiteral("historyRow_") + details->objectName().mid(12))) {
      const QSignalBlocker blocker(toggle); toggle->setChecked(target); toggle->setText(target ? tr("Hide") : tr("Show"));
    }
  }
}

void MeetingHistoryList::populate(QWidget *details, const MeetingRecord &meeting) {
  auto *layout = details->layout();
  if (mFactory) {
    auto *controller = mFactory(meeting.materialId, details);
    auto *summary = new AiSummaryWidget(controller, details);
    auto *points = new KeyPointsWidget(controller, details);
    connect(summary, &AiSummaryWidget::sourceRequested, this, &MeetingHistoryList::sourceRequested);
    connect(points, &KeyPointsWidget::sourceRequested, this, &MeetingHistoryList::sourceRequested);
    layout->addWidget(summary); layout->addWidget(points);
    points->setVisible(points->hasContent());
    if (controller)
      for (const auto &entry : controller->noteEntries())
        layout->addWidget(plain(entry.toObject()["markdown"].toString(), "historyNote", details));
  }
  QJsonArray records;
  for (const auto &text : meeting.agreements) records.append(QJsonObject{{"text", text}, {"active", true}});
  auto *agreements = new AgreementsWidget(details); agreements->setRecords(records);
  agreements->setVisible(agreements->hasContent());
  layout->addWidget(agreements);
}
```

Header private members: `void populate(QWidget *details, const MeetingRecord &meeting); MeetingControllerFactory mFactory; QHash<qint64, MeetingRecord> mMeetings;`. `PreviousMeetingWidget::setMeeting` must keep `previousMeetingDate` text starting with `dd.MM` — `meetingTitle` uses `dd.MM.yyyy HH:mm`, which satisfies `startsWith("14.10")`.

- [ ] **Step 4: Run tests**

Run: `cmake build-release && cmake --build build-release --target Sessio_meeting_history_tests --parallel 4 && ./build-release/test/Sessio_meeting_history_tests`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/pages/transcript_page/meeting_history_widgets.* src/pages/transcript_page/CMakeLists.txt test/meeting_history_tests.cpp
git commit -m "Add previous meeting and meeting history widgets (#151)"
```

---

### Task 8: Embedded mode for ClientNotesPage

**Files:**
- Modify: `src/pages/client_notes_page/client_notes_page.h` (public API), `client_notes_page.cpp` (`reloadCallEntries` at ~884, constructor layout ~365-470)
- Test: `test/client_notes_page_ui_tests.cpp`

**Interfaces:**
- Produces: `void ClientNotesPage::setEmbedded(bool embedded);` — when true: the header surface, feed filter, jump buttons and «Client history» are not shown; only this meeting's composer and entries remain; the feed scroll area never scrolls and the page's minimum height follows its content, so an outer scroll area scrolls it. The «This meeting» heading is not added (the outer card has the title).

- [ ] **Step 1: Write the failing test** (append to `test/client_notes_page_ui_tests.cpp`, reusing its fixture/style setup)

```cpp
TEST(ClientNotesPageEmbeddedTest, EmbeddedPageShowsOnlyThisMeetingWithoutOwnScroll) {
  QTemporaryDir dir;
  pcm::config::Config conf{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir.path().toStdString())}};
  auto db = std::make_shared<pcm::database::Database>(conf);
  ClientNotesPage page(db);
  page.setEmbedded(true);
  QJsonArray entries{QJsonObject{{"id", "e1"}, {"markdown", "First"}, {"created_at", 1}}};
  page.setCallNoteActions(1, {.entries = [&] { return entries; }});
  page.setClientInfo(*db->get_clients().at(0));
  page.resize(360, 600); page.show(); QCoreApplication::processEvents();
  for (auto *label : page.findChildren<QLabel *>()) {
    EXPECT_NE(label->text(), "Client history");
    EXPECT_NE(label->text(), "This meeting");
  }
  auto *scroll = page.findChild<QScrollArea *>();
  ASSERT_NE(scroll, nullptr);
  EXPECT_EQ(scroll->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
  EXPECT_GE(page.minimumHeight(), page.findChild<QWidget *>("notesEntryBody")->height());
}
```

Adjust the `CallNoteActions` initialisation and the entry keys to the real field names (`grep -n "struct CallNoteActions" -A9 src/pages/client_notes_page/client_notes_page.h`, `grep -n '\["' src/pages/client_notes_page/client_notes_page.cpp | grep entry`).

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build-release --target Sessio_client_notes_page_ui_tests --parallel 4`
Expected: `no member named 'setEmbedded'`.

- [ ] **Step 3: Implement**

Header: `void setEmbedded(bool embedded);` public; private `bool mEmbedded = false; QWidget *mHeaderSurface = nullptr;` (store `headerSurface` in the constructor).

`client_notes_page.cpp`:

```cpp
void ClientNotesPage::setEmbedded(bool embedded) {
  mEmbedded = embedded;
  mHeaderSurface->setVisible(!embedded);
  mScrollArea->setVerticalScrollBarPolicy(embedded ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
  mJumpToTodayButton->setVisible(false); mJumpToLatestButton->setVisible(false);
  refresh();
}
```

In `reloadCallEntries()`: wrap the `hasClient` branch so that when `mEmbedded` it calls `clearNotes()` instead of `reloadNotes()` and does not add the «Client history» label; skip adding the «This meeting» heading when `mEmbedded`. At the end of `reloadCallEntries()` and in `arrangeCompactUi()` add:

```cpp
  if (mEmbedded) {
    mFeedWidget->adjustSize();
    mScrollArea->setMinimumHeight(mFeedWidget->sizeHint().height());
    setMinimumHeight(sizeHint().height());
  }
```

In `setCallNoteActions` the header widgets are already hidden; keep that. Make sure `setClientInfo` in call mode does not re-show `mHeaderSurface` when embedded (guard any `show()` of it with `!mEmbedded`).

- [ ] **Step 4: Run tests**

Run: `cmake --build build-release --target Sessio_client_notes_page_ui_tests --parallel 4 && QT_QPA_PLATFORM=offscreen ./build-release/test/Sessio_client_notes_page_ui_tests`
Expected: PASS, including the existing tests.

- [ ] **Step 5: Commit**

```bash
git add src/pages/client_notes_page/client_notes_page.* test/client_notes_page_ui_tests.cpp
git commit -m "Add an embedded mode to the meeting notes editor (#151)"
```

---

### Task 9: CallNotesPage

**Files:**
- Create: `src/pages/transcript_page/call_notes_page.h/.cpp`
- Modify: `src/pages/transcript_page/CMakeLists.txt`
- Create: `test/call_notes_page_tests.cpp`; Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 3–8.
- Produces:

```cpp
class CallNotesPage final : public QWidget {
  Q_OBJECT
public:
  CallNotesPage(std::shared_ptr<pcm::database::Database> db, CallNotesController *controller,
                QWidget *parent = nullptr);
  ClientNotesPage *editor() const;              // «Мои заметки» body, embedded
  void setClient(const std::optional<DuckClient> &client);  // reloads «В прошлый раз» and history
  void setMeeting(const QString &details, const QString &status);  // «Встреча» card
  void setAgreements(const QJsonArray &records);
  void setControllerFactory(MeetingControllerFactory factory);
  void showHistory(qint64 materialId);          // switch to «История», expand that meeting
  bool historyMode() const;
signals:
  void sourceRequested(qint64 phraseId);
  void retryRequested();
};
```

Object names: switch buttons `notesModeCurrent`, `notesModeHistory` (checkable, exclusive; the switch row `notesModeSwitch`); cards: `meetingInfoCard` (NotesWidgetFrame «Meeting», label `meetingInfoDetails`), `myNotesCard` (NotesWidgetFrame «My notes» whose body holds `editor()`), plus the widgets from Tasks 4, 5, 7 (`AiSummaryWidget`, `KeyPointsWidget`, `AgreementsWidget`, `PreviousMeetingWidget`), history page `MeetingHistoryList`. The two modes are two `QWidget` columns inside one `QScrollArea`; only one is visible.

Visibility rules: switch row and «Last time» hidden without a client (`client->id <= 0`); `AiSummaryWidget`/`KeyPointsWidget`/`AgreementsWidget` visible only when `hasContent()` (re-evaluated on their `contentChanged()` and after `setAgreements`); «Meeting» and «My notes» always visible in «This meeting» mode.

- [ ] **Step 1: Write the failing tests** (`test/call_notes_page_tests.cpp`, with `NotesFixture` and the shared `main`)

```cpp
#include "call_notes_page.h"
#include "ai_summary_widget.h"
#include "agreements_widget.h"
#include "call_notes_controller.h"
#include "client_notes_page.h"
#include "key_points_widget.h"
#include "meeting_history_widgets.h"
#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <oclero/qlementine/style/QlementineStyle.hpp>
#include <gtest/gtest.h>

class CallNotesPageTest : public ::testing::Test, public NotesFixture {};

TEST_F(CallNotesPageTest, EmptyWidgetsAreHidden) {
  controller.beginCall();
  CallNotesPage page(db, &controller);
  page.show();
  EXPECT_FALSE(page.findChild<QWidget *>("meetingInfoCard")->isHidden());
  EXPECT_FALSE(page.findChild<QWidget *>("myNotesCard")->isHidden());
  EXPECT_TRUE(page.findChild<AiSummaryWidget *>()->isHidden());
  EXPECT_TRUE(page.findChild<KeyPointsWidget *>()->isHidden());
  EXPECT_TRUE(page.findChild<AgreementsWidget *>()->isHidden());
  EXPECT_TRUE(page.findChild<QWidget *>("notesModeSwitch")->isHidden());
  EXPECT_TRUE(page.findChild<PreviousMeetingWidget *>()->isHidden());
}

TEST_F(CallNotesPageTest, GeneratedSummaryAndAgreementsAppear) {
  CallNotesPage page(db, &controller);
  page.show();
  generate();
  EXPECT_FALSE(page.findChild<AiSummaryWidget *>()->isHidden());
  EXPECT_FALSE(page.findChild<KeyPointsWidget *>()->isHidden());
  page.setAgreements(QJsonArray{QJsonObject{{"text", "Sleep diary"}, {"active", true}}});
  EXPECT_FALSE(page.findChild<AgreementsWidget *>()->isHidden());
}

TEST_F(CallNotesPageTest, ClientShowsSwitchAndHistoryModeExpandsMeeting) {
  const auto client = *db->get_clients().at(0);
  const auto past = db->create_call_material(std::nullopt);
  const auto transcript = db->add_transcript(std::nullopt, "live_local_v1");
  ASSERT_TRUE(db->link_call_material_transcript(past, transcript));
  ASSERT_TRUE(db->set_transcript_clients(transcript, {client.id}));
  ASSERT_TRUE(db->save_call_material(past, db->get_call_material(past).revision,
      R"({"ended":true,"final":{"note":{"text":"Past summary"}}})"));
  CallNotesPage page(db, &controller);
  page.setControllerFactory([this](qint64 id, QObject *parent) {
    auto *c = new CallNotesController(db, parent); c->openMaterial(id); return c;
  });
  page.setClient(client); page.show();
  EXPECT_FALSE(page.findChild<QWidget *>("notesModeSwitch")->isHidden());
  auto *previous = page.findChild<PreviousMeetingWidget *>();
  EXPECT_FALSE(previous->isHidden());
  previous->findChild<QPushButton *>("previousMeetingOpen")->click();
  EXPECT_TRUE(page.historyMode());
  EXPECT_FALSE(page.findChild<QWidget *>(QStringLiteral("historyBody_%1").arg(past))->isHidden());
  page.findChild<QPushButton *>("notesModeCurrent")->click();
  EXPECT_FALSE(page.historyMode());
}

TEST_F(CallNotesPageTest, EditorIsEmbedded) {
  CallNotesPage page(db, &controller);
  EXPECT_EQ(page.editor()->findChild<QScrollArea *>()->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
}
```

Register:

```cmake
add_executable(Sessio_call_notes_page_tests call_notes_page_tests.cpp)
target_link_libraries(Sessio_call_notes_page_tests PRIVATE GTest::gtest Qt6::Test Sessio_transcription_ui)
target_include_directories(Sessio_call_notes_page_tests PRIVATE ${qlementine_SOURCE_DIR}/lib/include)
gtest_discover_tests(Sessio_call_notes_page_tests)
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake build-release && cmake --build build-release --target Sessio_call_notes_page_tests --parallel 4`
Expected: missing header.

- [ ] **Step 3: Implement** (`call_notes_page.cpp`; header per the interface with private members listed in the code)

```cpp
#include "call_notes_page.h"
#include "agreements_widget.h"
#include "ai_summary_widget.h"
#include "call_notes_controller.h"
#include "client_notes_page.h"
#include "key_points_widget.h"
#include "meeting_history.h"
#include "meeting_history_widgets.h"
#include "notes_widget_frame.h"
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

CallNotesPage::CallNotesPage(std::shared_ptr<pcm::database::Database> db, CallNotesController *controller,
                             QWidget *parent)
    : QWidget(parent), mDb(std::move(db)), mController(controller) {
  auto *outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(8);
  mSwitch = new QWidget(this); mSwitch->setObjectName("notesModeSwitch");
  auto *switchLayout = new QHBoxLayout(mSwitch); switchLayout->setContentsMargins(12, 8, 12, 0);
  auto *current = new QPushButton(tr("This meeting"), mSwitch); current->setObjectName("notesModeCurrent");
  auto *history = new QPushButton(tr("History"), mSwitch); history->setObjectName("notesModeHistory");
  auto *group = new QButtonGroup(this); group->setExclusive(true);
  for (auto *button : {current, history}) { button->setCheckable(true); group->addButton(button); switchLayout->addWidget(button); }
  current->setChecked(true);
  outer->addWidget(mSwitch); mSwitch->hide();

  auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *content = new QWidget(scroll); auto *contentLayout = new QVBoxLayout(content);
  contentLayout->setContentsMargins(12, 8, 12, 12); contentLayout->setSpacing(10);

  mCurrent = new QWidget(content); auto *column = new QVBoxLayout(mCurrent);
  column->setContentsMargins(0, 0, 0, 0); column->setSpacing(10);
  mPrevious = new PreviousMeetingWidget(mCurrent); column->addWidget(mPrevious);
  mMeeting = new NotesWidgetFrame(tr("Meeting"), mCurrent); mMeeting->setObjectName("meetingInfoCard");
  mMeetingDetails = new QLabel(mMeeting); mMeetingDetails->setObjectName("meetingInfoDetails");
  mMeetingDetails->setWordWrap(true); mMeeting->body()->addWidget(mMeetingDetails); column->addWidget(mMeeting);
  auto *notesCard = new NotesWidgetFrame(tr("My notes"), mCurrent); notesCard->setObjectName("myNotesCard");
  mEditor = new ClientNotesPage(mDb, notesCard); mEditor->setEmbedded(true);
  notesCard->body()->addWidget(mEditor); column->addWidget(notesCard);
  mSummary = new AiSummaryWidget(controller, mCurrent); column->addWidget(mSummary);
  mPoints = new KeyPointsWidget(controller, mCurrent); column->addWidget(mPoints);
  mAgreements = new AgreementsWidget(mCurrent); column->addWidget(mAgreements);
  column->addStretch();

  mHistory = new MeetingHistoryList(content);
  contentLayout->addWidget(mCurrent); contentLayout->addWidget(mHistory); mHistory->hide();
  scroll->setWidget(content); outer->addWidget(scroll, 1);

  connect(current, &QPushButton::clicked, this, [this] { setHistoryMode(false); });
  connect(history, &QPushButton::clicked, this, [this] { setHistoryMode(true); });
  connect(mPrevious, &PreviousMeetingWidget::openRequested, this, &CallNotesPage::showHistory);
  connect(mSummary, &AiSummaryWidget::contentChanged, this, &CallNotesPage::updateVisibility);
  connect(mPoints, &KeyPointsWidget::contentChanged, this, &CallNotesPage::updateVisibility);
  connect(mSummary, &AiSummaryWidget::sourceRequested, this, &CallNotesPage::sourceRequested);
  connect(mPoints, &KeyPointsWidget::sourceRequested, this, &CallNotesPage::sourceRequested);
  connect(mHistory, &MeetingHistoryList::sourceRequested, this, &CallNotesPage::sourceRequested);
  connect(mSummary, &AiSummaryWidget::retryRequested, this, &CallNotesPage::retryRequested);
  updateVisibility();
}

ClientNotesPage *CallNotesPage::editor() const { return mEditor; }

void CallNotesPage::setClient(const std::optional<DuckClient> &client) {
  mClientId = client ? client->id : 0;
  mEditor->setClientInfo(client);
  reloadHistory();
}

void CallNotesPage::reloadHistory() {
  const bool hasClient = mClientId > 0;
  mSwitch->setVisible(hasClient);
  if (!hasClient) { mPrevious->setMeeting(std::nullopt); setHistoryMode(false); return; }
  const auto exclude = mController ? mController->materialId() : 0;
  const auto meetings = loadMeetingHistory(*mDb, mClientId, exclude);
  mPrevious->setMeeting(meetings.empty() ? std::nullopt : std::optional<MeetingRecord>(meetings.front()));
  mHistory->setMeetings(meetings, loadNotesWithoutMeeting(*mDb, mClientId));
}

void CallNotesPage::setMeeting(const QString &details, const QString &status) {
  mMeetingDetails->setText(details); mMeeting->setStatus(status);
}

void CallNotesPage::setAgreements(const QJsonArray &records) { mAgreements->setRecords(records); updateVisibility(); }

void CallNotesPage::setControllerFactory(MeetingControllerFactory factory) { mHistory->setControllerFactory(std::move(factory)); }

void CallNotesPage::showHistory(qint64 materialId) { setHistoryMode(true); mHistory->expand(materialId); }

bool CallNotesPage::historyMode() const { return !mHistory->isHidden(); }

void CallNotesPage::setHistoryMode(bool history) {
  mHistory->setVisible(history); mCurrent->setVisible(!history);
  if (auto *button = findChild<QPushButton *>(history ? "notesModeHistory" : "notesModeCurrent")) button->setChecked(true);
}

void CallNotesPage::updateVisibility() {
  mSummary->setVisible(mSummary->hasContent());
  mPoints->setVisible(mPoints->hasContent());
  mAgreements->setVisible(mAgreements->hasContent());
}
```

Private members: `void reloadHistory(); void setHistoryMode(bool); void updateVisibility(); std::shared_ptr<pcm::database::Database> mDb; QPointer<CallNotesController> mController; qint64 mClientId = 0; QWidget *mSwitch, *mCurrent; PreviousMeetingWidget *mPrevious; NotesWidgetFrame *mMeeting; QLabel *mMeetingDetails; ClientNotesPage *mEditor; AiSummaryWidget *mSummary; KeyPointsWidget *mPoints; AgreementsWidget *mAgreements; MeetingHistoryList *mHistory;`. `EditorIsEmbedded` relies on Task 8.

Also reload history when the controller's call ends (a just-finished call must not appear as «Last time» while it is still open; `exclude` handles it), so connect `CallNotesController::changed` to nothing extra — history reloads on `setClient` only.

- [ ] **Step 4: Run tests**

Run: `cmake --build build-release --target Sessio_call_notes_page_tests --parallel 4 && QT_QPA_PLATFORM=offscreen ./build-release/test/Sessio_call_notes_page_tests`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/pages/transcript_page/call_notes_page.* src/pages/transcript_page/CMakeLists.txt test/call_notes_page_tests.cpp test/CMakeLists.txt
git commit -m "Assemble the call notes widgets page (#151)"
```

---

### Task 10: Wire the page into the app and remove CallNotesPanel

**Files:**
- Modify: `src/app/application.cpp:1132-1200` (live call) and `:1357-1400` (saved materials dialog); `src/app/application.h:127` stays `QPointer<ClientNotesPage> mCallNotesPanel` (now pointing at `CallNotesPage::editor()`).
- Delete: `src/pages/transcript_page/call_notes_panel.h/.cpp`, `test/call_notes_panel_tests.cpp`; remove them from both CMake lists.
- Modify: `src/pages/transcript_page/transcript_page.cpp:134` only if it references `CallNotesPanel` (check with grep).

**Interfaces:**
- Consumes: `CallNotesPage` (Task 9).

- [ ] **Step 1: Replace the live-call wiring**

In `application.cpp` replace

```cpp
    mCallNotesPanel = new ClientNotesPage(mDb, callsPage);
    auto *notes = new CallNotesController(mDb, this);
    auto *notesPanel = new CallNotesPanel(notes);
    notesPanel->setInlineMode(true);
    mCallNotesPanel->setAiPanelWidget(notesPanel);
```

with

```cpp
    auto *notes = new CallNotesController(mDb, this);
    auto *notesPage = new CallNotesPage(mDb, notes, callsPage);
    mCallNotesPanel = notesPage->editor();
```

Then:
- `new CallSidePanel(mCallNotesPanel, panel, callsPage)` → `new CallSidePanel(notesPage, panel, callsPage)`.
- Delete the `connect(notesPanel, &CallNotesPanel::editorRequested, ...)` line.
- `connect(notesPanel, &CallNotesPanel::generationCancelled, ...)` → delete (cancel lives in the controller now; `*notesWanted` is reset where cancel is requested by the prompter panel).
- `connect(notesPanel, &CallNotesPanel::startRequested, prompterPanel, &PrompterPanel::startRequested);` → `connect(notesPage, &CallNotesPage::retryRequested, prompterPanel, &PrompterPanel::startRequested);`
- `connect(notesPanel, &CallNotesPanel::sourceRequested, sidePanel, ...)` → same lambda on `connect(notesPage, &CallNotesPage::sourceRequested, sidePanel, ...)`.
- Agreements: after `auto *prompter = new CallPrompterController(mDb, this);` add

```cpp
    connect(prompter, &CallPrompterController::cardsChanged, notesPage, [notesPage, prompter] {
      notesPage->setAgreements(prompter->records());
    });
```

- Client: where `mCallNotesPanel->setClientInfo(callClient)` is called (~1308), call `notesPage->setClient(callClient);` instead (it forwards to the editor). Capture `notesPage` in that lambda.
- Meeting card: in the same place (call start) set `notesPage->setMeeting(<client name> + " · " + start time "dd.MM HH:mm", tr("live"))`; on call end (`notes->finishCall()` ~1318) set `notesPage->setMeeting(<same details>, tr("ended"))`. Use only the client display name already used in the call header — no contact details.
- History controllers: after `finals` is created add

```cpp
    notesPage->setControllerFactory([this, finals](qint64 materialId, QObject *parent) {
      auto *saved = new CallNotesController(mDb, parent);
      saved->openMaterial(materialId);
      connect(finals, &CallNotesFinalizer::materialChanged, saved, [saved](qint64 id) {
        if (saved->materialId() == id) saved->refreshFromStorage();
      });
      connect(saved, &CallNotesController::generationRequested, saved, [saved, finals] {
        saved->deferFinal(); finals->enqueue(saved->materialId());
      });
      return saved;
    });
```

- [ ] **Step 2: Replace the saved-materials dialog**

In `openMaterials`, replace the `CallNotesPanel *view` + `ClientNotesPage *editor` block with:

```cpp
      auto *page = new CallNotesPage(mDb, saved, dialog);
      bindNotesEditor(page->editor(), saved);
      try {
        const auto material = mDb->get_call_material(saved->materialId());
        if (material.event_id) page->setClient(mDb->get_client_by_event(*material.event_id));
      } catch (...) {}
      connect(page->editor(), &ClientNotesPage::openEventRequested, mMainWindow.get(), &MainWindow::returnToEvent);
      if (saved == notes)
        connect(page, &CallNotesPage::retryRequested, prompterPanel, &PrompterPanel::startRequested);
      auto *body = new QVBoxLayout(dialog); body->addWidget(page);
      connect(page, &CallNotesPage::sourceRequested, dialog, /* the existing view->sourceRequested lambda body */);
```

Keep the existing lambda body of `connect(view, &CallNotesPanel::sourceRequested, dialog, ...)` unchanged, only the sender changes.

- [ ] **Step 3: Delete CallNotesPanel**

```bash
git rm src/pages/transcript_page/call_notes_panel.h src/pages/transcript_page/call_notes_panel.cpp test/call_notes_panel_tests.cpp
grep -rn "CallNotesPanel\|call_notes_panel" src test CMakeLists.txt
```

Remove the entries from `src/pages/transcript_page/CMakeLists.txt` and the `Sessio_call_notes_panel_tests` block from `test/CMakeLists.txt`. Expected grep output after edits: nothing. `ClientNotesPage::setAiPanelWidget` becomes unused — delete it and `mAiHost` (and any `mAiHost` references in `reloadCallEntries`, replacing `mFeedLayout->indexOf(mAiHost)` positions with `mFeedLayout->count() - 1`).

- [ ] **Step 4: Build and run the whole suite**

Run: `cmake build-release && cmake --build build-release --parallel 4 && QT_QPA_PLATFORM=offscreen ctest --test-dir build-release --output-on-failure -j4 -LE models`
Expected: all tests PASS.

- [ ] **Step 5: Commit**

```bash
git add -A src/app/application.cpp src/pages test/CMakeLists.txt
git commit -m "Show call notes as a widgets page and drop the old panel (#151)"
```

---

### Task 11: Translations, docs, changelog, manual check

**Files:**
- Modify: `translation/app_ru.ts`, `translation/app_en.ts`
- Modify: `CHANGELOG.md` (0.2.33 section), `docs/antora/modules/ROOT/pages/call-prompter.adoc` (section «Заметки и материалы звонка»)

- [ ] **Step 1: Update translation sources**

Run: `cmake --build build-release --target update_translations`
Then translate every `type="unfinished"` entry in `translation/app_ru.ts` (and fill `app_en.ts` with the source text). Russian for the main strings:

| Source | Russian |
|---|---|
| AI summary | Итог ИИ |
| Key points | Ключевые пункты |
| Agreements | Договорённости |
| Last time | В прошлый раз |
| Meeting | Встреча |
| My notes | Мои заметки |
| This meeting | Эта встреча |
| History | История |
| Without a meeting | Без встречи |
| Open / Show / Hide | Открыть / Показать / Скрыть |
| Edit / Save / Cancel | Изменить / Сохранить / Отмена |
| Retry / Update | Повторить / Обновить |
| Create again / Delete | Создать заново / Удалить |
| More actions | Ещё действия |
| updating… / ready / outdated / failed | обновляется… / готов / устарел / ошибка |
| whole meeting · %n phrases | вся встреча · %n фраз (plural forms) |
| through %1 · %n phrases | до %1 · %n фраз (plural forms) |
| %1 · %2 min | %1 · %2 мин |
| The transcript changed after this summary. | Транскрипт изменился после этого итога. |
| Turn on transcription to get a summary | Включите транскрипцию, чтобы получить итог |
| Delete AI summary | Удалить итог ИИ |
| Delete the AI summary of this meeting? Your own notes are kept. | Удалить итог ИИ этой встречи? Ваши заметки останутся. |
| Edit AI summary | Изменить итог ИИ |
| Could not save — keep this page open | (already translated; reuse) |
| live / ended | идёт / завершена |

Check: `grep -c 'type="unfinished"' translation/app_ru.ts translation/app_en.ts` → `0` for both; obsolete entries of the deleted panel are marked `vanished` by lupdate — leave them.

- [ ] **Step 2: Docs and changelog**

`CHANGELOG.md`, under `## [0.2.33]`, replace the bullets about «editable model drafts» and «Updated the Notes, Transcript and Prompter tabs…» with:

```markdown
- Reworked the call Notes tab into a page of cards: meeting, my notes, AI
  summary, key points and agreements, each with its own status and labelled
  actions; the AI summary is kept with the call without an accept step.
- Added "Last time" and a History view with the client's earlier meetings.
```

In `call-prompter.adoc` update the Notes section to describe the cards, the «Эта встреча | История» switch and that the summary is stored without acceptance (3–6 sentences, same style as the surrounding text).

- [ ] **Step 3: Full check**

Run: `cmake --build build-release --parallel 4 && QT_QPA_PLATFORM=offscreen ctest --test-dir build-release --output-on-failure -j4 -LE models && git diff --check`
Expected: all PASS, no whitespace errors.

- [ ] **Step 4: Manual check with the user**

Build the host binary (`distrobox-host-exec cmake --build build-host --parallel 4 --target Sessio`). Ask the user before launching; if Sessio is running, do not touch it. With the user, check: a live call shows Meeting / My notes / AI summary appearing; after the call the summary turns «готов» automatically; «⋯ → Удалить» asks first; «В прошлый раз» → «Открыть» opens History on that meeting. Take one screenshot for the PR.

- [ ] **Step 5: Commit**

```bash
git add translation/app_ru.ts translation/app_en.ts CHANGELOG.md docs/antora/modules/ROOT/pages/call-prompter.adoc
git commit -m "Translate and document the call notes widgets page (#151)"
```
