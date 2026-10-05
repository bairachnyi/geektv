#include "ClockMode.h"
#include "Gfx.h"
#include "Net.h"
#include "Clock.h"
#include "Platform.h"
#include "MontserratBold.h"
#include "OfficeQuotes.h"
#include "CyrillicFont8x14.h"
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

// ---------------------------------------------------------------------------
// Fixed-cell digit rendering.
//
// Proportional fonts like Montserrat Bold give each digit a different bitmap
// width (e.g. "1" is 11 px while "0" is 19 px at 28pt).  Centering by the
// *current* text bounds therefore shifts the string left/right whenever a
// narrow digit replaces a wide one (09→10, 19→20, 23→00, …).
//
// The single drawFixedDigits() helper renders every digit and dash character
// inside a fixed-width cell derived from the widest digit bitmap.  Only ':'
// keeps its natural advance.  The result is a rock-solid layout on every tick:
//   --:-- and 00:00 occupy identical space, colons stay put.
//
// Cell width uses gl.width (actual bitmap pixels), not xAdvance.  Per-glyph
// centering accounts for xOffset (e.g. 28pt '3' has xOffset = -1):
//   cursor_x = cellStart + (cellW - gl.width) / 2 - gl.xOffset
//
// Glyph arrays in MontserratBold.h are in SRAM (no PROGMEM annotation), so
// ordinary struct reads are safe on all targets.
// ---------------------------------------------------------------------------

struct FontDigitMetrics {
  uint8_t cellW;       // fixed cell width = max digit bitmap width
  uint8_t maxDigitH;   // tallest digit bitmap height
  int8_t  minYOffset;  // most-negative yOffset among digits (= top of glyph)
  uint8_t colonW;      // ':' natural xAdvance
};

static FontDigitMetrics measureDigitMetrics(const GFXfont* font) {
  FontDigitMetrics m = {};
  const GFXglyph* glyphs = font->glyph;
  uint8_t first = font->first;
  for (char c = '0'; c <= '9'; c++) {
    const GFXglyph& gl = glyphs[(uint8_t)c - first];
    if (gl.width > m.cellW) m.cellW = gl.width;
    if (gl.height > m.maxDigitH) m.maxDigitH = gl.height;
    if (gl.yOffset < m.minYOffset) m.minYOffset = gl.yOffset;
  }
  m.colonW = glyphs[':' - first].xAdvance;
  return m;
}

// Cached metrics for the Montserrat bitmap fonts.
static const FontDigitMetrics& cachedMetrics(const GFXfont* font) {
  static const FontDigitMetrics m28 = measureDigitMetrics(&MontserratBold28pt7b);
  static const FontDigitMetrics m40 = measureDigitMetrics(&MontserratBold40pt7b);
  static const FontDigitMetrics m54 = measureDigitMetrics(&MontserratBold54pt7b);
  if (font == &MontserratBold54pt7b) return m54;
  return (font == &MontserratBold40pt7b) ? m40 : m28;
}

// Total pixel width of a time string under the fixed-cell layout.
// Digits and dashes use the digit cell; only ':' keeps its natural advance.
static int fixedTimeWidth(const char* txt, const FontDigitMetrics& m) {
  int w = 0;
  for (const char* p = txt; *p; ++p) {
    if (*p == ':') w += m.colonW;
    else           w += m.cellW;   // digits, dashes, anything else
  }
  return w;
}

// Render each character in a fixed cell, centered within [areaX .. areaX+areaW).
// Uses a stable baseline derived from the font's maximum digit ascent.
static void drawFixedDigits(const char* txt, int yCenter, const GFXfont* font,
                            uint16_t color, int areaX, int areaW) {
  Arduino_GFX* g = gfxDev();
  if (!g || !txt || !txt[0]) return;
  g->setFont(font);
  g->setTextSize(1);
  g->setTextWrap(false);
  g->setTextColor(color);

  const FontDigitMetrics& m = cachedMetrics(font);

  int totalW = fixedTimeWidth(txt, m);
  int x = areaX + max(0, (areaW - totalW) / 2);

  // Stable baseline: the visual center of the tallest digit is placed at
  // yCenter.  minYOffset (negative) gives the ascent above the baseline.
  int baseline = yCenter - (int)m.minYOffset - (int)m.maxDigitH / 2;

  for (const char* p = txt; *p; ++p) {
    char ch = *p;
    const GFXglyph& gl = font->glyph[(uint8_t)ch - font->first];

    if (ch == ':') {
      // Colon keeps its natural advance; center bitmap within that.
      int pad = ((int)m.colonW - (int)gl.width) / 2 - (int)gl.xOffset;
      g->setCursor(x + pad, baseline);
      g->print(ch);
      x += m.colonW;
    } else {
      // Digits and dashes share the fixed digit cell.
      int pad = ((int)m.cellW - (int)gl.width) / 2 - (int)gl.xOffset;
      g->setCursor(x + pad, baseline);
      g->print(ch);
      x += m.cellW;
    }
  }
}

// Full-width centered clock digits (theme 0 without seconds, and theme 1).
static void drawFontCentered(const char* txt, int yCenter, const GFXfont* font, uint16_t color) {
  drawFixedDigits(txt, yCenter, font, color, 0, 240);
}

// Clock digits centered inside x=8..164 (theme 0 with seconds visible).
static void drawClockFontInReservedArea(const char* text, int yCenter, uint16_t color) {
  drawFixedDigits(text, yCenter, &MontserratBold28pt7b, color, 8, 156);
}

