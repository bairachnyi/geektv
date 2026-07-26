#pragma once
#include "Mode.h"
#include "Settings.h"
#include <Arduino.h>

struct ForecastDay {
  bool   valid = false;
  float  temp = 0.0f;
  float  minTemp = 0.0f;
  float  maxTemp = 0.0f;
  uint8_t humidity = 0;
  uint16_t weatherCode = 0;
  String date;          // YYYY-MM-DD from the provider
  String description;
};

struct WeatherData {
  bool     valid = false;
  float    temp = 0.0f;
  uint8_t  humidity = 0;
  uint16_t weatherCode = 0;
  String   description;
  String   city;
  ForecastDay days[3];
  uint32_t lastUpdateMs = 0;
  String   error;
};

class ClockMode : public DisplayMode {
public:
  const char* id() const override { return "clock"; }
  uint8_t modeConst() const override { return MODE_CLOCK; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override;

  const WeatherData& weather() const { return m_weather; }

private:
  void fetchWeather(const Settings& s);
  void render(const Settings& s);
  void renderTimeOnly(const Settings& s, const struct tm& t);

  WeatherData m_weather;
  uint32_t    m_nextFetchMs = 0;
  int16_t     m_lastTick = -1;  // second when visible, otherwise minute
  int16_t     m_lastYday = -1;
  uint8_t     m_lastTheme = 0xFF;
  char        m_lastTime[16] = "";
  bool        m_fullRepaint = true;
};

extern ClockMode g_clockMode;
