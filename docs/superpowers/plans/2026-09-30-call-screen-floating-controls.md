# Плавающая панель управления на экране звонка — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** заменить докнутую строку控ролов (`controls`, `QHBoxLayout` под видео) на непрозрачную плавающую "таблетку" поверх видео, и вынести кнопку заметок в отдельную плавающую круглую иконку в углу.

**Architecture:** `pcm::video::detail::VideoStage` (в `src/pages/calls_page/call_page.h`/`.cpp`) получает два новых optional-слота (`setControlBarWidget`, `setNotesToggleWidget`), позиционируемых в `layoutChildren()` тем же прямым способом, что уже используется для remote/local-preview. Логика самих кнопок не меняется — меняется родитель и позиционирование.

**Tech Stack:** C++20, Qt Widgets, существующий `VideoStage`/`QOpenGLWidget`-рендерер без изменений.

## Global Constraints

- Никакой полупрозрачности поверх `RemoteVideoRenderer` (`QOpenGLWidget`) — все новые overlay-виджеты полностью непрозрачны (`QPalette`, без альфа-канала), как self-preview PIP.
- Логика кнопок (`toggled`-лямбды, `attachSession()`-ресинк, вызовы `VideoProvider`) не трогается — только их родитель/позиция.
- Публичный API `CallPage` (`attachSession`, `setSidePanelWidget`, `setSidePanelToggleVisible`, `setSidePanelExpandedByDefault`) не меняется по сигнатуре и внешнему поведению.
- `FakeVideoProvider` не трогаем.
- Каждый шаг, трогающий `tr()`-строки, заканчивается синхронизацией `translation/app_ru.ts`/`app_en.ts` (0 `type="unfinished"`).
- Каждый MR поднимает версию (`CMakeLists.txt` + `src/app/application.cpp`) и обновляет `CHANGELOG.md` (см. Task 5).

---

### Task 1: `notesIcon()` в `call_control_icons`

**Files:**
- Modify: `src/widgets/call_control_icons.h`
- Modify: `src/widgets/call_control_icons.cpp`
- Test: `test/call_control_icons_tests.cpp`

**Interfaces:**
- Produces: `QIcon pcm::widgets::notesIcon()` — самостоятельная иконка (блокнот/список), тот же self-drawn `QPainter`-подход и тот же общий `renderIcon(bool danger, Draw draw)` хелпер, что и у `microphoneIcon`/`cameraIcon`/`devicesIcon`/`fullscreenIcon`.

- [ ] **Step 1: Написать падающий тест**

Добавить в `test/call_control_icons_tests.cpp` (рядом с существующим `CallControlIconsTest.EveryIconIsNonNull`):

```cpp
TEST(CallControlIconsTest, NotesIconIsNonNull) {
  const QIcon icon = pcm::widgets::notesIcon();
  EXPECT_FALSE(icon.isNull());
}
```

- [ ] **Step 2: Прогнать тест — должен упасть**

Run: `cmake --build build --target Sessio_call_control_icons_tests && ctest --test-dir build -R CallControlIconsTest.NotesIconIsNonNull --output-on-failure`
Expected: FAIL (compile error — `notesIcon` не объявлен).

- [ ] **Step 3: Реализация**

В `call_control_icons.h`, рядом с объявлением `fullscreenIcon`:

```cpp
[[nodiscard]] QIcon notesIcon();
```

В `call_control_icons.cpp`, рядом с реализацией `fullscreenIcon` (используя тот же `renderIcon`, `false` — не деструктивная иконка):

