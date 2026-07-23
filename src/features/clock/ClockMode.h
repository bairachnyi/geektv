#pragma once
#include "Mode.h"
#include "Settings.h"
#include <Arduino.h>

struct WeatherData {
  bool     valid = false;
  float    temp = 0.0f;
  String   description;
  String   icon;
  String   city;
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

  WeatherData m_weather;
  uint32_t    m_nextFetchMs = 0;
  uint32_t    m_lastRenderMs = 0;
  int8_t      m_lastMin = -1;
  int8_t      m_lastSec = -1;
};

extern ClockMode g_clockMode;
