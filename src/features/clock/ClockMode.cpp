#include "ClockMode.h"
#include "Gfx.h"
#include "fonts.h"
#include "Net.h"
#include "Clock.h"
#include "Platform.h"
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>

ClockMode g_clockMode;

static void draw7SegmentDigit(int x, int y, int w, int h, char ch, uint16_t color, uint16_t dimColor) {
  Arduino_GFX* g = gfxDev();
  if (!g || w < 4 || h < 4) return;

  if (ch == ':') {
    int r = max(2, w / 6);
    g->fillRect(x + w / 2 - r / 2, y + h / 3 - r / 2, r, r, color);
    g->fillRect(x + w / 2 - r / 2, y + (2 * h) / 3 - r / 2, r, r, color);
    return;
  }

  if (ch < '0' || ch > '9') return;

  static const uint8_t segs[10] = {
    0x3F, // 0: A B C D E F
    0x06, // 1: B C
    0x5B, // 2: A B D E G
    0x4F, // 3: A B C D G
    0x66, // 4: B C F G
    0x6D, // 5: A C D F G
    0x7D, // 6: A C D E F G
    0x07, // 7: A B C
    0x7F, // 8: A B C D E F G
    0x6F  // 9: A B C D F G
  };

  uint8_t mask = segs[ch - '0'];
  int t = max(2, w / 6);
  int halfH = h / 2;

  auto drawSeg = [&](bool lit, int sx, int sy, int sw, int sh) {
    if (sw <= 0 || sh <= 0) return;
    uint16_t c = lit ? color : dimColor;
    if (c != 0x0000) {
      g->fillRect(sx, sy, sw, sh, c);
    }
  };

  // A (top)
  drawSeg(mask & 0x01, x + t, y, w - 2 * t, t);
  // B (top-right)
  drawSeg(mask & 0x02, x + w - t, y + t, t, halfH - t);
  // C (bottom-right)
  drawSeg(mask & 0x04, x + w - t, y + halfH + (t / 2), t, halfH - t);
  // D (bottom)
  drawSeg(mask & 0x08, x + t, y + h - t, w - 2 * t, t);
  // E (bottom-left)
  drawSeg(mask & 0x10, x, y + halfH + (t / 2), t, halfH - t);
  // F (top-left)
  drawSeg(mask & 0x20, x, y + t, t, halfH - t);
  // G (middle)
  drawSeg(mask & 0x40, x + t, y + halfH - (t / 2), w - 2 * t, t);
}

static void draw7SegmentString(const char* txt, int y, uint8_t sz, uint16_t color, uint16_t dimColor) {
  if (!txt || !txt[0]) return;
  int len = strlen(txt);
  int digitW = sz * 5 + 2;
  int digitH = sz * 8 + 4;
  int spacing = max(2, (int)sz);
  int totalW = len * digitW + (len - 1) * spacing;
  int startX = (240 - totalW) / 2;
  if (startX < 0) startX = 0;

  for (int i = 0; i < len; i++) {
    int dx = startX + i * (digitW + spacing);
    draw7SegmentDigit(dx, y, digitW, digitH, txt[i], color, dimColor);
  }
}

void ClockMode::begin(const Settings& s) {
  m_weather.valid = false;
  m_nextFetchMs = millis();
  m_lastMin = -1;
  m_lastSec = -1;
  m_lastRenderMs = 0;
  m_fullRepaint = true;
}

void ClockMode::invalidate(const Settings& s) {
  m_nextFetchMs = millis();
  m_lastMin = -1;
  m_lastSec = -1;
  m_fullRepaint = true;
  render(s);
}

void ClockMode::wake(const Settings& s) {
  m_lastMin = -1;
  m_lastSec = -1;
  m_fullRepaint = true;
  render(s);
}

