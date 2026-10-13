# Доработки экрана звонка (CallPage) — дизайн

> Продолжение [2026-09-27-native-call-ui-and-client-mode-design.md](2026-09-27-native-call-ui-and-client-mode-design.md) (issue [#80](https://github.com/Artem535/Sessio/issues/80)). Тот дизайн описывал у `Connected`-экрана mute/camera-toggle, device-switch и локальный PIP в прозе (§3), но в фактический план это не превратилось: [Task 12 плана](../plans/2026-09-27-native-call-ui-and-client-mode.md) (`CallPage`) специфицировал только `Leave` и переключаемую боковую панель — их так и не реализовали. Этот документ закрывает этот разрыв и добавляет новые доработки, запрошенные после ручного тестирования.

**Цель:** довести `Connected`-экран `CallPage` до того, что реально нужно для звонка психолог-клиент: управление своими устройствами на лету, self-preview, полноэкранный режим, единая типографика/анимация ожидания, и убрать визуальный баг с прозрачными областями.

**Архитектура:** расширение интерфейса `pcm::video::VideoProvider` новыми виртуальными методами (mute/camera-toggle, локальный preview-виджет, переключение устройств) с реализацией в `LiveKitVideoProvider` через уже существующий `VideoCaptureAdapter`; переработка `CallPage`'а `Connected`-экрана и статусных экранов без изменения его внешнего API (`attachSession`/`setSidePanelWidget`/`setSidePanelToggleVisible` не меняются).

**Tech Stack:** C++20, Qt Widgets (`QOpenGLWidget`, `QPainter`, `QPropertyAnimation`), уже используемый в проекте шрифт Inter/Inter Display (через `Qlementine`), существующий `livekit-sdk`.

## Global Constraints

- Не трогаем `VideoSession`'ов конечный автомат состояний и его сигналы (`stateChanged`, `joinFailed`, `reconnectFailed`, `connectionLost`, `mediaError`) — все доработки идут через новые *дополнительные* методы `VideoProvider`, не через изменение существующих.
- Заметки не должны попадать на удалённый (транслируемый) трек ни при каком новом состоянии UI (полноэкранный режим, PIP) — унаследовано из дизайна #80.
- Никаких групповых звонков, чата, screen share, waiting room, записи — вне скоупа как и раньше (роадмап §5.1/§5.5).
- `FakeVideoProvider` (test double, `test/fake_video_provider.h`) должен получить тривиальные реализации всех новых виртуальных методов `VideoProvider`, чтобы существующие тесты `CallPageTest`/`VideoSessionTest` не сломались, и новые тесты могли проверять проводку кнопок без реального SDK.
- Волновой индикатор звука (по типу cava) — явно отложен на отдельный fast-follow, не часть этого документа/плана.

---

## 1. Domain-слой: расширение `VideoProvider`

Новые виртуальные методы в `src/video/video_provider.h`, каждый с безопасным дефолтом (не меняет поведение существующих подклассов, пока они не переопределены):

```cpp
// Self-preview: аналог remoteVideoWidget(), но для локального видео.
// Владение — как у remoteVideoWidget(): провайдер владеет виджетом,
// вызывающий код может его репарентить, но обязан отдать обратно
// (setParent(nullptr)), а не удалять.
virtual QWidget *localVideoWidget() { return nullptr; }

virtual void setMicrophoneEnabled(bool enabled) { Q_UNUSED(enabled); }
virtual void setCameraEnabled(bool enabled) { Q_UNUSED(enabled); }
[[nodiscard]] virtual bool isMicrophoneEnabled() const { return true; }
[[nodiscard]] virtual bool isCameraEnabled() const { return true; }

virtual void switchCamera(const QCameraDevice &device) { Q_UNUSED(device); }
virtual void switchMicrophone(const QAudioDevice &device) { Q_UNUSED(device); }
virtual void switchSpeaker(const QAudioDevice &device) { Q_UNUSED(device); }
```

`LiveKitVideoProvider` реализует их через уже существующий `mVideoCapture`/`mAudioCapture` (`VideoCaptureAdapter`/`AudioCaptureAdapter`) — то есть тот же самый захват, что публикуется в LiveKit, а не отдельная параллельная камера-сессия:

- `localVideoWidget()`: небольшой, лениво создаваемый `QLabel`-виджет, подписанный на `mVideoCapture->previewSink()->videoFrameChanged` — тот же паттерн, что уже работает в `DeviceCheckWidget::ensurePreviewAdapter()`.
- `setCameraEnabled(false)`/`setMicrophoneEnabled(false)`: останавливают/приостанавливают публикацию соответствующего локального трека через LiveKit SDK (mute на `LocalVideoTrack`/`LocalAudioTrack`); `isCameraEnabled()`/`isMicrophoneEnabled()` отражают последнее установленное значение.
- `switchCamera/Microphone/Speaker(device)`: тот же порядок действий, что `DeviceCheckWidget::restartPreview()` уже делает для превью — останавливает старый захват, запускает новый с указанным устройством, republish трека. Ошибка (устройство пропало, занято другим процессом) идёт через уже существующий `mediaError()` — нового сигнала не нужно.

`FakeVideoProvider` получает тривиальные bool-поля для mic/camera-enabled и счётчики вызовов `switchCamera/Microphone/Speaker`, чтобы тесты `CallPage` могли проверять проводку без реального SDK.

## 2. Макет и типографика (п.1 запроса)

Единая система именно для экрана звонка (не общая тема приложения — она сейчас не унифицирована, и трогать её отдельная, более крупная задача):

- **20pt статусный заголовок** (`Connecting...`/`Waiting for the other participant...`/`Call ended.`) — `QFont("Inter Display", 20)`. Тот же шрифт, что `QlementineStyle` уже использует для роли `TitleFont` по всему приложению ([QlementineStyle.cpp:107-110](../../../third_party/qlementine/lib/src/style/QlementineStyle.cpp), [Theme.cpp:216](../../../third_party/qlementine/lib/src/style/Theme.cpp)) — уже вшит в бинарник, ничего дополнительно грузить не нужно.
- **16pt баннеры** (`Reconnecting...`, media-error) — `QFont("Inter", 16)`.
- **13pt подписи под кнопками управления** — `QFont("Inter", 13)`.
- Все статусные `QLabel` (`mConnectingLabel`, `mEndedReasonLabel`, будущий заголовок на `mEndedScreen`) получают `Qt::AlignCenter` и оборачиваются в layout с центрирующими stretch'ами по обе стороны, а не просто прижаты к верху `QVBoxLayout`, как сейчас.

## 3. Индикаторы ожидания (п.2)

Новый переиспользуемый виджет `BusySpinner` (`src/widgets/busy_spinner.h/.cpp`): `QWidget`, рисует вращающуюся дугу через `QPainter` в `paintEvent`, приводится в движение `QPropertyAnimation` на добавленном Q_PROPERTY угле поворота (~1 оборот/сек, анимируется только поворот — не геометрия, значит не вызывает релейаут). Останавливается (перестаёт тикать) когда `hide()`, чтобы не тратить CPU на скрытом экране.

Используется рядом с текстом (не вместо него — на разных экранах текст несёт разный смысл):
- На `mConnectingScreen` — над `mConnectingLabel`.
- В `mReconnectingBanner` — маленький (16×16) слева от текста.

## 4. Панель управления (п.3, 5, 6)

Новая строка кнопок в `buildConnectedScreen()`, под текущим `controls`-layout (который уже хранит `leaveButton`; `setSidePanelToggleVisible()` жёстко обращается к `mConnectedView->layout()->itemAt(1)` — этот индекс не меняется, новая строка добавляется отдельным layout'ом, не заменяет существующий).

- Переключаемые `QToolButton`, у каждого меняется и иконка, и фон при выключенном состоянии (не только цвет — доступность):
  - **Микрофон** — `checkable`, `toggled` → `mSession->provider()->setMicrophoneEnabled(!checked)`.
  - **Камера** — то же самое через `setCameraEnabled`.
- **Устройства** — кнопка открывает компактный `QMenu`/поповер с тремя `QComboBox` (камера/микрофон/динамик), заполненными из того же `DeviceManager*`, что уже передаётся в конструктор `CallPage`. Выбор в комбобоксе сразу дёргает `switchCamera/Microphone/Speaker` на текущем провайдере.
- **Fullscreen** — `checkable QToolButton`, переключает `window()->showFullScreen()`/`showNormal()` на верхнеуровневом окне приложения; `F11` и `Esc` — стандартные хоткеи (Esc выходит из fullscreen, не приводит к leave).
- `leaveButton` остаётся визуально отдельно (отступ побольше, самая заметная/красная кнопка) — не смешивается с группой toggle-кнопок.

## 5. Self-preview (п.4)

`mVideoRow`'s слот 0 (сейчас — просто виджет в `QHBoxLayout`) оборачивается в новый `mVideoStage` (`QWidget` без layout, только с `resizeEvent`), который держит:
- Remote-виджет, растянутый на весь `mVideoStage` через явный `setGeometry` в `resizeEvent` (замена того, что раньше делал `QHBoxLayout`).
- `mLocalPreview` — виджет из `provider->localVideoWidget()` (или скрыт, если провайдер вернул `nullptr`), фиксированного размера 160×90 (тот же аспект, что видеозахват 1280×720), позиционируется в правом нижнем углу `mVideoStage` с отступом 12px, `raise()`d поверх remote-виджета. Без drag — по запросу это просто окошко в углу, перетаскивание можно добавить позже отдельной доработкой.

`updateRemoteVideoWidget()` меняется на позиционирование через `mVideoStage`, но сохраняет текущий контракт владения (репарентинг без удаления заимствованного виджета провайдера) без изменений.

## 6. Баг с прозрачностью (п.7) — причина и фикс

Причина найдена в `RemoteVideoRenderer::paintGL()` ([remote_video_renderer.cpp:67-81](../../../src/video/remote_video_renderer.cpp)):
- Если кадра ещё нет (`frame.isNull()`), метод делает `return` без единой отрисовки — весь виджет остаётся с тем, что было в буфере `QOpenGLWidget` до этого.
- Если кадр есть, но его пропорции не совпадают с виджетом (`Qt::KeepAspectRatio` — леттербокс-полосы по краям), эти полосы никогда не закрашиваются.

В обоих случаях `QOpenGLWidget` показывает непрочищенный буфер — отсюда «прозрачные области там, где нет видео».

**Фикс:** в начале `paintGL()`, до раннего `return` и до отрисовки кадра — `painter.fillRect(rect(), Qt::black)` (стандартный цвет леттербокс-полос у видеозвонков — Zoom/Meet/Teams все используют чёрный). Заливка происходит всегда, независимо от того, есть кадр или нет.

## 7. Автораскрытие панели заметок (п.9)

`Application::connectSignals()` ([application.cpp:966-982](../../../src/app/application.cpp)) уже резолвит клиента для текущего звонка и вызывает `ClientNotesPage::setClientInfo()` — привязка к правильному клиенту работает. Единственное, чего не хватает: панель по умолчанию свёрнута, пока пользователь сам не нажмёт «Notes».

**Фикс:** `CallPage` получает новый метод `setSidePanelExpandedByDefault(bool)`, вызываемый из того же обработчика `eventKnownForCurrentCall`, когда `client` резолвится успешно (т.е. звонок привязан к реальному клиенту/событию) — `mNotesToggleButton->setChecked(true)` в этом случае. Звонки без привязанного клиента (гостевой/клиентский вход) — как раньше, свёрнуто.

## Тестирование

- `CallPageTest`: новые тесты на проводку кнопок mic/camera toggle → `FakeVideoProvider`'s счётчики/bool-поля; на появление/скрытие `mLocalPreview`, когда `localVideoWidget()` возвращает не-`nullptr`/`nullptr`; на `setSidePanelExpandedByDefault(true)` действительно разворачивающий панель.
- `RemoteVideoRenderer`: тест, что `paintGL()`-эквивалентная логика заливки фона вызывается и при пустом кадре, и при letterbox — тестируется через рефакторинг заливки в отдельный, тестируемый без реального GL-контекста метод (например, вычисление прямоугольников леттербокса как чистая функция), если прямое тестирование `paintGL()` невозможно без реального OpenGL-контекста.
- `BusySpinner`: тест, что таймер анимации не тикает, когда виджет скрыт (`hide()`), и тикает после `show()`.
- Ручная проверка (не автоматизируется): реальный звонок на двух машинах — переключение камеры/микрофона/устройств во время звонка, fullscreen/exit fullscreen, визуальная проверка отсутствия прозрачных областей.

## Явно вне скоупа

- Волновой индикатор звука (cava-style) — отдельный fast-follow после того, как макет панели управления устоится.
- Drag для self-preview PIP.
- Изменение общей темы/QSS приложения — типографика здесь ограничена экраном звонка.
- Любые изменения `VideoSession`'а состояний/сигналов.