// Incremental per-cell redraw: only clears and repaints cells where the
// character actually changed, within a tight glyph-band rectangle.  Preserves
// surrounding panel borders, date lines and unchanged digits.  oldTxt and
// newTxt must be the same length (caller ensures matching placeholder format).
static void updateFixedDigits(const char* oldTxt, const char* newTxt,
                              int yCenter, const GFXfont* font,
                              uint16_t fgColor, uint16_t bgColor,
                              int areaX, int areaW) {
  Arduino_GFX* g = gfxDev();
  if (!g || !newTxt || !newTxt[0]) return;
  if (!oldTxt || !oldTxt[0] || strlen(oldTxt) != strlen(newTxt)) {
    // Length mismatch (shouldn't happen with matching placeholders) — full draw.
    int bandTop, bandH;
    {
      const FontDigitMetrics& m = cachedMetrics(font);
      int baseline = yCenter - (int)m.minYOffset - (int)m.maxDigitH / 2;
      bandTop = baseline + (int)m.minYOffset;
      bandH   = (int)m.maxDigitH;
    }
    g->fillRect(areaX, bandTop, areaW, bandH, bgColor);
    drawFixedDigits(newTxt, yCenter, font, fgColor, areaX, areaW);
    return;
  }

  g->setFont(font);
  g->setTextSize(1);
  g->setTextWrap(false);

  const FontDigitMetrics& m = cachedMetrics(font);

  int totalW = fixedTimeWidth(newTxt, m);
  int x = areaX + max(0, (areaW - totalW) / 2);
  int baseline = yCenter - (int)m.minYOffset - (int)m.maxDigitH / 2;

  // Tight glyph-band: only clear the pixel rows actually used by glyphs.
  int bandTop = baseline + (int)m.minYOffset;
  int bandH   = (int)m.maxDigitH;

  const char* pOld = oldTxt;
  const char* pNew = newTxt;
  while (*pNew) {
    char chOld = *pOld;
    char chNew = *pNew;
    bool isColon = (chNew == ':');
    uint8_t cellW = isColon ? m.colonW : m.cellW;

    if (chOld != chNew) {
      // Erase just this cell's glyph band.
      g->fillRect(x, bandTop, (int)cellW, bandH, bgColor);

      // Redraw the new glyph.
      const GFXglyph& gl = font->glyph[(uint8_t)chNew - font->first];
      int pad;
      if (isColon)
        pad = ((int)m.colonW - (int)gl.width) / 2 - (int)gl.xOffset;
      else
        pad = ((int)m.cellW - (int)gl.width) / 2 - (int)gl.xOffset;

      g->setTextColor(fgColor);
      g->setCursor(x + pad, baseline);
      g->print(chNew);
    }

    x += cellW;
    ++pOld;
    ++pNew;
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
  return s.clock.showSeconds ? &MontserratBold28pt7b : &MontserratBold54pt7b;
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
  bool sunny = !strcmp(kind, "SUN") || !strcmp(kind, "PART");
  bool wet = !strcmp(kind, "RAIN") || !strcmp(kind, "SNOW");
  bool cloudy = !sunny || !strcmp(kind, "PART");
  if (sunny) {
    g->fillCircle(cx - (cloudy ? 5 : 0), cy - 2, 6, color);
    static const int8_t rayX[8] = {0, 6, 9, 6, 0, -6, -9, -6};
    static const int8_t rayY[8] = {-9, -6, 0, 6, 9, 6, 0, -6};
    for (uint8_t ray = 0; ray < 8; ray++) {
      int ox = cx - (cloudy ? 5 : 0);
      int x1 = ox + rayX[ray];
      int y1 = cy - 2 + rayY[ray];
      int x2 = ox + (rayX[ray] * 4) / 3;
      int y2 = cy - 2 + (rayY[ray] * 4) / 3;
      g->drawLine(x1, y1, x2, y2, color);
    }
  }
  if (cloudy) {
    int ox = sunny ? 5 : 0;
    g->fillCircle(cx - 7 + ox, cy + 3, 5, color);
    g->fillCircle(cx + ox, cy - 1, 7, color);
    g->fillCircle(cx + 8 + ox, cy + 3, 5, color);
    g->fillRect(cx - 12 + ox, cy + 3, 25, 7, color);
  }
  if (wet) {
    for (int drop = -1; drop <= 1; drop++) {
      int x = cx + drop * 7;
      if (!strcmp(kind, "SNOW")) {
        g->drawLine(x - 2, cy + 10, x + 2, cy + 14, color);
        g->drawLine(x + 2, cy + 10, x - 2, cy + 14, color);
      } else {
        g->drawLine(x, cy + 10, x - 2, cy + 15, color);
        g->drawLine(x - 2, cy + 15, x + 2, cy + 15, color);
      }
    }
  }
}

static void drawWindMark(int cx, int cy, uint16_t color) {
  Arduino_GFX* g = gfxDev();
  if (!g) return;
  g->drawFastHLine(cx - 12, cy - 6, 17, color);
  g->drawFastHLine(cx - 12, cy + 1, 12, color);
  g->drawFastHLine(cx - 12, cy + 8, 17, color);
  g->drawLine(cx + 5, cy - 6, cx + 10, cy - 2, color);
  g->drawLine(cx + 10, cy - 2, cx + 5, cy + 2, color);
  g->drawLine(cx, cy + 8, cx + 5, cy + 12, color);
  g->drawLine(cx + 5, cy + 12, cx, cy + 16, color);
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

static uint32_t monotonicUptimeSec() {
  static uint32_t lastMs = 0;
  static uint64_t totalMs = 0;
  uint32_t now = millis();
  totalMs += (uint32_t)(now - lastMs);
  lastMs = now;
  return (uint32_t)(totalMs / 1000ULL);
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
  m_nextSysUpdateMs = 0;
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
  m_nextSysUpdateMs = 0;
  render(s);
}

void ClockMode::wake(const Settings& s) {
  if (s.clock.theme == 3) {
    m_quoteIdx++;
  }
  m_lastTick = -1;
  m_lastYday = -1;
  m_lastTheme = 0xFF;
  m_lastTime[0] = '\0';
  m_fullRepaint = true;
  m_infoPage = 0;
  m_nextInfoPageMs = 0;
  m_nextSysUpdateMs = 0;
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
  const uint32_t minHeap = 3000;
  const uint32_t minBlock = 1800;
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

  monotonicUptimeSec();
  if (s.clock.theme == 0 && (int32_t)(nowMs - m_nextSysUpdateMs) >= 0) {
    renderSystemSummary(s);
    m_nextSysUpdateMs = nowMs + 10000UL;
  }
  if (s.clock.theme == 0 && (int32_t)(nowMs - m_nextInfoPageMs) >= 0) {
    m_infoPage = (uint8_t)((m_infoPage + 1) % 2);
    renderInfoLine(s);
    m_nextInfoPageMs = nowMs + 4000UL;
  }

  struct tm t;
  if (!clockNow(t)) return;

  // Theme 4 (Dino Pet): update animation frame smoothly without full screen repaint
  if (s.clock.theme == 4) {
    if ((int32_t)(nowMs - m_nextDinoAnimMs) >= 0) {
      m_dinoFrame = (m_dinoFrame + 1) & 7;
      m_nextDinoAnimMs = nowMs + 1200UL;
      renderDinoAnim(s);
    }
    // Update minute clock in top bar
    if (t.tm_min != m_lastTick || m_lastTheme != s.clock.theme) {
      m_lastTick = t.tm_min;
      m_lastTheme = s.clock.theme;
      render(s);
    }
    return;
  }

  // The forecast and office memo have no second/minute-dependent pixels. They are repainted only
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
    drawWindMark(19, 40, m_weather.valid ? s.clock.accentColor : UI_MUTED);
    if (m_weather.valid) snprintf(line, sizeof(line), "%.1f KM/H", m_weather.windKph);
    else strlcpy(line, "-- KM/H", sizeof(line));
    gfxPrint(39, 32, line, m_weather.valid ? s.clock.dateColor : UI_MUTED, 2);
    return;
  } else {
    String address = netMode() == NET_AP ? "192.168.4.1" : netIP();
    snprintf(line, sizeof(line), "IP %s", address.c_str());
    color = s.clock.accentColor;
  }
  gfxPrint(10, 32, line, color, 2);
}

void ClockMode::renderSystemSummary(const Settings& s) {
  if (s.clock.theme != 0) return;
  Arduino_GFX* g = gfxDev();
  if (!g) return;
  g->fillRect(104, 175, 136, 65, s.clock.bgColor);

  if (!netConnected()) {
    // 14 characters * 6px = 84px <= 128px available width
    gfxPrint(112, 194, "NO WIFI SIGNAL", UI_MUTED, 1);
  } else {
    gfxPrint(112, 194, "WIFI", s.clock.accentColor, 2);

    int rssi = netRSSI();
    int activeBars = 1;
    uint16_t barColor = 0x8000; // Burgundy / dark red (#800000)

    if (rssi >= -55) {
      activeBars = 5;
      barColor = C_GREEN; // 0x07E0
    } else if (rssi >= -65) {
      activeBars = 4;
      barColor = 0x7FE0;  // Light green (#7eff00)
    } else if (rssi >= -75) {
      activeBars = 3;
      barColor = C_YELLOW; // 0xFFE0
    } else if (rssi >= -85) {
      activeBars = 2;
      barColor = C_RED;    // 0xF800
    } else {
      activeBars = 1;
      barColor = 0x8000;   // Burgundy / dark red
    }

    // 5 bars starting at x = 172, total width 5*6 = 30px (x=172..201 <= 239)
    // Heights 4, 7, 10, 13, 16px anchored at bottom y = 208
    int bx = 172;
    for (int b = 1; b <= 5; b++) {
      int barH = b * 3 + 1; // 4, 7, 10, 13, 16
      int by = 208 - barH;
      uint16_t c = (b <= activeBars) ? barColor : UI_BORDER;
      g->fillRect(bx, by, 4, barH, c);
      bx += 6;
    }
  }

  uint32_t up = monotonicUptimeSec();
  uint32_t d = up / 86400, h = (up % 86400) / 3600, m = (up % 3600) / 60;
  char upStr[16];
  if (d >= 100) snprintf(upStr, sizeof(upStr), "UP %ud", (unsigned)d);
  else if (d > 0) snprintf(upStr, sizeof(upStr), "UP %ud %02uh", (unsigned)d, (unsigned)h);
  else snprintf(upStr, sizeof(upStr), "UP %uh %02um", (unsigned)h, (unsigned)m);
  gfxPrint(112, 218, upStr, s.clock.dateColor, 2);
}

// UTF-8 decoding and Cyrillic/ASCII mixed text rendering (8x14 glyphs)
static uint32_t utf8NextCodepoint(const char*& p) {
  if (!p || !*p) return 0;
  uint8_t c = (uint8_t)*p++;
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0) {
    uint8_t c2 = (uint8_t)*p++;
    return ((c & 0x1F) << 6) | (c2 & 0x3F);
  }
  if ((c & 0xF0) == 0xE0) {
    uint8_t c2 = (uint8_t)*p++;
    uint8_t c3 = (uint8_t)*p++;
    return ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
  }
  return c;
}