void ClockMode::fetchWeather(const Settings& s) {
  if (WiFi.status() != WL_CONNECTED) {
    m_weather.error = "No WiFi";
    return;
  }

  String city = s.clock.weatherCity.length() ? s.clock.weatherCity : "Moscow";
  String url;
  bool useOwm = false;
  if (s.clock.weatherApiKey.length()) {
    String units = (s.clock.weatherUnits == "f") ? "imperial" : "metric";
    url = "https://api.openweathermap.org/data/2.5/weather?q=" + city + "&appid=" + s.clock.weatherApiKey + "&units=" + units;
    useOwm = true;
  } else {
    url = "https://wttr.in/" + city + "?format=j1";
  }

  bool https = url.startsWith("https://");
  std::unique_ptr<NetClient> client;
  if (https) {
    if (ESP.getFreeHeap() < 16000) {
      m_weather.error = "Low memory";
      return;
    }
    client.reset(platformMakeSecureClient(2048));
  } else {
    client.reset(new WiFiClient());
  }

  HTTPClient http;
  http.setTimeout(6000);
  http.setReuse(false);
  http.useHTTP10(true);
  if (!http.begin(*client, url)) {
    m_weather.error = "Connect failed";
    return;
  }

  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    JsonDocument filter;
    if (useOwm) {
      filter["main"]["temp"] = true;
      filter["weather"][0]["main"] = true;
      filter["weather"][0]["icon"] = true;
    } else {
      filter["current_condition"][0]["temp_C"] = true;
      filter["current_condition"][0]["temp_F"] = true;
      filter["current_condition"][0]["weatherDesc"][0]["value"] = true;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    if (!err) {
      m_weather.city = city;
      if (useOwm) {
        m_weather.temp = doc["main"]["temp"].as<float>();
        m_weather.description = doc["weather"][0]["main"] | "Clear";
        m_weather.icon = doc["weather"][0]["icon"] | "";
      } else {
        JsonObjectConst cur = doc["current_condition"][0].as<JsonObjectConst>();
        m_weather.temp = cur["temp_C"].as<float>();
        if (s.clock.weatherUnits == "f" && cur["temp_F"].is<const char*>()) {
          m_weather.temp = cur["temp_F"].as<float>();
        }
        m_weather.description = cur["weatherDesc"][0]["value"] | "Clear";
      }
      m_weather.valid = true;
      m_weather.error = "";
      m_weather.lastUpdateMs = millis();
    } else {
      m_weather.error = "JSON err";
    }
  } else {
    m_weather.error = "HTTP " + String(code);
  }
  http.end();
}

void ClockMode::service(const Settings& s) {
  if (s.mode != MODE_CLOCK && s.mode != MODE_CAROUSEL) return;

  uint32_t nowMs = millis();

  // Weather update interval
  if ((int32_t)(nowMs - m_nextFetchMs) >= 0) {
    uint32_t interval = (uint32_t)s.clock.weatherPollSec * 1000UL;
    if (interval < 30000UL) interval = 30000UL;
    m_nextFetchMs = nowMs + interval;
    fetchWeather(s);
    m_fullRepaint = true;
    render(s);
  }

  // Clock render tick: re-render every second smoothly without full screen flicker
  struct tm t;
  if (clockNow(t)) {
    if (t.tm_sec != m_lastSec) {
      m_lastSec = t.tm_sec;
      m_lastMin = t.tm_min;
      render(s);
    }
  }
}

