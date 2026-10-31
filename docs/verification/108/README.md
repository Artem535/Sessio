# Проверка #108: акцентный цвет синий -> фиолетовый

Версия 0.2.12. Скриншоты сняты оффскрин (`QT_QPA_PLATFORM=offscreen`) на реальных виджетах
приложения с настоящей темой Qlementine Dark (`resources/themes/qlementine_dark.json`),
`QWidget::grab()`. Файлы `before_*` сняты на коде базовой ветки, `after_*` на этой ветке.

## Цвет

Источник: заливка логотипа `resources/icons/brain-solid-full.svg` (`fill="#A78BFA"`),
она же иконка в боковой панели, в трее и окне "О программе".

| Ключ темы | Было | Стало |
|---|---|---|
| primaryColor | #5086ff | #a78bfa |
| primaryColorHovered | #6494ff | #b9a2fb |
| primaryColorPressed | #7aa3ff | #c8b6fc |
| primaryColorDisabled | #2c3448 | #37344d |
| primaryAlternativeColor (фон Switch ON) | #3161f8 | #8b6ef5 |
| primaryAlternativeColorHovered / Pressed / Disabled | #4571fe / #5a82ff / #293346 | #9a80f7 / #9f88f7 / #322f4a |
| primaryColorForeground (+Hovered/Pressed) | #ffffff | #1b1730 |
| primaryColorForegroundDisabled | #455170 | #5a567a |
| focusColor | #3097ff6a | #a78bfa6a |

Белый текст на #a78bfa даёт контраст всего 2.7:1, поэтому текст на акценте тёмный (#1b1730):
6.4:1 на основном, 7.9:1 на hover, 9.5:1 на pressed (тест `TextOnAccentIsReadable` требует >= 4.5).
Нейтральные сине-серые фоны и границы (`backgroundColor*`, `borderColor*`, `neutralColor*`,
`semiTransparentColor*`, `secondaryAlternativeColor*`) не менялись: это поверхность тёмной темы,
а не акцент. Статусные цвета (info/success/warning/error) и цвета событий по умолчанию
(`defaultWorkEventColor`/`defaultPersonalEventColor`) и пользовательские настройки не тронуты.

## Что стало производным от акцента

Хелпер `src/widgets/accent_color.h` (`accentColor`, `onAccentColor`, `accentSoftColor`, `cssRgba`)
читает `QPalette::Highlight`/`HighlightedText`, которые Qlementine выставляет из `primaryColor`.
Убраны литералы: плашка выбранного дня (`rounded_calendar_widget.cpp`, текст на ней тоже
`HighlightedText`), плитки/подчёркивание месяца (`month_picker_widget.cpp`), подчёркивание и
цвет сегодняшнего дня (`event_item.cpp`, `event_info.cpp`; константы в `constants.hpp`
удалены), графики (`analytics_page.cpp`, `client_charts_widget.cpp`), кнопка "Join",
включённые кнопки панели звонка, ссылки/карточки сессий в заметках, аватар в звонках,
кнопки дней недели в редакторе события. Месячная сетка уже использовала палитру.
Столбцы "Personal events" на аналитике сделаны персиковыми (#f0b27a): прежний светло-сиреневый
слился бы с фиолетовыми рабочими.

## Скриншоты

| Экран | До | После |
|---|---|---|
| Календарь, Day (switch OFF, выбранный день, сегодня, New meeting) | before_calendar_day.png | after_calendar_day.png |
| Календарь, Month (switch ON, выбранный месяц, кольцо сегодня) | before_calendar_month.png | after_calendar_month.png |
| Аналитика | before_analytics.png | after_analytics.png |
| График клиента | before_client_charts.png | after_client_charts.png |
| Primary-кнопка, Switch ON/OFF, чекбокс, радио, слайдер, прогресс, фокус, disabled | before_controls.png | after_controls.png |

Все "после" просмотрены: контраст цифр на плашке/плитке и текста на кнопке читаемый, переключатель,
фокус-рамка и прогресс согласованы по цвету. Синие блоки событий в дневной сетке остались синими
намеренно (цвет события по умолчанию, #2563eb).

## Тесты

`Sessio_accent_color_tests`: значения темы, совпадение с заливкой SVG логотипа, контраст текста,
хелпер читает палитру Qlementine, плашка дня и плитка месяца реально рисуются цветом акцента
(и не рисуются старым `93,123,230`), в `src/` не осталось литералов
`5086ff / 93,123,230 / 9fc0ff / d9e6ff / 76,132,255 / 4f83ff / 120,170,255`.

## Ограничения

- Экран звонка, диалог Settings и редактор события целиком не снимались (нужны живой
  LiveKit/диалоги): их акцентные стили проверены только сборкой и поиском литералов.
- Снимки оффскрин, без Wayland-композитора.
