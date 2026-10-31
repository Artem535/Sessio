# Перегруппировка диалога настроек — дизайн

> Основано на инвентаризации `src/app/settings_dialog.{h,cpp}` (1290 строк, ~38 интерактивных контролов). Проблема — не отсутствие группировки как таковой (она уже есть: `SegmentedControl` + `QStackedWidget` на 4 вкладки, `QGroupBox` внутри каждой), а перекос: вкладка **General** содержит 6 `QGroupBox` и ~24 из 38 контролов, тогда как Events/Online/LiveKit — по одной коробке каждая.

**Цель:** разгрузить General, выделив Backup и Privacy & Security в собственные вкладки верхнего уровня; попутно исправить смысловой разнобой внутри коробки `"Timeline colors"` на Events (на деле там ещё и billing-поля, и настройки по умолчанию для расписания, а не только цвета).

**Архитектура:** чисто композиционная правка `SettingsDialog::setupUi()` — виджеты, их `objectName`, сигналы/слоты (`connectSignals()`) и хранение (`pcm::app_settings::*`) не меняются. Меняется только то, на какую `QStackedWidget`-страницу и в какой `QGroupBox` каждый контрол попадает.

**Tech Stack:** C++20, Qt Widgets, `oclero::qlementine::SegmentedControl`/`Switch`/`ColorEditor` (без изменений в этих классах).

## Global Constraints

- **Ничего не удаляется и не переименовывается на уровне API/хранения**: `pcm::app_settings::*` геттеры/сеттеры, `objectName()` каждого виджета (используется существующим тестом `settings_dialog_livekit_section_tests.cpp` через `findChild`), сигнатуры `connectSignals()` — без изменений. Только физическое место контрола в дереве виджетов.
- Единственный существующий тест-файл (`settings_dialog_livekit_section_tests.cpp`, вкладка LiveKit) не должен сломаться — вкладка LiveKit не переименовывается и не трогается по контенту, только её итоговый индекс в `SegmentedControl` может измениться (см. §2) — тест ищет виджеты через `findChild`, не через индекс страницы, так что порядок вкладок ему не важен.
- `makeSettingRow()` helper и визуальный паттерн "жирный заголовок + серое описание + контрол справа" — переиспользуются как есть, не меняются.
- Итоговое число вкладок верхнего уровня — не больше 6 (иначе `SegmentedControl` с `setItemsShouldExpand(true)` станет тесным на ширине диалога 560px).

---

## 1. Новая раскладка вкладок

| # | Вкладка (было) | Вкладка (стало) | Содержимое |
|---|---|---|---|
| 1 | General (6 коробок) | **General** | Language, Database — 2 контрола, было в General |
| 2 | — (было в General) | **Privacy & Security** (новая) | Notifications (3), App-lock (3), Clipboard (2) — было в General/Notifications и General/Privacy |
| 3 | — (было в General) | **Backup** (новая) | Manual backup (3 кнопки+статус), Encryption (5), Automatic (4) — было в General/Backup, /Automatic Backups |
| 4 | Events | **Events** | Те же контролы, но коробка `"Timeline colors"` делится на три: `"Scheduling defaults"`, `"Billing"`, `"Event colors"` (см. §3) |
| 5 | Online | **Online** | Без изменений |
| 6 | LiveKit | **LiveKit** | Без изменений |

Причина объединения Notifications+App-lock+Clipboard в одну вкладку "Privacy & Security", а не в три отдельных: Notifications (3 контрола) и App-lock (3) слишком малы для отдельных вкладок каждая — превратили бы `SegmentedControl` в 7-8 пунктов. Все три — про то, "что приложение показывает/скрывает и когда себя запирает", что логически один раздел.

## 2. `setupUi()`: правки

`mSettingsSections->addItem(...)` — новый порядок и состав:

```cpp
mSettingsSections->addItem(tr("General"), {}, {}, QStringLiteral("general"));
mSettingsSections->addItem(tr("Privacy & Security"), {}, {}, QStringLiteral("privacy"));
mSettingsSections->addItem(tr("Backup"), {}, {}, QStringLiteral("backup"));
mSettingsSections->addItem(tr("Events"), {}, {}, QStringLiteral("events"));
mSettingsSections->addItem(tr("Online"), {}, {}, QStringLiteral("online"));
mSettingsSections->addItem(tr("LiveKit"), {}, {}, QStringLiteral("livekit"));
```

