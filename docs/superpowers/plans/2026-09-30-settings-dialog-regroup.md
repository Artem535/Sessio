# Перегруппировка диалога настроек — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** разгрузить перегруженную вкладку **General** (`SettingsDialog`, `src/app/settings_dialog.cpp`), выделив Backup и Privacy & Security в отдельные вкладки верхнего уровня, и исправить смысловой разнобой в коробке Events `"Timeline colors"` (на деле там расписание по умолчанию + billing + цвета).

**Architecture:** чисто композиционная правка `SettingsDialog::setupUi()`. Виджеты, их `objectName` (кроме новых, добавляемых этим планом там, где их не было), сигналы/слоты (`connectSignals()`), хранение (`pcm::app_settings::*`) — не меняются. Меняется только на какую `QStackedWidget`-страницу и в какой `QGroupBox` каждый контрол попадает.

**Tech Stack:** C++20, Qt Widgets, `oclero::qlementine::SegmentedControl`/`Switch`/`ColorEditor` (без изменений в этих классах).

## Global Constraints

- Ничего не удаляется и не переименовывается на уровне `pcm::app_settings::*` API или `connectSignals()` — только физическое место контрола в дереве виджетов.
- Единственный существующий тест-файл (`test/settings_dialog_livekit_section_tests.cpp`) не должен сломаться — LiveKit-вкладка не меняется по содержимому; тест ищет виджеты через `findChild`, индекс вкладки ему не важен.
- `makeSettingRow()` — переиспользуется как есть, не меняется.
- Диалог настроек никогда не выше доступной высоты экрана, а каждая вкладка прокручивается по вертикали, если её содержимое не помещается (исходная жалоба: окно было слишком длинным и не влезало на экран) — см. Task 4.
- Итоговое число вкладок верхнего уровня — 6 (General, Privacy & Security, Backup, Events, Online, LiveKit), не больше.
- Каждый шаг, трогающий `tr()`-строки (новые заголовки вкладок/коробок), заканчивается синхронизацией `translation/app_ru.ts`/`app_en.ts`.
- Каждый MR поднимает версию и обновляет `CHANGELOG.md` (см. Task 5).

---

### Task 1: Новые вкладки "Privacy & Security" и "Backup" — каркас + перенос Backup

**Files:**
- Modify: `src/app/settings_dialog.cpp`
- Test: `test/settings_dialog_layout_tests.cpp` (новый файл)
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Produces: страницы `privacyPage` (индекс 1), `backupPage` (индекс 2) в `mSettingsStack`; итоговый порядок вкладок General(0)/Privacy & Security(1)/Backup(2)/Events(3)/Online(4)/LiveKit(5).

- [ ] **Step 1: Новый тест-файл, падающий**

Создать `test/settings_dialog_layout_tests.cpp`:

```cpp
#include "settings_dialog.h"
#include "fake_token_backend_credential_store.h"

#include <QApplication>
#include <QPushButton>
#include <QStackedWidget>
#include <QWidget>
#include <gtest/gtest.h>

namespace {
int pageIndexOf(SettingsDialog &dialog, const QString &objectName) {
  auto *stack = dialog.findChild<QStackedWidget *>();
  auto *control = dialog.findChild<QWidget *>(objectName);
  if (stack == nullptr || control == nullptr) {
    return -1;
  }
  QWidget *page = control;
  while (page != nullptr && stack->indexOf(page) == -1) {
    page = page->parentWidget();
  }
  return page == nullptr ? -1 : stack->indexOf(page);
}
} // namespace

TEST(SettingsDialogLayoutTest, CreateBackupButtonIsOnTheBackupPage) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  EXPECT_EQ(pageIndexOf(dialog, "createBackupButton"), 2);
}

TEST(SettingsDialogLayoutTest, BackupEncryptionSwitchIsOnTheBackupPage) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  EXPECT_EQ(pageIndexOf(dialog, "backupEncryptionEnabledSwitch"), 2);
}

TEST(SettingsDialogLayoutTest, AutoBackupEnabledSwitchIsOnTheBackupPage) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  EXPECT_EQ(pageIndexOf(dialog, "autoBackupEnabledSwitch"), 2);
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

В `test/CMakeLists.txt`, рядом с целью `Sessio_settings_dialog_livekit_section_tests`, добавить аналогичную цель `Sessio_settings_dialog_layout_tests` для нового файла — скопировать те же линкуемые библиотеки/инклюды, что у `Sessio_settings_dialog_livekit_section_tests`.

- [ ] **Step 2: Прогнать — должно упасть**

Run: `cmake --build build --target Sessio_settings_dialog_layout_tests && ctest --test-dir build -R SettingsDialogLayoutTest --output-on-failure`
Expected: FAIL (`pageIndexOf` возвращает `-1` — контролы либо не имеют `objectName`, либо сегодня на индексе 0/General, не 2).

- [ ] **Step 3: Реализация — вкладки и объектные имена**

В `setupUi()`:

1. Изменить список `mSettingsSections->addItem(...)` на:

```cpp
mSettingsSections->addItem(tr("General"), {}, {}, QStringLiteral("general"));
mSettingsSections->addItem(tr("Privacy & Security"), {}, {}, QStringLiteral("privacy"));
mSettingsSections->addItem(tr("Backup"), {}, {}, QStringLiteral("backup"));
mSettingsSections->addItem(tr("Events"), {}, {}, QStringLiteral("events"));
mSettingsSections->addItem(tr("Online"), {}, {}, QStringLiteral("online"));
mSettingsSections->addItem(tr("LiveKit"), {}, {}, QStringLiteral("livekit"));
```

2. Создать `privacyPage`/`backupPage` тем же способом, что `eventsPage`/`onlinePage`:

```cpp
auto *privacyPage = new QWidget(mSettingsStack);
auto *privacySettingsLayout = new QVBoxLayout(privacyPage);
privacySettingsLayout->setContentsMargins(0, 0, 0, 0);
privacySettingsLayout->setSpacing(16);

auto *backupPage = new QWidget(mSettingsStack);
auto *backupSettingsLayout = new QVBoxLayout(backupPage);
backupSettingsLayout->setContentsMargins(0, 0, 0, 0);
backupSettingsLayout->setSpacing(16);
```

и добавить их в `mSettingsStack` **в этом порядке**: `generalPage`, `privacyPage`, `backupPage`, `eventsPage`, `onlinePage` (LiveKit добавляется отдельно ниже по файлу — не трогать порядок его добавления относительно остальных, только вставить два новых `addWidget` перед `eventsPage`).

3. Перенести `backupBox` (строки создания — от `new QGroupBox(tr("Backup"), generalPage)` до соответствующего `generalSettingsLayout->addWidget(backupBox);`) на `backupPage`: заменить `generalPage` на `backupPage` в конструкторе `QGroupBox`, и `generalSettingsLayout->addWidget(backupBox)` на `backupSettingsLayout->addWidget(backupBox)`.

4. Аналогично перенести `autoBackupBox` (`"Automatic Backups"`) на `backupPage`, после `backupBox`.

5. Добавить `objectName`:
   - `mCreateBackupButton->setObjectName(QStringLiteral("createBackupButton"));` — сразу после создания.
   - `mBackupEncryptionEnabledSwitch->setObjectName(QStringLiteral("backupEncryptionEnabledSwitch"));`
   - `mAutoBackupEnabledSwitch->setObjectName(QStringLiteral("autoBackupEnabledSwitch"));`

6. Добавить `backupSettingsLayout->addStretch();` в конце (тем же паттерном, что `eventSettingsLayout->addStretch()`).

Строка `connect(mSettingsSections, &oclero::qlementine::SegmentedControl::currentIndexChanged, this, [...] { mSettingsStack->setCurrentIndex(mSettingsSections->currentIndex()); });` не меняется — она уже индекс-агностична относительно состава вкладок.

- [ ] **Step 4: Прогнать тесты**

Run: `cmake --build build --target Sessio_settings_dialog_layout_tests Sessio_settings_dialog_livekit_section_tests && ctest --test-dir build -R "SettingsDialogLayoutTest|SettingsDialogLiveKitSectionTest" --output-on-failure`
Expected: PASS все, включая старый LiveKit-тест (не должен был сломаться).

- [ ] **Step 5: Commit**

```bash
/usr/bin/git add src/app/settings_dialog.cpp test/settings_dialog_layout_tests.cpp test/CMakeLists.txt
/usr/bin/git commit -m "Split Backup settings into its own tab"
```

---

### Task 2: Notifications + App-lock/Privacy → вкладка "Privacy & Security"

**Files:**
- Modify: `src/app/settings_dialog.cpp`
- Modify: `test/settings_dialog_layout_tests.cpp`

**Interfaces:**
- Consumes: `privacyPage` (индекс 1, из Task 1).

- [ ] **Step 1: Падающие тесты**

Добавить в `test/settings_dialog_layout_tests.cpp`:

```cpp
TEST(SettingsDialogLayoutTest, NotificationsEnabledSwitchIsOnThePrivacyPage) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  EXPECT_EQ(pageIndexOf(dialog, "notificationsEnabledSwitch"), 1);
}