```cpp
QIcon notesIcon() {
  return renderIcon(false, [](QPainter &painter, const QRectF &r) {
    // Прямоугольный "блокнот" со сложенным уголком и тремя строками текста.
    QPainterPath page;
    page.moveTo(r.left() + r.width() * 0.22, r.top());
    page.lineTo(r.right() - r.width() * 0.22, r.top());
    page.lineTo(r.right() - r.width() * 0.05, r.top() + r.height() * 0.18);
    page.lineTo(r.right() - r.width() * 0.05, r.bottom());
    page.lineTo(r.left() + r.width() * 0.05, r.bottom());
    page.lineTo(r.left() + r.width() * 0.05, r.top() + r.height() * 0.18);
    page.closeSubpath();
    painter.drawPath(page);
    for (int i = 0; i < 3; ++i) {
      const qreal y = r.top() + r.height() * (0.42 + i * 0.16);
      painter.drawLine(QPointF(r.left() + r.width() * 0.22, y),
                        QPointF(r.right() - r.width() * 0.22, y));
    }
  });
}
```

(Проверить перед вставкой точную сигнатуру `Draw`/`renderIcon` в текущем файле — использовать те же типы `QPainter&`/`QRectF` и тот же стиль пера/кисти, что у соседних иконок; при расхождении с примером выше следовать реальной сигнатуре в файле, а не этому фрагменту дословно.)

- [ ] **Step 4: Прогнать тест — должен пройти**

Run: `cmake --build build --target Sessio_call_control_icons_tests && ctest --test-dir build -R CallControlIconsTest --output-on-failure`
Expected: PASS, все кейсы включая новый.

- [ ] **Step 5: Commit**

```bash
/usr/bin/git add src/widgets/call_control_icons.h src/widgets/call_control_icons.cpp test/call_control_icons_tests.cpp
/usr/bin/git commit -m "Add a self-drawn notes icon for the call control bar"
```

---

### Task 2: `VideoStage` — новые слоты control-bar и notes-toggle

**Files:**
- Modify: `src/pages/calls_page/call_page.h`
- Modify: `src/pages/calls_page/call_page.cpp`
- Test: `test/call_page_tests.cpp`

**Interfaces:**
- Consumes: ничего нового снаружи.
- Produces: `VideoStage::setControlBarWidget(QWidget*)`, `VideoStage::setNotesToggleWidget(QWidget*)` — используются в Task 3/4.

- [ ] **Step 1: Написать падающие тесты**

Добавить в `test/call_page_tests.cpp`, рядом с существующими geometry-тестами `VideoStage` (использовать уже существующий helper `resizeAndDeliverEvent(QWidget*, QSize)`):

```cpp
TEST(CallPageTest, VideoStagePositionsControlBarCenteredAtBottom) {
  pcm::video::detail::VideoStage stage;
  auto *controlBar = new QWidget(&stage);
  controlBar->resize(200, 48);
  stage.setControlBarWidget(controlBar);
  resizeAndDeliverEvent(&stage, QSize(640, 360));

  EXPECT_EQ(controlBar->parentWidget(), &stage);
  EXPECT_EQ(controlBar->geometry().center().x(), stage.rect().center().x());
  EXPECT_EQ(controlBar->geometry().bottom(), stage.height() - 20 - 1);
}

TEST(CallPageTest, VideoStagePositionsNotesToggleInTopRightCorner) {
  pcm::video::detail::VideoStage stage;
  auto *notesToggle = new QWidget(&stage);
  stage.setNotesToggleWidget(notesToggle);
  resizeAndDeliverEvent(&stage, QSize(640, 360));

  EXPECT_EQ(notesToggle->parentWidget(), &stage);
  EXPECT_EQ(notesToggle->geometry(), QRect(640 - 12 - 40, 12, 40, 40));
}

TEST(CallPageTest, SwappingControlBarHandsPreviousOneBackUnparented) {
  pcm::video::detail::VideoStage stage;
  auto *firstBar = new QWidget(&stage);
  stage.setControlBarWidget(firstBar);
  auto *secondBar = new QWidget(&stage);
  stage.setControlBarWidget(secondBar);

  EXPECT_EQ(firstBar->parentWidget(), nullptr);
  EXPECT_EQ(secondBar->parentWidget(), &stage);
  delete firstBar;
}
```

`pcm::video::detail::VideoStage` уже используется напрямую в существующих тестах этого файла (см. `VideoStagePositionsLocalPreviewInBottomRightCorner` или аналогичный) — использовать тот же способ инстанцирования/инклюда.

