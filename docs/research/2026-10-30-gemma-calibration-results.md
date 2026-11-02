# Калибровка Gemma E4B: B1, ясность инструкций

## Завершение калибровки, 31 октября

Приведённое ниже сравнение B1 сохранено как история эксперимента. Текущая
реализация использует B2: короткие инструкции, три примера извлечения и
вычисляемые pipeline подсказки дат относительно даты звонка. Отдельный личный
приём сохраняется без выдуманного второго участника; для переноса проверяется
точная дата следующего понедельника, включая границу года.

Ошибка constrained decoder исправлена в runtime: SentencePiece byte-fallback
токены передаются llguidance как исходные байты, строковый tokenizer вызывается
только со строками, одиночные stop tokens помечаются специальными. Минимальная
схема с кириллическим enum проверена отдельно. Runtime собран в изолированном
`build-summary-quality/calibration/runtime-token-bytes/install`.

Финальный запуск: **22/22**, включая четыре заранее зафиксированных Holdout
сценария из `2026-10-30-runtime-holdout.md`. Backend принудительно GPU:
AMD Radeon 780M Graphics (RADV PHOENIX), без CPU fallback. Модель и SHA256
остались указанными ниже. Логи: `calibration/gpu-final.log` и `gpu-final.xml`;
173.140 секунды вместе с загрузкой, один прогон, не benchmark. Это проверка
небольшого синтетического набора, а не общая оценка качества. Результат GPU
не сравнивается численно с прежними CPU запусками как эффект одного промпта.
Облачные провайдеры этим запуском не проверялись.

Нативное приложение собрано с текущими prompts и runtime. В ходе GUI-проверки
найден отдельный дефект Qlementine: stylesheet-обёртка мешала расчёту размера
popup. Исправление и регрессионные тесты находятся в общей интеграции стиля.

## Протокол до запуска

Сравнить исходные prompts из commit 8d09bba6 и B1: устранение противоречий
participants, accept/revise, tentative/accepted, пустой задачи и обязательного
завершения разговора. Язык инструкций, sampler, thinking и runtime не менять.
Это проверка пакета B1, а не изолированная оценка каждого предложения.

Development: существующие smoke сценарии chatter, OBS, две встречи, перенос,
поздняя коррекция summary. Новые пять Calibration* сценариев зафиксированы до
запуска и не повторяют примеры prompts: варианты сервера, резервное копирование
на диск, оборванное действие, личный приём, тарифы без решения. Они проверяют
перенос поведения на новые темы; размер набора недостаточен для общей оценки.
После просмотра этих результатов набор больше не считается закрытым holdout.

Сначала baseline, затем B1 на одинаковых тестах и одном backend. Фиксировать
hashes prompts/model, фактический backend, schema failures, результат каждого
теста, сырые синтетические ответы, model-call count и elapsed time. Notes
chunking зависит от размера prompts: сравнение end-to-end включает этот эффект.
Не приписывать изменение latency только качеству инструкции.

Критические ошибки: выдуманное обязательство, неверный участник личной встречи,
слияние двух встреч, потеря переноса, задача из обрывка. Внедрять B1 только при
отсутствии новых критических регрессий; оставшиеся дефекты указать явно.
Автоматические текстовые проверки дополняются чтением синтетических ответов.

## Результаты

Проверки без модели: SummaryPipeline 33/33, LivePointsPipeline 12/12,
CallPrompterPipeline 26/26 проходят с B1 resources.

### Окружение и исключённый предварительный запуск

Обнаружено, что установленный `~/.cache/sessio-litert-lm-gpu/install` содержит
заголовок/runtime до добавления `GenerateOptions::jsonSchema` и
`GenerateStats::constrained`. Старый runtime игнорирует добавленную схему.
Предварительный baseline дал 11/13, но **не входит в итоговое сравнение**.
Предварительный candidate остановлен. Эти логи не доказывают работу актуального
schema-constrained runtime.

Актуальный shim собран отдельно штатным `scripts/build-litert-lm-gpu.sh`:
переиспользованы CPU archives и проверенные prebuilt из существующего cache;
установленные CPU/GPU runtimes не заменялись. Компиляция в Ubuntu distrobox,
модельные проверки на Fedora CPU. Обе итоговые программы используют свежий
`gemma_engine.cpp`, одинаковые pipeline objects и новый shim; различаются
Qt resources prompts. Шаблон, sampler и thinking не менялись.

B1 leaf сокращён до 998 UTF-8 bytes (baseline 1099), форма tasks/questions
указана явно. Это адаптация на development-наблюдениях неправильных объектов;
пять новых сценариев после прочтения предварительных ответов уже не являются
закрытым holdout. Нельзя выдавать итог за независимую оценку обобщения.

