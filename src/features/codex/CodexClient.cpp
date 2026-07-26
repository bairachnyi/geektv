#include "CodexClient.h"
#include "Platform.h"
#include "Net.h"
#include <ArduinoJson.h>
#include <math.h>

static CodexData g_codex;
static uint32_t  g_nextPollMs = 0;
static bool      g_inited = false;

static void setCodexError(const char* code, const char* message) {
  g_codex.error = true;
  strlcpy(g_codex.errorCode, code ? code : "FETCH_FAILED", sizeof(g_codex.errorCode));
  strlcpy(g_codex.errorMessage, message ? message : "Codex data unavailable.", sizeof(g_codex.errorMessage));
}

void codexInit(const Settings& s) {
  (void)s;
  g_codex.clear();
  g_nextPollMs = millis();
  g_inited = true;
}

void codexForceRefresh() {
  g_nextPollMs = millis();
}

const CodexData& codexGet() {
  return g_codex;
}

bool codexFresh(uint32_t withinMs) {
  return g_codex.valid && (millis() - g_codex.lastOkMs) <= withinMs;
}

static bool applyCodexDoc(CodexData& d, JsonDocument& doc) {
  if (doc["ok"].is<bool>() && doc["ok"].as<bool>() == false) {
    JsonObjectConst e = doc["error"].as<JsonObjectConst>();
    setCodexError(e["code"] | "SOURCE_ERROR", e["message"] | (doc["message"] | "Codex source reported an error."));
    return false;
  }

  // 1. Direct fields
  if (doc["primary_pct"].is<float>() || doc["primary_pct"].is<int>()) {
    d.primaryPct = constrain(doc["primary_pct"].as<float>(), 0.0f, 100.0f);
  }
  if (doc["secondary_pct"].is<float>() || doc["secondary_pct"].is<int>()) {
    d.secondaryPct = constrain(doc["secondary_pct"].as<float>(), 0.0f, 100.0f);
  }
  if (doc["primary_reset"].is<const char*>()) {
    strlcpy(d.primaryReset, doc["primary_reset"].as<const char*>(), sizeof(d.primaryReset));
  }
  if (doc["secondary_reset"].is<const char*>()) {
    strlcpy(d.secondaryReset, doc["secondary_reset"].as<const char*>(), sizeof(d.secondaryReset));
  }
  if (doc["plan_type"].is<const char*>()) {
    strlcpy(d.planType, doc["plan_type"].as<const char*>(), sizeof(d.planType));
  }

  // 2. Nested rate_limits structure (from codex /status endpoint)
  JsonObjectConst rl = doc["rate_limits"].as<JsonObjectConst>();
  if (!rl.isNull()) {
    if (rl["plan_type"].is<const char*>()) {
      strlcpy(d.planType, rl["plan_type"].as<const char*>(), sizeof(d.planType));
    }
    JsonObjectConst prim = rl["primary"].as<JsonObjectConst>();
    if (!prim.isNull()) {
      if (prim["remaining_percent"].is<float>() || prim["remaining_percent"].is<int>()) {
        d.primaryPct = constrain(prim["remaining_percent"].as<float>(), 0.0f, 100.0f);
      }
      if (prim["reset_time"].is<const char*>()) {
        strlcpy(d.primaryReset, prim["reset_time"].as<const char*>(), sizeof(d.primaryReset));
      }
    }
    JsonObjectConst sec = rl["secondary"].as<JsonObjectConst>();
    if (!sec.isNull()) {
      if (sec["remaining_percent"].is<float>() || sec["remaining_percent"].is<int>()) {
        d.secondaryPct = constrain(sec["remaining_percent"].as<float>(), 0.0f, 100.0f);
      }
      if (sec["reset_time"].is<const char*>()) {
        strlcpy(d.secondaryReset, sec["reset_time"].as<const char*>(), sizeof(d.secondaryReset));
      }
    }
  }

  // 3. Token metrics
  if (doc["today_tokens"].is<uint32_t>() || doc["today_tokens"].is<int>()) d.todayTokens = doc["today_tokens"].as<uint32_t>();
  if (doc["today_calls"].is<uint32_t>() || doc["today_calls"].is<int>()) d.todayCalls = doc["today_calls"].as<uint32_t>();
  if (doc["today_input"].is<uint32_t>() || doc["today_input"].is<int>()) d.todayInput = doc["today_input"].as<uint32_t>();
  if (doc["today_output"].is<uint32_t>() || doc["today_output"].is<int>()) d.todayOutput = doc["today_output"].as<uint32_t>();
  if (doc["today_cached"].is<uint32_t>() || doc["today_cached"].is<int>()) d.todayCached = doc["today_cached"].as<uint32_t>();
  if (doc["today_reasoning"].is<uint32_t>() || doc["today_reasoning"].is<int>()) d.todayReasoning = doc["today_reasoning"].as<uint32_t>();

  if (doc["week_tokens"].is<uint32_t>() || doc["week_tokens"].is<int>()) d.weekTokens = doc["week_tokens"].as<uint32_t>();
  if (doc["total_tokens"].is<uint32_t>() || doc["total_tokens"].is<int>()) d.totalTokens = doc["total_tokens"].as<uint32_t>();

  if (doc["model1"].is<const char*>()) strlcpy(d.model1, doc["model1"].as<const char*>(), sizeof(d.model1));
  if (doc["model1_tokens"].is<uint32_t>() || doc["model1_tokens"].is<int>()) d.model1Tokens = doc["model1_tokens"].as<uint32_t>();
  if (doc["model2"].is<const char*>()) strlcpy(d.model2, doc["model2"].as<const char*>(), sizeof(d.model2));
  if (doc["model2_tokens"].is<uint32_t>() || doc["model2_tokens"].is<int>()) d.model2Tokens = doc["model2_tokens"].as<uint32_t>();

  d.valid = true;
  d.error = false;
  d.errorCode[0] = 0;
  d.errorMessage[0] = 0;
  d.lastOkMs = millis();
  return true;
}

