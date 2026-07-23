#include "WebPortal.h"
#include "Platform.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "webui.h"
#include "Net.h"
#include "Gfx.h"
#include "OtaUpdate.h"
#include "StockClient.h"
#include "UsageClient.h"
#include "GithubClient.h"
#include "Clock.h"

// Defined in main.cpp — re-init every mode + force a repaint after a config change.
extern void appInvalidate();
extern const char* appResetReason();   // last reset reason (diagnostics)
extern void appApplyBrightness();   // main.cpp: re-resolve effective brightness now

static WebServerClass server(80);
static Settings*        S = nullptr;
static bool             g_reboot = false;
static uint32_t         g_rebootAt = 0;
static bool             g_selfUpdate = false;   // GitHub self-update requested
static String           g_updateMsg;            // last self-update status/error
static bool             g_authed = false;       // true after password check passes
static uint32_t         g_authExpiry = 0;       // millis() when session expires

static const uint32_t AUTH_SESSION_MS = 3600000UL; // 1 hour

static bool checkAuth() {
  if (!S || S->adminPass.length() == 0) return true;  // no password set => open
  if (g_authed && millis() < g_authExpiry) return true;

  // Check header
  if (server.hasHeader("X-Admin-Pass")) {
    if (server.header("X-Admin-Pass") == S->adminPass) {
      g_authed = true;
      g_authExpiry = millis() + AUTH_SESSION_MS;
      return true;
    }
  }
  // Check query param
  if (server.arg("pass") == S->adminPass) {
    g_authed = true;
    g_authExpiry = millis() + AUTH_SESSION_MS;
    return true;
  }
  return false;
}

static bool requireAuth() {
  if (checkAuth()) return true;
  server.send(401, "application/json", "{\"ok\":false,\"error\":\"auth required\"}");
  return false;
}

static void scheduleReboot(uint32_t inMs) {
  g_reboot = true;
  g_rebootAt = millis() + inMs;
}

// ---------------------------------------------------------------------------
static void sendJson(JsonDocument& doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

static void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", WEBUI_HTML);
}

static void handleLogin() {
  if (!S || S->adminPass.length() == 0) {
    g_authed = true; g_authExpiry = millis() + AUTH_SESSION_MS;
    server.send(200, "application/json", "{\"ok\":true}");
    return;
  }
  if (server.hasArg("plain")) {
    JsonDocument doc;
    if (!deserializeJson(doc, server.arg("plain"))) {
      const char* p = doc["pass"] | "";
      if (String(p) == S->adminPass) {
        g_authed = true;
        g_authExpiry = millis() + AUTH_SESSION_MS;
        server.send(200, "application/json", "{\"ok\":true}");
        return;
      }
    }
  }
  server.send(200, "application/json", "{\"ok\":false}");
}

static void handleGetConfig() {
  if (!requireAuth()) return;
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  settingsToJson(*S, root, /*includeSecrets=*/false);
  // Which features are compiled in (so a lean build hides the tabs it dropped).
  JsonObject feat = root["features"].to<JsonObject>();
  feat["ticker"]  = (bool)WITH_TICKER;
  feat["usage"]   = (bool)WITH_USAGE;
  feat["github"]  = (bool)WITH_GITHUB;
  feat["clock"]   = (bool)WITH_CLOCK;
  feat["gallery"] = (bool)WITH_GALLERY;
  // Which chip this build runs on (the UI warns about per-chip limitations).
#if defined(SMALLTV_ESP32C2)
  root["chip"] = "esp32c2";
#elif defined(SMALLTV_ESP32)
  root["chip"] = "esp32";
#else
  root["chip"] = "esp8266";
#endif
  sendJson(doc);
}

