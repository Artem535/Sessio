# Проверка popup Qlementine

## Причина и общее исправление

`QStyleSheetStyle` оборачивает стиль дочернего QComboBox, даже если stylesheet
родителя не задаёт правила для самого списка. `qobject_cast<QlementineStyle*>`
в `ComboboxItemViewFilter::fixViewGeometry` возвращал nullptr. Воспроизведённый
список из двух строк высотой 24 px получал только 14 px высоты.

QlementineStyle теперь передаёт себя фильтру при polish. QPointer защищает
время жизни стиля. Исправление применяется через patch 0003 при конфигурации
сборки; patch 0002 отдельно устраняет рекурсивное создание popup в ChildAdded.

## Все места QWidget

Поиск конструкторов QComboBox в src и Designer-форм в ui:

- SettingsDialog: язык, приватность уведомлений, валюта.
- CloudSettingsPanel: выбор движка для каждой из трёх задач.
- TranscriptionSettingsPanel: ускорение суфлера.
- CallEntryWidget: приглашение Sessio / ссылка Jitsi.
- DeviceCheckWidget: камера, микрофон, динамики.
- CallPage: выбор устройства в меню звонка; общий combo_fit.
- TranscriptPage: выбор записи.
- ClientNotesJournal: фильтр журнала.
- SeriesTimezoneDialog: часовой пояс.
- QEventDetailsWidget: сервер Jitsi.
- eventdetails.ui: клиент, оплата, статус события, инициатор отмены.

Все используют общий QlementineStyle и получают исправление фильтра без
локальных обходов. В src нет замены view на пользовательский QTreeView.
Проверка каждого перечисленного экрана вручную ещё не завершена.

## Проверки

Регрессионный тест воспроизвёл сжатие до исправления и прошёл после него.
Проверены варианты со stylesheet и без, editable и обычный список, 2 и 40
элементов, maxVisibleItems=8, два открытия, скрытие после анимации и смена
выбора клавишей Down. Окно расположено у нижнего края экрана. Три теста
прошли с реальным XCB backend; геометрия двух строк также проверена offscreen.
Это проверка общего виджета, а не полная ручная приёмка каждого экрана.

QML ComboBox в MeetingsWorkspace, TranscriptReader, ClientNotesWorkspace,
NotesEditor, NotesWorkspace и TodoColumn используют Qt Quick Controls, а не
этот QWidget-фильтр. Исходники просмотрены отдельно; данный дефект фильтра
к ним не относится. Полная интерактивная проверка QML popup остаётся отдельной
проверкой интерфейса; её нельзя считать выполненной по QWidget-тестам.