static size_t utf8CharCount(const char* s) {
  size_t count = 0;
  const char* p = s;
  while (*p) {
    utf8NextCodepoint(p);
    count++;
  }
  return count;
}

static void drawCyrillicChar(int x, int y, uint32_t cp, uint16_t color, uint8_t size = 1) {
  Arduino_GFX* g = gfxDev();
  if (!g) return;

  int idx = cyrillicGlyphIndex(cp);
  if (idx >= 0) {
    // 8x14 Cyrillic bitmap from CYRILLIC_FONT_8X14
    for (int r = 0; r < 14; r++) {
      uint8_t rowByte = pgm_read_byte(&CYRILLIC_FONT_8X14[idx][r]);
      for (int c = 0; c < 8; c++) {
        if (rowByte & (0x80 >> c)) {
          if (size == 1) g->drawPixel(x + c, y + r, color);
          else g->fillRect(x + c * size, y + r * size, size, size, color);
        }
      }
    }
  } else if (cp < 0x80) {
    // Standard ASCII character: draw using built-in font scaled to match
    g->setFont(nullptr);
    g->setTextSize(size);
    g->setTextColor(color);
    g->setCursor(x, y + 3 * size); // align with 14px height
    char ch = (char)cp;
    g->print(ch);
  }
}

static void drawUtf8String(int x, int y, const char* s, uint16_t color, uint8_t size = 1) {
  const char* p = s;
  int curX = x;
  int charAdvance = 8 * size;
  while (*p) {
    uint32_t cp = utf8NextCodepoint(p);
    drawCyrillicChar(curX, y, cp, color, size);
    curX += charAdvance;
  }
}

