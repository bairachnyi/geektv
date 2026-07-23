#include "Settings.h"
#include "Platform.h"   // platformChipId() for the unique default hostname
#include <LittleFS.h>

static const char* CONFIG_PATH = "/config.json";

// ===========================================================================
// Ticker slice
// ===========================================================================
static const char* srcToStr(uint8_t s) {
  return (s == SRC_YAHOO) ? "yahoo"
       : (s == SRC_CASH)  ? "cash"
       : (s == SRC_GHUB)  ? "github" : "webhook";
}
static uint8_t srcFromStr(const String& s) {
  return s.equalsIgnoreCase("yahoo")  ? SRC_YAHOO
       : s.equalsIgnoreCase("cash")   ? SRC_CASH
       : s.equalsIgnoreCase("github") ? SRC_GHUB : SRC_WEBHOOK;
}

void TickerSettings::setDefaults() {
  webhookUrl = "";
  range = DEFAULT_RANGE;
  points = DEFAULT_POINTS;
  pollSec = DEFAULT_POLL_SEC;
  rotateSec = DEFAULT_ROTATE_SEC;
  colorInverted = false;
  changeOnRange = true;

  showName = true;
  showPrice = true;
  showChange = true;
  showChart = true;
  showRangeLabel = true;
  showUpdatedAgo = false;
  showPageDots = true;
  showPortfolio = true;   // only visible once a symbol has qty+cost set

  symbolCount = 0;
  for (uint8_t i = 0; i < MAX_SYMBOLS; i++) {
    symbols[i].symbol[0] = 0;
    symbols[i].name[0] = 0;
    symbols[i].source = DEFAULT_SOURCE;
    symbols[i].qty = 0;
    symbols[i].cost = 0;
  }
}

void TickerSettings::toJson(JsonObject o) const {
  o["webhookUrl"]     = webhookUrl;
  o["range"]          = range;
  o["points"]         = points;
  o["pollSec"]        = pollSec;
  o["rotateSec"]      = rotateSec;
  o["colorInverted"]  = colorInverted;
  o["changeOnRange"]  = changeOnRange;
  o["showName"]       = showName;
  o["showPrice"]      = showPrice;
  o["showChange"]     = showChange;
  o["showChart"]      = showChart;
  o["showRangeLabel"] = showRangeLabel;
  o["showUpdatedAgo"] = showUpdatedAgo;
  o["showPageDots"]   = showPageDots;
  o["showPortfolio"]  = showPortfolio;

  JsonArray arr = o["symbols"].to<JsonArray>();
  for (uint8_t i = 0; i < symbolCount; i++) {
    JsonObject e = arr.add<JsonObject>();
    e["symbol"] = symbols[i].symbol;
    e["name"]   = symbols[i].name;
    e["source"] = srcToStr(symbols[i].source);
    e["qty"]    = symbols[i].qty;
    e["cost"]   = symbols[i].cost;
  }
}

