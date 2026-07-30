#include "ClockMode.h"
#include "Gfx.h"
#include "Net.h"
#include "Clock.h"
#include "Platform.h"
#include "MontserratBold.h"
#include "GithubClient.h"
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>

ClockMode g_clockMode;

// RGB565 values are mirrored exactly by updateClockPreview() in webui.h.
static const uint16_t UI_DARK       = 0x0021; // #000408
static const uint16_t UI_PANEL      = 0x0863; // #081018
static const uint16_t UI_PANEL_BLUE = 0x08A4; // #081420
static const uint16_t UI_BORDER     = 0x2147; // #212838
static const uint16_t UI_WHITE      = 0xFFFF;
static const uint16_t UI_MUTED      = 0x8410;
static const uint16_t UI_GREEN      = 0x47E8;
static const uint16_t UI_RED        = 0xF800;
static const uint16_t UI_YELLOW     = 0xFFE0;

static void drawFontCentered(const char* txt, int yCenter, const GFXfont* font, uint16_t color) {
  Arduino_GFX* g = gfxDev();
  if (!g || !txt || !txt[0]) return;
  g->setFont(font);
  g->setTextColor(color);
  int16_t x1 = 0, y1 = 0;
  uint16_t w = 0, h = 0;
  g->getTextBounds((char*)txt, 0, 0, &x1, &y1, &w, &h);
  int x = max(0, (240 - (int)w) / 2 - x1);
  int baseline = yCenter - ((int)y1 + (int)h / 2);
  g->setCursor(x, baseline);
  g->print(txt);
}

static void drawClockFontInReservedArea(const char* text, int yCenter, uint16_t color) {
  Arduino_GFX* g = gfxDev();
  if (!g || !text || !text[0]) return;
  const GFXfont* font = &MontserratBold18pt7b;
  g->setFont(font);
  g->setTextColor(color);
  int16_t x1 = 0, y1 = 0;
  uint16_t w = 0, h = 0;
  g->getTextBounds((char*)text, 0, 0, &x1, &y1, &w, &h);
  // The visible glyphs are centered inside x=8..164. The independent seconds
  // area starts at x=177, so even the widest HH:MM cannot overlap it.
  int visibleX = 8 + max(0, (156 - (int)w) / 2);
  int cursorX = visibleX - x1;
  int baseline = yCenter - ((int)y1 + (int)h / 2);
  g->setCursor(cursorX, baseline);
  g->print(text);
}

static const char* compactRepo(const char* repo) {
  if (!repo) return "";
  const char* slash = strrchr(repo, '/');
  return slash && slash[1] ? slash + 1 : repo;
}

static void clockMarquee(const char* source, char* out, size_t outLen,
                         uint8_t visible, uint16_t frame) {
  if (!source) source = "";
  size_t len = strlen(source);
  if (len <= visible) { strlcpy(out, source, outLen); return; }
  const uint8_t gap = 3;
  size_t period = len + gap;
  size_t start = (frame / 2) % period;
  uint8_t count = min<uint8_t>(visible, outLen - 1);
  for (uint8_t i = 0; i < count; i++) {
    size_t at = (start + i) % period;
    out[i] = at < len ? source[at] : ' ';
  }
  out[count] = '\0';
}

static uint16_t githubStateColor(uint8_t state) {
  if (state == GH_SUCCESS) return 0x47E8;
  if (state == GH_FAILURE) return 0xF800;
  if (state == GH_RUNNING) return 0x07FF;
  if (state == GH_QUEUED) return 0xFD20;
  return UI_MUTED;
}