TEST(SettingsDialogLayoutTest, AppLockEnabledSwitchIsOnThePrivacyPage) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  EXPECT_EQ(pageIndexOf(dialog, "appLockEnabledSwitch"), 1);
}
```

- [ ] **Step 2: Прогнать — должно упасть**

Run: `cmake --build build --target Sessio_settings_dialog_layout_tests && ctest --test-dir build -R "NotificationsEnabledSwitchIsOnThePrivacyPage|AppLockEnabledSwitchIsOnThePrivacyPage" --output-on-failure`
Expected: FAIL.

- [ ] **Step 3: Реализация**

1. Перенести `notificationsBox` (`"Notifications"`) с `generalPage` на `privacyPage`: конструктор `QGroupBox(tr("Notifications"), generalPage)` → `privacyPage`; `generalSettingsLayout->addWidget(notificationsBox)` → `privacySettingsLayout->addWidget(notificationsBox)`.
2. Переименовать локальную переменную `privacyBox` → `appLockBox` во всём файле (только эта одна локальная переменная в `setupUi()` — во избежание путаницы с новой вкладкой "Privacy & Security"; сам `QGroupBox`'ов заголовок `tr("Privacy")` можно оставить как есть, либо сменить на `tr("App lock")`, чтобы не дублировать название вкладки — выбрать `tr("App lock")`, раз коробка теперь физически внутри вкладки "Privacy & Security", а не отдельная "Privacy" в общем списке).
3. Перенести переименованный `appLockBox` на `privacyPage`, после `notificationsBox`: `QGroupBox(tr("App lock"), privacyPage)`, `privacySettingsLayout->addWidget(appLockBox)`.
4. Добавить `objectName`:
   - `mNotificationsEnabledSwitch->setObjectName(QStringLiteral("notificationsEnabledSwitch"));`
   - `mAppLockEnabledSwitch->setObjectName(QStringLiteral("appLockEnabledSwitch"));`
5. Добавить `privacySettingsLayout->addStretch();` в конце.

- [ ] **Step 4: Прогнать все `SettingsDialogLayoutTest` и `SettingsDialogLiveKitSectionTest`**

Run: `cmake --build build --target Sessio_settings_dialog_layout_tests Sessio_settings_dialog_livekit_section_tests && ctest --test-dir build -R "SettingsDialogLayoutTest|SettingsDialogLiveKitSectionTest" --output-on-failure`
Expected: PASS все.

- [ ] **Step 5: Commit**

```bash
/usr/bin/git add src/app/settings_dialog.cpp test/settings_dialog_layout_tests.cpp
/usr/bin/git commit -m "Move notifications and app-lock settings into a Privacy & Security tab"
```

---

### Task 3: Events — разделить `"Timeline colors"` на три коробки

**Files:**
- Modify: `src/app/settings_dialog.cpp`
- Modify: `test/settings_dialog_layout_tests.cpp`

**Interfaces:**
- Consumes: `eventsPage` (индекс 3, после Task 1/2 сдвига).

- [ ] **Step 1: Падающие тесты**

```cpp
TEST(SettingsDialogLayoutTest, SchedulingAndBillingBoxesAreBothOnTheEventsPage) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  EXPECT_EQ(pageIndexOf(dialog, "preventOverlapsSwitch"), 3);
  EXPECT_EQ(pageIndexOf(dialog, "currencyCombo"), 3);
  EXPECT_EQ(pageIndexOf(dialog, "workEventColorEditor"), 3);
}