void ClockMode::renderOfficeMemo(const Settings& s) {
  Arduino_GFX* g = gfxDev();
  if (!g) return;

  uint16_t bg = s.clock.bgColor;
  uint16_t tc = s.clock.timeColor;
  uint16_t dc = s.clock.dateColor;

  g->fillScreen(bg);

  size_t qCount = OFFICE_QUOTES_COUNT;
  if (!qCount) return;
  size_t idx = m_quoteIdx % qCount;
  const char* qTextP = (const char*)pgm_read_ptr(&OFFICE_QUOTES[idx].text);
  const char* qAuthorP = (const char*)pgm_read_ptr(&OFFICE_QUOTES[idx].author);

  char words[450];
  strncpy_P(words, qTextP, sizeof(words) - 1);
  words[sizeof(words) - 1] = '\0';

  char authorName[64];
  strncpy_P(authorName, qAuthorP, sizeof(authorName) - 1);
  authorName[sizeof(authorName) - 1] = '\0';

  size_t charCount = utf8CharCount(words);
  // In 240px width:
  // With 8px glyph width: size 1 = 8px/char -> up to 28 chars/line
  // With size 2: 16px/char -> up to 14 chars/line (only for very short quotes <= 45 chars)
  uint8_t fontSz = (charCount <= 45) ? 2 : 1;
  size_t maxChars = (fontSz == 2) ? 14 : 26;
  int charW = 8 * fontSz;
  int lineH = (fontSz == 2) ? 20 : 16;

  char lines[12][96];
  uint8_t lineCount = 0;
  char curLine[96] = "";

  char* p = strtok(words, " ");
  while (p && lineCount < 10) {
    size_t wordLen = utf8CharCount(p);
    size_t curLen = utf8CharCount(curLine);
    if (curLen == 0) {
      strlcpy(curLine, p, sizeof(curLine));
    } else if (curLen + 1 + wordLen <= maxChars) {
      strlcat(curLine, " ", sizeof(curLine));
      strlcat(curLine, p, sizeof(curLine));
    } else {
      strlcpy(lines[lineCount++], curLine, sizeof(lines[0]));
      strlcpy(curLine, p, sizeof(curLine));
    }
    p = strtok(nullptr, " ");
  }
  if (curLine[0] && lineCount < 10) {
    strlcpy(lines[lineCount++], curLine, sizeof(lines[0]));
  }

  char authorBuf[80];
  snprintf(authorBuf, sizeof(authorBuf), "— %s", authorName);

  uint8_t authorSz = 1;
  int authorLineH = 14;
  int gap = (fontSz == 2) ? 16 : 12;
  int totalH = lineCount * lineH + gap + authorLineH;

  int startY = (240 - totalH) / 2;
  if (startY < 10) startY = 10;

  // Render centered quote lines
  for (uint8_t i = 0; i < lineCount; i++) {
    int w = utf8CharCount(lines[i]) * charW;
    int x = (240 - w) / 2;
    if (x < 4) x = 4;
    drawUtf8String(x, startY + i * lineH, lines[i], tc, fontSz);
  }

  // Render centered author line
  int authorW = utf8CharCount(authorBuf) * (8 * authorSz);
  int authorX = (240 - authorW) / 2;
  if (authorX < 4) authorX = 4;
  int authorY = startY + lineCount * lineH + gap;
  drawUtf8String(authorX, authorY, authorBuf, dc, authorSz);
}

// ---------------------------------------------------------------------------
// Dino Pet Sprites & Renderer
// ---------------------------------------------------------------------------
static void draw1bppBitmap(int x, int y, const uint8_t* bmp, int w, int h, uint16_t fg, uint8_t scale = 1) {
  Arduino_GFX* g = gfxDev();
  if (!g) return;
  int bytesPerRow = (w + 7) / 8;
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      uint8_t b = pgm_read_byte(&bmp[row * bytesPerRow + (col / 8)]);
      if (b & (0x80 >> (col % 8))) {
        if (scale == 1) g->drawPixel(x + col, y + row, fg);
        else g->fillRect(x + col * scale, y + row * scale, scale, scale, fg);
      }
    }
  }
}