void ClockMode::render(const Settings& s) {
  m_lastRenderMs = millis();

  struct tm t;
  bool timeOk = clockNow(t);

  char timeStr[16] = "--:--";
  char dateStr[32] = "NTP Syncing...";

  if (timeOk) {
    int hour = t.tm_hour;
    if (!s.clock.format24h) {
      hour = hour % 12;
      if (hour == 0) hour = 12;
    }
    if (s.clock.showSeconds) {
      snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", hour, t.tm_min, t.tm_sec);
    } else {
      snprintf(timeStr, sizeof(timeStr), "%02d:%02d", hour, t.tm_min);
    }

    static const char* const kDays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char* const kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

    snprintf(dateStr, sizeof(dateStr), "%s, %02d %s %04d",
             kDays[t.tm_wday % 7], t.tm_mday, kMonths[t.tm_mon % 12], t.tm_year + 1900);
  }

  char ipBuf[40];
  if (netMode() == NET_AP) {
    snprintf(ipBuf, sizeof(ipBuf), "AP: %s (192.168.4.1)", netSSID().c_str());
  } else {
    snprintf(ipBuf, sizeof(ipBuf), "IP: %s", netIP().c_str());
  }

  uint8_t theme = s.clock.theme;
  uint8_t fontSt = s.clock.fontStyle;

  // Resolve font scale: timeScale (1..7) and dateScale (1..4)
  uint8_t timeSz = s.clock.timeScale > 0 ? s.clock.timeScale : (s.clock.showSeconds ? 4 : 5);
  if (s.clock.showSeconds && timeSz > 5) timeSz = 5; // 8 chars "12:34:56" fit max at scale 5 (240px)
  uint8_t dateSz = s.clock.dateScale > 0 ? s.clock.dateScale : 2;

  uint16_t tc = s.clock.timeColor;
  uint16_t dc = s.clock.dateColor;
  uint16_t ac = s.clock.accentColor;

  // Helper: draw text according to fontStyle & boldText
  bool isBold = s.clock.boldText || (fontSt == 1) || (fontSt == 3);
  auto drawT = [&](const char* txt, int y, uint8_t sz, uint16_t color) {
    if (fontSt == 2 && txt && (txt[0] >= '0' && txt[0] <= '9')) {
      // Digital LCD 7-Segment vector rendering ONLY for numeric time digits
      draw7SegmentString(txt, y, sz, color, 0x1084);
    } else if (isBold) {
      gfxDrawCenteredBold(txt, y, sz, color);
    } else {
      gfxDrawCentered(txt, y, sz, color);
    }
  };
  auto drawL = [&](const char* txt, int x, int y, uint8_t sz, uint16_t color) {
    Arduino_GFX* g = gfxDev();
    if (!g) return;
    if (isBold) gfxPrintBold(g, x, y, txt, color, sz);
    else gfxPrint(x, y, txt, color, sz);
  };

  if (m_fullRepaint) {
    m_fullRepaint = false;
    gfxFillRect(0, 0, 240, 240, s.clock.bgColor);
  }

  if (theme == 0) {
    // Theme 0: Giant Fullscreen Clock
    int yOff = (timeSz >= 7) ? 36 : (timeSz == 6 ? 44 : (timeSz == 5 ? 52 : 62));
    int timeH = timeSz * 8 + 6;

    // Erase ONLY the time bounding box to prevent full-screen flickering
    gfxFillRect(0, yOff - 2, 240, timeH, s.clock.bgColor);

    if (fontSt == 2) {
      // Digital Segment: draw subtle background segment shadow box
      gfxDrawRoundRect(4, yOff - 4, 232, timeH + 4, 6, 0x18C6);
    }

    drawT(timeStr, yOff, timeSz, tc);

    if (s.clock.showDate) {
      int dateY = (timeSz >= 6) ? 168 : 142;
      gfxFillRoundRect(10, dateY, 220, 32, 8, 0x18C6);
      gfxDrawRoundRect(10, dateY, 220, 32, 8, dc);
      drawT(dateStr, dateY + 8, dateSz, dc);
    }

    gfxFillRect(0, 215, 240, 25, s.clock.bgColor);
    drawT(ipBuf, 218, 1, ac);
  } else if (theme == 1) {
    // Theme 1: Weather & Clock Station
    gfxFillRoundRect(8, 8, 224, 120, 10, 0x0186);
    gfxDrawRoundRect(8, 8, 224, 120, 10, 0x1C17);

    if (m_weather.valid) {
      String cityStr = m_weather.city;
      cityStr.toUpperCase();
      drawL(cityStr.c_str(), 18, 18, 2, ac);

      char tempBuf[16];
      snprintf(tempBuf, sizeof(tempBuf), "%+.1f%s", m_weather.temp, (s.clock.weatherUnits == "f") ? "F" : "C");
      drawL(tempBuf, 18, 46, 4, tc);

      String desc = m_weather.description;
      desc.toUpperCase();
      drawL(desc.c_str(), 18, 94, 2, 0xFFFF);
    } else {
      drawL("WEATHER SYNC...", 18, 45, 2, ac);
      drawL(m_weather.error.length() ? m_weather.error.c_str() : "Connecting...", 18, 80, 2, 0xFF5C);
    }

    // Bottom Clock Card
    gfxFillRoundRect(8, 134, 224, 98, 10, 0x08C9);
    gfxDrawRoundRect(8, 134, 224, 98, 10, 0x22F3);
    drawT(timeStr, 148, timeSz, 0xFFFF);
    drawT(dateStr, 198, dateSz, ac);
  } else if (theme == 2) {
    // Theme 2: Modern OLED Dashboard Clock
    gfxFillRoundRect(8, 8, 224, 130, 12, 0x1084);
    gfxDrawRoundRect(8, 8, 224, 130, 12, 0xA2FD);
    drawT(timeStr, 35, timeSz, 0xFFFF);
    if (s.clock.showDate) drawT(dateStr, 98, dateSz, 0xA2FD);

    gfxFillRoundRect(8, 144, 224, 88, 12, 0x0842);
    gfxDrawRoundRect(8, 144, 224, 88, 12, 0x2126);
    if (m_weather.valid) {
      String cityStr = m_weather.city;
      cityStr.toUpperCase();
      drawL(cityStr.c_str(), 18, 160, 2, 0xFFFF);

      char tempBuf[16];
      snprintf(tempBuf, sizeof(tempBuf), "%+.1f%s", m_weather.temp, (s.clock.weatherUnits == "f") ? "F" : "C");
      drawL(tempBuf, 135, 155, 3, 0xA2FD);
    }
  } else {
    // Theme 3: 3-Day Weather Forecast Breakdown
    gfxFillRoundRect(6, 6, 228, 72, 8, 0x0944);
    gfxDrawRoundRect(6, 6, 228, 72, 8, 0x1390);
    drawL("TODAY", 16, 16, 2, tc);
    if (m_weather.valid) {
      char tempBuf[16];
      snprintf(tempBuf, sizeof(tempBuf), "%+.1f%s", m_weather.temp, (s.clock.weatherUnits == "f") ? "F" : "C");
      drawL(tempBuf, 135, 12, 3, ac);
    }

    gfxFillRoundRect(6, 84, 228, 72, 8, 0x1084);
    gfxDrawRoundRect(6, 84, 228, 72, 8, 0x2126);
    drawL(timeStr, 16, 94, 2, ac);
    if (s.clock.showDate) drawL(dateStr, 16, 123, 1, dc);

    gfxFillRoundRect(6, 162, 228, 72, 8, 0x0186);
    gfxDrawRoundRect(6, 162, 228, 72, 8, 0x1C17);
    drawL(ipBuf, 16, 178, 1, ac);
  }
}
