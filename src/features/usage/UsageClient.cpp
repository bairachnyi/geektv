#include "UsageClient.h"
#include "Platform.h"
#include <ArduinoJson.h>
#include <math.h>

static UsageData g_usage;
static uint32_t  g_nextPollMs = 0;
static bool      g_inited = false;

// ---------------------------------------------------------------------------
void usageInit(const Settings& s) {
  (void)s;
  g_usage.clear();
  g_nextPollMs = millis();
  g_inited = true;
}

void usageForceRefresh() { g_nextPollMs = millis(); }

const UsageData& usageGet() { return g_usage; }

bool usageFresh(uint32_t withinMs) {
  return g_usage.valid && (millis() - g_usage.lastOkMs) <= withinMs;
}

// ---- parse: AI usage contract ----------------------------------------------
// { "a":29, "ar":142, "c":4, "cr":9876, "st":"ok", "ok":true }
//   a  = Antigravity used %      ar = minutes until reported reset
//   c  = Codex used %            cr = minutes until reported reset
// Legacy s/sr/w/wr keys remain accepted so existing Claude bridges do not break.
static void usageFilter(JsonDocument& f) {
  f["a"] = true; f["ar"] = true; f["c"] = true; f["cr"] = true;
  f["s"] = true; f["sr"] = true; f["w"] = true;
  f["wr"] = true; f["st"] = true; f["ok"] = true;
}

static bool applyUsageDoc(UsageData& d, JsonDocument& doc) {
  if (doc["ok"].is<bool>() && doc["ok"].as<bool>() == false) return false;
  bool modern = doc["a"].is<float>() || doc["a"].is<int>();
  bool legacy = doc["s"].is<float>() || doc["s"].is<int>();
  if (!modern && !legacy) return false;

  d.antigravityPct      = constrain(modern ? doc["a"].as<float>() : doc["s"].as<float>(), 0.0f, 100.0f);
  d.codexPct            = constrain(modern ? (doc["c"] | 0.0f) : (doc["w"] | 0.0f), 0.0f, 100.0f);
  d.antigravityResetMin = modern ? (doc["ar"] | 0) : (doc["sr"] | 0);
  d.codexResetMin       = modern ? (doc["cr"] | 0) : (doc["wr"] | 0);
  strlcpy(d.status, doc["st"] | "", sizeof(d.status));

  d.valid = true;
  d.error = false;
  d.lastOkMs = millis();
  return true;
}

static bool parseUsage(UsageData& d, Stream& stream) {
  JsonDocument filter; usageFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, stream, DeserializationOption::Filter(filter))) return false;
  return applyUsageDoc(d, doc);
}

// Pushed payload (POST /api/usage or /api/ai-usage): same compact contract.
bool usageApply(const String& body) {
  JsonDocument filter; usageFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  return applyUsageDoc(g_usage, doc);
}

// ---- one HTTP(S) GET + parse (mirrors StockClient::fetchUrl) ----------------
static bool fetchUsage(const Settings& s) {
  const String& url = s.usage.usageUrl;
  if (url.length() < 8) return false;
  bool https = url.startsWith("https://");

  std::unique_ptr<NetClient> client;
  if (https) {
    if (ESP.getFreeHeap() < 20000) return false;   // too little heap for TLS (incl. the 9 KB thunk); skip, don't crash
    client.reset(platformMakeSecureClient(2048));   // LAN / self-hosted endpoint
  } else {
    client.reset(new WiFiClient());
  }

  HTTPClient http;
  http.setTimeout(s.httpTimeout);
  http.setReuse(false);
  if (!http.begin(*client, url)) return false;
  http.addHeader("Accept", "application/json");

  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }

  bool ok = parseUsage(g_usage, http.getStream());
  http.end();
  return ok;
}

// ---------------------------------------------------------------------------
void usageService(const Settings& s) {
  if (!g_inited) usageInit(s);
  if ((int32_t)(millis() - g_nextPollMs) < 0) return;

  if (!fetchUsage(s)) g_usage.error = true;   // keep stale data, flag the error

  g_nextPollMs = millis() + (uint32_t)s.usage.pollSec * 1000UL;
}