static void drawCompactGithubIcon(Arduino_GFX* g, int x, int y, const GithubRun& run) {
  uint16_t color = githubStateColor(run.state);
  if (run.type == GH_EVENT_PULL_REQUEST) {
    g->fillCircle(x - 3, y - 5, 2, color);
    g->fillCircle(x - 3, y + 5, 2, color);
    g->fillCircle(x + 4, y - 5, 2, color);
    g->drawFastVLine(x - 3, y - 3, 6, color);
    g->drawLine(x - 1, y + 2, x + 4, y - 3, color);
  } else if (run.state == GH_SUCCESS) {
    g->drawLine(x - 5, y, x - 1, y + 4, color);
    g->drawLine(x - 1, y + 4, x + 6, y - 5, color);
  } else if (run.state == GH_FAILURE) {
    g->drawLine(x - 5, y - 5, x + 5, y + 5, color);
    g->drawLine(x + 5, y - 5, x - 5, y + 5, color);
  } else if (run.state == GH_RUNNING || run.state == GH_QUEUED) {
    g->drawCircle(x, y, 6, color);
    g->fillTriangle(x - 2, y - 4, x - 2, y + 4, x + 4, y, color);
  } else {
    g->drawCircle(x, y, 5, color);
  }
}

static void fitUpper(String value, char* out, size_t outLen, size_t maxChars) {
  value.toUpperCase();
  if (value.length() > maxChars) value = value.substring(0, maxChars - 2) + "..";
  strlcpy(out, value.c_str(), outLen);
}

static void formatTemp(float value, const Settings& s, char* out, size_t outLen) {
  snprintf(out, outLen, "%.0f%c", value, s.clock.weatherUnits == "f" ? 'F' : 'C');
}

static void formatClockTime(const struct tm& t, const Settings& s, char* out, size_t outLen) {
  int hour = t.tm_hour;
  if (!s.clock.format24h) { hour %= 12; if (!hour) hour = 12; }
  if (s.clock.showSeconds)
    snprintf(out, outLen, "%02d:%02d:%02d", hour, t.tm_min, t.tm_sec);
  else
    snprintf(out, outLen, "%02d:%02d", hour, t.tm_min);
}

static const GFXfont* clockTimeFont(const Settings& s) {
  return s.clock.showSeconds ? &MontserratBold28pt7b : &MontserratBold40pt7b;
}

static const char* weatherKind(uint16_t code) {
  if (code == 113) return "SUN";
  if (code == 116) return "PART";
  if (code == 119 || code == 122 || code == 143 || code == 248 || code == 260) return "CLOUD";
  if ((code >= 176 && code <= 359) || code == 386 || code == 389) return "RAIN";
  if (code >= 368 && code <= 395) return "SNOW";
  return "WX";
}

// Compact Meteocons-style status mark. Keeping it vector-based avoids adding
// another large bitmap/font table to the OTA-constrained ESP8266 image.
static void drawWeatherMark(int cx, int cy, uint16_t code, uint16_t color) {
  Arduino_GFX* g = gfxDev();
  if (!g) return;
  const char* kind = weatherKind(code);
  g->fillRoundRect(cx - 20, cy - 13, 40, 26, 8, UI_PANEL);
  g->drawRoundRect(cx - 20, cy - 13, 40, 26, 8, color);
  g->setFont(nullptr);
  g->setTextSize(1);
  g->setTextColor(color);
  g->setCursor(cx - gfxTextW(kind, 1) / 2, cy - 4);
  g->print(kind);
}

static void drawThermometer(int x, int y, uint16_t color) {
  Arduino_GFX* g = gfxDev();
  if (!g) return;
  g->drawRoundRect(x + 4, y, 7, 18, 3, color);
  g->fillCircle(x + 7, y + 19, 6, color);
  g->drawFastVLine(x + 7, y + 5, 12, color);
}

static void drawDroplet(int x, int y, uint16_t color) {
  Arduino_GFX* g = gfxDev();
  if (!g) return;
  g->drawLine(x + 8, y, x + 1, y + 12, color);
  g->drawLine(x + 8, y, x + 15, y + 12, color);
  g->drawCircle(x + 8, y + 13, 7, color);
}

