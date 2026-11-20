# Spike: мост звука QtWebEngine → C++

Цель — выбрать транспорт, по которому страница звонка в QtWebEngine (Jitsi / SaluteJazz через их JS SDK) отдаёт в C++ PCM каждого участника. Спецификация: `docs/superpowers/specs/2026-10-27-web-calls-kb-hints-cloud-design.md`, часть 1.

## Что сделано

Страница (`web/`) отдаётся из собственной схемы `sessio-audio://app` (secure origin — нужен для AudioWorklet и getUserMedia). `AudioContext` работает на 16 кГц, поэтому ресемплинг делает браузер. Каждая дорожка проходит через AudioWorklet `pcm-tap.js`, который режет её на int16 пачки по `--chunk-ms` и передаёт буфер в главный поток. Главный поток добавляет заголовок (версия, id дорожки, seq, время отправки) и отправляет пачку одним из транспортов:

| `--transport` | путь |
|---|---|
| `channel` | QWebChannel, слот `push(base64)` — доставка в GUI-поток |
| `ws` | WebSocket на 127.0.0.1 со случайным портом, одноразовым токеном и проверкой Origin; бинарные кадры; `--ws-thread` переносит сервер в отдельный поток |
| `scheme` | `fetch POST sessio-audio://app/push`, тело читается через `QWebEngineUrlRequestJob::requestBody()` |
| `none` | ничего не отправляет — база для CPU |

Режимы источника: `synthetic` — тоны с AM-модуляцией; `loopback` — те же тоны через пару `RTCPeerConnection` в странице, то есть настоящие удалённые WebRTC-дорожки (Opus, jitter buffer); `jitsi` — `lib-jitsi-meet` с сервера `--jitsi-domain`, комната `--room`, каждая удалённая аудиодорожка тапается под `participantId:displayName`; `--mic` добавляет локальный микрофон как `local`.

C++ (`audio_stats.cpp`) разбирает пачки одинаково для всех транспортов: считает пачки, разрывы seq, переупорядочивание, RMS и задержку «отправка из главного потока страницы → разбор в C++»; в конце сверяет число полученных пачек с числом созданных страницей. CPU — сумма utime+stime всего дерева процессов (браузерный процесс, zygote, renderer, GPU, utility) после прогрева.

## Как запустить

```bash
cmake -S spikes/webengine-audio-bridge -B build-bridge -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-bridge -j4
QT_QPA_PLATFORM=offscreen build-bridge/audio_bridge_spike --transport ws --ws-thread --mode loopback --seconds 60
spikes/webengine-audio-bridge/run-matrix.sh build-bridge/audio_bridge_spike 60 results.jsonl
python3 spikes/webengine-audio-bridge/summarize.py results.jsonl
```

Собирается с Qt 6.10.2 из distrobox (Ubuntu 26.04), WebEngine/WebChannel есть, QtWebSockets нет — поэтому в спайке минимальный RFC 6455 сервер на `QTcpServer`.

## Результаты

60 с на прогон, offscreen, 20 мс пачки, 2026-10-27, сырые данные в `results-2026-10-27.jsonl`. CPU — проценты одного ядра для всего дерева процессов; «none» — база без транспорта (WebAudio + WebRTC loopback / синтетика).

| transport | mode | tracks | GUI jank ms | CPU % (tree) | p50 ms | p99 ms | max ms | missing | gaps |
|---|---|---|---|---|---|---|---|---|---|
| none | loopback | 3 | 0 | 13.8 | 0.00 | 0.00 | 0.0 | 0 | 0 |
| channel | loopback | 3 | 0 | 16.4 | 0.26 | 0.48 | 7.7 | 0 | 0 |
| ws | loopback | 3 | 0 | 15.1 | 0.13 | 0.38 | 7.7 | 0 | 0 |
| scheme | loopback | 3 | 0 | 24.1 | 0.52 | 1.01 | 3.7 | 0 | 0 |
| ws-thread | loopback | 3 | 0 | 15.2 | 0.13 | 0.38 | 1.1 | 0 | 0 |
| none | synthetic | 10 | 0 | 5.5 | 0.00 | 0.00 | 0.0 | 0 | 0 |
| channel | synthetic | 10 | 0 | 10.1 | 0.14 | 0.35 | 4.2 | 0 | 0 |
| ws | synthetic | 10 | 0 | 8.3 | 0.12 | 0.32 | 3.3 | 0 | 0 |
| scheme | synthetic | 10 | 0 | 31.0 | 0.45 | 0.89 | 3.8 | 0 | 0 |
| channel | loopback | 3 | 300 | 15.7 | 0.26 | 283.22 | 299.4 | 0 | 0 |
| ws | loopback | 3 | 300 | 15.0 | 0.18 | 273.95 | 300.3 | 0 | 0 |
| scheme | loopback | 3 | 300 | 25.1 | 0.60 | 270.15 | 296.9 | 0 | 0 |
| ws-thread | loopback | 3 | 300 | 16.3 | 0.26 | 0.48 | 0.9 | 0 | 0 |

