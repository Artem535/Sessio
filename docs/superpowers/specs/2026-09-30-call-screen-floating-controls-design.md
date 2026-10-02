# Плавающая панель управления на экране звонка — дизайн

> Продолжение [2026-09-29-call-ui-polish-design.md](2026-09-29-call-ui-polish-design.md). Тот документ докнул панель управления (`controls`, `QHBoxLayout`) отдельной строкой под видео. После визуального ревью макетов (см. переписку) решили заменить её на непрозрачную плавающую панель поверх видео — тот же приём, что уже применён для self-preview PIP в `VideoStage`.

**Цель:** сделать `Connected`-экран `CallPage` более "immersive": видео занимает всю площадь `VideoStage`,控ролы (mic/camera/devices/fullscreen/leave) — непрозрачная плавающая "таблетка" снизу по центру, кнопка заметок — отдельная плавающая круглая иконка в углу.

**Архитектура:** расширение `pcm::video::detail::VideoStage` двумя новыми управляемыми слотами (control-bar и notes-toggle), тем же способом, что уже используется для remote/local-preview слотов — прямое позиционирование в `layoutChildren()`, без `QLayout`. Кнопки (`mMicrophoneToggleButton` и т.д.) и их сигнальная проводка **не меняются** — меняется только то, чьим ребёнком и где они физически расположены.

**Tech Stack:** C++20, Qt Widgets, существующий `VideoStage`/`QOpenGLWidget`-рендерер без изменений.

## Global Constraints

- **Никакой полупрозрачности** поверх `RemoteVideoRenderer` (`QOpenGLWidget`) — решение принято явно после обсуждения риска регресса transparency-бага, исправленного в Task 4 предыдущего плана (`painter.fillRect(rect(), Qt::black)` в `paintGL()`). Все новые overlay-виджеты — полностью непрозрачные (`background-color` без альфа-канала), как уже сделано для self-preview PIP.
- Логика самих кнопок (`toggled`-лямбды, `attachSession()`-ресинк, `setMicrophoneEnabled`/`setCameraEnabled`/`switchCamera` и т.д.) не трогается — это чисто композиционный рефакторинг родителя/позиционирования.
- `setSidePanelToggleVisible(bool)`/`setSidePanelExpandedByDefault(bool)` — публичный контракт `CallPage` не меняется (сигнатуры и поведение снаружи те же), но их **внутренняя реализация обязана измениться**: `mNotesToggleButton` больше не живёт в `controls` (`QHBoxLayout`), поэтому `mConnectedView->layout()->itemAt(1)->layout()->addWidget(...)` (текущий хардкод) должен быть заменён на прямое размещение в `mVideoStage`.
- `FakeVideoProvider` не трогаем — доработка не касается `VideoProvider`.
- Аудио-waveform (cava-style) по-прежнему вне скоупа.

---

## 1. `VideoStage`: новые слоты

`src/pages/calls_page/call_page.h`/`.cpp`. Текущий класс управляет `mRemoteWidget` (fill) и `mLocalPreviewWidget` (PIP, нижний правый угол). Добавляем два новых optional-слота:

```cpp
class VideoStage final : public QWidget {
public:
  explicit VideoStage(QWidget *parent = nullptr);

  void setRemoteWidget(QWidget *widget);
  void setLocalPreviewWidget(QWidget *widget);
  // Новое: непрозрачная "таблетка" с mic/camera/devices/fullscreen/leave.
  // nullptr скрывает контрол-бар целиком (не используется сегодня, но
  // симметрично setLocalPreviewWidget(nullptr), которое уже так себя ведёт).
  void setControlBarWidget(QWidget *widget);
  // Новое: отдельная круглая кнопка заметок, верхний правый угол.
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

`layoutChildren()` позиционирует все четыре независимо (порядок вызовов `raise()` важен — контролы и кнопка заметок должны быть выше remote-видео и локального preview):

- `mRemoteWidget` — `setGeometry(rect())`, как сейчас.
- `mLocalPreviewWidget` — 160×90, правый нижний угол, отступ 12px, как сейчас.
- `mControlBarWidget` — `sizeHint()`-ширина (панель сама знает свою ширину через `QHBoxLayout` внутри), высота фиксированная (48px), по центру по горизонтали, отступ 20px снизу.
- `mNotesToggleWidget` — 40×40, правый верхний угол, отступ 12px.

Каждый setter, если виджет уже был раньше установлен и заменяется, вызывает `oldWidget->setParent(nullptr)` (тот же borrowed-widget паттерн, что и `setRemoteWidget`/`setLocalPreviewWidget` — здесь применяется не к чужим (borrowed) виджетам, а к собственным дочерним `CallPage`, но контракт (никогда не удалять внутри `VideoStage`) сохраняется ради единообразия и симметрии с уже написанными тестами).

## 2. `CallPage`: сборка панели управления

`buildConnectedScreen()` в `call_page.cpp` меняется так:

- Убираем `controls` (`QHBoxLayout`) как строку `mConnectedView`'ного `layout()->addLayout(controls)`.
- Кнопки (`mMicrophoneToggleButton`, `mCameraToggleButton`, `mFullscreenToggleButton`, `mDevicesButton`, `leaveButton`) создаются как раньше (родитель временно любой — конструктор Qt-виджета всё равно репарентится при `addWidget`), но добавляются в новый `QWidget *mControlBar` с `QHBoxLayout` внутри:

```cpp
mControlBar = new QWidget(mVideoStage);
mControlBar->setObjectName("controlBar");
// Фон задаётся только через QSS (непрозрачно, без альфы): как только QSS-правило
// сматчилось, QStyleSheetStyle сам рисует виджет и заливка через
// setAutoFillBackground/QPalette не отрисовывается вовсе (проверено рендером).
mControlBar->setStyleSheet(
    "#controlBar { background-color: rgb(20,20,20); border-radius: 24px; }");