- [ ] **Step 2: Прогнать тесты — должны упасть**

Run: `cmake --build build --target Sessio_call_page_tests && ctest --test-dir build -R "CallPageTest.VideoStagePositionsControlBar|CallPageTest.VideoStagePositionsNotesToggle|CallPageTest.SwappingControlBarHandsPreviousOneBackUnparented" --output-on-failure`
Expected: FAIL (compile error — методы не существуют).

- [ ] **Step 3: Реализация в `call_page.h`**

```cpp
class VideoStage final : public QWidget {
public:
  explicit VideoStage(QWidget *parent = nullptr);

  void setRemoteWidget(QWidget *widget);
  void setLocalPreviewWidget(QWidget *widget);
  void setControlBarWidget(QWidget *widget);
  void setNotesToggleWidget(QWidget *widget);

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  void layoutChildren();

  QPointer<QWidget> mRemoteWidget;
  QPointer<QWidget> mLocalPreviewWidget;
  QPointer<QWidget> mControlBarWidget;
  QPointer<QWidget> mNotesToggleWidget;
};
```

- [ ] **Step 4: Реализация в `call_page.cpp`**

Рядом с существующими `setRemoteWidget`/`setLocalPreviewWidget`:

```cpp
void VideoStage::setControlBarWidget(QWidget *widget) {
  if (mControlBarWidget == widget) {
    return;
  }
  if (mControlBarWidget) {
    mControlBarWidget->setParent(nullptr);
  }
  mControlBarWidget = widget;
  if (mControlBarWidget) {
    mControlBarWidget->setParent(this);
    mControlBarWidget->show();
  }
  layoutChildren();
}

void VideoStage::setNotesToggleWidget(QWidget *widget) {
  if (mNotesToggleWidget == widget) {
    return;
  }
  if (mNotesToggleWidget) {
    mNotesToggleWidget->setParent(nullptr);
  }
  mNotesToggleWidget = widget;
  if (mNotesToggleWidget) {
    mNotesToggleWidget->setParent(this);
    mNotesToggleWidget->show();
  }
  layoutChildren();
}
```

В `layoutChildren()`, после существующего позиционирования `mRemoteWidget`/`mLocalPreviewWidget`, добавить (раньше — remote, потом local-preview, потом control-bar/notes-toggle — порядок вставки определяет порядок `raise()`, оба новых виджета должны в итоге быть выше remote-видео):

```cpp
if (mControlBarWidget) {
  const QSize hint = mControlBarWidget->sizeHint();
  const int barWidth = hint.width() > 0 ? hint.width() : mControlBarWidget->width();
  const int barHeight = 48;
  const int x = (width() - barWidth) / 2;
  const int y = height() - 20 - barHeight;
  mControlBarWidget->setGeometry(x, y, barWidth, barHeight);
  mControlBarWidget->raise();
}
if (mNotesToggleWidget) {
  constexpr int kSize = 40;
  constexpr int kMargin = 12;
  mNotesToggleWidget->setGeometry(width() - kMargin - kSize, kMargin, kSize, kSize);
  mNotesToggleWidget->raise();
}
if (mLocalPreviewWidget) {
  mLocalPreviewWidget->raise();
}
```

(Финальный `mLocalPreviewWidget->raise()` — чтобы self-preview оставался выше нового control-bar/notes-toggle, если геометрии когда-либо пересекутся; в текущей раскладке они не пересекаются, но `raise()` здесь бесплатен и защищает от будущих сдвигов констант.)

- [ ] **Step 5: Прогнать тесты — должны пройти**

Run: `cmake --build build --target Sessio_call_page_tests && ctest --test-dir build -R CallPageTest --output-on-failure`
Expected: PASS, все кейсы `CallPageTest` включая новые три.

- [ ] **Step 6: Commit**

```bash
/usr/bin/git add src/pages/calls_page/call_page.h src/pages/calls_page/call_page.cpp test/call_page_tests.cpp
/usr/bin/git commit -m "Add control-bar and notes-toggle overlay slots to VideoStage"
```