TEST(SettingsDialogLayoutTest, SchedulingBillingAndColorsAreThreeSeparateGroupBoxes) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  auto *overlapsSwitch = dialog.findChild<QWidget *>("preventOverlapsSwitch");
  auto *currencyCombo = dialog.findChild<QWidget *>("currencyCombo");
  auto *colorEditor = dialog.findChild<QWidget *>("workEventColorEditor");
  ASSERT_NE(overlapsSwitch, nullptr);
  ASSERT_NE(currencyCombo, nullptr);
  ASSERT_NE(colorEditor, nullptr);

  auto groupBoxOf = [](QWidget *w) -> QWidget * {
    while (w != nullptr && qobject_cast<QGroupBox *>(w) == nullptr) {
      w = w->parentWidget();
    }
    return w;
  };
  QWidget *schedulingBox = groupBoxOf(overlapsSwitch);
  QWidget *billingBox = groupBoxOf(currencyCombo);
  QWidget *colorsBox = groupBoxOf(colorEditor);
  ASSERT_NE(schedulingBox, nullptr);
  ASSERT_NE(billingBox, nullptr);
  ASSERT_NE(colorsBox, nullptr);
  EXPECT_NE(schedulingBox, billingBox);
  EXPECT_NE(billingBox, colorsBox);
  EXPECT_NE(schedulingBox, colorsBox);
}
```

Добавить `#include <QGroupBox>` в `test/settings_dialog_layout_tests.cpp`.

- [ ] **Step 2: Прогнать — должно упасть**

Run: `cmake --build build --target Sessio_settings_dialog_layout_tests && ctest --test-dir build -R "SchedulingAndBillingBoxesAreBothOnTheEventsPage|SchedulingBillingAndColorsAreThreeSeparateGroupBoxes" --output-on-failure`
Expected: FAIL (сегодня все три виджета — дети одного `eventsBox`, `objectName` не выставлены).

- [ ] **Step 3: Реализация**

Заменить единственный `eventsBox` (`"Timeline colors"`, строки создания от `new QGroupBox(tr("Timeline colors"), eventsPage)` до `eventSettingsLayout->addWidget(eventsBox);`) на три коробки, каждая — свой `QGroupBox`+`QVBoxLayout` (16,16,16,16 margins, 14 spacing, как у оригинала), добавленные в `eventSettingsLayout` в этом порядке:

```cpp
auto *schedulingDefaultsBox = new QGroupBox(tr("Scheduling defaults"), eventsPage);
auto *schedulingDefaultsLayout = new QVBoxLayout(schedulingDefaultsBox);
schedulingDefaultsLayout->setContentsMargins(16, 16, 16, 16);
schedulingDefaultsLayout->setSpacing(14);
mPreventOverlapsSwitch = new oclero::qlementine::Switch(schedulingDefaultsBox);
mPreventOverlapsSwitch->setObjectName(QStringLiteral("preventOverlapsSwitch"));
mWorkDayStartEdit = new QTimeEdit(schedulingDefaultsBox);
mWorkDayStartEdit->setDisplayFormat("HH:mm");
mWorkDayEndEdit = new QTimeEdit(schedulingDefaultsBox);
mWorkDayEndEdit->setDisplayFormat("HH:mm");
mDefaultSessionDurationSpinBox = new QSpinBox(schedulingDefaultsBox);
mDefaultSessionDurationSpinBox->setMinimum(5);
mDefaultSessionDurationSpinBox->setMaximum(480);
mDefaultSessionDurationSpinBox->setSingleStep(5);
mDefaultSessionDurationSpinBox->setSuffix(tr(" min"));
mDefaultBufferBeforeSpinBox = new QSpinBox(schedulingDefaultsBox);
mDefaultBufferBeforeSpinBox->setRange(0, 240);
mDefaultBufferBeforeSpinBox->setSuffix(tr(" min"));
mDefaultBufferAfterSpinBox = new QSpinBox(schedulingDefaultsBox);
mDefaultBufferAfterSpinBox->setRange(0, 240);
mDefaultBufferAfterSpinBox->setSuffix(tr(" min"));
schedulingDefaultsLayout->addWidget(
    makeSettingRow(tr("Disallow overlapping events"),
                   tr("Reject saves when the selected time range intersects another event."),
                   mPreventOverlapsSwitch, schedulingDefaultsBox));
schedulingDefaultsLayout->addWidget(
    makeSettingRow(tr("Work day start"), tr("Start time used for quick session suggestions."),
                   mWorkDayStartEdit, schedulingDefaultsBox));
schedulingDefaultsLayout->addWidget(
    makeSettingRow(tr("Work day end"), tr("End time used for quick session suggestions."),
                   mWorkDayEndEdit, schedulingDefaultsBox));
schedulingDefaultsLayout->addWidget(makeSettingRow(
    tr("Default session duration"),
    tr("Duration used for quick session suggestions and new sessions."),
    mDefaultSessionDurationSpinBox, schedulingDefaultsBox));
schedulingDefaultsLayout->addWidget(
    makeSettingRow(tr("Default buffer before"), tr("Time reserved before each new session and Quick Slot."),
                   mDefaultBufferBeforeSpinBox, schedulingDefaultsBox));
schedulingDefaultsLayout->addWidget(
    makeSettingRow(tr("Default buffer after"), tr("Time reserved after each new session and Quick Slot."),
                   mDefaultBufferAfterSpinBox, schedulingDefaultsBox));
eventSettingsLayout->addWidget(schedulingDefaultsBox);

auto *billingBox = new QGroupBox(tr("Billing"), eventsPage);
auto *billingLayout = new QVBoxLayout(billingBox);
billingLayout->setContentsMargins(16, 16, 16, 16);
billingLayout->setSpacing(14);
mCurrencyCombo = new QComboBox(billingBox);
mCurrencyCombo->setObjectName(QStringLiteral("currencyCombo"));
mCurrencyCombo->addItem(tr("Russian Ruble (₽)"), QStringLiteral("RUB"));
mCurrencyCombo->addItem(tr("US Dollar ($)"), QStringLiteral("USD"));
mCurrencyCombo->addItem(tr("Euro (€)"), QStringLiteral("EUR"));
mCurrencyCombo->addItem(tr("British Pound (£)"), QStringLiteral("GBP"));
mDefaultWorkCostSpinBox = new QDoubleSpinBox(billingBox);
mDefaultWorkCostSpinBox->setDecimals(2);
mDefaultWorkCostSpinBox->setMinimum(0.0);
mDefaultWorkCostSpinBox->setMaximum(1'000'000.0);
mDefaultWorkCostSpinBox->setSingleStep(100.0);
mDefaultWorkCostSpinBox->setSuffix(QStringLiteral(" ") + pcm::app_settings::currencySymbol());
billingLayout->addWidget(makeSettingRow(tr("Currency"),
                                        tr("Symbol shown next to cost values throughout the app."),
                                        mCurrencyCombo, billingBox));
billingLayout->addWidget(makeSettingRow(tr("Default work event cost"),
                                        tr("Used to prefill new work sessions."),
                                        mDefaultWorkCostSpinBox, billingBox));
eventSettingsLayout->addWidget(billingBox);

auto *eventColorsBox = new QGroupBox(tr("Event colors"), eventsPage);
auto *eventColorsLayout = new QVBoxLayout(eventColorsBox);
eventColorsLayout->setContentsMargins(16, 16, 16, 16);
eventColorsLayout->setSpacing(14);
mWorkEventColorEditor = new oclero::qlementine::ColorEditor(eventColorsBox);
mWorkEventColorEditor->setObjectName(QStringLiteral("workEventColorEditor"));
mPersonalEventColorEditor = new oclero::qlementine::ColorEditor(eventColorsBox);
eventColorsLayout->addWidget(makeSettingRow(tr("Work events"),
                                            tr("Accent color for work sessions in the timeline."),
                                            mWorkEventColorEditor, eventColorsBox));
eventColorsLayout->addWidget(makeSettingRow(tr("Personal events"),
                                            tr("Accent color for personal events in the timeline."),
                                            mPersonalEventColorEditor, eventColorsBox));
eventSettingsLayout->addWidget(eventColorsBox);
```

