---
title: AI usage meter
description: Antigravity and Codex usage on SmallTV through a trusted local bridge.
---

Open **Display → Mode → AI usage** to show two large meters:

- Antigravity reported usage and reset countdown;
- Codex reported usage and reset countdown.

The ESP does not sign in to Google, OpenAI or Anthropic. It receives only a
small JSON snapshot from a trusted bridge on the same LAN. Provider credentials
must stay on the Mac or server.

## Current API limitation

Google does not currently expose a public personal Antigravity allowance API.
OpenAI likewise does not expose an API for the personal ChatGPT/Codex plan meter
shown in Settings → Usage. Official organization/admin APIs measure API or
enterprise activity and are not interchangeable with those personal limits.

Consequently, the screen and bridge contract are ready, but automatic personal
Antigravity/Codex collection still needs a supported local adapter. Until then,
the emulator serves controllable mock values.

## Bridge contract

Set **AI Usage bridge URL** to a LAN endpoint returning:

```json
{
  "a": 34,
  "ar": 120,
  "c": 57,
  "cr": 360,
  "st": "ok",
  "ok": true
}
```

| Key | Meaning |
| --- | --- |
| `a` | Antigravity usage, 0–100 percent. |
| `ar` | Minutes until the reported Antigravity reset. |
| `c` | Codex usage, 0–100 percent. |
| `cr` | Minutes until the reported Codex reset. |
| `st` | Short bridge status: `ok`, `partial`, `mock` or `error`. |
| `ok` | `false` means the snapshot is unavailable. |

For push mode, leave the URL blank and POST the same JSON to
`http://<device>/api/ai-usage`. The legacy `/api/usage` route and
`s/sr/w/wr` payload remain accepted only for migration.

## Local emulator

The local bridge exposes `http://<mac-ip>:8788/api/ai-usage`. Its default mock
values can be overridden before startup:

```sh
AI_ANTIGRAVITY_PCT=34 \
AI_ANTIGRAVITY_RESET_MIN=120 \
AI_CODEX_PCT=57 \
AI_CODEX_RESET_MIN=360 \
node emulator/server.mjs
```

When data becomes stale or the bridge cannot be reached, the screen switches to
the idle animation instead of presenting old percentages as current.