void TickerSettings::fromJson(JsonObjectConst o) {
  // Legacy (pre-2.4) configs carried one global "source"; it becomes the
  // default for any symbol that doesn't carry its own.
  uint8_t legacySrc = DEFAULT_SOURCE;
  if (o["source"].is<const char*>()) legacySrc = srcFromStr(o["source"].as<String>());

  if (o["webhookUrl"].is<const char*>()) webhookUrl = o["webhookUrl"].as<String>();
  if (o["range"].is<const char*>())      range = o["range"].as<String>();
  if (o["points"].is<int>())             points = constrain((int)o["points"], 0, MAX_SPARK_POINTS);
  if (o["pollSec"].is<int>())            pollSec = max(10, (int)o["pollSec"]);
  if (o["rotateSec"].is<int>())          rotateSec = max(2, (int)o["rotateSec"]);
  if (o["colorInverted"].is<bool>())     colorInverted = o["colorInverted"];
  if (o["changeOnRange"].is<bool>())     changeOnRange = o["changeOnRange"];

  if (o["showName"].is<bool>())       showName = o["showName"];
  if (o["showPrice"].is<bool>())      showPrice = o["showPrice"];
  if (o["showChange"].is<bool>())     showChange = o["showChange"];
  if (o["showChart"].is<bool>())      showChart = o["showChart"];
  if (o["showRangeLabel"].is<bool>()) showRangeLabel = o["showRangeLabel"];
  if (o["showUpdatedAgo"].is<bool>()) showUpdatedAgo = o["showUpdatedAgo"];
  if (o["showPageDots"].is<bool>())   showPageDots = o["showPageDots"];
  if (o["showPortfolio"].is<bool>())  showPortfolio = o["showPortfolio"];

  if (o["symbols"].is<JsonArrayConst>()) {
    JsonArrayConst arr = o["symbols"].as<JsonArrayConst>();
    symbolCount = 0;
    for (JsonObjectConst e : arr) {
      if (symbolCount >= MAX_SYMBOLS) break;
      const char* sym = e["symbol"] | "";
      if (!sym[0]) continue;                 // skip blank rows
      SymbolCfg& dst = symbols[symbolCount];
      strlcpy(dst.symbol, sym, MAX_SYMBOL_LEN);
      strlcpy(dst.name, e["name"] | "", MAX_NAME_LEN);
      dst.source = e["source"].is<const char*>()
                     ? srcFromStr(e["source"].as<String>()) : legacySrc;
      dst.qty  = e["qty"].as<float>();     // absent -> 0
      dst.cost = e["cost"].as<float>();
      if (dst.qty < 0)  dst.qty = 0;
      if (dst.cost < 0) dst.cost = 0;
      symbolCount++;
    }
  }
}

// ===========================================================================
// Usage slice
// ===========================================================================
void UsageSettings::setDefaults() {
  usageUrl = "";
  pollSec = DEFAULT_POLL_SEC;
}

void UsageSettings::toJson(JsonObject o) const {
  o["usageUrl"] = usageUrl;
  o["pollSec"]  = pollSec;
}

void UsageSettings::fromJson(JsonObjectConst o) {
  if (o["usageUrl"].is<const char*>()) usageUrl = o["usageUrl"].as<String>();
  if (o["pollSec"].is<int>())          pollSec = max(10, (int)o["pollSec"]);
}

// ===========================================================================
// GitHub dashboard slice
// ===========================================================================
void GithubSettings::setDefaults() {
  statusUrl = "";
  accessToken = "";
  pollSec = DEFAULT_GITHUB_POLL_SEC;
  rotateSec = DEFAULT_GITHUB_ROTATE_SEC;
}

void GithubSettings::toJson(JsonObject o, bool includeSecrets) const {
  o["statusUrl"] = statusUrl;
  o["tokenSet"] = accessToken.length() > 0;
  if (includeSecrets) o["accessToken"] = accessToken;
  o["pollSec"] = pollSec;
  o["rotateSec"] = rotateSec;
}

void GithubSettings::fromJson(JsonObjectConst o) {
  if (o["statusUrl"].is<const char*>()) statusUrl = o["statusUrl"].as<String>();
  if (o["accessToken"].is<const char*>()) {
    String next = o["accessToken"].as<String>();
    if (next.length()) accessToken = next; // blank keeps the stored token
  }
  if (o["clearToken"].is<bool>() && o["clearToken"].as<bool>()) accessToken = "";
  if (o["pollSec"].is<int>()) pollSec = constrain((int)o["pollSec"], 5, 3600);
  if (o["rotateSec"].is<int>()) rotateSec = constrain((int)o["rotateSec"], 3, 300);
}

// ===========================================================================
// Photo gallery slice
// ===========================================================================
void GallerySettings::setDefaults() {
  rotateSec = 10;
  randomOrder = false;
}

void GallerySettings::toJson(JsonObject o) const {
  o["rotateSec"]   = rotateSec;
  o["randomOrder"] = randomOrder;
}

void GallerySettings::fromJson(JsonObjectConst o) {
  if (o["rotateSec"].is<int>())    rotateSec = constrain((int)o["rotateSec"], 2, 3600);
  if (o["randomOrder"].is<bool>()) randomOrder = o["randomOrder"];
}