static void handleStatus() {
  if (!requireAuth()) return;
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  o["fw"] = FW_NAME;
  o["version"] = FW_VERSION;
  o["repo"] = REPO_URL;
  if (g_updateMsg.length()) o["updateMsg"] = g_updateMsg;
  o["mode"] = (netMode() == NET_AP) ? "ap" : "sta";
  o["connected"] = netConnected();
  o["ssid"] = netSSID();
  o["ip"] = netIP();
  o["rssi"] = netRSSI();
  o["heap"] = ESP.getFreeHeap();
  o["maxblk"] = platformMaxFreeBlock();     // largest contiguous block (TLS handshake needs one)
  o["contstk"] = platformFreeContStack();   // primary stack headroom (ESP8266)
  o["uptime"] = millis() / 1000;
  o["reset"] = appResetReason();
  o["synced"] = clockSynced();
  { String ts = clockTimeStr(); if (ts.length()) o["time"] = ts; }
  o["tz"]        = S->clock.tz;
  o["night"]     = clockNightActive();   // dimming now
  o["nightHeld"] = clockNightHeld();      // in the window but waiting for a fresh NTP sync
  o["clockFresh"] = clockTrusted();       // last NTP sync within the trust window

#if WITH_TICKER
  JsonArray arr = o["tickers"].to<JsonArray>();
  for (uint8_t i = 0; i < stocksCount(); i++) {
    const StockData& d = stockAt(i);
    JsonObject t = arr.add<JsonObject>();
    t["symbol"] = d.symbol;
    t["valid"] = d.valid;
    t["error"] = d.error;
    if (d.valid) {
      t["price"] = d.price;
      float chg, pct;
      bool onRange = false;
      if (stockDisplayChange(d, S->ticker, chg, pct, &onRange)) {
        t["changePct"] = pct;                       // as displayed on the device
        t["basis"] = onRange ? "range" : "day";     // which basis that was
      }
    }
  }
#endif
  sendJson(doc);
}

// Fingerprint of everything network-identity related: the WiFi list and the
// hostname. Changing any of it needs a reboot, because the connection and the
// mDNS registration are established once at boot.
static String netFingerprint(const Settings& s) {
  String f((int)s.wifiCount);
  for (uint8_t i = 0; i < s.wifiCount; i++) {
    f += '\n';
    f += s.wifi[i].ssid;
    f += '\x01';
    f += s.wifi[i].pass;
  }
  f += '\n';
  f += s.hostname;
  return f;
}

static void handlePostConfig() {
  if (!requireAuth()) return;
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
  if (server.arg("plain").length() > 8192) { server.send(413, "text/plain", "payload too large"); return; }

  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json");
    return;
  }

  String oldNet = netFingerprint(*S);
  uint8_t oldRot = S->rotation;

  settingsApplyJson(*S, doc.as<JsonObjectConst>());
  bool saved = saveSettings(*S);

  // Live apply (no reboot needed for these)
  clockReapply(*S);         // re-arm SNTP iff the timezone changed
  appApplyBrightness();     // apply effective brightness (respects night/auto/manual)
  if (S->rotation != oldRot) gfxSetRotation(S->rotation);
  appInvalidate();          // re-init every mode + repaint (covers mode/URL/symbol changes)

  bool wifiChanged = netFingerprint(*S) != oldNet;

  JsonDocument res;
  res["ok"] = saved;
  res["reboot"] = wifiChanged;
  sendJson(res);

  if (wifiChanged) scheduleReboot(800);
}

static void handleScan() {
  if (!requireAuth()) return;
  int n = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n && i < 25; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["enc"] = !platformScanIsOpen(i);
  }
  WiFi.scanDelete();
  sendJson(doc);
}

static void handleReboot() {
  if (!requireAuth()) return;
  server.send(200, "application/json", "{\"ok\":true}");
  scheduleReboot(400);
}

static void handleFactory() {
  if (!requireAuth()) return;
  factoryReset(*S);
  saveSettings(*S);
  server.send(200, "application/json", "{\"ok\":true}");
  scheduleReboot(400);
}

// Full settings backup: stream the persisted config.json verbatim. It includes
// the WiFi passwords — same trust domain as typing them into this page.
static void handleExport() {
  if (!requireAuth()) return;
  File f = LittleFS.open("/config.json", "r");
  if (!f) { server.send(404, "text/plain", "no config saved yet"); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=smalltv-config.json");
  server.streamFile(f, "application/json");
  f.close();
}

// Restore a backup: apply everything, persist, reboot (WiFi/hostname may change).
static void handleImport() {
  if (!requireAuth()) return;
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
  if (server.arg("plain").length() > 8192) { server.send(413, "text/plain", "payload too large"); return; }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json");
    return;
  }
  settingsApplyJson(*S, doc.as<JsonObjectConst>());
  bool saved = saveSettings(*S);
  server.send(saved ? 200 : 500, "application/json",
              saved ? "{\"ok\":true,\"reboot\":true}" : "{\"ok\":false,\"error\":\"save failed\"}");
  if (saved) scheduleReboot(800);
}