// Chrome-style Dino (20x22 pixels)
// Frame 0: Left foot down
static const uint8_t DINO_RUN0[] PROGMEM = {
  0x00, 0x7F, 0x00, // ........ .1111111 ........
  0x00, 0x77, 0x00, // ........ .1110111 ........ (eye at col 12)
  0x00, 0x7F, 0x00, // ........ .1111111 ........
  0x00, 0x7F, 0x00, // ........ .1111111 ........
  0x00, 0x7F, 0x00, // ........ .1111111 ........
  0x00, 0x7C, 0x00, // ........ .1111100 ........
  0x00, 0x7F, 0x00, // ........ .1111111 ........ (snout/mouth)
  0x00, 0x70, 0x00, // ........ .1110000 ........
  0x00, 0x78, 0x00, // ........ .1111000 ........
  0x01, 0xF8, 0x00, // .......1 11111000 ........ (neck)
  0x03, 0xF8, 0x00, // ......11 11111000 ........
  0x07, 0xFE, 0x00, // .....111 11111110 ........ (body & arm)
  0x0F, 0xFC, 0x00, // ....1111 11111100 ........
  0x1F, 0xF8, 0x00, // ...11111 11111000 ........
  0x3F, 0xF8, 0x00, // ..111111 11111000 ........
  0x7F, 0xF0, 0x00, // .1111111 11110000 ........
  0x3F, 0xE0, 0x00, // ..111111 11100000 ........
  0x1F, 0xC0, 0x00, // ...11111 11000000 ........
  0x0F, 0x80, 0x00, // ....1111 10000000 ........
  0x07, 0x80, 0x00, // .....111 10000000 ........
  0x06, 0x80, 0x00, // .....110 10000000 ........
  0x06, 0x00, 0x00  // .....110 ........ ........ (leg 0)
};

// Frame 1: Right foot down
static const uint8_t DINO_RUN1[] PROGMEM = {
  0x00, 0x7F, 0x00,
  0x00, 0x77, 0x00,
  0x00, 0x7F, 0x00,
  0x00, 0x7F, 0x00,
  0x00, 0x7F, 0x00,
  0x00, 0x7C, 0x00,
  0x00, 0x7F, 0x00,
  0x00, 0x70, 0x00,
  0x00, 0x78, 0x00,
  0x01, 0xF8, 0x00,
  0x03, 0xF8, 0x00,
  0x07, 0xFE, 0x00,
  0x0F, 0xFC, 0x00,
  0x1F, 0xF8, 0x00,
  0x3F, 0xF8, 0x00,
  0x7F, 0xF0, 0x00,
  0x3F, 0xE0, 0x00,
  0x1F, 0xC0, 0x00,
  0x0F, 0x80, 0x00,
  0x07, 0x80, 0x00,
  0x01, 0x80, 0x00,
  0x00, 0xC0, 0x00
};

// Dino Sleeping posture (20x16 pixels)
static const uint8_t DINO_SLEEP[] PROGMEM = {
  0x00, 0x00, 0x00,
  0x00, 0x00, 0x00,
  0x00, 0x00, 0x00,
  0x00, 0x00, 0x00,
  0x00, 0x1F, 0x00,
  0x00, 0x1D, 0x00, // closed eye
  0x00, 0x1F, 0x00,
  0x01, 0xFC, 0x00,
  0x07, 0xFE, 0x00,
  0x0F, 0xFE, 0x00,
  0x1F, 0xFE, 0x00,
  0x3F, 0xFE, 0x00,
  0x7F, 0xFE, 0x00,
  0x7F, 0xFE, 0x00,
  0x3F, 0xFC, 0x00,
  0x1F, 0xF8, 0x00
};

// Cactus obstacle prop (10x16 pixels)
static const uint8_t CACTUS_SPRITE[] PROGMEM = {
  0x0C, 0x00, // ....1100 ........
  0x0C, 0x00,
  0x6C, 0x00, // .1101100 ........
  0x6C, 0x00,
  0x6D, 0x80, // .1101101 1.......
  0x7F, 0x80, // .1111111 1.......
  0x3F, 0x80, // ..111111 1.......
  0x0F, 0x80, // ....1111 1.......
  0x0C, 0x00,
  0x0C, 0x00,
  0x0C, 0x00,
  0x0C, 0x00,
  0x0C, 0x00,
  0x0C, 0x00,
  0x0C, 0x00,
  0x0C, 0x00
};

// Umbrella prop (16x16 pixels)
static const uint8_t UMBRELLA_SPRITE[] PROGMEM = {
  0x01, 0x80, // .......1 1.......
  0x07, 0xE0, // .....111 111.....
  0x1F, 0xF8, // ...11111 11111...
  0x3F, 0xFC, // ..111111 111111..
  0x7F, 0xFE, // .1111111 1111111.
  0xFF, 0xFF, // 11111111 11111111
  0x01, 0x80, // .......1 1.......
  0x01, 0x80,
  0x01, 0x80,
  0x01, 0x80,
  0x01, 0x80,
  0x01, 0x80,
  0x01, 0x80,
  0x03, 0x80,
  0x03, 0x00,
  0x01, 0x00
};

// Sunglasses (12x5 pixels)
static const uint8_t SUNGLASSES_SPRITE[] PROGMEM = {
  0xFF, 0xF0, // 11111111 1111....
  0xEF, 0xB0, // 11101111 1011....
  0xEF, 0xB0,
  0x6F, 0x30, // .1101111 ..11....
  0x3E, 0x00  // ..111110 ........
};

