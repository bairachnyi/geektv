# SmallTV-Ultra V9.0.51 compatibility audit

This directory documents the factory firmware currently running at
`192.168.1.141`. No write, upload, reset, reboot or OTA endpoint was called
during the audit.

## Source availability

The public GeekMagic repository and its `bairachnyi/smalltv-ultra` fork contain
manuals and compiled firmware only. The V9.0.50 archive contains one application
`.bin` plus its MD5. The device runs V9.0.51, which is not published there.

The binary contains embedded compressed web resources such as
`/settings.html.gz` and `/network.html.gz`, but it does not provide maintainable
firmware source. The custom `src/webui.h` in this worktree is a replacement UI,
not the V9.0.51 UI.

**Do not install the custom firmware on the physical device yet.** OTA replaces
the application and cannot preserve factory clocks, weather, gallery and theme
logic merely by keeping similarly named web pages.

## Factory navigation and themes

Factory navigation:

1. Network
2. Weather
3. Time
4. Pictures
5. Settings

Factory display themes:

1. Weather Clock Today
2. Weather Forecast
3. Photo Album
4. Time Style 1
5. Time Style 2
6. Time Style 3
7. Simple Weather Clock

Auto Switch stores an enable flag for every theme plus a global enable flag and
interval. GitHub must eventually become an eighth theme only if factory firmware
source becomes available or all factory behavior is independently reimplemented
and verified.

## Read-only API map

Pages load configuration through GET-only JSON endpoints:

- Settings: `/v.json`, `/brt.json`, `/delay.json`, `/app.json`,
  `/timebrt.json`, `/theme_list.json`
- Network: `/config.json`, `/delay.json`, `/wifi.json?q=1`
- Weather: `/city.json`, `/w_i.json`, `/unit.json`, `/key.json`
- Time: `/tz.json`, `/hour12.json`, `/ntp.json`, `/day.json`,
  `/timecolor.json`, `/font.json`, `/timebrt.json`, `/colon.json`,
  `/daytimer.json`
- Pictures: `/album.json`, `/space.json`, `/filelist?dir=/image/`
- Weather GIF: `/space.json`, `/filelist?dir=/gif`

Mutating endpoints observed in JavaScript, but not called during the audit:

- `/set?...` for theme, brightness, weather, clock and album settings
- `/wifisave?s=...&p=...`
- `POST /doUpload?dir=/image/` and `POST /doUpload?dir=/gif`
- `/delete?file=...`
- `/set?clear=image` and `/set?clear=gif`
- `/update` for firmware OTA

The factory Pictures page already crops JPG/JPEG to 240×240 in the browser using
Cropper.js before upload. GIF files are uploaded without conversion and are
expected to be prepared at 240×240.

## Safest GitHub prototype

The lowest-risk first physical-device experiment is **not firmware OTA**:

1. The Mac bridge renders the latest six GitHub events into a 240×240 JPG or
   animated GIF.
2. A deliberate upload step places that file in `/image/`.
3. Factory theme 3 (Photo Album) shows it.
4. Existing Auto Switch can include Photo Album alongside weather and clocks.

This preserves factory firmware and its recovery/update page. It is not yet a
true eighth theme: the GitHub image shares Photo Album and changes require a new
upload. Automatic replacement must not be enabled until upload semantics,
filename replacement, flash wear and failure recovery are tested with a
disposable test image.

Create a local four-screen 240×240 GIF without contacting the device:

```bash
node tools/render_github_album.mjs --demo
```

Or render the current bridge feed:

```bash
node tools/render_github_album.mjs http://localhost:8788/api/github
```

The result is written to the ignored
`research/stock-v9.0.51/raw/github-album/` directory. Rendering does not upload
or change anything on the SmallTV.

## Creating a local snapshot

Run:

```bash
node tools/snapshot_stock_web.mjs http://192.168.1.141/
```

The script performs GET requests only. It stores pages, CSS/JS, sanitized JSON
state and file-list HTML under `research/stock-v9.0.51/raw/`. That directory is
ignored by Git because filenames and local state belong to the device owner.
Known Wi-Fi, API-key and location fields are redacted before being written.

The snapshot is reference material, not permission to redistribute GeekMagic or
third-party web assets.

## OTA safety gate

There is currently no defensible "small binary patch." The OTA page writes a
complete ESP8266 application image. The published V9.0.50 image is about 505 KB,
the device runs unpublished V9.0.51, and the current custom application is a
different implementation with different settings and display modes.

Do not perform firmware OTA until all of these are true:

1. An exact V9.0.51 recovery image or a verified full 4 MB flash backup exists.
2. Serial/USB recovery has been tested far enough to prove that the backup can
   be restored if Wi-Fi and the web updater disappear.
3. Factory source is available, or the replacement firmware independently
   implements and tests every required factory theme and setting.
4. The candidate image is tested on a spare device or equivalent hardware
   before the primary unit.

Re-uploading V9.0.50 would be a downgrade, not a safe no-op. Uploading the
current custom `.bin` would replace V9.0.51 rather than extend it.

## Physical-device experiment log

### 2026-07-23 — Photo Album GIF

- Uploaded one new file: `/image/github-status.gif`
- Format: animated GIF, 240×240, two five-second pages, three events per page
- Size reported by the device: 37 KB
- SHA-256 verified after downloading it back from the device:
  `1db3848ac731158959eecf46a9e4b09046a22ce05a3a6bf680ccef51bfd32957`
- Device response: HTTP 200
- Existing files were not overwritten or deleted
- Current theme remained `1`
- Auto Switch remained enabled at 60 seconds
- Photo Album autoplay remained enabled at 5 seconds
- Firmware and OTA were not touched

### 2026-07-23 — Larger two-card GitHub album

- Deleted the earlier `/image/github-status.gif` and uploaded the redesigned
  file under the same name after explicit user approval.
- Format: animated GIF, 240×240, four screens, two large events per screen.
- Active builds and PR checks appear first in a `FOCUS` screen; completed history
  follows in the demonstration only to show the post-completion state.
- Long repository, workflow and branch values move as a marquee.
- A double cyan frame marks the newest event.
- Device-reported size: 121 KB; exact downloaded size: 124,834 bytes.
- Local and downloaded SHA-256:
  `739cc8ed1c352e1af1a06582581b1e36932d14c801d2cfaccfa7cd8d2f2bfdbf`.
- Firmware, OTA, settings and all unrelated gallery files were not touched.