auto *controlsLayout = new QHBoxLayout(mControlBar);
controlsLayout->setContentsMargins(12, 6, 12, 6);
controlsLayout->setSpacing(8);
controlsLayout->addWidget(mMicrophoneToggleButton);
controlsLayout->addWidget(mCameraToggleButton);
controlsLayout->addWidget(mDevicesButton);
controlsLayout->addWidget(mFullscreenToggleButton);
controlsLayout->addWidget(leaveButton);
mVideoStage->setControlBarWidget(mControlBar);
```

`leaveButton` остаётся `QPushButton` с `tr("Leave")`, но т.к. теперь он сидит в тёмной непрозрачной таблетке без окружающего фона формы — оставляем существующий стиль кнопки (Qlementine сам красит деструктивные кнопки; если контраст окажется недостаточным при ручной проверке — это тюнинг стиля, не архитектурная правка, делается в рамках того же таска через `setProperty("qlementine_role", "destructive")`, если такое свойство уже используется где-то в проекте — проверить перед использованием, иначе оставить как есть).

`mMediaErrorBanner` (некритичный баннер ошибки устройства) остаётся в основном `QVBoxLayout` `mConnectedView`, под `mVideoRow` — он не часть control-bar и не перемещается.

## 3. `mNotesToggleButton`: отдельная плавающая кнопка

Сейчас `setSidePanelToggleVisible()` создаёт `mNotesToggleButton` как `QPushButton(tr("Notes"))` и вставляет его в `controls`-layout по хардкоду индекса. Новая версия:

```cpp
void CallPage::setSidePanelToggleVisible(bool visible) {
  if (visible && !mNotesToggleButton) {
    mNotesToggleButton = new QToolButton(mVideoStage);
    mNotesToggleButton->setObjectName("notesToggleButton");
    mNotesToggleButton->setCheckable(true);
    mNotesToggleButton->setIcon(pcm::widgets::notesIcon());
    mNotesToggleButton->setToolTip(tr("Notes"));
    mNotesToggleButton->setAccessibleName(tr("Notes"));
    mNotesToggleButton->setStyleSheet(
        "#notesToggleButton { background-color: rgb(20,20,20); border: none;"
        " border-radius: 20px; }"
        "#notesToggleButton:hover { background-color: rgb(45,45,45); }"
        "#notesToggleButton:checked { background-color: rgb(70,70,70); }");
    connect(mNotesToggleButton, &QToolButton::toggled, this,
            [this](bool checked) { mSidePanelHost->setVisible(checked); });
    mVideoStage->setNotesToggleWidget(mNotesToggleButton);
  } else if (!visible && mNotesToggleButton) {
    delete mNotesToggleButton;
    mNotesToggleButton = nullptr;
  }
}
```

Тип меняется с `QPushButton*` на `QToolButton*` (единообразно с остальными icon-кнопками панели — `mMicrophoneToggleButton` и т.д. уже `QToolButton*`), под это меняется и объявление поля в `call_page.h`. Новая иконка `notesIcon()` добавляется в `src/widgets/call_control_icons.h`/`.cpp` (тот же self-drawn `QPainter`-подход, что и у существующих `microphoneIcon`/`cameraIcon`/`devicesIcon`/`fullscreenIcon` — блокнот/список, минималистичный контур).

`setSidePanelExpandedByDefault()` не меняется (`mNotesToggleButton->setChecked(expanded)` работает одинаково для `QPushButton` и `QToolButton`).

## 4. Самопросмотр (self-preview) и `mSidePanelHost`

Не меняются. `mSidePanelHost` (боковая панель заметок) остаётся докнутым виджетом в `mVideoRow` рядом с `mVideoStage`, а не внутри `VideoStage` — заметки не накладываются на видео, только раскрываются/схлопываются сбоку, как сейчас.

## 5. Тесты

Существующие тесты `CallPageTest`, которые опираются на то, что кнопки — прямые дети `mConnectedView`/`controls`, нужно проверить и, если они полагаются на конкретного родителя или геометрию `controls`-layout, поправить на `parentWidget() == mControlBar` (или `mVideoStage` — для кнопки заметок). Конкретный список тестов на выяснение и правку — в плане реализации (задача тестового аудита предшествует правке, т.к. итоговый список тестов, завязанных на старую структуру, не перечислен в этом документе).

Новый тест на `VideoStage`: `layoutChildren()` позиционирует control-bar по центру снизу и notes-toggle в верхнем правом углу — по аналогии с уже существующими geometry-тестами для remote/local-preview (`resizeAndDeliverEvent()` helper уже есть в `test/call_page_tests.cpp`, переиспользуется).