// ===========================================================================
// Clock / night mode / weather slice
// ===========================================================================
static uint16_t hhmmToMin(const char* s, uint16_t fallback) {
  if (!s || !s[0]) return fallback;
  int h = 0, m = 0;
  if (sscanf(s, "%d:%d", &h, &m) != 2) return fallback;
  if (h < 0 || h > 23 || m < 0 || m > 59) return fallback;
  return (uint16_t)(h * 60 + m);
}
static String minToHhmm(uint16_t v) {
  if (v > 1439) v = 0;
  char b[6];
  snprintf(b, sizeof(b), "%02u:%02u", (unsigned)(v / 60), (unsigned)(v % 60));
  return String(b);
}

void ClockSettings::setDefaults() {
  tz            = DEFAULT_TZ_NAME;
  tzPosix       = DEFAULT_TZ_POSIX;
  nightEnabled  = DEFAULT_NIGHT_ENABLED;
  nightStartMin = DEFAULT_NIGHT_START_MIN;
  nightEndMin   = DEFAULT_NIGHT_END_MIN;
  nightLevel    = DEFAULT_NIGHT_LEVEL;

  format24h     = true;
  showSeconds   = false;
  showDate      = true;
  theme         = 0;
  weatherCity   = "Moscow";
  weatherApiKey = "";
  weatherUnits  = "c";
  weatherPollSec= 900;

  timeColor     = 0x39E7;  // cyan
  dateColor     = 0xFFB6;  // amber
  accentColor   = 0x58A9;  // muted blue
  bgColor       = 0x0000;  // pure black
  fontScale     = 0;       // 0 = theme default
  boldText      = false;
}

void ClockSettings::toJson(JsonObject o) const {
  o["tz"]            = tz;
  o["tzPosix"]       = tzPosix;
  o["nightEnabled"]  = nightEnabled;
  o["nightStart"]    = minToHhmm(nightStartMin);
  o["nightEnd"]      = minToHhmm(nightEndMin);
  o["nightLevel"]    = nightLevel;

  o["format24h"]     = format24h;
  o["showSeconds"]   = showSeconds;
  o["showDate"]      = showDate;
  o["theme"]         = theme;
  o["weatherCity"]   = weatherCity;
  o["weatherApiKey"] = weatherApiKey;
  o["weatherUnits"]  = weatherUnits;
  o["weatherPollSec"]= weatherPollSec;

  // Colors as 4-digit hex strings for the web UI (e.g. "39E7")
  char buf[8];
  snprintf(buf, sizeof(buf), "%04X", timeColor);   o["timeColor"]   = buf;
  snprintf(buf, sizeof(buf), "%04X", dateColor);   o["dateColor"]   = buf;
  snprintf(buf, sizeof(buf), "%04X", accentColor); o["accentColor"] = buf;
  snprintf(buf, sizeof(buf), "%04X", bgColor);     o["bgColor"]     = buf;
  o["fontScale"] = fontScale;
  o["boldText"] = boldText;
}

static uint16_t parseColorHex(const char* str, uint16_t defVal) {
  if (!str || !str[0]) return defVal;
  if (str[0] == '#') str++;
  size_t len = strlen(str);
  if (len == 6) {
    long v = strtol(str, nullptr, 16);
    uint8_t r = (v >> 16) & 0xFF;
    uint8_t g = (v >> 8) & 0xFF;
    uint8_t b = v & 0xFF;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
  }
  return (uint16_t)strtol(str, nullptr, 16);
}

