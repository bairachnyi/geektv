#include "GithubClient.h"
#include "Platform.h"
#include <ArduinoJson.h>

static GithubData g_data;
static uint32_t g_nextPollMs = 0;

static void setGithubError(const char* code, const char* message,
                           const char* source = "", const char* repo = "") {
  g_data.error = true;
  strlcpy(g_data.errorCode, code ? code : "FETCH_FAILED", sizeof(g_data.errorCode));
  strlcpy(g_data.errorMessage, message ? message : "GitHub data unavailable.", sizeof(g_data.errorMessage));
  strlcpy(g_data.errorSource, source ? source : "", sizeof(g_data.errorSource));
  strlcpy(g_data.errorRepo, repo ? repo : "", sizeof(g_data.errorRepo));
  g_data.revision++;
}

static uint8_t stateFrom(JsonObjectConst o) {
  String status = o["status"] | "";
  String conclusion = o["conclusion"] | "";
  status.toLowerCase(); conclusion.toLowerCase();
  if (status == "queued" || status == "waiting" || status == "pending" || status == "requested") return GH_QUEUED;
  if (status == "in_progress" || status == "running") return GH_RUNNING;
  if (conclusion == "success" || status == "success") return GH_SUCCESS;
  if (conclusion == "failure" || conclusion == "timed_out" || conclusion == "action_required" || status == "failure") return GH_FAILURE;
  if (conclusion == "cancelled" || conclusion == "skipped" || status == "cancelled") return GH_CANCELLED;
  return GH_UNKNOWN;
}

static uint8_t typeFrom(const char* value) {
  if (!value) return GH_EVENT_ACTION;
  if (!strcmp(value, "deployment")) return GH_EVENT_DEPLOYMENT;
  if (!strcmp(value, "pull_request")) return GH_EVENT_PULL_REQUEST;
  if (!strcmp(value, "release")) return GH_EVENT_RELEASE;
  return GH_EVENT_ACTION;
}

static void addGithubFilter(JsonObject f) {
  f["repo"] = true; f["type"] = true; f["workflow"] = true; f["branch"] = true;
  f["status"] = true; f["conclusion"] = true; f["age"] = true; f["when"] = true;
  f["latest"] = true;
}

static bool parseGithub(Stream& stream) {
  JsonDocument filter;
  filter["ok"] = true; filter["message"] = true;
  filter["error"]["code"] = true; filter["error"]["message"] = true;
  filter["error"]["source"] = true; filter["error"]["repo"] = true;
  addGithubFilter(filter["items"][0].to<JsonObject>());
  addGithubFilter(filter["runs"][0].to<JsonObject>()); // v0.1 bridge compatibility

  JsonDocument doc;
  if (deserializeJson(doc, stream, DeserializationOption::Filter(filter))) {
    setGithubError("BAD_RESPONSE", "Bridge returned invalid JSON.", "bridge");
    return false;
  }
  if (doc["ok"].is<bool>() && !doc["ok"].as<bool>()) {
    JsonObjectConst e = doc["error"].as<JsonObjectConst>();
    setGithubError(e["code"] | "FETCH_FAILED", e["message"] | (doc["message"] | "GitHub request failed."),
                   e["source"] | "bridge", e["repo"] | "");
    return false;
  }
  JsonArrayConst items = doc["items"].is<JsonArrayConst>()
    ? doc["items"].as<JsonArrayConst>() : doc["runs"].as<JsonArrayConst>();
  if (items.isNull()) {
    setGithubError("BAD_RESPONSE", "Bridge response has no event list.", "bridge");
    return false;
  }

  GithubData next; next.clear();
  strlcpy(next.message, doc["message"] | "", sizeof(next.message));
  JsonObjectConst warning = doc["error"].as<JsonObjectConst>();
  if (!warning.isNull()) {
    next.error = true;
    strlcpy(next.errorCode, warning["code"] | "SOURCE_FAILED", sizeof(next.errorCode));
    strlcpy(next.errorMessage, warning["message"] | "One GitHub source failed.", sizeof(next.errorMessage));
    strlcpy(next.errorSource, warning["source"] | "github", sizeof(next.errorSource));
    strlcpy(next.errorRepo, warning["repo"] | "", sizeof(next.errorRepo));
  }
  for (JsonObjectConst o : items) {
    if (next.runCount >= MAX_GITHUB_RUNS) break;
    const char* repo = o["repo"] | "";
    if (!repo[0]) continue;
    GithubRun& r = next.runs[next.runCount++];
    strlcpy(r.repo, repo, sizeof(r.repo));
    strlcpy(r.workflow, o["workflow"] | "workflow", sizeof(r.workflow));
    strlcpy(r.branch, o["branch"] | "", sizeof(r.branch));
    strlcpy(r.when, o["when"] | "-- --- --:--", sizeof(r.when));
    r.state = stateFrom(o);
    r.type = typeFrom(o["type"] | "action");
    r.ageSec = o["age"] | 0;
    r.latest = o["latest"] | false;
    if (r.state == GH_RUNNING || r.state == GH_QUEUED) next.runningCount++;
    else if (r.state == GH_SUCCESS) next.successCount++;
    else if (r.state == GH_FAILURE) next.failureCount++;
  }
  next.valid = true;
  next.lastOkMs = millis();
  next.revision = g_data.revision + 1;
  g_data = next;
  return true;
}

static bool fetchGithub(const Settings& s) {
  const String& url = s.github.statusUrl;
  if (url.length() < 8) { setGithubError("FEED_URL_INVALID", "Set the bridge feed URL.", "config"); return false; }
  bool https = url.startsWith("https://");
  std::unique_ptr<NetClient> client;
  if (https) {
    if (ESP.getFreeHeap() < 20000) { setGithubError("LOW_MEMORY", "Not enough memory for HTTPS.", "device"); return false; }
    client.reset(platformMakeSecureClient(3072));
  } else client.reset(new WiFiClient());

  HTTPClient http;
  http.setTimeout(s.httpTimeout);
  http.setReuse(false);
  if (!http.begin(*client, url)) { setGithubError("FEED_URL_INVALID", "Cannot open the bridge URL.", "config"); return false; }
  http.addHeader("Accept", "application/json");
  if (s.github.accessToken.length()) http.addHeader("X-Device-Token", s.github.accessToken);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    if (code < 0) setGithubError("BRIDGE_OFFLINE", "Cannot connect to the bridge.", "network");
    else if (code == 401) setGithubError("DEVICE_TOKEN_DENIED", "Bridge rejected the device token.", "bridge");
    else setGithubError("BRIDGE_HTTP", "Bridge returned an HTTP error.", "bridge");
    return false;
  }
  bool ok = parseGithub(http.getStream());
  http.end();
  return ok;
}

void githubInit(const Settings&) { g_data.clear(); g_nextPollMs = millis(); }
void githubForceRefresh() { g_nextPollMs = millis(); }
const GithubData& githubGet() { return g_data; }
bool githubFresh(uint32_t withinMs) { return g_data.valid && millis() - g_data.lastOkMs <= withinMs; }

void githubService(const Settings& s) {
  if ((int32_t)(millis() - g_nextPollMs) < 0) return;
  uint32_t before = g_data.revision;
  if (!fetchGithub(s) && g_data.revision == before)
    setGithubError("FETCH_FAILED", "GitHub data unavailable.", "device");
  g_nextPollMs = millis() + (uint32_t)s.github.pollSec * 1000UL;
}
