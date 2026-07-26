#include "ClockMode.h"
#include "Gfx.h"
#include "Net.h"
#include "Clock.h"
#include "Platform.h"
#include "MontserratBold.h"
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
}

void ClockMode::invalidate(const Settings& s) {
  m_nextFetchMs = millis();
  m_lastTick = -1;
  m_lastYday = -1;
  m_lastTheme = 0xFF;
  m_lastTime[0] = '\0';
  m_fullRepaint = true;
  render(s);
}

void ClockMode::wake(const Settings& s) {
  m_lastTick = -1;
  m_lastYday = -1;
  m_lastTheme = 0xFF;
  m_lastTime[0] = '\0';
  m_fullRepaint = true;
  render(s);
}

void ClockMode::fetchWeather(const Settings& s) {
  if (WiFi.status() != WL_CONNECTED) {
    m_weather.error = "Wi-Fi offline";
    return;
  }

  String city = s.clock.weatherCity.length() ? s.clock.weatherCity : "Moscow";
  city.replace(" ", "+");
  String url = "https://wttr.in/" + city + "?format=j1";

  if (ESP.getFreeHeap() < 17000) {
    m_weather.error = "Low memory";
    return;
  }
  std::unique_ptr<NetClient> client(platformMakeSecureClient(2048));
  HTTPClient http;
  http.setTimeout(s.httpTimeout);
  http.setReuse(false);
  http.useHTTP10(true);
  if (!http.begin(*client, url)) {
    m_weather.error = "Weather URL";
    return;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    m_weather.error = code < 0 ? "Weather offline" : "Weather HTTP " + String(code);
    http.end();
    return;
  }

  JsonDocument filter;
  filter["current_condition"][0]["temp_C"] = true;
  filter["current_condition"][0]["temp_F"] = true;
  filter["current_condition"][0]["humidity"] = true;
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
    return;
  }

  JsonObjectConst cur = doc["current_condition"][0].as<JsonObjectConst>();
  if (cur.isNull()) {
    m_weather.error = "Weather empty";
    return;
  }

  WeatherData next;
  next.city = doc["nearest_area"][0]["areaName"][0]["value"] | s.clock.weatherCity.c_str();
  next.temp = (s.clock.weatherUnits == "f") ? cur["temp_F"].as<float>() : cur["temp_C"].as<float>();
  next.humidity = constrain(cur["humidity"].as<int>(), 0, 100);
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
}

void ClockMode::service(const Settings& s) {
  if (s.mode != MODE_CLOCK && s.mode != MODE_CAROUSEL) return;
  uint32_t nowMs = millis();
  if ((int32_t)(nowMs - m_nextFetchMs) >= 0) {
    uint32_t interval = (uint32_t)s.clock.weatherPollSec * 1000UL;
    if (interval < 60000UL) interval = 60000UL;
    m_nextFetchMs = nowMs + interval;
    fetchWeather(s);
    m_fullRepaint = true;
  }

  if (m_fullRepaint) {
    render(s);
    return;
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

void ClockMode::renderTimeOnly(const Settings& s, const struct tm& t) {
  if (s.clock.theme >= 2) return;
  char nextTime[16];
  formatClockTime(t, s, nextTime, sizeof(nextTime));
  if (!strcmp(nextTime, m_lastTime)) return;

  const GFXfont* font = clockTimeFont(s);
  const int centerY = s.clock.theme == 0 ? 116 : 43;

  // GFX custom-font text is transparent. Painting the previous glyphs in the
  // panel color erases only pixels that changed, avoiding a visible rectangular
  // clear and the old full-screen repaint.
  if (m_lastTime[0]) drawFontCentered(m_lastTime, centerY, font, UI_PANEL);
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
    // Screen 1: time dominates; date and IP remain readable from a distance.
    gfxFillRoundRect(6, 6, 228, 44, 10, UI_PANEL_BLUE);
    gfxDrawRoundRect(6, 6, 228, 44, 10, dc);
    if (s.clock.showDate) drawFontCentered(dateStr, 28, &MontserratBold18pt7b, dc);

    gfxFillRoundRect(6, 56, 228, 120, 12, UI_PANEL);
    gfxDrawRoundRect(6, 56, 228, 120, 12, tc);
    drawFontCentered(timeStr, 116, timeFont, tc);

    gfxFillRoundRect(6, 182, 228, 52, 10, UI_PANEL_BLUE);
    gfxDrawRoundRect(6, 182, 228, 52, 10, ac);
    drawFontCentered(ip, 208, &MontserratBold18pt7b, ac);
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
