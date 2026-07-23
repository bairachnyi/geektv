# GeekTV Dashboard

Custom open firmware **GeekTV** for the **GeekMagic SmallTV Ultra**. This
fork is based on [giovi321/smalltv-mod](https://github.com/giovi321/smalltv-mod)
and is tailored for the `bairachnyi` device and GitHub projects.

The first custom feature is a 240×240 GitHub status dashboard. Version 0.5.0 shows:

- queued and currently running workflows;
- successful runs and failures;
- repository, workflow and branch;
- up to sixteen active/recent events, two large cards per rotating page;
- projects owned by `bairachnyi` and `ananas-it` by default.
- Actions, deployments/environments, pull-request checks and releases;
- universal `owner/repository` allowlists and per-owner read-only credentials.

Ticker, AI Usage and GitHub are normal display modes and can participate in the
configurable carousel. Plane Radar has been removed.

Important: clocks, weather and the photo gallery from factory firmware V9.0.51
have not been ported yet. Do not flash this development candidate as a complete
replacement for the factory firmware.

## Documentation

- [GitHub GH//STAT screen, tokens, fields and errors](docs/src/content/docs/features/github.md)
- [Every web-interface setting](docs/src/content/docs/reference/settings.md)
- [Firmware architecture and module map](docs/src/content/docs/reference/architecture.md)
- [Device and bridge HTTP API](docs/src/content/docs/reference/http-api.md)

## How the GitHub feed works

The ESP8266 reads a small JSON endpoint from a bridge running on a Mac or
server. The GitHub credential stays on that bridge, avoiding storage of a
powerful token on the device and keeping GitHub API/TLS work away from its
limited RAM.

```text
GitHub Actions -> Mac/server bridge -> /api/github -> SmallTV Ultra
```

The bridge currently supports polling GitHub. Its response format is documented
in [docs/src/content/docs/features/github.md](docs/src/content/docs/features/github.md).
A later version can receive GitHub webhooks for near-instant updates.

## Run the Mac emulator

No dependencies are required beyond Node.js 18 or newer.

```bash
node emulator/server.mjs
```

Open <http://localhost:8788>. The browser displays the exact 240×240 layout and
lets you switch between idle, deploying, failure and mixed test scenarios.

To test real public and private repositories, create a fine-grained GitHub token
with read-only **Actions**, **Deployments**, **Pull requests**, **Checks** and
**Contents** access, then run:

```bash
GITHUB_MODE=live GITHUB_TOKEN=github_pat_xxx node emulator/server.mjs
```

The defaults are `GITHUB_OWNERS=bairachnyi,ananas-it`. Set `GITHUB_REPOS` to a
comma-separated list when you want an explicit allowlist. See
[emulator/README.md](emulator/README.md) for every option.

On the device, open **GitHub**, enter the LAN feed printed by the emulator, for
example `http://192.168.1.246:8788/api/github`, and save. The Mac and SmallTV
must be on the same Wi-Fi network and macOS must allow incoming Node connections.

## Build

Install PlatformIO and build the two ESP8266 images:

```bash
pio run -e smalltv
pio run -e smalltv_loader
```

Outputs:

- `.pio/build/smalltv/firmware.bin` — full firmware;
- `.pio/build/smalltv_loader/firmware.bin` — first-install OTA loader.

GitHub Actions publishes them as `smalltv-ultra-firmware.bin` and
`smalltv-ultra-loader.bin`.

## First installation on SmallTV Ultra

The stock Ultra partition is too small for the full custom image, so the first
installation is deliberately two-step:

1. Back up the stock firmware if recovery matters to you. The newest retained
   vendor package is in `stock-firmware/V9.0.50`.
2. Open `http://<device-ip>/update` and upload `smalltv-ultra-loader.bin`.
3. Join the open Wi-Fi network `SmallTV-Loader` and open
   `http://192.168.4.1/update`.
4. Upload `smalltv-ultra-firmware.bin`.
5. Join `SmallTV-Setup`, configure the 2.4 GHz Wi-Fi network, then use the IP or
   `.local` address shown on the screen.

After that, normal updates are available in the web interface. Do not upload
the full image directly to the original Ultra updater: it normally fails with
`Not Enough Space`.

## Repository status

- Old V9.0.24–V9.0.46 vendor firmware was removed.
- Translated/manual PDF bundles and Chinese material were removed.
- V9.0.50 is retained only as the latest stock recovery reference.
- Current development firmware version is `0.5.0`.

## License and attribution

Firmware code inherits the [WTFPL](LICENSE) license from `smalltv-mod`. GeekMagic
and GitHub are trademarks of their respective owners; this project is not
affiliated with either company.