static void dateLabel(const String& iso, uint8_t index, char* out, size_t outLen) {
  if (index == 0) { strlcpy(out, "TODAY", outLen); return; }
  if (index == 1) { strlcpy(out, "TOMORROW", outLen); return; }
  if (iso.length() >= 10) {
    static const char* months[] = {"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
    int month = iso.substring(5, 7).toInt();
    int day = iso.substring(8, 10).toInt();
    snprintf(out, outLen, "%02d %s", day, months[constrain(month, 1, 12) - 1]);
  } else strlcpy(out, "DAY +2", outLen);
}

void ClockMode::begin(const Settings&) {
  m_weather = WeatherData();
  m_nextFetchMs = millis();
  m_lastTick = -1;
  m_lastYday = -1;
  m_lastTheme = 0xFF;
  m_lastTime[0] = '\0';
  m_fullRepaint = true;
  m_infoPage = 0;
  m_nextInfoPageMs = 0;
  m_lastGithubRevision = UINT32_MAX;
  m_nextGithubFrameMs = 0;
  m_githubFrame = 0;
}

void ClockMode::invalidate(const Settings& s) {
  m_nextFetchMs = millis();
  m_lastTick = -1;
  m_lastYday = -1;
  m_lastTheme = 0xFF;
  m_lastTime[0] = '\0';
  m_fullRepaint = true;
  m_infoPage = 0;
  m_nextInfoPageMs = 0;
  m_lastGithubRevision = UINT32_MAX;
  m_nextGithubFrameMs = 0;
  m_githubFrame = 0;
  render(s);
}

void ClockMode::wake(const Settings& s) {
  m_lastTick = -1;
  m_lastYday = -1;
  m_lastTheme = 0xFF;
  m_lastTime[0] = '\0';
  m_fullRepaint = true;
  m_infoPage = 0;
  m_nextInfoPageMs = 0;
  m_lastGithubRevision = UINT32_MAX;
  m_nextGithubFrameMs = 0;
  m_githubFrame = 0;
  render(s);
}

bool ClockMode::fetchWeather(const Settings& s) {
  if (WiFi.status() != WL_CONNECTED) {
    m_weather.error = "Wi-Fi offline";
    return false;
  }

  String city = s.clock.weatherCity.length() ? s.clock.weatherCity : "Moscow";
  city.replace(" ", "+");
  // Weather is public, non-sensitive data. On the ESP8266, avoiding TLS saves
  // enough contiguous heap for the filtered JSON parser while the other
  // dashboard features remain compiled in. ESP32 targets retain HTTPS.
#if defined(SMALLTV_ESP8266)
  String url = "http://wttr.in/" + city + "?format=j1";
  const uint32_t minHeap = 7500;
  const uint32_t minBlock = 4500;
#else
  String url = "https://wttr.in/" + city + "?format=j1";
  const uint32_t minHeap = 17000;
  const uint32_t minBlock = 9000;
#endif

  if (ESP.getFreeHeap() < minHeap || platformMaxFreeBlock() < minBlock) {
    m_weather.error = "Low memory";
    return false;
  }
#if defined(SMALLTV_ESP8266)
  std::unique_ptr<NetClient> client(new WiFiClient());
#else
  std::unique_ptr<NetClient> client(platformMakeSecureClient(2048));
#endif
  HTTPClient http;
  http.setTimeout(s.httpTimeout);
  http.setReuse(false);
  http.useHTTP10(true);
  if (!http.begin(*client, url)) {
    m_weather.error = "Weather URL";
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    m_weather.error = code < 0 ? "Weather offline" : "Weather HTTP " + String(code);
    http.end();
    return false;
  }

  JsonDocument filter;
  filter["current_condition"][0]["temp_C"] = true;
  filter["current_condition"][0]["temp_F"] = true;
  filter["current_condition"][0]["humidity"] = true;
  filter["current_condition"][0]["windspeedKmph"] = true;
  filter["current_condition"][0]["weatherCode"] = true;
  filter["current_condition"][0]["weatherDesc"][0]["value"] = true;
  filter["nearest_area"][0]["areaName"][0]["value"] = true;
  filter["weather"][0]["date"] = true;
  filter["weather"][0]["avgtempC"] = true;
  filter["weather"][0]["avgtempF"] = true;
  filter["weather"][0]["mintempC"] = true;
  filter["weather"][0]["mintempF"] = true;
  filter["weather"][0]["maxtempC"] = true;
  filter["weather"][0]["maxtempF"] = true;
  filter["weather"][0]["hourly"][0]["humidity"] = true;
  filter["weather"][0]["hourly"][0]["weatherCode"] = true;
  filter["weather"][0]["hourly"][0]["weatherDesc"][0]["value"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    m_weather.error = "Weather JSON";
    return false;
  }

  JsonObjectConst cur = doc["current_condition"][0].as<JsonObjectConst>();
  if (cur.isNull()) {
    m_weather.error = "Weather empty";
    return false;
  }

  WeatherData next;
  // Keep the label chosen by the user. The provider's nearest-area result can
  // be a small neighbouring district (for example Ban Thing Iung for Hua Hin).
  next.city = s.clock.weatherCity.length()
    ? s.clock.weatherCity
    : String(doc["nearest_area"][0]["areaName"][0]["value"] | "Weather");
  next.temp = (s.clock.weatherUnits == "f") ? cur["temp_F"].as<float>() : cur["temp_C"].as<float>();
  next.humidity = constrain(cur["humidity"].as<int>(), 0, 100);
  next.windKph = cur["windspeedKmph"].as<float>();
  next.weatherCode = cur["weatherCode"].as<uint16_t>();
  next.description = cur["weatherDesc"][0]["value"] | "Weather";

  JsonArrayConst days = doc["weather"].as<JsonArrayConst>();
  uint8_t dayIndex = 0;
  for (JsonObjectConst day : days) {
    if (dayIndex >= 3) break;
    ForecastDay& out = next.days[dayIndex++];
    out.valid = true;
    out.date = day["date"] | "";
    if (s.clock.weatherUnits == "f") {
      out.temp = day["avgtempF"].as<float>();
      out.minTemp = day["mintempF"].as<float>();
      out.maxTemp = day["maxtempF"].as<float>();
    } else {
      out.temp = day["avgtempC"].as<float>();
      out.minTemp = day["mintempC"].as<float>();
      out.maxTemp = day["maxtempC"].as<float>();
    }
    JsonArrayConst hours = day["hourly"].as<JsonArrayConst>();
    JsonObjectConst representative;
    uint8_t hourIndex = 0;
    for (JsonObjectConst hour : hours) {
      if (hourIndex == 4 || representative.isNull()) representative = hour;
      if (++hourIndex > 4) break;
    }
    out.humidity = constrain(representative["humidity"].as<int>(), 0, 100);
    out.weatherCode = representative["weatherCode"].as<uint16_t>();
    out.description = representative["weatherDesc"][0]["value"] | "Weather";
  }

  next.valid = dayIndex > 0;
  next.lastUpdateMs = millis();
  next.error = "";
  m_weather = next;
  return true;
}

void ClockMode::service(const Settings& s) {
  if (s.mode != MODE_CLOCK && s.mode != MODE_CAROUSEL) return;
  uint32_t nowMs = millis();
  if ((int32_t)(nowMs - m_nextFetchMs) >= 0) {
    uint32_t interval = (uint32_t)s.clock.weatherPollSec * 1000UL;
    if (interval < 60000UL) interval = 60000UL;
    bool ok = fetchWeather(s);
    // A transient network/memory failure should not leave both weather pages
    // empty for the full 15-minute normal interval.
    m_nextFetchMs = nowMs + (ok ? interval : 30000UL);
    m_fullRepaint = true;
  }

  if (m_fullRepaint) {
    render(s);
    return;
  }

  if (s.github.statusUrl.length() >= 8) githubService(s);
  const GithubData& github = githubGet();
  bool githubChanged = github.revision != m_lastGithubRevision;
  if (s.clock.theme == 0 &&
      (githubChanged || (int32_t)(nowMs - m_nextGithubFrameMs) >= 0)) {
    m_lastGithubRevision = github.revision;
    m_githubFrame++;
    renderGithubSummary(s);
    m_nextGithubFrameMs = nowMs + 1000UL;
  }
  if (s.clock.theme == 0 && (int32_t)(nowMs - m_nextInfoPageMs) >= 0) {
    m_infoPage = (uint8_t)((m_infoPage + 1) % 3);
    renderInfoLine(s);
    m_nextInfoPageMs = nowMs + 4000UL;
  }

  struct tm t;
  if (!clockNow(t)) return;

  // The forecast has no second/minute-dependent pixels. It is repainted only
  // after wake/settings/weather changes.
  if (s.clock.theme >= 2) return;

  int16_t tick = s.clock.showSeconds ? t.tm_sec : t.tm_min;
  if (m_lastTheme != s.clock.theme || m_lastYday != t.tm_yday) {
    render(s);
  } else if (tick != m_lastTick) {
    m_lastTick = tick;
    renderTimeOnly(s, t);
  }
}

void ClockMode::renderInfoLine(const Settings& s) {
  if (s.clock.theme != 0) return;
  Arduino_GFX* g = gfxDev();
  if (!g) return;
  g->fillRect(8, 30, 224, 20, s.clock.bgColor);

  char line[30];
  uint16_t color = UI_WHITE;
  if (m_infoPage == 0) {
    if (m_weather.valid) snprintf(line, sizeof(line), "WIND %.1f KM/H", m_weather.windKph);
    else strlcpy(line, "WIND --", sizeof(line));
  } else if (m_infoPage == 1) {
    String address = netMode() == NET_AP ? "192.168.4.1" : netIP();
    snprintf(line, sizeof(line), "IP %s", address.c_str());
    color = s.clock.accentColor;
  } else {
    if (m_weather.valid) fitUpper(m_weather.description, line, sizeof(line), 25);
    else strlcpy(line, m_weather.error.length() ? m_weather.error.c_str() : "WEATHER CONNECTING", sizeof(line));
    color = m_weather.valid ? s.clock.dateColor : UI_MUTED;
  }
  gfxPrint(10, 32, line, color, 2);
}

void ClockMode::renderGithubSummary(const Settings& s) {
  if (s.clock.theme != 0) return;
  Arduino_GFX* g = gfxDev();
  if (!g) return;
  g->fillRect(104, 176, 136, 64, s.clock.bgColor);

  const GithubData& data = githubGet();
  if (s.github.statusUrl.length() < 8) {
    gfxPrint(112, 199, "GH NOT SET", UI_MUTED, 1);
    return;
  }
  if (!data.valid || !data.runCount) {
    gfxPrint(112, 199, data.error ? "GH ERROR" : "GH SYNC...", data.error ? UI_RED : UI_MUTED, 1);
    return;
  }

  uint8_t count = min<uint8_t>(2, data.runCount);
  for (uint8_t i = 0; i < count; i++) {
    const GithubRun& run = data.runs[i];
    int y = 191 + i * 30;
    drawCompactGithubIcon(g, 113, y, run);

    char repo[10];
    clockMarquee(compactRepo(run.repo), repo, sizeof(repo), 9, m_githubFrame + i * 3);
    gfxPrint(126, y - 8, repo, UI_WHITE, 2);
  }
}

void ClockMode::renderTimeOnly(const Settings& s, const struct tm& t) {
  if (s.clock.theme >= 2) return;
  char nextTime[16];
  if (s.clock.theme == 0) {
    int hour = t.tm_hour;
    if (!s.clock.format24h) { hour %= 12; if (!hour) hour = 12; }
    snprintf(nextTime, sizeof(nextTime), "%02d:%02d", hour, t.tm_min);

    if (strcmp(nextTime, m_lastTime)) {
      Arduino_GFX* g = gfxDev();
      if (s.clock.showSeconds) {
        g->fillRect(6, 72, 160, 62, s.clock.bgColor);
        drawClockFontInReservedArea(nextTime, 103, s.clock.timeColor);
      } else {
        g->fillRect(0, 72, 240, 62, s.clock.bgColor);
        drawFontCentered(nextTime, 103, &MontserratBold28pt7b, s.clock.timeColor);
      }
      strlcpy(m_lastTime, nextTime, sizeof(m_lastTime));
    }

    if (s.clock.showSeconds) {
      Arduino_GFX* g = gfxDev();
      g->fillRect(177, 87, 60, 34, s.clock.bgColor);
      char seconds[4];
      snprintf(seconds, sizeof(seconds), "%02d", t.tm_sec);
      gfxPrint(184, 91, seconds, UI_YELLOW, 3);
    }
    return;
  }

  const int centerY = s.clock.theme == 0 ? 116 : 43;
  // GFX custom-font text is transparent. Painting the previous glyphs in the
  // panel color erases only pixels that changed, avoiding a visible rectangular
  // clear and the old full-screen repaint.
  uint16_t eraseColor = s.clock.theme == 0 ? s.clock.bgColor : UI_PANEL;
  const GFXfont* font = clockTimeFont(s);
  if (m_lastTime[0]) drawFontCentered(m_lastTime, centerY, font, eraseColor);
  drawFontCentered(nextTime, centerY, font, s.clock.timeColor);
  strlcpy(m_lastTime, nextTime, sizeof(m_lastTime));
}

void ClockMode::render(const Settings& s) {
  Arduino_GFX* g = gfxDev();
  if (!g) return;
  m_fullRepaint = false;
  g->setFont(nullptr);
  g->fillScreen(s.clock.bgColor);

  struct tm t;
  bool timeOk = clockNow(t);
  char timeStr[16] = "--:--";
  char dateStr[28] = "SYNCING CLOCK";
  if (timeOk) {
    formatClockTime(t, s, timeStr, sizeof(timeStr));
    static const char* days[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
    static const char* months[] = {"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
    snprintf(dateStr, sizeof(dateStr), "%s, %02d %s %04d", days[t.tm_wday], t.tm_mday, months[t.tm_mon], t.tm_year + 1900);
  }

  char ip[32];
  snprintf(ip, sizeof(ip), "%s: %s", netMode() == NET_AP ? "AP" : "IP", netMode() == NET_AP ? "192.168.4.1" : netIP().c_str());
  uint16_t tc = s.clock.timeColor, dc = s.clock.dateColor, ac = s.clock.accentColor;
  const GFXfont* timeFont = clockTimeFont(s);

  if (s.clock.theme == 0) {
    char city[18];
    fitUpper(s.clock.weatherCity.length() ? s.clock.weatherCity : "WEATHER",
             city, sizeof(city), 15);
    gfxPrint(10, 8, city, UI_YELLOW, 2);
    bool wifiConnected = WiFi.status() == WL_CONNECTED;
    g->fillCircle(220, 16, 7, wifiConnected ? UI_GREEN : UI_RED);
    g->drawCircle(220, 16, 8, UI_WHITE);

    m_infoPage = 0;
    renderInfoLine(s);
    m_nextInfoPageMs = millis() + 4000UL;

    int hour = timeOk ? t.tm_hour : 0;
    if (!s.clock.format24h) { hour %= 12; if (!hour) hour = 12; }
    if (timeOk) snprintf(timeStr, sizeof(timeStr), "%02d:%02d", hour, t.tm_min);
    else strlcpy(timeStr, "--:--", sizeof(timeStr));
    if (s.clock.showSeconds) {
      drawClockFontInReservedArea(timeStr, 103, tc);
      char seconds[4] = "--";
      if (timeOk) snprintf(seconds, sizeof(seconds), "%02d", t.tm_sec);
      gfxPrint(184, 91, seconds, UI_YELLOW, 3);
    } else {
      drawFontCentered(timeStr, 103, &MontserratBold28pt7b, tc);
    }

    if (s.clock.showDate) gfxDrawCentered(dateStr, 149, 2, dc);

    if (m_weather.valid) {
      char temp[12], humid[12];
      formatTemp(m_weather.temp, s, temp, sizeof(temp));
      snprintf(humid, sizeof(humid), "%u%%", m_weather.humidity);
      drawThermometer(11, 177, tc);
      gfxPrint(35, 181, temp, UI_WHITE, 2);
      drawDroplet(10, 207, ac);
      gfxPrint(35, 211, humid, UI_WHITE, 2);
    } else {
      gfxPrint(10, 184, "TEMP --", UI_MUTED, 2);
      gfxPrint(10, 212, "HUM --", UI_MUTED, 2);
    }

    m_lastGithubRevision = UINT32_MAX;
    m_githubFrame = 0;
    renderGithubSummary(s);
    m_nextGithubFrameMs = millis() + 1000UL;
  } else if (s.clock.theme == 1) {
    // Screen 2: clock + current weather + location + humidity + date + IP.
    gfxFillRoundRect(6, 6, 228, 78, 10, UI_PANEL);
    gfxDrawRoundRect(6, 6, 228, 78, 10, tc);
    drawFontCentered(timeStr, 43, timeFont, tc);
    if (s.clock.showDate) gfxDrawCentered(dateStr, 70, 1, dc);

    gfxFillRoundRect(6, 90, 228, 104, 10, UI_PANEL_BLUE);
    gfxDrawRoundRect(6, 90, 228, 104, 10, ac);
    if (m_weather.valid) {
      char city[18], temp[12], humid[12], desc[22];
      fitUpper(m_weather.city, city, sizeof(city), 16);
      formatTemp(m_weather.temp, s, temp, sizeof(temp));
      snprintf(humid, sizeof(humid), "HUM %u%%", m_weather.humidity);
      fitUpper(m_weather.description, desc, sizeof(desc), 20);
      gfxPrint(14, 100, city, UI_WHITE, 2);
      drawWeatherMark(206, 112, m_weather.weatherCode, ac);
      gfxPrint(14, 127, temp, tc, 3);
      gfxPrint(122, 132, humid, dc, 1);
      gfxPrint(14, 163, desc, UI_MUTED, 1);
    } else {
      gfxDrawCentered("WEATHER UNAVAILABLE", 119, 2, ac);
      gfxDrawCentered(m_weather.error.length() ? m_weather.error.c_str() : "Connecting", 153, 1, UI_MUTED);
    }
    gfxFillRoundRect(6, 200, 228, 34, 9, UI_DARK);
    gfxDrawRoundRect(6, 200, 228, 34, 9, UI_BORDER);
    gfxDrawCentered(ip, 211, 2, ac);
  } else {
    // Screen 3: real provider values for today, tomorrow and the day after.
    static const uint16_t fills[3] = {0x08A4, 0x1063, 0x08A4};
    for (uint8_t i = 0; i < 3; i++) {
      int y = 6 + i * 78;
      uint16_t color = i == 0 ? tc : (i == 1 ? dc : ac);
      gfxFillRoundRect(6, y, 228, 72, 8, fills[i]);
      gfxDrawRoundRect(6, y, 228, 72, 8, color);
      const ForecastDay& day = m_weather.days[i];
      if (!m_weather.valid || !day.valid) {
        gfxPrint(16, y + 14, i == 0 ? "TODAY" : (i == 1 ? "TOMORROW" : "DAY +2"), color, 2);
        gfxPrint(16, y + 43, m_weather.error.length() ? m_weather.error.c_str() : "SYNCING", UI_MUTED, 1);
        continue;
      }
      char label[16], temp[12], range[22], desc[17];
      dateLabel(day.date, i, label, sizeof(label));
      formatTemp(day.temp, s, temp, sizeof(temp));
      snprintf(range, sizeof(range), "%.0f..%.0f  H%u%%", day.minTemp, day.maxTemp, day.humidity);
      fitUpper(day.description, desc, sizeof(desc), 15);
      gfxPrint(14, y + 10, label, color, 2);
      gfxPrint(154, y + 10, temp, UI_WHITE, 2);
      gfxPrint(14, y + 36, desc, UI_WHITE, 1);
      gfxPrint(14, y + 53, range, UI_MUTED, 1);
      drawWeatherMark(210, y + 49, day.weatherCode, color);
    }
  }
  g->setFont(nullptr);
  m_lastTheme = s.clock.theme;
  if (timeOk) {
    m_lastTick = s.clock.showSeconds ? t.tm_sec : t.tm_min;
    m_lastYday = t.tm_yday;
    strlcpy(m_lastTime, timeStr, sizeof(m_lastTime));
  } else {
    m_lastTick = -1;
    m_lastYday = -1;
    m_lastTime[0] = '\0';
  }
}