(`eventSettingsLayout->addStretch();` уже существует после текущего `eventSettingsLayout->addWidget(eventsBox);` — оставить как есть, просто в конце после трёх новых `addWidget`.)

- [ ] **Step 4: Прогнать тесты**

Run: `cmake --build build --target Sessio_settings_dialog_layout_tests Sessio_settings_dialog_livekit_section_tests && ctest --test-dir build -R "SettingsDialogLayoutTest|SettingsDialogLiveKitSectionTest" --output-on-failure`
Expected: PASS все.

- [ ] **Step 5: Commit**

```bash
/usr/bin/git add src/app/settings_dialog.cpp test/settings_dialog_layout_tests.cpp
/usr/bin/git commit -m "Split the Events timeline-colors box into scheduling, billing, and colors"
```

---

### Task 4: Прокручиваемые страницы и ограничение высоты диалога по экрану

Исходная жалоба: окно настроек слишком длинное и не помещается на экран. Одной перегруппировки по вкладкам недостаточно (вкладки Backup и Events всё равно высокие, а `resize(560, 760)` фиксирован), поэтому каждая страница должна прокручиваться по вертикали, а высота диалога — ограничиваться доступной высотой экрана.

**Files:**
- Modify: `src/app/settings_dialog.cpp`
- Modify: `test/settings_dialog_layout_tests.cpp`

**Interfaces:**
- Consumes: 6 страниц `mSettingsStack` из Tasks 1–3.
- Produces: каждая страница `mSettingsStack` теперь — `QScrollArea` (виджет страницы лежит внутри). `pageIndexOf` из Task 1 не меняется: обход родителей контрол -> ... -> страница -> viewport -> `QScrollArea` доходит до виджета, у которого `stack->indexOf(...)` определён.

- [ ] **Step 1: Падающие тесты**

Добавить в `test/settings_dialog_layout_tests.cpp` (включить `<QScrollArea>`, `<QScrollBar>`, `<QGuiApplication>`, `<QScreen>`):

```cpp
TEST(SettingsDialogLayoutTest, EveryPageIsAVerticallyScrollableScrollArea) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  auto *stack = dialog.findChild<QStackedWidget *>();
  ASSERT_NE(stack, nullptr);
  ASSERT_EQ(stack->count(), 6);
  for (int i = 0; i < stack->count(); ++i) {
    auto *scrollArea = qobject_cast<QScrollArea *>(stack->widget(i));
    ASSERT_NE(scrollArea, nullptr) << "page " << i << " is not a QScrollArea";
    EXPECT_TRUE(scrollArea->widgetResizable());
    EXPECT_EQ(scrollArea->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    EXPECT_EQ(scrollArea->frameShape(), QFrame::NoFrame);
  }
}

TEST(SettingsDialogLayoutTest, DialogHeightNeverExceedsTheAvailableScreenHeight) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  const auto *screen = QGuiApplication::primaryScreen();
  ASSERT_NE(screen, nullptr);
  EXPECT_LE(dialog.height(), screen->availableGeometry().height());
}

TEST(SettingsDialogLayoutTest, TallPagesScrollInsteadOfOverflowingTheDialog) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  dialog.resize(560, 300);
  dialog.show();
  auto *stack = dialog.findChild<QStackedWidget *>();
  ASSERT_NE(stack, nullptr);
  auto *backupPage = qobject_cast<QScrollArea *>(stack->widget(2));
  ASSERT_NE(backupPage, nullptr);
  stack->setCurrentIndex(2);
  QApplication::processEvents();
  EXPECT_GT(backupPage->verticalScrollBar()->maximum(), 0);
}
```

- [ ] **Step 2: Прогнать — должно упасть**

Run: `cmake --build build --target Sessio_settings_dialog_layout_tests && ctest --test-dir build -R "EveryPageIsAVerticallyScrollableScrollArea|DialogHeightNeverExceedsTheAvailableScreenHeight|TallPagesScrollInsteadOfOverflowingTheDialog" --output-on-failure`
Expected: FAIL (страницы — обычные `QWidget`, не `QScrollArea`; высота фиксирована 760).

- [ ] **Step 3: Реализация**