// Wrench tool (10x12 pixels)
static const uint8_t WRENCH_SPRITE[] PROGMEM = {
  0x73, 0x00, // .1110011 ........
  0xF7, 0x80, // 11110111 1.......
  0xEF, 0x80, // 11101111 1.......
  0x7F, 0x00, // .1111111 ........
  0x3E, 0x00, // ..111110 ........
  0x1C, 0x00, // ...11100 ........
  0x38, 0x00, // ..111000 ........
  0x70, 0x00, // .1110000 ........
  0xE0, 0x00, // 11100000 ........
  0xDC, 0x00, // 11011100 ........
  0xFC, 0x00, // 11111100 ........
  0x78, 0x00  // .1111000 ........
};

void ClockMode::renderDinoPet(const Settings& s) {
  Arduino_GFX* g = gfxDev();
  if (!g) return;

  uint16_t bg = s.clock.bgColor;
  uint16_t tc = s.clock.timeColor;
  uint16_t ac = s.clock.accentColor;
  uint16_t dc = s.clock.dateColor;

  g->fillScreen(bg);

  // 1. Top Mini-Header (y=6..38, h=32)
  g->fillRoundRect(6, 6, 228, 32, 6, UI_PANEL);
  g->drawRoundRect(6, 6, 228, 32, 6, UI_BORDER);

  struct tm t;
  bool ok = clockNow(t);
  char timeBuf[12];
  int hour = ok ? t.tm_hour : 12;
  if (!s.clock.format24h) { hour %= 12; if (!hour) hour = 12; }
  if (ok) snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", hour, t.tm_min);
  else strlcpy(timeBuf, "--:--", sizeof(timeBuf));

  gfxPrint(14, 14, timeBuf, tc, 2);

  // Weather mark or temperature in header
  if (m_weather.valid) {
    char tempBuf[10];
    formatTemp(m_weather.temp, s, tempBuf, sizeof(tempBuf));
    gfxPrint(90, 14, tempBuf, UI_WHITE, 2);
    drawWeatherMark(145, 22, m_weather.weatherCode, ac);
  } else {
    gfxPrint(90, 17, "OFFLINE", UI_MUTED, 1);
  }

  // Mini Wi-Fi bars in top header (x=175..225)
  bool wifiOk = netConnected();
  if (!wifiOk) {
    gfxPrint(170, 17, "NO WIFI", UI_RED, 1);
  } else {
    int rssi = netRSSI();
    int activeBars = (rssi >= -55) ? 5 : (rssi >= -65) ? 4 : (rssi >= -75) ? 3 : (rssi >= -85) ? 2 : 1;
    uint16_t barColor = (activeBars >= 4) ? UI_GREEN : (activeBars == 3) ? UI_YELLOW : UI_RED;
    int bx = 192;
    for (int b = 1; b <= 5; b++) {
      int barH = b * 2 + 2; // 4, 6, 8, 10, 12
      int by = 28 - barH;
      g->fillRect(bx, by, 3, barH, (b <= activeBars) ? barColor : UI_BORDER);
      bx += 5;
    }
  }

  // 2. Playfield / World Scene (y=42..196)
  // Ground line at y = 175
  const int groundY = 175;
  g->drawFastHLine(6, groundY, 228, UI_BORDER);
  g->drawFastHLine(10, groundY + 1, 220, UI_DARK);

  // Little stones / ground dots
  g->fillRect(20, groundY + 3, 3, 1, UI_BORDER);
  g->fillRect(65, groundY + 4, 2, 1, UI_BORDER);
  g->fillRect(130, groundY + 3, 4, 1, UI_BORDER);
  g->fillRect(185, groundY + 5, 3, 1, UI_BORDER);
  g->fillRect(215, groundY + 3, 2, 1, UI_BORDER);

  // Determine Dino state
  bool isNight = ok && (t.tm_hour >= 22 || t.tm_hour < 7);
  const char* wKind = m_weather.valid ? weatherKind(m_weather.weatherCode) : "";
  bool isRainy = !strcmp(wKind, "RAIN") || !strcmp(wKind, "SNOW");
  bool isHot = m_weather.valid && ((s.clock.weatherUnits == "f" ? (m_weather.temp >= 85) : (m_weather.temp >= 29)) || !strcmp(wKind, "SUN"));

  const char* statusMsg = "PET: CHILLING";
  uint16_t dinoColor = UI_GREEN;

  // Draw background environment props (clouds / sun / moon / rain)
  if (isNight) {
    statusMsg = "PET: SLEEPING";
    // Crescent Moon in sky
    g->fillCircle(200, 65, 10, UI_YELLOW);
    g->fillCircle(196, 63, 8, bg);
    // Stars
    g->drawPixel(35, 60, UI_WHITE);
    g->drawPixel(80, 50, UI_WHITE);
    g->drawPixel(140, 68, UI_WHITE);
    g->drawPixel(165, 52, UI_WHITE);
  } else if (isRainy) {
    statusMsg = "PET: RAINY DAY";
    // Cloud
    g->fillCircle(50, 60, 8, UI_MUTED);
    g->fillCircle(62, 56, 11, UI_MUTED);
    g->fillCircle(75, 60, 8, UI_MUTED);
    g->fillRect(42, 60, 42, 8, UI_MUTED);
    // Raindrops
    for (int r = 0; r < 6; r++) {
      int rx = 35 + r * 30 + ((m_dinoFrame & 1) ? 4 : 0);
      int ry = 75 + (r % 3) * 15;
      g->drawLine(rx, ry, rx - 2, ry + 6, ac);
    }
  } else if (isHot) {
    statusMsg = "PET: SUNBATHING";
    // Bright sun with rays
    g->fillCircle(35, 65, 9, UI_YELLOW);
    for (int deg = 0; deg < 8; deg++) {
      static const int8_t rx[8] = {0, 10, 14, 10, 0, -10, -14, -10};
      static const int8_t ry[8] = {-14, -10, 0, 10, 14, 10, 0, -10};
      g->drawLine(35 + rx[deg]*3/4, 65 + ry[deg]*3/4, 35 + rx[deg], 65 + ry[deg], UI_YELLOW);
    }
  } else {
    // Normal daytime clouds
    g->fillCircle(180, 62, 7, 0x31A6);
    g->fillCircle(190, 58, 9, 0x31A6);
    g->fillCircle(202, 62, 7, 0x31A6);
    g->fillRect(173, 62, 36, 7, 0x31A6);
  }

  // Draw decorative cacti on edges
  draw1bppBitmap(12, groundY - 32, CACTUS_SPRITE, 10, 16, 0x2444, 2);
  draw1bppBitmap(206, groundY - 32, CACTUS_SPRITE, 10, 16, 0x2444, 2);

  // Position Dino centered horizontally: 20px wide * 3 = 60px, 22px high * 3 = 66px
  // Center at x = (240 - 60)/2 = 90. dinoY = 175 - 66 = 109.
  int dinoX = 90;
  int dinoY = groundY - 66; // 109

  if (!wifiOk) {
    statusMsg = "PET: NO SIGNAL!";
    dinoColor = UI_YELLOW;
    // Dino standing confused (scaled 3x: 60x66)
    draw1bppBitmap(dinoX, dinoY, DINO_RUN0, 20, 22, dinoColor, 3);
    // Holding wrench tool
    draw1bppBitmap(dinoX + 54, dinoY + 24, WRENCH_SPRITE, 10, 12, UI_WHITE, 2);
    // Question mark above head
    gfxPrint(dinoX + 24, dinoY - 20, "?", UI_RED, 2);
  } else if (isNight) {
    // Dino sleeping curled on the ground (scaled 3x: 60x48, 175 - 48 = 127)
    int sleepY = groundY - 48;
    draw1bppBitmap(dinoX, sleepY, DINO_SLEEP, 20, 16, 0x34A6, 3);
    // Animated "z Z Z" floating above head
    if (m_dinoFrame % 2 == 0) {
      gfxPrint(dinoX + 48, sleepY - 14, "z", UI_WHITE, 1);
      gfxPrint(dinoX + 58, sleepY - 26, "Z", ac, 2);
    } else {
      gfxPrint(dinoX + 46, sleepY - 18, "z", ac, 1);
      gfxPrint(dinoX + 56, sleepY - 30, "Z", UI_WHITE, 2);
    }
  } else if (isRainy) {
    // Dino standing under umbrella (scaled 3x)
    draw1bppBitmap(dinoX, dinoY, (m_dinoFrame & 1) ? DINO_RUN1 : DINO_RUN0, 20, 22, dinoColor, 3);
    // Umbrella above dino head
    draw1bppBitmap(dinoX + 14, dinoY - 32, UMBRELLA_SPRITE, 16, 16, UI_RED, 2);
  } else if (isHot) {
    // Dino wearing cool sunglasses (scaled 3x)
    draw1bppBitmap(dinoX, dinoY, (m_dinoFrame & 1) ? DINO_RUN1 : DINO_RUN0, 20, 22, dinoColor, 3);
    // Sunglasses over eye (scaled 3x: col 11 * 3 = +33, row 1 * 3 = +3)
    draw1bppBitmap(dinoX + 30, dinoY + 3, SUNGLASSES_SPRITE, 12, 5, UI_WHITE, 2);
  } else {
    // Normal happy daytime dino (scaled 3x)
    draw1bppBitmap(dinoX, dinoY, (m_dinoFrame & 1) ? DINO_RUN1 : DINO_RUN0, 20, 22, dinoColor, 3);
    if ((m_dinoFrame & 3) == 2) {
      // Little heart bubble or chirp occasionally
      gfxPrint(dinoX + 54, dinoY - 14, "<3", dc, 1);
    }
  }

  // 3. Bottom Status Card (y=196..234, h=38)
  g->fillRoundRect(6, 196, 228, 38, 6, UI_PANEL);
  g->drawRoundRect(6, 196, 228, 38, 6, UI_BORDER);

  // Status text on the left (e.g. "PET: SLEEPING" or "PET: CHILLING")
  gfxPrint(14, 206, statusMsg, tc, 2);

  // Clean right-side level tag without overlapping rectangular frame
  gfxPrint(184, 210, "LVL 99", ac, 1);
}