Надбавка транспорта к базе: 3 WebRTC-дорожки — channel +2.6, ws +1.3, scheme +10.3 п. п.; 10 синтетических — channel +4.6, ws +2.8, scheme +25.5 п. п. Потерь и разрывов seq нет ни в одном прогоне. Задержка «главный поток страницы → C++» без блокировок — доли миллисекунды у всех.

Блокировка GUI-потока на 300 мс каждые 2 с: channel, ws в GUI-потоке и scheme получают p99 ≈ 270–285 мс (данные копятся и приходят пачкой, но не теряются). ws в отдельном потоке — p99 0.48 мс: звук не зависит от интерфейса.

## Выводы

**Транспорт — локальный WebSocket, сервер в отдельном потоке.**

- `scheme` отпадает: каждый `fetch` — отдельный запрос сетевого сервиса Chromium, на 10 дорожках это четверть ядра.
- `channel` работает, но вдвое дороже ws (base64 + JSON) и всегда доставляет в GUI-поток; транскрипция становится заложником подвисаний интерфейса.
- ws: бинарные кадры, минимальная надбавка, приём вне GUI-потока. Защита: 127.0.0.1, случайный порт, одноразовый токен в URL, проверка `Origin: sessio-audio://app`.
- Для продакшена — QtWebSockets (`QWebSocketServer`) вместо самописного сервера спайка; в CI-тулчейны Windows/macOS его нужно добавить.
- Страницу отдавать из собственной secure-схемы, как в спайке: AudioWorklet и getUserMedia работают, внешние скрипты SDK грузятся.

**Jitsi.** `lib-jitsi-meet` грузится с сервера в нашу страницу, XMPP по WebSocket с origin `sessio-audio://app` сервером не отвергается. Но meet.jit.si больше не пускает анонимные подключения: `connection.passwordRequired token required` (SASL). Для проверки живого звонка нужен свой сервер Jitsi (docker-jitsi-meet) или JaaS с JWT. В продакшене это значит: адрес сервера и, при необходимости, JWT — настройки пользователя; meet.jit.si как сервер по умолчанию не годится.

**Живой звонок на своём сервере (2026-10-28).** Тестовый docker-jitsi-meet `stable-11248-2` на `jitsi.sessio-pcm.ru` (сервер Sessio, `/opt/jitsi`, только JWT, токены — `/opt/jitsi/mint-token.py`). Спайк (`--mode jitsi --jwt-file`) берёт XMPP-домены из `config.js` сервера, подключается по JWT и входит в комнату. Участник «Ноутбук» из браузера: спайк получил его аудиодорожку как `712314e6:Ноутбук`, 21.8 с речи, без потерь, задержка «страница → C++» p99 0.33 мс, CPU всего дерева 3.8 % ядра. Дорожка участника дважды исчезала и появлялась заново (переключение P2P ↔ JVB при двух участниках), выход участника обработан. Вывод для продукта: на одного участника приходит несколько дорожек подряд — склеивать по participantId, а seq/состояние вести на участника, а не на дорожку (в спайке seq начинается заново, отсюда 23 «reordered»).

Грабли сервера: `firewall-cmd --reload` сбрасывает правила netavark, и контейнеры перестают резолвить друг друга (Jitsi web → prosody висит с 499). Лечится `podman network reload --all`; порты открывать без reload.

**Не проверено:** три и больше участника одновременно (только JVB-режим; телефон в комнату не зашёл); часовой прогон; влияние видео и отрисовки окна (замер offscreen); SaluteJazz; Windows и macOS.
