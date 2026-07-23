---
title: HTTP API
description: Device and GitHub bridge HTTP endpoints, payloads, secrets and status codes.
---

SmallTV device server работает на port 80. GitHub bridge по умолчанию работает
на port 8788. Оба API предназначены прежде всего для локальной сети.

## Безопасность

Device API сейчас не имеет login/password. Любой клиент в той же сети может
читать status, изменять settings, reboot или начать OTA. Не публикуйте IP/port
устройства в интернет и используйте изолированную доверенную LAN/VLAN.

Bridge API можно защитить переменной окружения `DEVICE_TOKEN`. Тогда все
`/api/*` запросы должны содержать:

```http
X-Device-Token: <shared secret>
```

GitHub fine-grained PAT никогда не возвращается API и не передаётся устройству.

## Device API

### `GET /`

Возвращает embedded settings HTML из `src/webui.h` с `Cache-Control: no-cache`.

### `GET /api/config`

Возвращает settings с masked secrets, feature flags и chip type.

Особенности:

- Wi-Fi passwords не возвращаются; вместо них UI получает признак сохранённого
  значения;
- GitHub device token не возвращается, только `tokenSet`;
- `features` сообщает, какие tabs скомпилированы;
- `chip`: `esp8266`, `esp32c2` или `esp32`.

### `POST /api/config`

Принимает JSON object. Update частичный: можно отправить только изменяемый slice.

```json
{
  "mode": "github",
  "brightness": 80,
  "github": {
    "statusUrl": "http://192.168.1.139:8788/api/github",
    "pollSec": 15,
    "rotateSec": 8
  }
}
```

Ответ:

```json
{ "ok": true, "reboot": false }
```

`reboot:true` означает изменение Wi-Fi list или hostname. Device планирует
reboot примерно через 800 ms. Ошибки body: `400 no body` или `400 bad json`.

Чтобы заменить сохранённый GitHub device token, передайте непустой
`github.accessToken`; пустая строка сохраняет старое значение. Для удаления
используется `github.clearToken:true`.

### `GET /api/status`

Основные keys:

| Key | Значение |
| --- | --- |
| `fw`, `version`, `repo` | Firmware identity. |
| `mode` | Network mode: `sta` или `ap`. |
| `connected`, `ssid`, `ip`, `rssi` | Network status. |
| `heap`, `maxblk`, `contstk` | Свободная память, contiguous block и stack headroom. |
| `uptime` | Секунды с boot. |
| `reset` | Причина последнего reset и crash addresses, если есть. |
| `synced`, `time`, `tz` | NTP/timezone status. |
| `night`, `nightHeld`, `clockFresh` | Night state machine. |
| `tickers` | Symbol status/price/change summary, если ticker feature включён. |
| `updateMsg` | Последний self-update result/error, если есть. |

### `GET /api/scan`

Блокирующе сканирует Wi-Fi и возвращает до 25 entries:

```json
[{ "ssid": "Home", "rssi": -48, "enc": true }]
```

### `POST /api/refresh`

Возвращает `{"ok":true}` и принудительно разрешает следующий ticker/GitHub fetch.

### `POST /api/reboot`

Возвращает `{"ok":true}` и reboot примерно через 400 ms.

### `POST /api/factory`

Сбрасывает settings к defaults, сохраняет новый `/config.json` и reboot.

### `GET /api/export`

Скачивает `smalltv-config.json` с реальными secrets. `404 no config saved yet`,
если settings ещё не сохранялись.

### `POST /api/import`

Принимает полный JSON backup как raw body, применяет, сохраняет и reboot.

```json
{ "ok": true, "reboot": true }
```

### `GET /api/checkupdate`

```json
{
  "current": "0.5.0",
  "ok": true,
  "latest": "v0.5.0",
  "newer": false
}
```

При неудаче содержит `error`.

### `POST /api/selfupdate`

Возвращает ответ немедленно и ставит update в основной loop. На ESP8266 далее
следует update-at-boot flow.

### `POST /api/ai-usage`