void ClockMode::renderDinoAnim(const Settings& s) {
  Arduino_GFX* g = gfxDev();
  if (!g) return;

  struct tm t;
  bool ok = clockNow(t);
  bool isNight = ok && (t.tm_hour >= 22 || t.tm_hour < 7);
  const char* wKind = m_weather.valid ? weatherKind(m_weather.weatherCode) : "";
  bool isRainy = !strcmp(wKind, "RAIN") || !strcmp(wKind, "SNOW");
  bool isHot = m_weather.valid && ((s.clock.weatherUnits == "f" ? (m_weather.temp >= 85) : (m_weather.temp >= 29)) || !strcmp(wKind, "SUN"));
  bool wifiOk = netConnected();

  const int groundY = 175;
  int dinoX = 90;
  int dinoY = groundY - 66; // 109
  uint16_t dinoColor = UI_GREEN;
  uint16_t bg = s.clock.bgColor;
  uint16_t ac = s.clock.accentColor;
  uint16_t dc = s.clock.dateColor;

  if (!wifiOk) {
    // Stationary standing with wrench, no sprite change
    return;
  } else if (isNight) {
    // Sleeping: gently alternate "z Z Z" bubbles without touching Dino body
    int sleepY = groundY - 48;
    g->fillRect(dinoX + 44, sleepY - 32, 34, 30, bg);
    if (m_dinoFrame % 2 == 0) {
      gfxPrint(dinoX + 48, sleepY - 14, "z", UI_WHITE, 1);
      gfxPrint(dinoX + 58, sleepY - 26, "Z", ac, 2);
    } else {
      gfxPrint(dinoX + 46, sleepY - 18, "z", ac, 1);
      gfxPrint(dinoX + 56, sleepY - 30, "Z", UI_WHITE, 2);
    }
  } else if (isRainy) {
    // Clear only legs bounding area (y = dinoY + 54..groundY, x = dinoX..dinoX+60)
    g->fillRect(dinoX, dinoY + 54, 60, 12, bg);
    draw1bppBitmap(dinoX, dinoY, (m_dinoFrame & 1) ? DINO_RUN1 : DINO_RUN0, 20, 22, dinoColor, 3);
    // Keep umbrella intact
    draw1bppBitmap(dinoX + 14, dinoY - 32, UMBRELLA_SPRITE, 16, 16, UI_RED, 2);
  } else if (isHot) {
    // Clear only legs bounding area
    g->fillRect(dinoX, dinoY + 54, 60, 12, bg);
    draw1bppBitmap(dinoX, dinoY, (m_dinoFrame & 1) ? DINO_RUN1 : DINO_RUN0, 20, 22, dinoColor, 3);
    draw1bppBitmap(dinoX + 30, dinoY + 3, SUNGLASSES_SPRITE, 12, 5, UI_WHITE, 2);
  } else {
    // Normal daytime: alternate walking legs seamlessly (scaled 3x)
    g->fillRect(dinoX, dinoY + 54, 60, 12, bg);
    draw1bppBitmap(dinoX, dinoY, (m_dinoFrame & 1) ? DINO_RUN1 : DINO_RUN0, 20, 22, dinoColor, 3);
    // Floating chirp/heart bubble
    g->fillRect(dinoX + 52, dinoY - 16, 22, 14, bg);
    if ((m_dinoFrame & 3) == 2) {
      gfxPrint(dinoX + 54, dinoY - 14, "<3", dc, 1);
    }
  }
}

