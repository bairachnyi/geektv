// UsageData.h — runtime AI quota snapshot from a trusted LAN bridge.
#pragma once
#include <Arduino.h>

struct UsageData {
  float    antigravityPct;       // Antigravity allowance used (0..100)
  int      antigravityResetMin;  // minutes until its reported reset
  float    codexPct;             // Codex allowance used (0..100)
  int      codexResetMin;        // minutes until its reported reset
  char     status[16];           // bridge status, e.g. "ok", "partial", "error"

  bool     valid;            // populated at least once
  bool     error;            // most recent fetch failed
  uint32_t lastOkMs;         // millis() of last good update

  void clear() {
    antigravityPct = codexPct = 0;
    antigravityResetMin = codexResetMin = 0;
    status[0] = 0;
    valid = false;
    error = false;
    lastOkMs = 0;
  }
};
