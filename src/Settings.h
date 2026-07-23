// Settings.h — persisted configuration (LittleFS /config.json)
//
// Layout is segmented per feature: shared device/network fields live at the top
// level, and each feature owns a nested settings slice (ticker / usage / GitHub).
// config.json mirrors this: { ..shared.., "ticker":{...}, "usage":{...}, ... }.
// The JSON reader also still accepts the old flat layout, so a device upgrading
// from the pre-segmentation firmware keeps its WiFi + symbols; the next save
// rewrites it nested.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

struct SymbolCfg {
  char    symbol[MAX_SYMBOL_LEN];
  char    name[MAX_NAME_LEN];
  uint8_t source;     // SRC_* per ticker (see config.h)
  float   qty;        // position size; 0 = not a position
  float   cost;       // cost basis per unit, in the instrument's currency
};

// One saved WiFi station network. The device keeps up to MAX_WIFI_NETS and
// joins the strongest visible one at boot (hidden SSIDs are tried last).
struct WifiCred {
  String ssid;
  String pass;
};

// ---- Ticker (stock/crypto) feature slice ----------------------------------
// The data source is per symbol (SymbolCfg.source); webhookUrl is shared by
// every symbol whose source is SRC_WEBHOOK.
struct TickerSettings {
  String   webhookUrl;    // custom webhook base URL (used by webhook symbols)
  String   range;         // chart timeframe token (e.g. "1d", "5d", "1mo", "1y")
  uint16_t points;        // sparkline points requested
  uint16_t pollSec;       // refresh period
  uint16_t rotateSec;     // per-symbol on-screen time
  bool     colorInverted; // false: up=green/down=red ; true: swapped
  bool     changeOnRange; // true: change/% over the chart timeframe; false: provider's 1-day change

  // What to show
  bool showName;
  bool showPrice;
  bool showChange;
  bool showChart;
  bool showRangeLabel;
  bool showUpdatedAgo;
  bool showPageDots;
  bool showPortfolio;   // P/L line on position tickers + portfolio summary page

  SymbolCfg symbols[MAX_SYMBOLS];
  uint8_t   symbolCount;

  void setDefaults();
  void toJson(JsonObject o) const;
  void fromJson(JsonObjectConst o);   // applies only the keys present
};

// ---- AI usage feature slice ------------------------------------------------
struct UsageSettings {
  String   usageUrl;      // trusted LAN bridge endpoint; legacy key retained
  uint16_t pollSec;       // refresh period

  void setDefaults();
  void toJson(JsonObject o) const;
  void fromJson(JsonObjectConst o);
};

// ---- GitHub Actions dashboard slice --------------------------------------
// The device reads a compact JSON feed produced by the local emulator or a
// trusted bridge. The bridge owns GitHub credentials; the ESP only receives a
// narrow device token which is sent as X-Device-Token.
struct GithubSettings {
  String   statusUrl;
  String   accessToken;
  uint16_t pollSec;
  uint16_t rotateSec;

  void setDefaults();
  void toJson(JsonObject o, bool includeSecrets) const;
  void fromJson(JsonObjectConst o);
};

// ---- Photo gallery feature slice ------------------------------------------
struct GallerySettings {
  uint16_t rotateSec;     // per-image dwell time
  bool     randomOrder;   // shuffle images

  void setDefaults();
  void toJson(JsonObject o) const;
  void fromJson(JsonObjectConst o);
};

// ---- Clock / night mode / weather slice (device-wide) --------------------
struct ClockSettings {
  String   tz;            // IANA display name, e.g. "Europe/Rome" (UI round-trip)
  String   tzPosix;       // POSIX TZ rule the device feeds SNTP
  bool     nightEnabled;  // dim/blank on a nightly schedule
  uint16_t nightStartMin; // minutes since local midnight (0..1439)
  uint16_t nightEndMin;
  uint8_t  nightLevel;    // 0..100, 0 = backlight off

  // Display & Weather options
  bool     format24h;     // true: 24h format, false: 12h format
  bool     showSeconds;
  bool     showDate;
  uint8_t  theme;         // 0 = Digital, 1 = Minimal/Simple Weather Clock, 2 = Analog
  String   weatherCity;
  String   weatherApiKey;
  String   weatherUnits;  // "c" or "f"
  uint16_t weatherPollSec;

  // Colors (RGB565)
  uint16_t timeColor;     // color for time digits
  uint16_t dateColor;     // color for date text
  uint16_t accentColor;   // accent / secondary color
  uint16_t bgColor;       // background tint (0 = pure black)

  // Font
  uint8_t  fontScale;     // 1..5, text size multiplier for time
  bool     boldText;      // true = bold effect (double-strike rendering)

  void setDefaults();
  void toJson(JsonObject o) const;
  void fromJson(JsonObjectConst o);
};

// ---- Top-level settings ----------------------------------------------------
struct Settings {
  // --- WiFi station networks (the device joins one of these) ---
  WifiCred wifi[MAX_WIFI_NETS];
  uint8_t  wifiCount;

  // --- Access point (config / fallback hotspot) ---
  String apSsid;
  String apPass;        // empty => open network
  String hostname;      // mDNS name => http://<hostname>.local

  // --- Web UI admin password ---
  String adminPass;     // empty => no auth required; default "1111"

  // --- Active feature ---
  uint8_t mode;         // MODE_STOCKS / MODE_USAGE / MODE_CAROUSEL / MODE_GITHUB / MODE_CLOCK / MODE_GALLERY

  // --- Carousel (mode == MODE_CAROUSEL): dwell + which features rotate ---
  uint16_t carouselSec;
  bool carouselTicker, carouselUsage, carouselGithub, carouselClock, carouselGallery;
  bool carouselClockDigital, carouselClockWeather, carouselClockModern, carouselClockForecast;

  // --- Shared HTTP / display ---
  uint16_t httpTimeout; // ms
  uint8_t  brightness;        // 0..100 %
  bool     autoBrightness;    // use LDR on A0
  bool     backlightInverted; // active-low backlight
  uint8_t  rotation;          // 0..3 screen orientation

  // --- Feature slices ---
  TickerSettings  ticker;
  UsageSettings   usage;
  GithubSettings  github;
  ClockSettings   clock;
  GallerySettings gallery;

  void setDefaults();
};

// Persistence
bool settingsBegin();                       // mount LittleFS
bool loadSettings(Settings& s);             // false => defaults applied
bool saveSettings(const Settings& s);
void factoryReset(Settings& s);             // wipe file + defaults

// JSON <-> struct. `includeSecrets=false` masks passwords for the web API.
void settingsToJson(const Settings& s, JsonObject root, bool includeSecrets);
void settingsApplyJson(Settings& s, JsonObjectConst root); // partial update allowed
