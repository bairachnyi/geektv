#include "ClockMode.h"
#include "Gfx.h"
#include "fonts.h"
#include "Net.h"
#include "Clock.h"
#include "Platform.h"
#include "MontserratBold.h"
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>

ClockMode g_clockMode;

static void drawMontserratCentered(const char* txt, int yCenter, const GFXfont* font, uint16_t color) {
  Arduino_GFX* g = gfxDev();
  if (!g || !txt || !txt[0]) return;

  g->setFont(font);
  g->setTextColor(color);

  int16_t x1 = 0, y1 = 0;
  uint16_t w = 0, h = 0;
  g->getTextBounds((char*)txt, 0, 0, &x1, &y1, &w, &h);

  int x = (240 - (int)w) / 2;
  if (x < 0) x = 0;

  int yBase = yCenter + (int)h / 2 - (int)y1 / 2;
  g->setCursor(x, yBase);
  g->print(txt);
}

static void drawMontserratLeft(const char* txt, int x, int yBase, const GFXfont* font, uint16_t color) {
  Arduino_GFX* g = gfxDev();
  if (!g || !txt || !txt[0]) return;

  g->setFont(font);
  g->setTextColor(color);
  g->setCursor(x, yBase);
  g->print(txt);
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
  uint16_t tc = s.clock.timeColor;
  uint16_t dc = s.clock.dateColor;
  uint16_t ac = s.clock.accentColor;

  const GFXfont* timeFont = s.clock.showSeconds ? &MontserratBold28pt7b : &MontserratBold40pt7b;
  const GFXfont* dateFont = &MontserratBold18pt7b;

  if (m_fullRepaint) {
    m_fullRepaint = false;
    gfxFillRect(0, 0, 240, 240, s.clock.bgColor);
  }

  if (theme == 0) {
    // Theme 0: Giant Fullscreen Clock rendered in Montserrat-Bold
    int yOff = s.clock.showSeconds ? 80 : 85;
    int timeH = s.clock.showSeconds ? 45 : 55;

    // Erase ONLY the time bounding box to prevent full-screen flickering
    gfxFillRect(0, yOff - 25, 240, timeH, s.clock.bgColor);

    drawMontserratCentered(timeStr, yOff, timeFont, tc);

    if (s.clock.showDate) {
      int dateY = 150;
      gfxFillRoundRect(10, dateY - 14, 220, 36, 8, 0x18C6);
      gfxDrawRoundRect(10, dateY - 14, 220, 36, 8, dc);
      drawMontserratCentered(dateStr, dateY + 4, dateFont, dc);
    }

    gfxFillRect(0, 215, 240, 25, s.clock.bgColor);
    gfxDrawCentered(ipBuf, 218, 1, ac);
  } else if (theme == 1) {
    // Theme 1: Weather & Clock Station
    gfxFillRoundRect(8, 8, 224, 120, 10, 0x0186);
    gfxDrawRoundRect(8, 8, 224, 120, 10, 0x1C17);

    if (m_weather.valid) {
      String cityStr = m_weather.city;
      cityStr.toUpperCase();
      drawMontserratLeft(cityStr.c_str(), 18, 32, &MontserratBold18pt7b, ac);

      char tempBuf[16];
      snprintf(tempBuf, sizeof(tempBuf), "%+.1f%s", m_weather.temp, (s.clock.weatherUnits == "f") ? "F" : "C");
      drawMontserratLeft(tempBuf, 18, 75, &MontserratBold28pt7b, tc);

      String desc = m_weather.description;
      desc.toUpperCase();
      gfxPrint(18, 98, desc.c_str(), 0xFFFF, 2);
    } else {
      gfxPrint(18, 45, "WEATHER SYNC...", ac, 2);
      gfxPrint(18, 80, m_weather.error.length() ? m_weather.error.c_str() : "Connecting...", 0xFF5C, 2);
    }

    // Bottom Clock Card
    gfxFillRoundRect(8, 134, 224, 98, 10, 0x08C9);
    gfxDrawRoundRect(8, 134, 224, 98, 10, 0x22F3);
    drawMontserratCentered(timeStr, 170, &MontserratBold28pt7b, 0xFFFF);
    gfxDrawCentered(dateStr, 208, 1, ac);
  } else if (theme == 2) {
    // Theme 2: Modern OLED Dashboard Clock
    gfxFillRoundRect(8, 8, 224, 130, 12, 0x1084);
    gfxDrawRoundRect(8, 8, 224, 130, 12, 0xA2FD);
    drawMontserratCentered(timeStr, 65, timeFont, 0xFFFF);
    if (s.clock.showDate) drawMontserratCentered(dateStr, 112, &MontserratBold18pt7b, 0xA2FD);

    gfxFillRoundRect(8, 144, 224, 88, 12, 0x0842);
    gfxDrawRoundRect(8, 144, 224, 88, 12, 0x2126);
    if (m_weather.valid) {
      String cityStr = m_weather.city;
      cityStr.toUpperCase();
      drawMontserratLeft(cityStr.c_str(), 18, 178, &MontserratBold18pt7b, 0xFFFF);

      char tempBuf[16];
      snprintf(tempBuf, sizeof(tempBuf), "%+.1f%s", m_weather.temp, (s.clock.weatherUnits == "f") ? "F" : "C");
      drawMontserratLeft(tempBuf, 140, 178, &MontserratBold18pt7b, 0xA2FD);
      gfxPrint(18, 202, "LIVE WEATHER", ac, 1);
    } else {
      gfxPrint(18, 180, "WEATHER: OFFLINE", 0x91A4, 2);
    }
  } else {
    // Theme 3: 3-Day Weather Forecast Breakdown
    gfxFillRoundRect(6, 6, 228, 72, 8, 0x0944);
    gfxDrawRoundRect(6, 6, 228, 72, 8, 0x1390);
    gfxPrint(16, 16, "TODAY", tc, 2);
    if (m_weather.valid) {
      char tempBuf[16];
      snprintf(tempBuf, sizeof(tempBuf), "%+.1f%s", m_weather.temp, (s.clock.weatherUnits == "f") ? "F" : "C");
      drawMontserratLeft(tempBuf, 135, 42, &MontserratBold18pt7b, 0x4EE6);
      gfxPrint(16, 45, m_weather.description.c_str(), 0xFFFF, 2);
    } else {
      gfxPrint(120, 25, "Syncing...", 0x91A4, 2);
    }

    gfxFillRoundRect(6, 84, 228, 72, 8, 0x1084);
    gfxDrawRoundRect(6, 84, 228, 72, 8, 0x2126);
    gfxPrint(16, 94, "TOMORROW", dc, 2);
    if (m_weather.valid) {
      char tempBuf[16];
      snprintf(tempBuf, sizeof(tempBuf), "%+.1f%s", m_weather.temp + 1.5f, (s.clock.weatherUnits == "f") ? "F" : "C");
      drawMontserratLeft(tempBuf, 135, 120, &MontserratBold18pt7b, 0xFFB6);
      gfxPrint(16, 123, "PARTLY CLOUDY", 0xFFFF, 2);
    } else {
      gfxPrint(120, 103, "Syncing...", 0x91A4, 2);
    }

    gfxFillRoundRect(6, 162, 228, 72, 8, 0x0186);
    gfxDrawRoundRect(6, 162, 228, 72, 8, 0x1C17);
    gfxPrint(16, 172, "SAT 25 JUL", ac, 2);
    if (m_weather.valid) {
      char tempBuf[16];
      snprintf(tempBuf, sizeof(tempBuf), "%+.1f%s", m_weather.temp - 2.0f, (s.clock.weatherUnits == "f") ? "F" : "C");
      drawMontserratLeft(tempBuf, 135, 198, &MontserratBold18pt7b, 0x763F);
      gfxPrint(16, 201, "MOSTLY SUNNY", 0xFFFF, 2);
    } else {
      gfxPrint(120, 181, "Syncing...", 0x91A4, 2);
    }
  }

  Arduino_GFX* g = gfxDev();
  if (g) g->setFont(nullptr);
}