void ClockSettings::fromJson(JsonObjectConst o) {
  if (o["tz"].is<const char*>())          tz = o["tz"].as<String>();
  if (o["tzPosix"].is<const char*>())     tzPosix = o["tzPosix"].as<String>();
  if (o["nightEnabled"].is<bool>())       nightEnabled = o["nightEnabled"];
  if (o["nightStart"].is<const char*>())  nightStartMin = hhmmToMin(o["nightStart"], nightStartMin);
  if (o["nightEnd"].is<const char*>())    nightEndMin   = hhmmToMin(o["nightEnd"], nightEndMin);
  if (o["nightLevel"].is<int>())          nightLevel = constrain((int)o["nightLevel"], 0, 100);

  if (o["format24h"].is<bool>())     format24h = o["format24h"];
  if (o["showSeconds"].is<bool>())   showSeconds = o["showSeconds"];
  if (o["showDate"].is<bool>())      showDate = o["showDate"];
  if (o["theme"].is<int>())         theme = (uint8_t)constrain((int)o["theme"], 0, 3);
  if (o["weatherCity"].is<const char*>())   weatherCity = o["weatherCity"].as<String>();
  if (o["weatherApiKey"].is<const char*>()) weatherApiKey = o["weatherApiKey"].as<String>();
  if (o["weatherUnits"].is<const char*>())  weatherUnits = o["weatherUnits"].as<String>();
  if (o["weatherPollSec"].is<int>()) weatherPollSec = constrain((int)o["weatherPollSec"], 60, 86400);

  if (o["timeColor"].is<const char*>())   timeColor   = parseColorHex(o["timeColor"].as<const char*>(), 0x39E7);
  if (o["dateColor"].is<const char*>())   dateColor   = parseColorHex(o["dateColor"].as<const char*>(), 0xFFB6);
  if (o["accentColor"].is<const char*>()) accentColor = parseColorHex(o["accentColor"].as<const char*>(), 0x58A9);
  if (o["bgColor"].is<const char*>())     bgColor     = parseColorHex(o["bgColor"].as<const char*>(), 0x0000);
  if (o["fontScale"].is<int>()) fontScale = (uint8_t)constrain((int)o["fontScale"], 0, 5);
  if (o["boldText"].is<bool>()) boldText = o["boldText"];
}

// ===========================================================================
// Top-level settings
// ===========================================================================
void Settings::setDefaults() {
  wifiCount = 0;
  for (uint8_t i = 0; i < MAX_WIFI_NETS; i++) {
    wifi[i].ssid = "";
    wifi[i].pass = "";
  }
  apSsid  = DEFAULT_AP_SSID;
  apPass  = DEFAULT_AP_PASS;
  // Unique per device so several SmallTVs on one network don't collide on
  // mDNS out of the box. A hostname saved in config.json overrides this.
  hostname = String(DEFAULT_HOSTNAME) + "-" + String(platformChipId() & 0xFFFF, HEX);
  adminPass = "1111";

  mode = DEFAULT_MODE;
  carouselSec = DEFAULT_CAROUSEL_SEC;
  carouselTicker = carouselUsage = carouselGithub = carouselClock = carouselGallery = true;
  carouselClockDigital = true;
  carouselClockWeather = carouselClockModern = carouselClockForecast = false;
  httpTimeout = DEFAULT_HTTP_TIMEOUT;

  brightness = DEFAULT_BRIGHTNESS;
  autoBrightness = false;
  backlightInverted = TFT_BL_DEFAULT_INVERTED;
  rotation = 0;

  ticker.setDefaults();
  usage.setDefaults();
  github.setDefaults();
  clock.setDefaults();
  gallery.setDefaults();
}

// ---------------------------------------------------------------------------
bool settingsBegin() {
  if (LittleFS.begin()) return true;
  // First boot on a fresh chip: format then mount.
  if (LittleFS.format() && LittleFS.begin()) return true;
  return false;
}

bool loadSettings(Settings& s) {
  s.setDefaults();
  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  settingsApplyJson(s, doc.as<JsonObjectConst>());
  return true;
}

bool saveSettings(const Settings& s) {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  settingsToJson(s, root, /*includeSecrets=*/true);

  const char* tmpPath = "/config.json.tmp";
  File f = LittleFS.open(tmpPath, "w");
  if (!f) return false;
  size_t bytesWritten = serializeJson(doc, f);
  f.close();

  if (bytesWritten == 0) {
    LittleFS.remove(tmpPath);
    return false;
  }
  LittleFS.remove(CONFIG_PATH);
  return LittleFS.rename(tmpPath, CONFIG_PATH);
}

void factoryReset(Settings& s) {
  LittleFS.remove(CONFIG_PATH);
  s.setDefaults();
}

