# Currency Selection in Settings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a "Currency" dropdown to Settings so the hardcoded ruble sign shown next to cost values can be changed to USD/EUR/GBP.

**Architecture:** Add `pcm::app_settings::currencyCode()`/`setCurrencyCode()`/`currencySymbol()` (mirroring the existing `languageCode()` pattern), add a `mCurrencyCombo` to `SettingsDialog`'s Events section, and replace the four hardcoded `" ₽"` literals with `pcm::app_settings::currencySymbol()`.

**Tech Stack:** C++20, Qt6 Widgets, `QSettings`.

Spec: `docs/superpowers/specs/2026-07-26-currency-setting-design.md`

## Global Constraints

- This is a display-formatting change only — no currency conversion, no per-record currency, no change to how costs are stored (`DOUBLE`) or computed.
- The symbol is always a trailing suffix (`"2500 $"`), for every currency — no per-currency prefix/suffix logic.
- `event_item.cpp`'s existing `QLocale(QLocale::Russian)` number-grouping stays unchanged — only the trailing symbol literal changes.
- No new automated tests: this is a Qt display-formatting change over `src/app`/`src/pages`/`src/event_view`, which have no existing test coverage in this repo (matches `docs/asciidoc/07-testing.adoc`'s established scope). Verification is manual.
- Every MR raises the version in both `CMakeLists.txt` and `src/app/application.cpp`, and updates `CHANGELOG.md` (per `AGENTS.md`).

---

### Task 1: Add currency setting to `pcm::app_settings`

**Files:**
- Modify: `src/widgets/app_settings.h`
- Modify: `src/widgets/app_settings.cpp`

**Interfaces:**
- Produces: `QString pcm::app_settings::currencyCode();`, `void pcm::app_settings::setCurrencyCode(const QString &code);`, `QString pcm::app_settings::currencySymbol();` — Task 2 (Settings UI) and Task 3 (call sites) both call these.

- [ ] **Step 1: Add the declarations**

In `src/widgets/app_settings.h`, add these lines inside `namespace pcm::app_settings { ... }`, right after the existing `meetingInviteTemplate()`/`setMeetingInviteTemplate()` pair:

```cpp
QString currencyCode();
void setCurrencyCode(const QString &code);
QString currencySymbol();
```

- [ ] **Step 2: Implement `currencyCode()`/`setCurrencyCode()`**

In `src/widgets/app_settings.cpp`, add this key constant to the anonymous-namespace block at the top, right after `kMeetingInviteTemplateKey`:

```cpp
constexpr auto kCurrencyCodeKey = "ui/currency";
```

Then add this implementation inside `namespace pcm::app_settings { ... }`, right after `setMeetingInviteTemplate()`:

```cpp
QString currencyCode() {
  QSettings settings;
  return settings.value(kCurrencyCodeKey, QStringLiteral("RUB")).toString();
}

void setCurrencyCode(const QString &code) {
  QSettings settings;
  settings.setValue(kCurrencyCodeKey, code);
}
```

- [ ] **Step 3: Implement `currencySymbol()`**

In the same file, add this implementation right after `setCurrencyCode()`:

```cpp
QString currencySymbol() {
  const auto code = currencyCode();
  if (code == QStringLiteral("USD")) {
    return QStringLiteral("$");
  }
  if (code == QStringLiteral("EUR")) {
    return QStringLiteral("€");
  }
  if (code == QStringLiteral("GBP")) {
    return QStringLiteral("£");
  }
  return QStringLiteral("₽");
}

} // namespace pcm::app_settings
```

Note this replaces the file's existing closing `} // namespace pcm::app_settings` line — don't duplicate it.

- [ ] **Step 4: Build to verify**

Run: `cmake --build build --target PsyClientManager_widgets`
Expected: builds with no errors. If `build/` isn't configured yet, run `cmake -S . -B build -DPCM_BUILD_TESTS=ON` first.

- [ ] **Step 5: Commit**

```bash
git add src/widgets/app_settings.h src/widgets/app_settings.cpp
git commit -m "feat(app): add currency code/symbol setting"
```

---

### Task 2: Add currency dropdown to Settings UI

**Files:**
- Modify: `src/app/settings_dialog.h`
- Modify: `src/app/settings_dialog.cpp`

**Interfaces:**
- Consumes: `pcm::app_settings::currencyCode()`, `pcm::app_settings::setCurrencyCode()` (Task 1).
- Produces: nothing consumed by a later task — the dropdown is self-contained UI wiring.

- [ ] **Step 1: Add the member declaration**

In `src/app/settings_dialog.h`, replace:

```cpp
  QDoubleSpinBox *mDefaultWorkCostSpinBox{nullptr};
```

with:

```cpp
  QComboBox *mCurrencyCombo{nullptr};
  QDoubleSpinBox *mDefaultWorkCostSpinBox{nullptr};
```

- [ ] **Step 2: Add the combo box to `setupUi()`**

In `src/app/settings_dialog.cpp`, replace:

```cpp
  mPreventOverlapsSwitch = new oclero::qlementine::Switch(eventsBox);
  mWorkEventColorEditor = new oclero::qlementine::ColorEditor(eventsBox);
  mPersonalEventColorEditor = new oclero::qlementine::ColorEditor(eventsBox);
  mDefaultWorkCostSpinBox = new QDoubleSpinBox(eventsBox);
  mDefaultWorkCostSpinBox->setDecimals(2);
  mDefaultWorkCostSpinBox->setMinimum(0.0);
  mDefaultWorkCostSpinBox->setMaximum(1'000'000.0);
  mDefaultWorkCostSpinBox->setSingleStep(100.0);
  mDefaultWorkCostSpinBox->setSuffix(tr(" ₽"));
```

with:

```cpp
  mPreventOverlapsSwitch = new oclero::qlementine::Switch(eventsBox);
  mWorkEventColorEditor = new oclero::qlementine::ColorEditor(eventsBox);
  mPersonalEventColorEditor = new oclero::qlementine::ColorEditor(eventsBox);
  mCurrencyCombo = new QComboBox(eventsBox);
  mCurrencyCombo->addItem(tr("Russian Ruble (₽)"), QStringLiteral("RUB"));
  mCurrencyCombo->addItem(tr("US Dollar ($)"), QStringLiteral("USD"));
  mCurrencyCombo->addItem(tr("Euro (€)"), QStringLiteral("EUR"));
  mCurrencyCombo->addItem(tr("British Pound (£)"), QStringLiteral("GBP"));
  mDefaultWorkCostSpinBox = new QDoubleSpinBox(eventsBox);
  mDefaultWorkCostSpinBox->setDecimals(2);
  mDefaultWorkCostSpinBox->setMinimum(0.0);
  mDefaultWorkCostSpinBox->setMaximum(1'000'000.0);
  mDefaultWorkCostSpinBox->setSingleStep(100.0);
  mDefaultWorkCostSpinBox->setSuffix(QStringLiteral(" ") + pcm::app_settings::currencySymbol());
```

Then, in the same method, replace:

```cpp
  eventsLayout->addWidget(
      makeSettingRow(tr("Default work event cost"),
                     tr("Used to prefill new work sessions."),
                     mDefaultWorkCostSpinBox, eventsBox));
```

with:

```cpp
  eventsLayout->addWidget(
      makeSettingRow(tr("Currency"),
                     tr("Symbol shown next to cost values throughout the app."),
                     mCurrencyCombo, eventsBox));
  eventsLayout->addWidget(
      makeSettingRow(tr("Default work event cost"),
                     tr("Used to prefill new work sessions."),
                     mDefaultWorkCostSpinBox, eventsBox));
```

- [ ] **Step 3: Load the saved value in `loadSettings()`**

Replace:

```cpp
  mDefaultWorkCostSpinBox->setValue(pcm::app_settings::defaultWorkEventCost());
```

with:

```cpp
  const auto currencyIndex = mCurrencyCombo->findData(pcm::app_settings::currencyCode());
  mCurrencyCombo->setCurrentIndex(currencyIndex >= 0 ? currencyIndex : 0);
  mDefaultWorkCostSpinBox->setValue(pcm::app_settings::defaultWorkEventCost());
```

- [ ] **Step 4: Save on change in `connectSignals()`**

Replace:

```cpp
  connect(mDefaultWorkCostSpinBox, &QDoubleSpinBox::valueChanged, this,
          [](const double value) {
            pcm::app_settings::setDefaultWorkEventCost(value);
          });
```

with:

```cpp
  connect(mCurrencyCombo, &QComboBox::currentIndexChanged, this, [this](const int index) {
    pcm::app_settings::setCurrencyCode(mCurrencyCombo->itemData(index).toString());
    mDefaultWorkCostSpinBox->setSuffix(QStringLiteral(" ") + pcm::app_settings::currencySymbol());
  });
  connect(mDefaultWorkCostSpinBox, &QDoubleSpinBox::valueChanged, this,
          [](const double value) {
            pcm::app_settings::setDefaultWorkEventCost(value);
          });
```

(The suffix is re-applied here so that if the user changes the currency while this same Settings dialog instance is still open, the spinbox they're looking at updates immediately — not just on the next open.)

- [ ] **Step 5: Build to verify**

Run: `cmake --build build --target PsyClientManager_app`
Expected: builds with no errors.

- [ ] **Step 6: Commit**

```bash
git add src/app/settings_dialog.h src/app/settings_dialog.cpp
git commit -m "feat(app): add currency dropdown to Settings"
```

---

### Task 3: Replace hardcoded ruble sign at the three remaining call sites

**Files:**
- Modify: `src/event_view/event_item.cpp`
- Modify: `src/pages/analytics_page/analytics_page.cpp`
- Modify: `src/pages/event_info_page/qevent_details_widget.cpp`

**Interfaces:**
- Consumes: `pcm::app_settings::currencySymbol()` (Task 1).
- Produces: nothing consumed by a later task — final task.

- [ ] **Step 1: `event_item.cpp`**

Replace:

```cpp
QString formatEventCost(const std::optional<double>& cost) {
  if (!cost.has_value()) {
    return {};
  }

  return QLocale(QLocale::Russian).toString(*cost, 'f', 0) + QStringLiteral(" ₽");
}
```

with:

```cpp
QString formatEventCost(const std::optional<double>& cost) {
  if (!cost.has_value()) {
    return {};
  }

  return QLocale(QLocale::Russian).toString(*cost, 'f', 0) + QStringLiteral(" ") +
         pcm::app_settings::currencySymbol();
}
```

This file already includes `../widgets/app_settings.h` (line 2) — no new include needed.

- [ ] **Step 2: `analytics_page.cpp`**

Add `#include "../../widgets/app_settings.h"` to the top include block, right after `#include "../../widgets/constants.hpp"`:

```cpp
#include "analytics_page.h"

#include "qcustomplot.h"
#include "../../widgets/app_settings.h"
#include "../../widgets/constants.hpp"
```

Then replace:

```cpp
QString formatCurrency(const double value) {
  const auto rounded = qRound64(value);
  return QLocale().toString(rounded) + QObject::tr(" ₽");
}
```

with:

```cpp
QString formatCurrency(const double value) {
  const auto rounded = qRound64(value);
  return QLocale().toString(rounded) + QStringLiteral(" ") +
         pcm::app_settings::currencySymbol();
}
```

- [ ] **Step 3: `qevent_details_widget.cpp`**

Replace:

```cpp
  mUI->mCostSpinBox->setSuffix(tr(" ₽"));
```

with:

```cpp
  mUI->mCostSpinBox->setSuffix(QStringLiteral(" ") + pcm::app_settings::currencySymbol());
```

This file already includes `../../widgets/app_settings.h` (line 2) — no new include needed.

- [ ] **Step 4: Build to verify**

Run: `cmake --build build`
Expected: full build succeeds with no errors (this is the last code task — do a full build, not just a single target, to catch any missed call site).

- [ ] **Step 5: Full test suite regression check**

Run: `ctest --test-dir build --output-on-failure`
Expected: all existing tests still pass (this task touches no code any test exercises, so this is a pure regression check).

- [ ] **Step 6: Manual verification**

Run the built app, then:
1. Open Settings → Events → Currency, confirm it defaults to "Russian Ruble (₽)" and the "Default work event cost" field shows a "₽" suffix.
2. Change currency to "US Dollar ($)"; confirm the "Default work event cost" suffix updates immediately (Task 2 Step 4's live-update).
3. Close Settings. Open an existing work event with a cost on the timeline; confirm its cost label shows "$".
4. Open the Analytics page; confirm the income summary card shows "$".
5. Click "Add event" (or edit an existing work event); confirm the Cost field's suffix shows "$".

- [ ] **Step 7: Commit**

```bash
git add src/event_view/event_item.cpp src/pages/analytics_page/analytics_page.cpp src/pages/event_info_page/qevent_details_widget.cpp
git commit -m "feat(app): use the configured currency symbol at all display sites"
```

---

### Task 4: Docs, version, and changelog

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/app/application.cpp`
- Modify: `CHANGELOG.md`

**Interfaces:**
- Consumes: nothing.
- Produces: nothing consumed by a later task — final task.

- [ ] **Step 1: Bump the project version**

Check the current version first: `grep -n "project(PsyClientManager" CMakeLists.txt`. This branch was forked from `main`, which is at `0.1.8` (the `0.1.9` bump lives on the separate, not-yet-merged `feat/10-local-backup-service` branch). Bump the patch version by one from whatever the grep shows (expected `0.1.8` → `0.1.9`; if it has moved further since this plan was written, bump from the current value instead and use that same new value in Steps 2 and 3).

In `CMakeLists.txt`:

```cmake
project(PsyClientManager VERSION 0.1.9 LANGUAGES CXX)
```

- [ ] **Step 2: Bump the application version string**

In `src/app/application.cpp`:

```cpp
  app.setApplicationVersion("0.1.9");
```

- [ ] **Step 3: Add a CHANGELOG entry**

In `CHANGELOG.md`, insert this new section right after the `# Changelog` header and its intro line, before the current most-recent version entry:

```markdown
## [0.1.9] - 2026-07-26

### Added

- Currency selection in Settings (₽/$/€/£), applied to the default work
  event cost field, the event editor, the timeline, and analytics.

```

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt src/app/application.cpp CHANGELOG.md
git commit -m "docs: document currency setting and bump version"
```
