# GitHub dashboard emulator

Run with the Node.js version bundled with Codex or any Node.js 18+ runtime:

```bash
node emulator/server.mjs
```

Open both local pages:

- `http://localhost:8788` — the 240×240 device screen and GitHub bridge controls.
- `http://localhost:8789/settings.html` — the real settings HTML embedded in the firmware.

The settings page also includes a local-only virtual SmallTV. It reacts immediately
to the selected display mode, carousel membership and carousel interval, animates
the transitions between template screens, and marks the form as `UNSAVED` until
the local settings API confirms a save. Carousel demos use a four-second interval
so changes are easy to inspect; the caption shows the real interval that the
physical device would use. Previous/next and pause controls make each selected
screen inspectable without waiting.

The GitHub screen inside the settings preview is not a separate mockup: it embeds
the same 240×240 GH//STAT renderer used by port 8788. Cards, colors, FOCUS
priority, running timers, spinners, status icons, pagination and marquees
therefore stay visually identical on both pages. Ticker and AI Usage use the
same dark operations-console palette and card geometry.

The virtual device is injected only by `emulator/server.mjs`. It is not embedded
in the ESP8266 firmware, does not use its flash or RAM, and cannot update the
physical SmallTV.

Save settings on the second page and the device preview will reload them within
about one second. This includes the selected screen mode, GitHub feed URL,
polling interval and page rotation interval. The settings are stored only in
`emulator/device-config.local.json`, which is excluded from Git.

The diagnostics panel on the emulator page records configuration changes,
GitHub/API failures, invalid feed responses and simulated device actions. It
keeps the latest 250 entries in memory and deliberately excludes passwords and
tokens. The **Clear logs** button resets it.

The bridge also exposes a mock AI Usage feed at
`http://<Mac-LAN-IP>:8788/api/ai-usage`. Override its values with
`AI_ANTIGRAVITY_PCT`, `AI_ANTIGRAVITY_RESET_MIN`, `AI_CODEX_PCT`, and
`AI_CODEX_RESET_MIN`. This tests the complete device contract without pretending
that personal Antigravity or Codex plan quotas have public provider APIs.

The first page lets
you switch between idle, deploying, failure, mixed and structured error scenarios. It also edits
the bridge accounts, per-owner tokens, repository allowlist, event filters and
mock/live mode. The preview rotates through the same two-card pages as the
physical 240×240 display and reproduces active/waiting spinners, static success/failure states,
live elapsed timer, event date/time and pagination. When any event is running or
queued, only active events are paginated and the header shows `FOCUS`; completed
history returns after all active work finishes. The newest event has a cyan frame.
Configure the SmallTV GitHub feed with the LAN URL printed by the server.

For live public data from `bairachnyi` and `ananas-it`:

```bash
GITHUB_MODE=live node emulator/server.mjs
```

For private repositories, use a fine-grained read-only token kept on the Mac:

```bash
GITHUB_MODE=live GITHUB_TOKEN=github_pat_xxx node emulator/server.mjs
```

Grant only Actions, Deployments, Pull requests, Checks and Contents repository
permissions at read level. In the web settings, tokens are configured per owner
so private personal and organization repositories can use separate credentials.

Optional variables: `GITHUB_OWNERS`, `GITHUB_REPOS`, `DEVICE_TOKEN`, `PORT`,
`SETTINGS_PORT`, `CACHE_SEC` (120 by default, minimum 60),
`GITHUB_DELIVERY=webhook`, `GITHUB_WEBHOOK_URL`, and `GITHUB_WEBHOOK_SECRET`.
Never place a GitHub token in the SmallTV settings; `DEVICE_TOKEN` is a separate
low-privilege secret used only between the display and this bridge.

The display may request its local feed every 10 seconds. This does not mean the
bridge requests GitHub every 10 seconds: responses are cached, simultaneous
readers share one refresh, and the effective GitHub interval is automatically
raised according to the repository/event request estimate. Prefer an explicit
repository allowlist. Private organization repositories require an approved
fine-grained token for that organization.

Bridge settings changed in the emulator or the device's GitHub tab are stored in
`emulator/config.local.json` with owner-only file permissions. Both local settings files are
ignored by Git and may contain GitHub tokens. Blank token fields keep the
previous values, so saved credentials are never returned to the browser.

Webhook mode receives near-real-time `workflow_run`, `deployment`,
`deployment_status`, `pull_request`, `check_suite`, and `release` deliveries at
`POST /api/github/webhook`. Every request requires a valid
`X-Hub-Signature-256`; duplicate `X-GitHub-Delivery` values are ignored.
Normalized state is stored in ignored `emulator/github-events.local.json`.
A permanent public HTTPS tunnel or relay must forward the original body and
headers to this local endpoint. A GitHub App installed for all repositories is
recommended over configuring each repository separately.

Firmware upload and self-update actions are intentionally simulated locally.
They never write to the physical SmallTV and never produce an OTA image.