// ---------------------------------------------------------------------------
void settingsToJson(const Settings& s, JsonObject root, bool includeSecrets) {
  root["hostname"]   = s.hostname;
  if (includeSecrets) root["adminPass"] = s.adminPass;

  // WiFi networks. Passwords only reach the config file, never the web API.
  JsonArray wf = root["wifi"].to<JsonArray>();
  for (uint8_t i = 0; i < s.wifiCount; i++) {
    JsonObject e = wf.add<JsonObject>();
    e["ssid"]    = s.wifi[i].ssid;
    e["passSet"] = s.wifi[i].pass.length() > 0;
    if (includeSecrets) e["pass"] = s.wifi[i].pass;
  }
  // Legacy mirror of the primary network, kept for one release so a firmware
  // downgrade still finds its WiFi in config.json.
  root["staSsid"]    = s.wifiCount ? s.wifi[0].ssid : "";
  root["staPassSet"] = s.wifiCount && s.wifi[0].pass.length() > 0;
  root["apSsid"]     = s.apSsid;
  root["apPassSet"]  = s.apPass.length() > 0;
  if (includeSecrets) {
    root["staPass"]  = s.wifiCount ? s.wifi[0].pass : "";
    root["apPass"]   = s.apPass;
  }

  // Mode + shared HTTP/display
  root["mode"]              = (s.mode == MODE_GITHUB)   ? "github"
                            : (s.mode == MODE_USAGE)    ? "usage"
                            : (s.mode == MODE_CLOCK)    ? (s.clock.theme == 1 ? "clock_weather" : (s.clock.theme == 2 ? "clock_modern" : (s.clock.theme == 3 ? "clock_forecast" : "clock")))
                            : (s.mode == MODE_GALLERY)  ? "gallery"
                            : (s.mode == MODE_CAROUSEL) ? "carousel" : "stocks";
  root["carouselSec"]       = s.carouselSec;
  root["carouselTicker"]    = s.carouselTicker;
  root["carouselUsage"]     = s.carouselUsage;
  root["carouselGithub"]    = s.carouselGithub;
  root["carouselClock"]            = s.carouselClock;
  root["carouselClockDigital"]     = s.carouselClockDigital;
  root["carouselClockWeather"]     = s.carouselClockWeather;
  root["carouselClockModern"]      = s.carouselClockModern;
  root["carouselClockForecast"]    = s.carouselClockForecast;
  root["carouselGallery"]          = s.carouselGallery;
  root["httpTimeout"]       = s.httpTimeout;
  root["brightness"]        = s.brightness;
  root["autoBrightness"]    = s.autoBrightness;
  root["backlightInverted"] = s.backlightInverted;
  root["rotation"]          = s.rotation;

  // Feature slices
  s.ticker.toJson(root["ticker"].to<JsonObject>());
  s.usage.toJson(root["usage"].to<JsonObject>());
  s.github.toJson(root["github"].to<JsonObject>(), includeSecrets);
  s.clock.toJson(root["clock"].to<JsonObject>());
  s.gallery.toJson(root["gallery"].to<JsonObject>());
}