В анонимный namespace `settings_dialog.cpp` (рядом с `makeSettingRow`) добавить хелпер:

```cpp
QScrollArea *makeScrollPage(QWidget *page, QWidget *parent) {
  auto *scrollArea = new QScrollArea(parent);
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scrollArea->setWidget(page);
  return scrollArea;
}
```

В `setupUi()` каждый `mSettingsStack->addWidget(xxxPage)` заменить на `mSettingsStack->addWidget(makeScrollPage(xxxPage, mSettingsStack))` — для всех шести страниц, включая LiveKit-страницу (она добавляется в `setupLiveKitSection()`, найти её `addWidget` и обернуть так же). Родитель самих страниц (`new QWidget(mSettingsStack)`) не менять — `setWidget` переродит их во viewport.

Высоту диалога ограничить доступной высотой экрана: заменить `resize(560, 760);` на

```cpp
const auto *screen = QGuiApplication::primaryScreen();
const int availableHeight =
    screen != nullptr ? screen->availableGeometry().height() : 760;
resize(560, qMin(760, availableHeight - 80));
```

(`80` — запас на рамку окна и панели; включить `<QGuiApplication>`, `<QScreen>`, `<QScrollArea>`.)

- [ ] **Step 4: Прогнать все `SettingsDialogLayoutTest` и `SettingsDialogLiveKitSectionTest`**

Run: `cmake --build build --target Sessio_settings_dialog_layout_tests Sessio_settings_dialog_livekit_section_tests && ctest --test-dir build -R "SettingsDialogLayoutTest|SettingsDialogLiveKitSectionTest" --output-on-failure`
Expected: PASS все (тесты индексов страниц из Tasks 1–3 продолжают проходить).

- [ ] **Step 5: Commit**

```bash
/usr/bin/git add src/app/settings_dialog.cpp test/settings_dialog_layout_tests.cpp
/usr/bin/git commit -m "Make settings pages scrollable and cap the dialog height to the screen"
```

---

### Task 5: Версия, changelog, переводы, финальная проверка

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/app/application.cpp`
- Modify: `CHANGELOG.md`
- Modify: `translation/app_ru.ts`, `translation/app_en.ts`

- [ ] **Step 1: Поднять версию**

`CMakeLists.txt`/`src/app/application.cpp` — поднять патч-версию на 1 от фактического текущего значения на момент выполнения таска (ожидается `0.2.3` → `0.2.4`, если предыдущий план уже выполнен; если нет — взять актуальное значение).

- [ ] **Step 2: `CHANGELOG.md`**

```markdown
## [0.2.4] - <дата выполнения таска, YYYY-MM-DD>

### Changed

- The Settings dialog is reorganized into more focused tabs: Backup and
  Privacy & Security are now their own sections instead of being crowded
  into General; the Events tab's scheduling defaults, billing, and event
  colors are now separate groups instead of one mixed box. Long tabs now
  scroll, and the dialog no longer grows taller than the screen.

### Fixed

- On Linux (Wayland), the application window is now tied to its desktop
  entry, so the taskbar/panel shows the Sessio icon instead of a foreign one.
```

- [ ] **Step 3: Синхронизация переводов**

Run: `cmake --build build-release --target update_translations`

Новые `tr()`-строки, введённые этим планом: `tr("Privacy & Security")`, `tr("Backup")` (уже существовал как заголовок коробки — теперь ещё и заголовок вкладки, тот же текст, не новая строка), `tr("App lock")` (новый, если выбран этот вариант в Task 2), `tr("Scheduling defaults")`, `tr("Billing")`, `tr("Event colors")` — все новые. Перевести каждую в `app_ru.ts` на русский, а в `app_en.ts` — тем же текстом, что `<source>` (существующая конвенция). Повторно прогнать `update_translations`, убедиться в 0 `type="unfinished"` в обоих файлах.

- [ ] **Step 4: Полная пересборка и тесты**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`
Expected: 100% PASS.

- [ ] **Step 5: Commit**

```bash
/usr/bin/git add CMakeLists.txt src/app/application.cpp CHANGELOG.md translation/app_ru.ts translation/app_en.ts
/usr/bin/git commit -m "Bump version and changelog for the settings dialog regroup"
```