bool codexApply(const String& body) {
  JsonDocument doc;
  g_codex.lastAttemptMs = millis();
  if (deserializeJson(doc, body)) {
    setCodexError("BAD_JSON", "The pushed Codex payload is not valid JSON.");
    return false;
  }
  return applyCodexDoc(g_codex, doc);
}

static bool fetchCodex(const Settings& s) {
  const String& url = s.codex.statusUrl;
  g_codex.lastAttemptMs = millis();
  if (url.length() < 8) { setCodexError("URL_NOT_SET", "Set a Codex JSON URL or push data to /api/codex."); return false; }

  bool https = url.startsWith("https://");
  std::unique_ptr<NetClient> client;
  if (https) {
    if (ESP.getFreeHeap() < 16000) { setCodexError("LOW_MEMORY", "Not enough free memory for HTTPS."); return false; }
    client.reset(platformMakeSecureClient(2048));
  } else {
    client.reset(new WiFiClient());
  }

  HTTPClient http;
  http.setTimeout(s.httpTimeout);
  http.setReuse(false);
  if (!http.begin(*client, url)) { setCodexError("BAD_URL", "Cannot open the configured Codex URL."); return false; }
  http.addHeader("Accept", "application/json");

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    if (code < 0) setCodexError("SOURCE_OFFLINE", "Cannot connect to the Codex status source.");
    else if (code == 401 || code == 403) setCodexError("SOURCE_DENIED", "The Codex source rejected this request.");
    else setCodexError("SOURCE_HTTP", "The Codex source returned an HTTP error.");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();

  if (err) { setCodexError("BAD_RESPONSE", "The Codex source returned invalid JSON."); return false; }
  return applyCodexDoc(g_codex, doc);
}

void codexService(const Settings& s) {
  if (!g_inited) codexInit(s);

  if ((int32_t)(millis() - g_nextPollMs) >= 0) {
    uint32_t interval = (uint32_t)s.codex.pollSec * 1000UL;
    if (interval < 5000UL) interval = 5000UL;
    g_nextPollMs = millis() + interval;

    fetchCodex(s);
  }
}