// Apply only the keys that are present (partial update friendly). Accepts both
// the nested layout and the legacy flat layout (feature keys at the top level).
void settingsApplyJson(Settings& s, JsonObjectConst root) {
  if (root["hostname"].is<const char*>()) s.hostname = root["hostname"].as<String>();
  if (root["adminPass"].is<const char*>()) {
    String p = root["adminPass"].as<String>();
    if (p.length() > 0) s.adminPass = p;
  }

  if (root["wifi"].is<JsonArrayConst>()) {
    // The list is authoritative when present (order = try priority, missing
    // row = deletion). A blank password keeps the stored one, matched by SSID
    // so rows survive reordering.
    WifiCred old[MAX_WIFI_NETS];
    uint8_t oldCount = s.wifiCount;
    for (uint8_t i = 0; i < oldCount; i++) old[i] = s.wifi[i];

    s.wifiCount = 0;
    for (JsonObjectConst e : root["wifi"].as<JsonArrayConst>()) {
      if (s.wifiCount >= MAX_WIFI_NETS) break;
      const char* ssid = e["ssid"] | "";
      if (!ssid[0]) continue;                // skip blank rows
      WifiCred& dst = s.wifi[s.wifiCount];
      dst.ssid = ssid;
      const char* pass = e["pass"] | "";
      dst.pass = pass;
      if (!pass[0])
        for (uint8_t i = 0; i < oldCount; i++)
          if (old[i].ssid == dst.ssid) { dst.pass = old[i].pass; break; }
      s.wifiCount++;
    }
  } else if (root["staSsid"].is<const char*>()) {
    // Legacy single-network layout (pre-2.4 config.json or an old cached web
    // page): it becomes/updates the primary network, extras stay untouched.
    String ssid = root["staSsid"].as<String>();
    if (ssid.length()) {
      s.wifi[0].ssid = ssid;
      if (root["staPass"].is<const char*>()) {
        String p = root["staPass"].as<String>();
        if (p.length() > 0) s.wifi[0].pass = p;   // blank = keep
      }
      if (s.wifiCount < 1) s.wifiCount = 1;
    }
  }
  if (root["apSsid"].is<const char*>()) s.apSsid = root["apSsid"].as<String>();
  // AP password: apply as-is when present (empty allowed => open AP).
  if (root["apPass"].is<const char*>()) s.apPass = root["apPass"].as<String>();

  if (root["mode"].is<const char*>()) {
    String m = root["mode"].as<String>();
    if (m.equalsIgnoreCase("clock_weather")) {
      s.mode = MODE_CLOCK;
      s.clock.theme = 1;
    } else if (m.equalsIgnoreCase("clock_modern")) {
      s.mode = MODE_CLOCK;
      s.clock.theme = 2;
    } else if (m.equalsIgnoreCase("clock_forecast")) {
      s.mode = MODE_CLOCK;
      s.clock.theme = 3;
    } else {
      s.mode = m.equalsIgnoreCase("github")   ? MODE_GITHUB
             : m.equalsIgnoreCase("radar")    ? MODE_STOCKS
             : m.equalsIgnoreCase("usage")    ? MODE_USAGE
             : m.equalsIgnoreCase("clock")    ? MODE_CLOCK
             : m.equalsIgnoreCase("gallery")  ? MODE_GALLERY
             : m.equalsIgnoreCase("carousel") ? MODE_CAROUSEL : MODE_STOCKS;
      if (m.equalsIgnoreCase("clock")) s.clock.theme = 0;
    }
  }
  if (root["carouselSec"].is<int>())      s.carouselSec = constrain((int)root["carouselSec"], 5, 3600);
  if (root["carouselTicker"].is<bool>())  s.carouselTicker = root["carouselTicker"];
  if (root["carouselUsage"].is<bool>())   s.carouselUsage = root["carouselUsage"];
  if (root["carouselGithub"].is<bool>())  s.carouselGithub = root["carouselGithub"];
  if (root["carouselClock"].is<bool>())     s.carouselClock = root["carouselClock"];
  if (root["carouselClockDigital"].is<bool>()) s.carouselClockDigital = root["carouselClockDigital"];
  if (root["carouselClockWeather"].is<bool>()) s.carouselClockWeather = root["carouselClockWeather"];
  if (root["carouselClockModern"].is<bool>())  s.carouselClockModern = root["carouselClockModern"];
  if (root["carouselClockForecast"].is<bool>()) s.carouselClockForecast = root["carouselClockForecast"];
  if (root["carouselGallery"].is<bool>())   s.carouselGallery = root["carouselGallery"];

  if (root["httpTimeout"].is<int>())        s.httpTimeout = constrain((int)root["httpTimeout"], 1000, 20000);
  if (root["brightness"].is<int>())         s.brightness = constrain((int)root["brightness"], 0, 100);
  if (root["autoBrightness"].is<bool>())    s.autoBrightness = root["autoBrightness"];
  if (root["backlightInverted"].is<bool>()) s.backlightInverted = root["backlightInverted"];
  if (root["rotation"].is<int>())           s.rotation = (uint8_t)(((int)root["rotation"]) & 3);

  // Feature slices: prefer the nested object; fall back to the top level so a
  // legacy flat config.json (or a legacy POST) still applies. The old shared
  // "pollSec" thus seeds both ticker and usage cadence on first upgrade.
  JsonObjectConst t = root["ticker"].is<JsonObjectConst>() ? root["ticker"].as<JsonObjectConst>() : root;
  s.ticker.fromJson(t);
  JsonObjectConst u = root["usage"].is<JsonObjectConst>() ? root["usage"].as<JsonObjectConst>() : root;
  s.usage.fromJson(u);
  if (root["github"].is<JsonObjectConst>())  s.github.fromJson(root["github"].as<JsonObjectConst>());
  if (root["clock"].is<JsonObjectConst>())   s.clock.fromJson(root["clock"].as<JsonObjectConst>());
  if (root["gallery"].is<JsonObjectConst>()) s.gallery.fromJson(root["gallery"].as<JsonObjectConst>());
}
