---
title: Архитектура прошивки
description: Устройство проекта SmallTV Ultra, lifecycle, модули, хранение настроек, сеть, дисплей, OTA и ограничения ESP8266.
---

Проект — единая Arduino/PlatformIO firmware для 240×240 ST7789 SmallTV. Основная
цель архитектуры — держать feature-код изолированным и работать в ограничениях
ESP8266: 80 KB RAM, 4 MB flash, небольшой contiguous heap и отсутствие framebuffer.

## Поддерживаемые build targets

| PlatformIO environment | Плата/chip | Результат |
| --- | --- | --- |
| `smalltv` | GeekMagic SmallTV / Ultra, ESP-12F ESP8266 | `.pio/build/smalltv/firmware.bin` |
| `smalltv_loader` | Минимальный ESP8266 OTA loader | `.pio/build/smalltv_loader/firmware.bin` |
| `smalltv_c2` | ESP32-C2 / ESP8684 с CH340C | `firmware.factory.bin` и OTA image |
| `smalltv_esp32` | NM-TV-154 / WROOM-32E | `firmware.factory.bin` и OTA image |

Для текущего Ultra используется `smalltv`. ESP8266 layout — 4 MB flash,
LittleFS и место для двух sketch copies, необходимое OTA.

## Каталоги и главные файлы

| Путь | Ответственность |
| --- | --- |
| `src/main.cpp` | Boot sequence, mode registry, carousel, главный `loop()`, brightness priority. |
| `src/config.h` | Firmware version, feature flags, limits, defaults, source IDs и repository constants. |
| `src/Settings.*` | Typed settings, defaults, JSON migration, LittleFS `/config.json`. |
| `src/Platform.*` | Различия ESP8266/ESP32: Wi-Fi, TLS client, reset info, NTP, update API. |
| `src/Net.*` | STA/AP state machine, выбор Wi-Fi, reconnect/failover, mDNS, captive DNS. |
| `src/Gfx.*` | ST7789 initialization, SPI mode 3, rotation, brightness и общие drawing helpers. |
| `src/WebPortal.*` | HTTP server, config/status API, OTA upload, import/export и embedded UI. |
| `src/webui.h` | Полная HTML/CSS/JavaScript settings page, встроенная в firmware как flash string. |
| `src/Clock.*` | SNTP, timezone, night schedule и trust gate свежести времени. |
| `src/OtaUpdate.*` | GitHub Release check, self-update и ESP8266 update-at-boot. |
| `src/BearSslTuning.cpp` | Ограничения TLS/ECC для памяти ESP8266. |
| `src/Mode.h` | Общий интерфейс `DisplayMode`. |
| `src/features/ticker/` | Sources, stock model и ticker rendering. |
| `src/features/usage/` | AI usage pull/push, compact data model и mascot animation. |
| `src/features/github/` | GitHub feed client, event model, GH//STAT rendering и animations. |
| `emulator/server.mjs` | GitHub bridge, GitHub API normalization, cache, credentials и browser emulator. |
| `partitions/` | ESP32 partition layout. |
| `n8n/` | Примеры custom webhook workflows. |
| `.github/workflows/` | CI build, docs publishing и scheduled quote data. |

## Последовательность загрузки

1. Serial и reset diagnostics.
2. Mount LittleFS, defaults и загрузка `/config.json`.
3. Инициализация ST7789 и boot screen.
4. Подключение к сохранённому Wi-Fi либо запуск setup AP `192.168.4.1`.
5. При необходимости запуск NTP/night schedule.
6. Ранний ESP8266 OTA update, если он был поставлен в очередь.
7. Запуск HTTP settings server.
8. `begin()` всех скомпилированных modes.
9. Экран network information примерно на 3.5 секунды.
10. Переход в основной неблокирующий `loop()`.

Если предыдущая загрузка завершилась exception reset, firmware входит в safe
mode: показывает crash address и оставляет web/OTA доступными, но не запускает
feature rendering, чтобы не повторять boot loop.

## Главный цикл

`loop()` последовательно обслуживает:

- DNS/mDNS/Wi-Fi reconnect;
- HTTP server;
- запланированный reboot;
- clock/night state machine;
- итоговую brightness;
- `service()` активного display mode;
- короткий `delay(5)` для Wi-Fi stack/watchdog.

Только активный mode получает регулярный `service()`. Carousel вызывает `wake()`
при переключении и использует уже кэшированные данные, не создавая лишний запрос.

## Интерфейс DisplayMode

Каждый feature реализует:

- `id()` — строковый identifier;
- `modeConst()` — значение `MODE_*`;
- `begin(settings)` — начальная инициализация;
- `service(settings)` — fetch, state machine и rendering;
- `invalidate(settings)` — настройки изменились, нужно перечитать/перерисовать;
- `wake(settings)` — feature стал видимым после carousel.

Feature обычно разделён на `*Client`, `*Data` и `*Mode`: сеть/парсинг, компактное
состояние в RAM и отрисовка.

## Настройки и LittleFS

Постоянная конфигурация хранится в `/config.json`. Верхний уровень содержит
Wi-Fi, AP, hostname, display и carousel; feature-настройки находятся в объектах
`ticker`, `usage`, `github`, `clock`.

JSON update частичный: отсутствующие keys сохраняют старые значения. Reader
принимает некоторые legacy flat fields и после следующего сохранения переводит
их в актуальную nested схему.

