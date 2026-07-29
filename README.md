<p align="center">
  <img src="docs/src/assets/logo.svg" width="112" alt="GeekTV logo">
</p>

<h1 align="center">GeekTV</h1>

<p align="center">
  Open firmware for GeekMagic SmallTV — clocks, weather, gallery, market
  tickers, GitHub CI/CD status, Codex usage, web settings and OTA updates.
</p>

<p align="center">
  <a href="https://github.com/bairachnyi/geektv/actions/workflows/build.yml"><img alt="Build firmware" src="https://github.com/bairachnyi/geektv/actions/workflows/build.yml/badge.svg"></a>
  <a href="LICENSE"><img alt="License: MIT" src="https://img.shields.io/badge/License-MIT-blue.svg"></a>
  <img alt="Display: 240×240" src="https://img.shields.io/badge/display-240%C3%97240-00bcd4">
  <img alt="ESP8266 and ESP32" src="https://img.shields.io/badge/chips-ESP8266%20%7C%20ESP32-6c63ff">
</p>

GeekTV is free, open-source firmware for 240×240 GeekMagic SmallTV-style
displays. It turns the device into a configurable desk dashboard with clocks,
weather, a photo/GIF gallery, stock and crypto prices, Codex usage, and a
GitHub CI/CD status screen. Configuration, image upload, diagnostics, backup,
and OTA updates are available from the built-in web interface.

> **Project status:** actively developed. The current firmware line is `0.8.x`.
> Always match the binary to the device chip before flashing.

## Start here