void ClockMode::renderTimeOnly(const Settings& s, const struct tm& t) {
  if (s.clock.theme >= 2) return;

  // Format the time string ONCE, shared by both theme 0 and theme 1.
  char nextTime[16];
  int hour = t.tm_hour;
  if (!s.clock.format24h) { hour %= 12; if (!hour) hour = 12; }
  if (s.clock.showSeconds)
    snprintf(nextTime, sizeof(nextTime), "%02d:%02d:%02d", hour, t.tm_min, t.tm_sec);
  else
    snprintf(nextTime, sizeof(nextTime), "%02d:%02d", hour, t.tm_min);

  if (s.clock.theme == 0) {
    // Theme 0 shows HH:MM in the main area (with or without a separate
    // seconds display).  Format HH:MM only for the main clock field.
    char mainTime[8];
    snprintf(mainTime, sizeof(mainTime), "%02d:%02d", hour, t.tm_min);

    if (strcmp(mainTime, m_lastTime)) {
      if (s.clock.showSeconds) {
        updateFixedDigits(m_lastTime, mainTime, 103, &MontserratBold28pt7b,
                          s.clock.timeColor, s.clock.bgColor, 8, 156);
      } else {
        updateFixedDigits(m_lastTime, mainTime, 103, &MontserratBold54pt7b,
                          s.clock.timeColor, s.clock.bgColor, 0, 240);
      }
      strlcpy(m_lastTime, mainTime, sizeof(m_lastTime));
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

  // Theme 1: per-cell incremental update within the panel (y=6..84).
  // Only changed character cells are cleared and redrawn in the tight
  // glyph band — the rounded panel border and date line are preserved.
  if (strcmp(nextTime, m_lastTime)) {
    const int centerY = 43;
    const GFXfont* font = clockTimeFont(s);
    updateFixedDigits(m_lastTime, nextTime, centerY, font,
                      s.clock.timeColor, UI_PANEL, 0, 240);
    strlcpy(m_lastTime, nextTime, sizeof(m_lastTime));
  }
}

void ClockMode::render(const Settings& s) {
  Arduino_GFX* g = gfxDev();
  if (!g) return;
  m_fullRepaint = false;
  g->setFont(nullptr);
  g->fillScreen(s.clock.bgColor);

  struct tm t;
  bool timeOk = clockNow(t);
  char timeStr[16];
  strlcpy(timeStr, s.clock.showSeconds ? "--:--:--" : "--:--", sizeof(timeStr));
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
      drawFontCentered(timeStr, 103, &MontserratBold54pt7b, tc);
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

    renderSystemSummary(s);
    m_nextSysUpdateMs = millis() + 10000UL;
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
      gfxPrint(14, 126, temp, tc, 3);
      gfxPrint(14, 158, humid, dc, 2);
      gfxPrint(14, 178, desc, UI_MUTED, 1);
    } else {
      gfxDrawCentered("WEATHER UNAVAILABLE", 119, 2, ac);
      gfxDrawCentered(m_weather.error.length() ? m_weather.error.c_str() : "Connecting", 153, 1, UI_MUTED);
    }
    gfxFillRoundRect(6, 200, 228, 34, 9, UI_DARK);
    gfxDrawRoundRect(6, 200, 228, 34, 9, UI_BORDER);
    gfxDrawCentered(ip, 211, 2, ac);
  } else if (s.clock.theme == 3) {
    // Screen 4: Office Memo / Dunder Mifflin quotes
    renderOfficeMemo(s);
  } else if (s.clock.theme == 4) {
    // Screen 5: Pixel Pet / Dino Tamagotchi
    renderDinoPet(s);
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