Артефакт: Gemma 4 E4B IT LiteRT, SHA256
`0b2a8980ce155fd97673d8e820b4d29d9c7d99b8fa6806f425d969b145bd52e0`.
CPU выбран явно (`SESSIO_PROMPTER_TEST_CPU=1`), 4096 context tokens,
1024 output tokens для schema calls, greedy k=1/temperature=0, thinking OFF.

Локальные артефакты: исходный checkout,
`build-summary-quality/calibration/{manifest.json,schema-baseline.log,schema-candidate.log,schema-baseline.xml,schema-candidate.xml}`.
Manifest содержит SHA256 модели, shim и каждого prompt для обоих вариантов.
Использованы только синтетические реплики; внешние/платные вызовы не делались.

### Итоговое сравнение B1

| Результат на тех же 13 сценариях | Baseline | B1 |
|---|---:|---:|
| Прошли проверки | 10 | 11 |
| Ошибка содержания | 2 | 1 |
| Ошибка runtime/decoder | 1 | 1 |

Baseline 260.938 s, B1 252.880 s вместе с загрузкой модели. Один последовательный
прогон каждого варианта, не benchmark; ускорение не заявляется. Более ранние
несовместимые runtime-запуски в таблицу не включены.

**Улучшение:** SummaryPreservesCorrectionAndCitesOriginalSources. Baseline
оставлял «во вторник, поправка: в четверг»; B1 сохраняет задачу
«Анна отправит план выступления в четверг». Отменённый срок исчез из результата.

**Сохранились:** две отдельные встречи, новая встреча без переноса прежней,
отказ от непринятого предложения, prompt injection как данные, отсев chatter,
принятое решение о резервном диске, отсутствие задачи из обрывка и отсутствие
придуманного завершения обсуждения тарифов. Ответы просмотрены вручную;
новый live point «Рассмотреть перенос архива … как вариант» сохраняет
неопределённость, но его повелительная форма ещё нуждается в стилистической
калибровке. Проверки по подстрокам не являются полноценной semantic evaluation.

**Не закрыто:** CalibrationPersonalMeetingDoesNotInventTheOtherParticipant:
оба варианта пропускают объявленный личный приём. Одно явное правило также не
помогло. Вариант с дополнительным примером остановился на decoder error до
ответа; дополнительные правило/пример убраны. В репозитории оставлен именно
B1 из основного сравнения, а не непроверенный вариант.

**Runtime blocker:** AcceptedRescheduleTargetsTheMeetingWithoutAHint в обоих
вариантах завершается `inference failed` с Parser Error о несовместимых UTF-8
байтах token/grammar. Установлена воспроизводимость, не корневая причина.
Дополнительный пример личного приёма тоже дошёл до такого сбоя. Нельзя считать
это исправленным промптом или объявлять весь model smoke зелёным.

Выбор: оставить B1 как локальное улучшение ясности с измеренным выигрышем на
одном development-сценарии и без новых падений в этом наборе. До полной приёмки
нужны исправление decoder/runtime, повтор личного приёма, независимый holdout
и проверка на рабочем GPU. Cloud-модели этим экспериментом не проверялись.
Полная сборка/GUI приложения не запускались; собраны конкретные pipelines,
Qt resources, engine и тестовые программы с переиспользованием vcpkg.

Чтобы воспроизвести на обычной сборке с актуальным shim:

```bash
rtk proxy env SESSIO_PROMPTER_TEST_CPU=1 \
  SESSIO_PROMPTER_MODEL=/path/to/gemma-4-E4B-it.litertlm \
  SESSIO_PROMPTER_CACHE_DIR=/tmp/sessio-calibration-cache \
  build-release/test/Sessio_call_prompter_model_smoke_tests \
  --gtest_filter='ModelSmokeTest.Calibration*:ModelSmokeTest.AdditionalAnnouncedMeetingIsSeparateFromTheFirst:ModelSmokeTest.AnnouncingAnotherTimeDoesNotRescheduleTheExistingMeeting:ModelSmokeTest.AcceptedRescheduleTargetsTheMeetingWithoutAHint:ModelSmokeTest.UnacceptedProposalAbstains:ModelSmokeTest.InstructionsInsideASourceAreData:ModelSmokeTest.SummaryPreservesCorrectionAndCitesOriginalSources:ModelSmokeTest.LivePointsIgnoreGreetingsAudioChecksAndIncompleteAsr:ModelSmokeTest.LivePointsKeepAConcreteDecisionAmidChatter'
```
