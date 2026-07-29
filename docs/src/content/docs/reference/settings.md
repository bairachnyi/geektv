---
title: Все настройки устройства
description: Полный справочник вкладок, полей, кнопок и ограничений веб-интерфейса SmallTV Ultra.
---

Веб-интерфейс доступен по IP устройства, например
`http://192.168.1.50/` (вымышленный адрес), или по mDNS-имени
`http://<hostname>.local`.
Основная кнопка **Save settings** сохраняет все обычные вкладки. Некоторые
действия — Wi-Fi, bridge, OTA и import — имеют отдельные кнопки.

## Status

### Device

Показывает версию firmware, режим сети (`STA` или setup `AP`), SSID, IP, RSSI,
uptime, причину последнего reset, свободный heap, крупнейший непрерывный блок
памяти и запас stack. Эти значения нужны для диагностики сетевых проблем, TLS и
перезагрузок.

Также отображаются состояние NTP, выбранный timezone и активность night mode.

### Tickers

Показывает состояние каждого ticker: symbol, наличие ошибки, цену и рассчитанный
процент изменения. **Refresh data now** сбрасывает таймеры опроса и просит
активные источники обновиться при следующем цикле.

## WiFi

### Saved networks

| Элемент | Описание |
| --- | --- |
| Scan networks | Сканирует видимые сети 2.4 GHz и показывает до 25 результатов. |
| SSID | Имя сохраняемой Wi-Fi сети. |
| Password | Пароль сети. Пустое поле сохраняет старый пароль. |
| `+ Add network` | Добавляет строку. Максимум четыре сети. |
| `×` | Удаляет строку из будущей конфигурации. |
| Save & connect | Сохраняет список и перезагружает устройство. |

При загрузке SmallTV выбирает наиболее сильную видимую сохранённую сеть. Hidden
SSID пробуются после видимых. Поддерживается только 2.4 GHz.

### Device name

**Hostname** — сетевое имя mDNS. Например, `smalltv-desk` открывается как
`http://smalltv-desk.local`. Несколько устройств должны иметь разные hostname.
Изменение требует reboot.

### Setup hotspot (AP)

- **AP name** — SSID fallback/setup точки доступа.
- **AP password** — пустое значение оставляет AP открытой; непустой WPA2-пароль
  должен содержать минимум восемь символов.

AP включается, если Wi-Fi не настроен или подключение к сохранённым сетям не
удалось. В AP mode любой неизвестный URL перенаправляется на captive portal.

## Display

### Mode

**What this device shows** выбирает единственный активный экран:

- Stock / crypto ticker;
- Clock — большие часы;
- Clock + weather — часы и текущая погода;
- Weather — прогноз на три дня;
- Gallery;
- Codex usage;
- GitHub deploys;
- Carousel.

Для Carousel:

- **Switch mode every** — 5–3600 секунд на feature;
- checkbox каждого feature определяет, участвует ли он в ротации.

### Screen

| Поле | Диапазон/значение |
| --- | --- |
| Brightness | 0–100%. |
| Auto-brightness | Использует датчик освещённости на A0, если он присутствует на плате. |
| Orientation | 0°, 90°, 180° или 270°. Применяется без reboot. |
| Backlight is active-low | Инвертирует PWM подсветки. Использовать, если яркость работает наоборот или экран тёмный. |

Приоритет яркости: night mode → auto-brightness → ручное значение.

### Clock & night mode

| Поле | Описание |
| --- | --- |
| Timezone | IANA timezone, преобразованный UI в POSIX TZ rule для firmware. |
| Clock | Текущее время устройства после NTP sync. |
| Night mode | Включает ночное расписание. |
| From / To | Начало и конец интервала в локальном timezone; переход через полночь поддерживается. |
| Night brightness | 0–100%; `0` полностью выключает подсветку. |

Night mode начинает затемнять экран только после доверенного NTP sync. DST
учитывается правилом timezone. После reboot нормальная яркость может сохраняться
несколько секунд до синхронизации.

## Ticker

### Rotation & data

| Поле | Описание |
| --- | --- |
| Show each ticker | 2–300 секунд показа одного symbol. |
| Refresh data | Минимум 10 секунд между запросами источника. |
| Chart timeframe | `1d`, `5d`, `1mo`, `3mo`, `6mo`, `ytd`, `1y`, `2y`, `5y`, `max`. |
| Chart points | 0–60 точек; меньше двух означает отсутствие полноценного sparkline. |
| Change & % basis | Изменение за выбранный chart timeframe либо классическое дневное изменение. |
| Webhook URL | Общий URL для symbol со source `Webhook`. |

### Color scheme