static void handleRefresh() {
  if (!requireAuth()) return;
#if WITH_TICKER
  stocksForceRefresh();
#endif
#if WITH_GITHUB
  githubForceRefresh();
#endif
  server.send(200, "application/json", "{\"ok\":true}");
}

// Check the newest GitHub release against the running version.
static void handleCheckUpdate() {
  if (!requireAuth()) return;
  OtaLatest r = otaCheckLatest(*S);
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  o["current"] = FW_VERSION;
  o["ok"] = r.ok;
  o["latest"] = r.tag;
  o["newer"] = r.newer;
  if (!r.ok) o["error"] = r.error;
  sendJson(doc);
}

// Trigger the self-update. The actual (blocking) download runs from the loop so
// this response returns first; on success the device reboots into the new image.
static void handleSelfUpdate() {
  if (!requireAuth()) return;
  g_selfUpdate = true;
  g_updateMsg = "starting...";
  server.send(200, "application/json", "{\"ok\":true}");
}

// Push endpoint: a trusted LAN bridge POSTs compact AI usage when the device
// cannot pull it. Legacy Claude payload keys remain accepted.
static void handleUsagePush() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
#if WITH_USAGE
  bool ok = usageApply(server.arg("plain"));
#else
  bool ok = false;
#endif
  server.send(ok ? 200 : 400, "application/json",
              ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

// ---- OTA ------------------------------------------------------------------
static void handleUpdateDone() {
  bool ok = !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : platformUpdateError().c_str());
  if (ok) scheduleReboot(1200);
}

static void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
#if defined(SMALLTV_ESP8266)
    WiFiUDP::stopAll();   // free UDP sockets so the OTA has max contiguous flash/heap
#endif
    uint32_t freeSketch = ESP.getFreeSketchSpace();
    uint32_t maxSpace = (freeSketch > 0x1000) ? ((freeSketch - 0x1000) & 0xFFFFF000) : freeSketch;
    if (maxSpace == 0) maxSpace = freeSketch;
    if (!Update.begin(maxSpace)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.end();
  }
  yield();
}

// ---- Photo Gallery --------------------------------------------------------
static void handleGetPhotos() {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  JsonArray arr = root["photos"].to<JsonArray>();

  FSInfo fs_info;
  if (LittleFS.info(fs_info)) {
    root["fsTotal"] = fs_info.totalBytes;
    root["fsUsed"] = fs_info.usedBytes;
    root["fsFree"] = (fs_info.totalBytes > fs_info.usedBytes) ? (fs_info.totalBytes - fs_info.usedBytes) : 0;
  }

  if (!LittleFS.exists("/photos")) {
    LittleFS.mkdir("/photos");
  }

  Dir dir = LittleFS.openDir("/photos");
  while (dir.next()) {
    String name = dir.fileName();
    if (name.endsWith(".jpg") || name.endsWith(".jpeg") || name.endsWith(".png") || name.endsWith(".gif") || name.endsWith(".raw")) {
      JsonObject o = arr.add<JsonObject>();
      o["name"] = name;
      o["path"] = "/photos/" + name;
      o["size"] = dir.fileSize();
    }
  }

  sendJson(doc);
}

static void handlePhotoDelete() {
  if (!requireAuth()) return;
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json");
    return;
  }
  const char* path = doc["path"] | "";
  bool deleted = false;
  // Only allow deleting files inside /photos/ directory
  if (path[0] && strncmp(path, "/photos/", 8) == 0 && LittleFS.exists(path)) {
    deleted = LittleFS.remove(path);
  }
  server.send(deleted ? 200 : 404, "application/json", deleted ? "{\"ok\":true}" : "{\"ok\":false}");
  appInvalidate();
}

static File s_uploadFile;
static void handlePhotoUploadDone() {
  if (!checkAuth()) {
    server.send(401, "application/json", "{\"ok\":false,\"error\":\"auth required\"}");
    return;
  }
  if (s_uploadFile) {
    s_uploadFile.close();
  }
  server.sendHeader("Connection", "close");
  server.send(200, "application/json", "{\"ok\":true}");
  appInvalidate();
}