Предпочтительный push endpoint trusted AI Usage bridge. Принимает compact
contract `{a,ar,c,cr,st,ok}`, где `a/c` — Antigravity/Codex percentages, а
`ar/cr` — reset minutes. Возвращает `200 {"ok":true}` либо `400 {"ok":false}`.

`POST /api/usage` и legacy keys `{s,sr,w,wr}` временно сохранены для миграции.

### `POST /update`

Multipart OTA upload endpoint. Browser отправляет `.bin` chunk-by-chunk.
Успех: `200 OK` и reboot; ошибка: `500` с platform update message.

### Captive portal endpoints

`/generate_204`, `/gen_204`, `/hotspot-detect.html`, `/connecttest.txt` и любой
unknown path в AP mode перенаправляются на `http://192.168.4.1`.

## GitHub bridge API

### `GET /api/github`

Возвращает нормализованную ленту v0.3. Детальный schema описан в
[GitHub GH//STAT](/smalltv-ultra/features/github/#формат-ленты-v03).

### `GET /api/config`

Возвращает публичную bridge configuration:

```json
{
  "ok": true,
  "mode": "mock",
  "delivery": "webhook",
  "cacheSec": 120,
  "accounts": [
    { "owner": "bairachnyi", "tokenSet": true }
  ],
  "repositories": ["bairachnyi/smalltv-ultra"],
  "events": {
    "actions": true,
    "deployments": true,
    "pullRequests": true,
    "releases": true
  },
  "polling": {
    "repoCount": 1,
    "requestsPerRefresh": 7,
    "recommendedCacheSec": 60,
    "effectiveCacheSec": 120
  },
  "rateLimit": {
    "limit": 5000,
    "remaining": 4920,
    "resetAt": 1784790000000,
    "blocked": false,
    "blockedUntil": 0
  },
  "webhook": {
    "publicUrl": "https://github-smalltv.example.com/api/github/webhook",
    "endpoint": "/api/github/webhook",
    "secretSet": true,
    "received": 18,
    "tracked": 7,
    "lastDeliveryAt": 1784790000000
  }
}
```

PAT не возвращается.

### `POST /api/config`

Принимает полный bridge config. В отличие от device partial update, здесь
validation строгая: mode, 1–6 owners, token prefix, до 50 repositories, хотя бы
один event и cache 60–3600. Поле `polling.effectiveCacheSec` в ответе показывает
фактический безопасный интервал с учётом объёма запросов.

Пустой token/secret сохраняет старое значение. `clearToken:true` и
`webhook.clearSecret:true` удаляют соответствующий secret.

### `POST /api/github/webhook`

Публичный endpoint для GitHub App или organization webhook. Требует:

- raw JSON body до 2 MB;
- `X-GitHub-Event`;
- уникальный `X-GitHub-Delivery`;
- корректный `X-Hub-Signature-256`, рассчитанный сохранённым webhook secret.

Поддерживаются `workflow_run`, `deployment`, `deployment_status`,
`pull_request`, `check_suite`, `release` и `ping`. Успешный приём возвращает
`202 Accepted`. Повтор delivery ID также возвращает 202, но состояние повторно
не изменяет.

### `POST /api/scenario`

Только emulator/mock control:

```json
{ "scenario": "mixed" }
```

Допустимы `idle`, `deploying`, `failure`, `mixed`, `token_error`, `repo_error`,
`bridge_offline`, `stale_data`.

## Структурированная ошибка bridge

```json
{
  "ok": false,
  "error": {
    "code": "INVALID_REPOSITORY",
    "message": "Repository row 1: use owner/repository...",
    "field": "repositories",
    "repo": "",
    "source": ""
  }
}
```

`field` помогает UI выделить неверное поле, `repo` указывает проблемный
repository, `source` различает config/GitHub/network/bridge/device.

## CORS

Bridge отвечает `Access-Control-Allow-Origin: *` и разрешает `Content-Type` и
`X-Device-Token`, чтобы settings page устройства могла напрямую настраивать
bridge на другом LAN origin. Device API CORS не включает.