- **Green up / Red down** — классическая схема;
- **Red up / Green down** — инвертированная региональная схема.

### What to show

Checkbox отдельно включает name/symbol, price, absolute и percent change,
sparkline, timeframe label, возраст обновления, page dots и position P/L с
portfolio summary.

### Tickers

Каждая строка содержит:

| Колонка | Описание |
| --- | --- |
| Symbol | Yahoo symbol, cash.ch listing key, GitHub quote symbol или webhook identifier. До 23 символов. |
| Name | Необязательное экранное имя, до 19 символов. |
| Source | Yahoo Finance, cash.ch, GitHub static JSON или custom webhook. |
| Qty | Количество единиц позиции; `0` отключает position calculation. |
| Cost | Себестоимость одной единицы. Вместе с Qty включает P/L. |

Максимум восемь tickers. **cash.ch symbol finder** принимает URL, ISIN, valor
или текстовое имя и добавляет выбранный listing key.

## Clock & Weather

Вкладка содержит три экрана, и те же три варианта доступны в Display/Carousel:

1. **Большие часы** — время, текущая дата, день недели и IP внизу.
2. **Часы + погода** — время, IP, location, текущая температура, влажность,
   weather mark, дата и день недели.
3. **Прогноз** — три секции: сегодня, завтра и послезавтра.

**Location** передаётся в `wttr.in`; API key не нужен. **Timezone** задаёт
локальное время и дату. Виртуальный дисплей использует те же настройки и близкую
палитру, но не гарантирует идентичную растеризацию bitmap-шрифтов на физической
панели. Weather refresh ограничен минимумом в 60 секунд, чтобы не создавать
лишний сетевой трафик.

## Gallery

- **Show each image** — длительность JPEG или одного цикла показа GIF;
- **Fit mode** — crop/contain для JPEG;
- upload принимает JPEG и анимированный GIF;
- рекомендуемый размер — 240×240, максимальный файл — 600 KB.

Файлы хранятся в LittleFS. GIF декодируется кадр за кадром непосредственно на
дисплей, поэтому не требует буфера на весь animation. Неподдерживаемый формат,
переполнение flash или слишком большой файл возвращают понятную ошибку upload.

## Codex

- **Codex bridge URL** — HTTP endpoint в доверенной локальной сети;
- **Refresh** — период pull-запроса;
- если URL пуст, bridge может отправлять данные в `POST /api/codex`.

Экран показывает до трёх страниц: overview, модели и context window. При
временной ошибке последние корректные данные сохраняются и помечаются
`WARN`/`STALE`; выдуманные model/token значения не подставляются. Текст ошибки и
код доступны на вкладке Status.

## Покрытие factory-функций

В firmware 0.8.3 реализованы часы, текущая погода, прогноз на три дня, JPEG/GIF
gallery, GitHub и Codex. Plane Radar и прежний общий AI Usage удалены. Это
самостоятельная replacement firmware, поэтому перед OTA всё равно необходимо
проверить конкретную ревизию дисплея, rotation, backlight polarity и свободную
память на физическом SmallTV-Ultra.

## GitHub

Полное объяснение `GH//STAT`, токенов, каждого поля и ошибок находится в разделе
[GitHub GH//STAT](/geektv/features/github/).

Кратко: ESP8266 хранит feed URL, poll/rotation interval и необязательный device
token. GitHub owners, repositories и fine-grained PAT настраиваются браузером
на bridge и в firmware не попадают.

## Update

### Update from GitHub

- **Installed** — текущая `FW_VERSION`;
- **Check for latest** — читает последнюю release вашего repository;
- **Update now** — начинает self-update. На ESP8266 обновление ставится в очередь,
  устройство reboot и загружает firmware на ранней стадии boot, когда доступно
  больше contiguous heap.

### Manual update (OTA)

Выберите `.bin` и нажмите **Upload & flash**. После успешной записи устройство
перезагрузится. Используйте только firmware для правильной модели/chip/partition
layout.

### Settings backup

- **Export settings** скачивает реальный `/config.json`, включая Wi-Fi passwords.
- **Import & reboot** проверяет JSON, записывает его и перезагружает устройство.

Backup является секретным файлом и не должен попадать в Git или публичные cloud
storage.

### Maintenance

- **Reboot** — обычная перезагрузка без изменения настроек;
- **Factory reset** — удаляет `/config.json`, восстанавливает defaults и reboot.

## Сохранение и применение

Обычная кнопка **Save settings** отправляет частичный JSON в `POST /api/config`.
Brightness, rotation, mode и feature settings применяются сразу. Изменение Wi-Fi
или hostname планирует reboot. Пустые password/token inputs обычно означают
«сохранить прежний секрет», а не «стереть».