| I want to… | Go to |
| --- | --- |
| Download firmware | [Releases](https://github.com/bairachnyi/geektv/releases) or the latest [build artifact](https://github.com/bairachnyi/geektv/actions/workflows/build.yml) |
| Install it safely | [Flashing guide](docs/src/content/docs/getting-started/flashing.md) |
| Configure the device | [First setup](docs/src/content/docs/getting-started/setup.md) and [all settings](docs/src/content/docs/reference/settings.md) |
| Test without hardware | [Local emulator](#local-emulator) |
| Track GitHub projects | [GH//STAT guide](docs/src/content/docs/features/github.md) |
| Report a problem or contribute | [Contributing guide](CONTRIBUTING.md) |

## Features

- Two clock layouts plus a three-section forecast for today and the next two days.
- Current location, temperature, humidity, date, weekday, and device IP.
- JPEG, RGB565 RAW, and animated GIF gallery stored in LittleFS.
- Stock and crypto ticker with sparklines, rotation, optional holdings, Yahoo,
  cash.ch, static GitHub JSON, or a custom webhook source.
- `GH//STAT` dashboard for GitHub Actions, deployments/environments,
  pull-request checks, and releases.
- Codex quota and token-usage pages supplied by a trusted local bridge.
- Carousel mode with selectable screens, interval, brightness, orientation,
  night schedule, and optional light-sensor control.
- Browser-based settings, diagnostics, Wi-Fi management, configuration
  import/export, manual OTA, and release self-update.
- Local macOS/Linux/Windows emulator for testing the screen and settings before
  flashing physical hardware.

Plane Radar and the former generic AI Usage screen are intentionally not part
of GeekTV.

## Supported hardware

| Build environment | Hardware | Chip | First installation |
| --- | --- | --- | --- |
| `smalltv` | GeekMagic SmallTV / SmallTV Ultra | ESP8266 / ESP-12F | Web OTA; some stock layouts need the small loader first |
| `smalltv_c2` | SmallTV clone with CH340C | ESP32-C2 / ESP8684 | USB-C with `esptool` |
| `smalltv_esp32` | NMMiner NM-TV-154 | ESP32-WROOM-32E | USB with `esptool` |

All targets use a 1.54-inch 240×240 ST7789 IPS panel. Confirm the chip and board
revision before flashing: images are not interchangeable.

## Download

Download a board-specific image from
[GitHub Releases](https://github.com/bairachnyi/geektv/releases) or from the
artifact attached to the latest successful
[build workflow](https://github.com/bairachnyi/geektv/actions/workflows/build.yml).

Keep a recovery image and export your settings before the first custom flash.
Factory firmware, pin mappings, partition layouts, and backlight polarity vary
between revisions.

## Build from source

Install [PlatformIO](https://platformio.org/), clone the repository, and run:

```bash
git clone https://github.com/bairachnyi/geektv.git
cd geektv
pio run -e smalltv
```

Other targets:

```bash
pio run -e smalltv_c2
pio run -e smalltv_esp32
pio run -e smalltv_loader
```

The application image is written to `.pio/build/<environment>/firmware.bin`.
ESP32 targets also produce the factory image described in the
[building guide](docs/src/content/docs/reference/building.md).

## Flash and first setup

### ESP8266 web OTA

1. Export the current configuration and keep a recovery firmware image.
2. Open `http://<device-ip>/update`.
3. Upload `.pio/build/smalltv/firmware.bin`.
4. If the stock updater reports insufficient space, install
   `.pio/build/smalltv_loader/firmware.bin`, join `SmallTV-Loader`, open
   `http://192.168.4.1/update`, and upload the application image.
5. Join `SmallTV-Setup` if shown, open `http://192.168.4.1`, and save a
   2.4 GHz Wi-Fi network.

### ESP32-C2 and ESP32

Use the board-specific factory image and the command from
[Flashing](docs/src/content/docs/getting-started/flashing.md). Never write an
ESP8266 binary to an ESP32 target or vice versa.

After connection, open `http://<device-ip>/` or
`http://<hostname>.local`. The display shows the assigned IP during startup.

## Web-interface tabs

| Tab | Purpose |
| --- | --- |
| Status | Firmware/network state, RSSI, uptime, reset reason, heap, stack, NTP, ticker and integration errors |
| WiFi | Scan and save up to four 2.4 GHz networks, hostname, setup AP, and admin password |
| Display | Active mode, carousel screens/timing, brightness, rotation, backlight, timezone, and night mode |
| Clock & Weather | Clock layout, seconds/date, colors, location, units, refresh interval, and preview |
| Gallery | Rotation, image list/delete, upload, browser-side crop/resize, JPEG/GIF conversion |
| Ticker | Symbols, sources, chart range, refresh/rotation, colors, visible fields, and holdings |
| GitHub | Device feed, bridge settings, owners, repositories, read-only credentials, webhooks, events, and errors |
| Codex | Trusted bridge/push endpoint, refresh/rotation, and current quota/usage status |
| Update | Release check, manual OTA, settings import/export, reboot, and factory reset |

The complete field reference is in
[All settings](docs/src/content/docs/reference/settings.md).

## Local emulator

Node.js 18 or newer is sufficient:

```bash
node emulator/server.mjs
```

Open:

- `http://localhost:8788` — 240×240 live screen;
- `http://localhost:8789/settings.html` — settings and virtual device.

Mock GitHub events work without credentials. Local bridge configuration is
stored in ignored `*.local.json` files. Example names such as
`octo-user/demo-dashboard` and `acme-labs/api-service` are fictional.

## GitHub private repositories

The ESP device reads compact JSON from the local bridge; it does not need a
GitHub credential. For REST polling, create a fine-grained token restricted to
selected repositories and read-only permissions for Actions, Deployments,
Pull requests, Checks, Contents, and Releases. For near-real-time updates, use
signed GitHub webhooks. Never commit tokens, webhook secrets, exported device
settings, or private repository names.

See [GitHub GH//STAT](docs/src/content/docs/features/github.md) and the
[bridge README](emulator/README.md).

## Data sources and limitations

- Weather uses `wttr.in`; availability and accuracy depend on that service.
- Yahoo Finance and cash.ch endpoints are third-party and may change or
  rate-limit requests.
- GitHub REST is rate-limited; webhooks are preferred for active repositories.
- Codex does not expose a universal device API; GeekTV expects a trusted local
  JSON bridge or pushed aggregate data.
- ESP8266 RAM is limited. Keep payloads, images, TLS work, and refresh rates
  bounded. The firmware reports heap and largest free block on Status.
- The browser preview reproduces the 240×240 layout but cannot perfectly match
  every physical panel's rasterization, color response, or backlight.

## Documentation

- [Hardware](docs/src/content/docs/getting-started/hardware.md)
- [Flashing](docs/src/content/docs/getting-started/flashing.md)
- [First setup](docs/src/content/docs/getting-started/setup.md)
- [Clock and weather](docs/src/content/docs/features/clock-weather.md)
- [Gallery](docs/src/content/docs/features/gallery.md)
- [Ticker](docs/src/content/docs/features/ticker.md)
- [GitHub GH//STAT](docs/src/content/docs/features/github.md)
- [Codex](docs/src/content/docs/features/codex.md)
- [Architecture](docs/src/content/docs/reference/architecture.md)
- [HTTP API](docs/src/content/docs/reference/http-api.md)
- [Recovery](docs/src/content/docs/reference/recovery.md)

## Security and privacy

Use the device only on a trusted LAN. Change the default admin password, limit
GitHub tokens to read-only access, use a strong webhook secret, and protect
configuration exports because they contain Wi-Fi passwords. See
[SECURITY.md](SECURITY.md).

## Contributing

Bug reports, hardware validation, documentation, and focused pull requests are
welcome. See [CONTRIBUTING.md](CONTRIBUTING.md). Do not attach private tokens,
Wi-Fi credentials, personal photos, configuration exports, or proprietary
firmware to issues.

## License and attribution

GeekTV is available under the [MIT License](LICENSE). It is derived in part from
[`giovi321/smalltv-mod`](https://github.com/giovi321/smalltv-mod); see
[NOTICE.md](NOTICE.md). GeekMagic, GitHub, Codex, Yahoo, and other product names
are trademarks of their respective owners. This community project is not
affiliated with or endorsed by those companies.