---

### Task 3: Панель управления — непрозрачная плавающая таблетка

**Files:**
- Modify: `src/pages/calls_page/call_page.cpp`

**Interfaces:**
- Consumes: `VideoStage::setControlBarWidget` (Task 2).

- [ ] **Step 1: Написать падающий тест**

```cpp
TEST(CallPageTest, ControlBarIsAChildOfVideoStageNotConnectedView) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);

  auto *micButton = page.findChild<QToolButton *>("microphoneToggleButton");
  ASSERT_NE(micButton, nullptr);
  auto *videoStage = page.findChild<pcm::video::detail::VideoStage *>();
  ASSERT_NE(videoStage, nullptr);

  QWidget *ancestor = micButton->parentWidget();
  bool foundVideoStageAncestor = false;
  while (ancestor != nullptr) {
    if (ancestor == videoStage) {
      foundVideoStageAncestor = true;
      break;
    }
    ancestor = ancestor->parentWidget();
  }
  EXPECT_TRUE(foundVideoStageAncestor);
}
```

- [ ] **Step 2: Прогнать тест — должен упасть**

Run: `cmake --build build --target Sessio_call_page_tests && ctest --test-dir build -R CallPageTest.ControlBarIsAChildOfVideoStageNotConnectedView --output-on-failure`
Expected: FAIL (сегодня `micButton`'s ancestor chain ведёт к `mConnectedView`, не к `videoStage`).

- [ ] **Step 3: Реализация**

В `buildConnectedScreen()`, найти существующий блок, создающий `controls` (`auto *controls = new QHBoxLayout();`) и добавляющий в него `mMicrophoneToggleButton`/`mCameraToggleButton`/`mFullscreenToggleButton`/`mDevicesButton`/`leaveButton`, а затем `layout->addLayout(controls);`.

Убрать `controls` и `layout->addLayout(controls);`. Кнопки создаются как раньше (весь код создания и `connect(...)` для `mMicrophoneToggleButton`, `mCameraToggleButton`, `mFullscreenToggleButton`, `mDevicesButton`, `leaveButton` — **не меняется**), но вместо `controls->addWidget(...)` — собрать их в новый `mControlBar`:

```cpp
mControlBar = new QWidget(mVideoStage);
mControlBar->setObjectName("controlBar");
mControlBar->setAutoFillBackground(true);
QPalette controlBarPalette = mControlBar->palette();
controlBarPalette.setColor(QPalette::Window, QColor(20, 20, 20));
mControlBar->setPalette(controlBarPalette);
mControlBar->setStyleSheet(QStringLiteral("#controlBar { border-radius: 24px; }"));
auto *controlBarLayout = new QHBoxLayout(mControlBar);
controlBarLayout->setContentsMargins(12, 6, 12, 6);
controlBarLayout->setSpacing(8);
controlBarLayout->addWidget(mMicrophoneToggleButton);
controlBarLayout->addWidget(mCameraToggleButton);
controlBarLayout->addWidget(mDevicesButton);
controlBarLayout->addWidget(mFullscreenToggleButton);
controlBarLayout->addWidget(leaveButton);
mVideoStage->setControlBarWidget(mControlBar);
```

Важно: этот код должен идти **после** того, как `mVideoStage` уже создан и добавлен в `mVideoRow` (сегодня это происходит раньше в `buildConnectedScreen()` — проверить фактический порядок и переставить блок создания кнопок ниже создания `mVideoStage`, если понадобится).

В `call_page.h` добавить поле:

```cpp
QWidget *mControlBar{nullptr};
```

(рядом с `QHBoxLayout *mVideoRow{nullptr};` — и удалить объявление типа `controls`, если оно было полем; по факту `controls` сегодня локальная переменная, не поле, так что убрать её просто как локальную переменную).

- [ ] **Step 4: Прогнать тест и полный набор `CallPageTest`**

Run: `cmake --build build --target Sessio_call_page_tests && ctest --test-dir build -R CallPageTest --output-on-failure`
Expected: PASS для всех, включая новый. Если что-то из существующих тестов упадёт по причинам, не описанным в этом плане (неучтённая зависимость от `controls`-layout) — остановиться и задокументировать находку в отчёте таска, не чинить вслепую мимо этого плана.

- [ ] **Step 5: Commit**

```bash
/usr/bin/git add src/pages/calls_page/call_page.h src/pages/calls_page/call_page.cpp test/call_page_tests.cpp
/usr/bin/git commit -m "Move the call control buttons into a floating opaque control bar"
```

---

### Task 4: Кнопка заметок — отдельная плавающая иконка

**Files:**
- Modify: `src/pages/calls_page/call_page.h`
- Modify: `src/pages/calls_page/call_page.cpp`
- Modify: `test/call_page_tests.cpp`

**Interfaces:**
- Consumes: `VideoStage::setNotesToggleWidget` (Task 2), `pcm::widgets::notesIcon()` (Task 1).

- [ ] **Step 1: Обновить существующие тесты под новый тип виджета**

В `test/call_page_tests.cpp`, ровно 4 места ссылаются на `notesToggleButton` как `QPushButton`; заменить на `QToolButton` (тип уже подключён в файле — `#include <QToolButton>` уже есть):

- Строка (тест `SidePanelToggleHiddenByDefaultUntilMadeVisible`): оба `page.findChild<QPushButton *>("notesToggleButton")` → `page.findChild<QToolButton *>("notesToggleButton")`.
- Тест `SetSidePanelExpandedByDefaultChecksTheNotesToggle`: `auto *toggle = page.findChild<QPushButton *>("notesToggleButton");` → `page.findChild<QToolButton *>("notesToggleButton");`.
- Тест `SetSidePanelExpandedByDefaultIsANoOpWithoutATotoggleYet`: `page.findChild<QPushButton *>("notesToggleButton")` → `page.findChild<QToolButton *>("notesToggleButton")`.

Это ожидаемо упадёт на компиляции прямо сейчас (тип поля в `call_page.h` ещё `QPushButton*`) — это нормально для этого шага, реализация следующим шагом приведёт всё в соответствие.

- [ ] **Step 2: Прогнать — должно не собраться / упасть**

Run: `cmake --build build --target Sessio_call_page_tests`
Expected: либо ошибка компиляции (тип `mNotesToggleButton` ещё `QPushButton*` в `call_page.cpp`), либо (если случайно собралось) падение тестов — ожидаемо на этом шаге.

- [ ] **Step 3: Реализация**

В `call_page.h`: `QPushButton *mNotesToggleButton{nullptr};` → `QToolButton *mNotesToggleButton{nullptr};`.

В `call_page.cpp`, `setSidePanelToggleVisible()`:

```cpp
void CallPage::setSidePanelToggleVisible(bool visible) {
  if (visible && !mNotesToggleButton) {
    mNotesToggleButton = new QToolButton(mVideoStage);
    mNotesToggleButton->setObjectName("notesToggleButton");
    mNotesToggleButton->setCheckable(true);
    mNotesToggleButton->setIcon(pcm::widgets::notesIcon());
    mNotesToggleButton->setToolTip(tr("Notes"));
    mNotesToggleButton->setAccessibleName(tr("Notes"));
    mNotesToggleButton->setAutoFillBackground(true);
    QPalette notesPalette = mNotesToggleButton->palette();
    notesPalette.setColor(QPalette::Button, QColor(20, 20, 20));
    mNotesToggleButton->setPalette(notesPalette);
    mNotesToggleButton->setStyleSheet(QStringLiteral("#notesToggleButton { border-radius: 20px; }"));
    connect(mNotesToggleButton, &QToolButton::toggled, this,
            [this](bool checked) { mSidePanelHost->setVisible(checked); });
    mVideoStage->setNotesToggleWidget(mNotesToggleButton);
  } else if (!visible && mNotesToggleButton) {
    delete mNotesToggleButton;
    mNotesToggleButton = nullptr;
  }
}
```

Убедиться, что старый хардкод `static_cast<QHBoxLayout *>(mConnectedView->layout()->itemAt(1)->layout())->addWidget(mNotesToggleButton);` полностью удалён (он же уже неактуален — `controls`-layout как строка `mConnectedView` больше не существует после Task 3).

Рядом с созданием `mMediaErrorBanner` (`buildConnectedScreen()`) есть комментарий, ссылающийся именно на этот удалённый хардкод (`"Added last (after \`controls\`) so setSidePanelToggleVisible()'s hard-coded mConnectedView->layout()->itemAt(1) lookup ... keeps working"`) — обновить его, убрав упоминание `itemAt(1)`/`controls` (например, заменить на короткую фразу о том, что порядок вставки `mMediaErrorBanner` в `layout` `mConnectedView` больше ни от чего не зависит), чтобы не оставлять недостоверную документацию.

`setSidePanelExpandedByDefault()` не меняется (`mNotesToggleButton->setChecked(expanded)` работает одинаково для обоих типов).

Добавить `#include <QToolButton>` в `call_page.cpp`, если ещё не включён (в `call_page.h` `QToolButton` уже forward-declared, но `.cpp` нуждается в полном заголовке для `new QToolButton(...)` — проверить, возможно уже подключён ради `mMicrophoneToggleButton` и т.п.).

- [ ] **Step 4: Прогнать полный `CallPageTest`**

Run: `cmake --build build --target Sessio_call_page_tests && ctest --test-dir build -R CallPageTest --output-on-failure`
Expected: PASS, все кейсы.

- [ ] **Step 5: Commit**

```bash
/usr/bin/git add src/pages/calls_page/call_page.h src/pages/calls_page/call_page.cpp test/call_page_tests.cpp
/usr/bin/git commit -m "Make the notes toggle a floating icon button instead of a control-bar entry"
```

---

### Task 5: Версия, changelog, переводы, финальная проверка

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/app/application.cpp`
- Modify: `CHANGELOG.md`
- Modify: `translation/app_ru.ts`, `translation/app_en.ts`

- [ ] **Step 1: Поднять версию**

`CMakeLists.txt`: `project(Sessio VERSION 0.2.2 LANGUAGES CXX)` → `0.2.3` (если текущее значение на момент выполнения таска уже не `0.2.2` — взять актуальное и поднять патч-версию на 1, независимо от точного числа здесь).
`src/app/application.cpp`: `app.setApplicationVersion("0.2.2");` → `"0.2.3"` (тем же правилом).

- [ ] **Step 2: `CHANGELOG.md`**

Новая запись (сверить точный стиль заголовков/буллетов с текущим файлом перед вставкой):

```markdown
## [0.2.3] - <дата выполнения таска, YYYY-MM-DD>

### Changed

- The call screen's control bar (mute, camera, devices, fullscreen, leave)
  now floats as an opaque bar over the video instead of sitting in a
  docked strip below it.
- The notes toggle is now a separate floating button in the corner of the
  call screen instead of living in the control bar.
```

- [ ] **Step 3: Синхронизация переводов**

Run: `cmake --build build-release --target update_translations`

Новые/изменившиеся `tr()`-строки, введённые этим планом: ни одной новой — `tr("Notes")` уже существовал (раньше текст кнопки, теперь текст tooltip/accessibleName, но строка та же). Если `update_translations` всё равно покажет `type="unfinished"` записи (например, из-за смены контекста использования строки в `.ts`-файле) — перевести их в `app_ru.ts` на русский и в `app_en.ts` — тем же текстом, что и `<source>` (существующая конвенция), затем повторно прогнать `update_translations` и убедиться, что `type="unfinished"` записей не осталось ни в одном файле.

- [ ] **Step 4: Полная пересборка и тесты**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`
Expected: 100% PASS.

- [ ] **Step 5: Commit**

```bash
/usr/bin/git add CMakeLists.txt src/app/application.cpp CHANGELOG.md translation/app_ru.ts translation/app_en.ts
/usr/bin/git commit -m "Bump version and changelog for the floating call control bar"
```