`mSettingsStack->addWidget(...)` вызывается в том же порядке (`SegmentedControl::currentIndexChanged` → `mSettingsStack->setCurrentIndex(...)` требует 1:1 соответствие индексов — текущий код уже это соблюдает, просто расширяем список).

Новые страницы: `privacyPage`, `backupPage` — создаются как остальные (`new QWidget(mSettingsStack)` + `QVBoxLayout`, `setContentsMargins(0,0,0,0)`, `setSpacing(16)`).

## 3. Перенос коробок

- `languageBox`, `databaseBox` — остаются на `generalPage`, без изменений в содержимом.
- `notificationsBox` (Session reminders, lead time, privacy mode) — переносится с `generalPage` на `privacyPage`. Создание виджетов и сигналы не меняются, меняется только `generalSettingsLayout->addWidget(notificationsBox)` → `privacySettingsLayout->addWidget(notificationsBox)` (и родитель конструктора `QGroupBox(tr(...), generalPage)` → `QGroupBox(tr(...), privacyPage)`).
- `privacyBox` (переименовать переменную в коде на `appLockBox` во избежание путаницы с новой вкладкой "Privacy & Security" — App-lock toggle, timeout, change PIN, clipboard-clear toggle, clipboard-clear delay) — переносится на `privacyPage`, после `notificationsBox`.
- `backupBox` (manual actions + encryption sub-section) и `automaticBackupsBox` — переносятся на `backupPage`.
- На Events: `eventsBox` (`"Timeline colors"`, сейчас содержит `mPreventOverlapsSwitch`, work hours, duration, buffers, `mCurrencyCombo`, `mDefaultWorkCostSpinBox`, `mWorkEventColorEditor`, `mPersonalEventColorEditor`) — делится на три `QGroupBox` на той же `eventsPage`, в этом порядке:
  1. `schedulingDefaultsBox` (`tr("Scheduling defaults")`) — `mPreventOverlapsSwitch`, work day start/end, default duration, buffers before/after.
  2. `billingBox` (`tr("Billing")`) — `mCurrencyCombo`, `mDefaultWorkCostSpinBox`.
  3. `eventColorsBox` (`tr("Event colors")`) — `mWorkEventColorEditor`, `mPersonalEventColorEditor`.
- `onlineBox`, LiveKit-секция (`setupLiveKitSection()`) — без изменений.

Во всех случаях переноса: у существующих `QGroupBox`/`makeSettingRow` вызовов меняется только `parent`-аргумент конструктора (на новую страницу) и то, в какой `layout->addWidget(...)` они попадают — сам контрол, его `objectName`, сигнал/слот проводка не трогаются.

## 4. Тесты

Существующий `settings_dialog_livekit_section_tests.cpp` не меняется (ищет по `objectName`, не по индексу вкладки — должен продолжать проходить без правок; тем не менее запускается как часть верификации каждого таска).

Новый тест-файл `settings_dialog_layout_tests.cpp`: `QStackedWidget` — приватное поле `SettingsDialog`, но Qt-интроспекция (`findChild`) обходит C++ access control через дерево `QObject`-родителей, а не через C++-члены, так что тест может достать его напрямую:

```cpp
SettingsDialog dialog(nullptr, credentialStore);
auto *stack = dialog.findChild<QStackedWidget *>();
ASSERT_NE(stack, nullptr);

auto *notificationsSwitch = dialog.findChild<QWidget *>("notificationsEnabledSwitch");
ASSERT_NE(notificationsSwitch, nullptr);
// Поднимаемся до страницы: switch -> makeSettingRow-строка -> QGroupBox -> ...Page
QWidget *page = notificationsSwitch;
while (page->parentWidget() != nullptr && stack->indexOf(page) == -1) {
  page = page->parentWidget();
}
EXPECT_EQ(stack->indexOf(page), 1); // Privacy & Security — индекс 1 по таблице §1
```

По одному такому кейсу на каждый перенесённый контрол (минимум: один представитель из Notifications, один из App-lock, один из Backup/manual, один из Backup/encryption, один из Backup/automatic, плюс по одному из каждой новой Events-коробки — Scheduling defaults/Billing/Event colors), подтверждающему итоговый индекс страницы по таблице §1. `objectName()` каждого контрола уже существует в коде (используется/будет использоваться этими тестами) — если у конкретного виджета его сегодня нет, задача первого таска плана — доставить `setObjectName(...)` туда, где он отсутствует, до переноса.