`GET /api/config` маскирует secrets. Полный export отдаёт реальный файл с Wi-Fi
passwords, поэтому backup нужно защищать.

## Сеть

При нескольких сохранённых сетях boot scan сортирует видимые SSID по RSSI,
невидимые/hidden идут после них. Видимой сети даётся до 15 секунд, невидимой —
до 8 секунд. После потери связи firmware сначала пытается reconnect текущей сети,
а после длительного outage переключается между сохранёнными entries.

Если подключение невозможно, запускается AP:

- IP `192.168.4.1`;
- wildcard DNS;
- redirect captive-portal probe и unknown URLs на settings page.

В STA mode регистрируется `_http._tcp` mDNS; usage builds также публикуют
`_ai-usage._tcp` с id, version и push path.

## Дисплей

ST7789 рисуется напрямую через SPI без полного framebuffer. Для SmallTV панель
требует SPI mode 3; custom subclass повторно задаёт MADCTL/BGR при rotation.

Общие цвета и helpers находятся в `Gfx.*`, но каждый feature полностью владеет
своим layout. Text wrapping глобально выключен, поэтому каждый mode обязан
обрезать длинные строки самостоятельно.

Brightness записывается только при изменении effective value. Порядок приоритета:

1. night brightness;
2. LDR auto-brightness;
3. manual brightness.

## GitHub subsystem

### Bridge side

`emulator/server.mjs` поддерживает два источника. Основной webhook source
принимает подписанные GitHub deliveries, дедуплицирует их и сохраняет состояние
через `github-webhooks.mjs`. REST polling оставлен как compatibility fallback:
он выполняет запросы последовательно, объединяет одновременные refresh и
соблюдает rate-limit reset. Лента сортируется active → failed → other и отдаёт
до шестнадцати событий.

В webhook mode owners и allowlist являются фильтрами, а PAT не требуется.
Credentials используются только polling/reconciliation.

### Device side

`GithubClient.cpp`:

- выбирает HTTP или HTTPS client;
- отправляет optional `X-Device-Token`;
- применяет ArduinoJson filter, чтобы не держать весь document;
- поддерживает `items` v0.3 и legacy `runs`;
- сохраняет до `MAX_GITHUB_RUNS`;
- не уничтожает последний успешный cache при сетевой ошибке;
- превращает transport/JSON/API проблемы в стабильные error codes.

`GithubMode.cpp`:

- рисует две крупные карточки на страницу;
- обновляет animation frame каждые 200 ms;
- увеличивает активный timer от `age + millis since fetch`;
- меняет страницу по `rotateSec`;
- считает данные stale после `pollSec × 3 + 10` секунд без успеха;
- показывает отдельный error screen вместо неоднозначного пустого экрана.

Полная пользовательская документация — [GitHub GH//STAT](/smalltv-ultra/features/github/).

## Web UI

`webui.h` — одна embedded HTML page без внешних CSS/JS dependencies. Это важно:
страница работает, когда устройство находится в setup AP без интернета.

Browser получает masked config, формирует partial payload и отправляет его назад.
GitHub bridge settings — исключение: browser обращается непосредственно к bridge,
поэтому GitHub PAT никогда не проходит через ESP8266.

### Локальный связанный стенд

`node emulator/server.mjs` запускает два HTTP-сервера:

- `http://localhost:8788` — экран 240×240, mock/live GitHub bridge и журнал;
- `http://localhost:8789/settings.html` — HTML из `src/webui.h` с эмуляцией API устройства.

Второй сервер хранит локальную конфигурацию в игнорируемом файле
`emulator/device-config.local.json`. После **Save settings** первый сервер видит
изменение примерно за секунду и применяет mode, GitHub feed, poll и rotation без
перезапуска. Это позволяет проверять UI и поведение до сборки `.bin`.

Диагностический ring buffer содержит до 250 событий конфигурации, feed/GitHub
ошибок и эмулированных device actions. Он доступен в панели **Live diagnostics**
и по `GET /api/logs`; `POST /api/logs/clear` очищает его. Secrets в журнал не
записываются. OTA и self-update на локальном сервере только имитируются.

## OTA

Manual OTA принимает `.bin` через `POST /update` и пишет его в свободный OTA slot.
Неудачная запись не заменяет текущий рабочий sketch.

GitHub self-update сравнивает latest release tag с `FW_VERSION`. ESP32 скачивает
в рабочем runtime. ESP8266 ставит update request в LittleFS, reboot, скачивает
image до запуска тяжёлых features и снова reboot после успеха. Причина неудачи
сохраняется и показывается в UI после следующей загрузки.

## Ограничения ESP8266

- 80 KB RAM и чувствительность TLS к крупнейшему непрерывному heap block;
- максимум 8 tickers, 8 GitHub events, 24 aircraft, 6 airports и 4 Wi-Fi entries;
- ArduinoJson filters обязательны для крупных ответов;
- network requests не должны выполняться каждый display frame;
- secrets нельзя компилировать в firmware;
- новый UI/animation нужно проверять не только в browser emulator, но и по RAM,
  flash, watchdog и реальному SPI display.

## Сборка и проверка

```bash
PLATFORMIO_CORE_DIR=.platformio \
PLATFORMIO_SETTING_ENABLE_TELEMETRY=no \
.venv/bin/pio run -e smalltv -j 1
```

До OTA проверяются JavaScript embedded UI, `node --check emulator/server.mjs`,
mock/live feed, error scenarios, browser layout 240×240 и PlatformIO size report.
