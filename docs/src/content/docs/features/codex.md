---
title: Codex usage
description: Codex usage pages, bridge contract, freshness and error behaviour.
---

Codex rotates through an overview, per-model usage and context-window page.
Absent model data is shown as `No model data`; firmware never manufactures
token counts or model names.

Set a trusted-LAN pull URL in the Codex tab, or leave it empty and push
normalized JSON to `POST /api/codex`. Credentials stay on the bridge, not the
device.

On a temporary refresh failure the last valid snapshot remains visible. The
header changes to `WARN`, then `STALE` after the freshness window. With no valid
snapshot, the display and Status tab show a stable error code and concise
remediation such as URL missing, offline, access denied, HTTP failure or invalid
JSON.