static void handlePhotoUpload() {
  if (!checkAuth()) return;
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (!LittleFS.exists("/photos")) LittleFS.mkdir("/photos");
    String filename = up.filename;
    int lastSlash = filename.lastIndexOf('/');
    if (lastSlash >= 0) filename = filename.substring(lastSlash + 1);
    
    // Sanitize filename: convert to lowercase standard characters
    filename.toLowerCase();
    filename.replace(" ", "_");
    filename.replace("%20", "_");
    
    if (!filename.endsWith(".jpg") && !filename.endsWith(".jpeg") && !filename.endsWith(".png") && !filename.endsWith(".gif")) {
      filename += ".jpg";
    }

    String targetPath = "/photos/" + filename;
    if (LittleFS.exists(targetPath)) LittleFS.remove(targetPath);
    s_uploadFile = LittleFS.open(targetPath, "w");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (s_uploadFile) s_uploadFile.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    if (s_uploadFile) s_uploadFile.close();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (s_uploadFile) s_uploadFile.close();
  }
}

// ---- captive portal & file serving ----------------------------------------
static void handleNotFound() {
  if (server.uri().startsWith("/photos/")) {
    String path = server.uri();
    if (LittleFS.exists(path)) {
      File f = LittleFS.open(path, "r");
      String contentType = "application/octet-stream";
      if (path.endsWith(".jpg") || path.endsWith(".jpeg")) contentType = "image/jpeg";
      else if (path.endsWith(".gif")) contentType = "image/gif";
      server.streamFile(f, contentType);
      f.close();
      return;
    }
  }

  if (netMode() == NET_AP) {
    // Redirect everything to the config page so the captive portal pops.
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

// ---------------------------------------------------------------------------
void webPortalBegin(Settings& settings) {
  S = &settings;

  server.collectHeaders("X-Admin-Pass");

  // If the last boot ran a queued GitHub update and failed, surface why
  // (success reboots into the new image before we ever get here).
  g_updateMsg = otaTakeBootResult();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/login", HTTP_POST, handleLogin);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/factory", HTTP_POST, handleFactory);
  server.on("/api/refresh", HTTP_POST, handleRefresh);
  server.on("/api/export", HTTP_GET, handleExport);
  server.on("/api/import", HTTP_POST, handleImport);
  server.on("/api/checkupdate", HTTP_GET, handleCheckUpdate);
  server.on("/api/selfupdate", HTTP_POST, handleSelfUpdate);
  server.on("/api/usage", HTTP_POST, handleUsagePush);      // legacy route
  server.on("/api/ai-usage", HTTP_POST, handleUsagePush);   // preferred route
  server.on("/api/photos", HTTP_GET, handleGetPhotos);
  server.on("/api/photos/delete", HTTP_POST, handlePhotoDelete);
  server.on("/api/photos/upload", HTTP_POST, handlePhotoUploadDone, handlePhotoUpload);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);

  // Common captive-portal probe endpoints
  server.on("/generate_204", handleNotFound);
  server.on("/gen_204", handleNotFound);
  server.on("/hotspot-detect.html", handleNotFound);
  server.on("/connecttest.txt", handleNotFound);
  server.onNotFound(handleNotFound);

  server.begin();
}

void webPortalLoop() {
  server.handleClient();

  // Run the GitHub self-update outside the request handler so the browser gets its
  // response first.
  if (g_selfUpdate) {
    g_selfUpdate = false;
#if defined(SMALLTV_ESP8266)
    // RAM-tight chip: verify there is something to install, then queue the
    // download for the next boot (otaBootUpdate in setup(), ~45 KB free) and
    // reboot. A failure there lands back in g_updateMsg via otaTakeBootResult.
    OtaLatest r = otaCheckLatest(*S);
    if (!r.ok)         g_updateMsg = "check failed: " + r.error;
    else if (!r.newer) g_updateMsg = "already up to date (" FW_VERSION ")";
    else if (otaRequestBootUpdate(r.tag.c_str())) {
      g_updateMsg = "updating...";
      scheduleReboot(400);
    } else {
      g_updateMsg = F("could not queue update (storage error)");
    }
#else
    // ESP32 targets: mbedTLS has the RAM to download in place; blocks while it
    // runs and reboots into the new image on success.
    String err = otaUpdateFromGitHub(*S);
    g_updateMsg = err.length() ? err : "updating...";
#endif
  }
}

bool webPortalRebootDue() {
  return g_reboot && (int32_t)(millis() - g_rebootAt) >= 0;
}
