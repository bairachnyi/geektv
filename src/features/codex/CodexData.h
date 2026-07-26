#pragma once
#include <Arduino.h>

struct CodexData {
  bool     valid = false;
  bool     error = false;
  uint32_t lastOkMs = 0;
  uint32_t lastAttemptMs = 0;
  char     errorCode[24] = "";
  char     errorMessage[56] = "";

  // Rate Limits / Quotas
  float    primaryPct = 100.0f;       // Primary quota remaining % (e.g. 81.0)
  float    secondaryPct = 100.0f;     // Secondary quota remaining % (e.g. 95.0)
  char     primaryReset[20] = "";     // Reset time e.g. "14:42" or "in 2h"
  char     secondaryReset[20] = "";   // Reset time e.g. "in 3d"
  char     planType[20] = "Plus";     // Plan name e.g. "Plus" / "Pro" / "Enterprise"

  // Usage Token Statistics
  uint32_t todayTokens = 0;           // Total tokens used today (e.g. 31700)
  uint32_t todayCalls = 0;            // Number of API calls today
  uint32_t todayInput = 0;
  uint32_t todayOutput = 0;
  uint32_t todayCached = 0;
  uint32_t todayReasoning = 0;

  uint32_t weekTokens = 0;            // Rolling 7-day total tokens (e.g. 2160000)
  uint32_t weekCalls = 0;
  uint32_t totalTokens = 0;           // All-time / monthly tokens (e.g. 5400000)

  // Model breakdowns (top 2 models)
  char     model1[24] = "";
  uint32_t model1Tokens = 0;
  char     model2[24] = "";
  uint32_t model2Tokens = 0;

  void clear() {
    valid = false;
    error = false;
    lastOkMs = 0;
    lastAttemptMs = 0;
    errorCode[0] = 0;
    errorMessage[0] = 0;
    primaryPct = 100.0f;
    secondaryPct = 100.0f;
    primaryReset[0] = 0;
    secondaryReset[0] = 0;
    strlcpy(planType, "Plus", sizeof(planType));
    todayTokens = 0;
    todayCalls = 0;
    todayInput = 0;
    todayOutput = 0;
    todayCached = 0;
    todayReasoning = 0;
    weekTokens = 0;
    weekCalls = 0;
    totalTokens = 0;
    model1[0] = 0; model1Tokens = 0;
    model2[0] = 0; model2Tokens = 0;
  }
};
