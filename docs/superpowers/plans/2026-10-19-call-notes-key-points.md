# Call notes and native tabs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Перенести согласованные материалы звонка и оформление вкладок в C++/Qt Sessio.
**Architecture:** Локальное хранение материалов без обязательного клиента; последовательные запросы заметок через существующий LiteRT-LM worker; отдельные состояния live/final, источники и ручные записи. Нативные QWidget используют тему приложения.
**Tech Stack:** C++20, Qt Widgets, DuckDB, LiteRT-LM C++.
**Spec:** docs/superpowers/specs/2026-10-19-call-notes-key-points-design.md; интерактивный макет согласован пользователем 19 октября.

## Global Constraints

- Только C++, один локальный экземпляр Gemma, отсутствие Python и облачного инференса.
- Ручные записи не перезаписываются генерацией; сохранение не требует клиента.
- Записи привязываются к звонку/транскрипции и доступны через событие, когда оно существует.
- Иконки имеют tooltip, оформление следует палитре темы.

## Review Focus

- Отзыв согласия и удаление не допускают позднего сохранения результата.
- При редактировании источника результат устаревает.
- Ошибка/отмена итога сохраняет промежуточный материал и ручные записи.
- Длинная транскрипция обрабатывается ограниченными частями, без молчаливого обрезания.
- Повторное открытие и backup/restore сохраняют заметки без клиента.

### Task 1: Storage and lifecycle

Files: src/database/{database.h,database.cpp,schema.hpp,prompter_database.cpp}, src/backup/*, test/*.
- [x] Add tests for notes without client, revisions, deletion and backup round-trip.
- [x] Implement local call-material persistence, optional event link and recovery of interrupted processing.
- [x] Run database/migration/backup regression tests.

### Task 2: Notes generation

Files: src/call_prompter/*, src/pages/transcript_page/{prompter_worker,call_prompter_controller}.*, test/*.
- [x] Add bounded structured note/key-point requests with evidence validation and separate live/final status.
- [x] Share the existing model worker; coalesce live updates, cancel safely and reject stale replies.
- [x] Test synthetic scenarios, long transcript chunking and actual Gemma inference.

### Task 3: Native views and integration

Files: src/pages/transcript_page/*, src/pages/calls_page/call_page.*, test/*.
- [x] Implement native Notes sections: manual text, editable model draft, key points and sources.
- [x] Apply approved visual structure to Transcript/Prompter tabs using theme-derived colors.
- [x] Wire live call completion and saved transcript views, retry/cancel, accept/edit/dismiss and event access.
- [x] Verify focus preservation, idempotent acceptance and empty/error states.

### Task 4: Delivery

- [x] Update Antora, EN/RU translations and CHANGELOG/version as required by the existing PR.
- [x] Build Release, run affected tests and isolated runtime validation.
- [x] Review diff, preserve unrelated submodule changes, report real-call validation separately.

### Follow-up: resume 2026-10-20

Пользователь попросил сначала завершить исправления, затем вернуться к UI макета.
Отметки выше относятся к предыдущему прогону, а не к проверке нового diff.

- [x] Preserve call A's automatic final while its writer drains and call B starts; defer and retry background work without mixing materials (controller regression; full app validation follows).
- [x] Merge generated drafts with manual edits made in another view while inference runs.
- [ ] Diagnose and fix the preview-to-call window disappearance with a focused regression.
- [ ] Recheck native Notes / Transcript / Prompter against the approved mockup, including lightweight key points and the ended-call information card.
- [x] Build the complete application, verify changed tests and real-model notes, and capture fresh native screenshots.

Проверено 20 октября: 14 тестов контроллера, 11 тестов панели (с новыми снимками),
два CPU-сценария на настоящей Gemma: исправленный срок со ссылками на источники
и отмена в конце длинной транскрипции. Во время параллельной сборки генерация
заняла 42,4 с и 136,6 с (три модельных вызова) соответственно; это не замер
скорости во время видеозвонка.

Vulkan: проверена осуществимость. Актуальный Linux GPU-путь LiteRT — WebGPU/Vulkan;
в текущем runtime GPU отключён. GPU backend, совместимые динамические библиотеки
и проверка на Radeon 780M ещё не реализованы; C++ остаётся обязательным.

### Follow-up: visual verification 2026-10-21

- Убраны заливка карточки черновика и системные пиктограммы: тонкая линия слева,
  единые контурные иконки, компактные действия, время источников справа у пунктов.
- Исправлен контраст вторичных подписей в тёмной теме; цвет выводится из текущей
  темы приложения, а не из фиксированного цвета макета.
- Release-приложение и тест панели пересобраны с parallel 8. Все 12 тестов панели
  прошли; получены 18 снимков трёх вкладок при ширине 345/440/490 в двух темах.
- Sessio 0.2.33 запущен в изолированном профиле. Это проверка запуска, не звонка.
- Проверки 20 октября: controller 15, CallsPage 20, notes pipeline 17;
  настоящий CPU Gemma — 2 сценария, 28,4 и 88,7 секунды после загрузки модели.
  Эти модельные прогоны 21 октября не повторялись: изменено только оформление.
- Тест preview→call с первым синтетическим видеокадром прошёл под Xvfb/XCB;
  камера и настоящий звонок не проверены. Поэтому соответствующий пункт выше
  остаётся открытым до ручной проверки на рабочем столе.
- Проверка полноценной карточки завершённой встречи и одновременного запуска
  видео + ASR + модели также остаётся открытой. GPU не включён.
