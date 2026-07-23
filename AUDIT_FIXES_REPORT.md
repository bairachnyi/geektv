# Audit Fixes Report — smalltv-ultra firmware

**Date:** 2025-07-23
**Files modified:** 8
**Issues addressed:** 14 (2 Critical, 3 High, 5 Medium, 4 Low)

---

## Changes by file

### 1. `src/Settings.h`
**Added field to `Settings` struct (line ~130):**
```cpp
String adminPass;     // empty => no auth required; default "1111"
```
Placed after `hostname`, before mode/carousel fields. Type matches existing `String` members.

### 2. `src/Settings.cpp`

**`Settings::setDefaults()` (~line 270):**
```cpp
adminPass = "1111";
```

**`settingsToJson()` (~line 340):**
```cpp
if (includeSecrets) root["adminPass"] = s.adminPass;
```
Only serialized when `includeSecrets=true` (internal save). Excluded from web API export (`includeSecrets=false`).

**`settingsApplyJson()` (~line 395):**
```cpp
if (root["adminPass"].is<const char*>()) {
  String p = root["adminPass"].as<String>();
  if (p.length() > 0) s.adminPass = p;
}
```
Blank password keeps the stored one (matches existing WiFi/AP password pattern).

---

### 3. `src/WebPortal.cpp`

**Auth system (new, after static vars ~line 20):**

```cpp
static bool             g_authed = false;
static uint32_t         g_authExpiry = 0;
static const uint32_t AUTH_SESSION_MS = 3600000UL; // 1 hour

static bool checkAuth() {
  if (!S || S->adminPass.length() == 0) return true;  // no password = open
  if (g_authed && millis() < g_authExpiry) return true;
  // Check header X-Admin-Pass
  if (server.hasHeader("X-Admin-Pass")) {
    if (server.header("X-Admin-Pass") == S->adminPass) {
      g_authed = true; g_authExpiry = millis() + AUTH_SESSION_MS;
      return true;
    }
  }
  // Check query param ?pass=
  if (server.arg("pass") == S->adminPass) {
    g_authed = true; g_authExpiry = millis() + AUTH_SESSION_MS;
    return true;
  }
  return false;
}

static bool requireAuth() {
  if (checkAuth()) return true;
  server.send(401, "application/json", "{\"ok\":false,\"error\":\"auth required\"}");
  return false;
}
```

**Design notes:**
- Empty `adminPass` disables auth entirely (backward-compatible with existing configs)
- Session lasts 1 hour, stored in RAM only (lost on reboot = re-login required)
- Auth via HTTP header `X-Admin-Pass` (primary) or query param `?pass=` (fallback)
- `requireAuth()` sends 401 JSON and returns false — callers just `if (!requireAuth()) return;`

**`handleLogin()` (new endpoint):**
```cpp
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
        g_authed = true; g_authExpiry = millis() + AUTH_SESSION_MS;
        server.send(200, "application/json", "{\"ok\":true}");
        return;
      }
    }
  }
  server.send(200, "application/json", "{\"ok\":false}");
}
```
Always returns HTTP 200 (bad password → `{"ok":false}`, good → `{"ok":true}`). JS checks `d.ok`.

**Auth-wrapped handlers (added `if (!requireAuth()) return;` at top):**
- `handleGetConfig`, `handleStatus`, `handlePostConfig`, `handleScan`
- `handleReboot`, `handleFactory`, `handleExport`, `handleImport`
- `handleRefresh`, `handleCheckUpdate`, `handleSelfUpdate`
- `handlePhotoDelete`, `handlePhotoUploadDone`, `handlePhotoUpload`

**NOT auth-wrapped (intentionally open):**
- `handleRoot` — serves the HTML page (login overlay is client-side)
- `handleLogin` — must be accessible without auth
- `handleUpdateDone` / `handleUpdateUpload` — `/update` OTA endpoint stays open so device is always flashable
- `handleNotFound` — photo serving, captive portal redirects
- `handleUsagePush` — external bridge push endpoint

**Path traversal fix in `handlePhotoDelete()` (~line 387):**
```cpp
// Before: if (path[0] && LittleFS.exists(path)) {
if (path[0] && strncmp(path, "/photos/", 9) == 0 && LittleFS.exists(path)) {
```

**Body size limit in `handlePostConfig()` and `handleImport()`:**
```cpp
if (server.arg("plain").length() > 8192) {
  server.send(413, "text/plain", "payload too large"); return;
}
```

**`saveSettings()` return value checked in `handlePostConfig()`:**
```cpp
bool saved = saveSettings(*S);
// ...
res["ok"] = saved;  // was: res["ok"] = true;
```

**`saveSettings()` return value checked in `handleImport()`:**
```cpp
bool saved = saveSettings(*S);
server.send(saved ? 200 : 500, "application/json",
            saved ? "{\"ok\":true,\"reboot\":true}" : "{\"ok\":false,\"error\":\"save failed\"}");
if (saved) scheduleReboot(800);
```

**Route registration — added login endpoint:**
```cpp
server.on("/api/login", HTTP_POST, handleLogin);
```

---

### 4. `src/webui.h`

**XSS fix in `esc()` (~line 468):**
```javascript
// Before: .replace(/[<>&"]/g, function(c){return {'<':'&lt;','>':'&gt;','&':'&amp;','"':'&quot;'}[c]})
// After:
function esc(s){return (''+(s==null?'':s)).replace(/[<>&"']/g,function(c){
  return {'<':'&lt;','>':'&gt;','&':'&amp;','"':'&quot;',"'":'&#39;'}[c]
})}
```
Now escapes single quotes — prevents injection in `onclick` handlers (`deletePhoto('"+esc(p.path)+"')`).

**XSS fix in `kv()` (~line 861):**
```javascript
// Before: function kv(k,v){return '...<b>'+v+'</b></div>'}
function kv(k,v){return '<div class="kv"><span class="muted">'+k+'</span><b>'+esc(v)+'</b></div>'}
```
All status values (hostname, SSID, IP, reset reason) are now escaped before innerHTML insertion.

**Login overlay (HTML, before `<script>`):**
```html
<div id="loginOverlay" style="position:fixed;inset:0;background:var(--bg);z-index:200;display:none;...">
  <div class="card" style="max-width:320px;width:100%">
    <h2>Enter Password</h2>
    <input id="loginPass" type="password" placeholder="Password" onkeydown="if(event.key==='Enter')doLogin()">
    <div style="margin-top:12px"><button class="btn" onclick="doLogin()">Login</button></div>
    <div id="loginErr" ...>Wrong password</div>
  </div>
</div>
```
Initially `display:none`. Shown by `showLogin()` (sets `display:flex`). Hidden after successful login.

**Auth state and fetch wrapper (~line 458):**
```javascript
var _adminPass='';

function j(url,opt){
  if(!opt)opt={};
  if(!opt.headers)opt.headers={};
  if(_adminPass)opt.headers['X-Admin-Pass']=_adminPass;
  return fetch(url,opt).then(function(r){
    if(r.status===401){showLogin();throw Error('auth required')}
    return r.json()
  });
}
```
All API calls via `j()` automatically include the auth header. 401 response triggers login overlay.

**Login function (~line 472):**
```javascript
function doLogin(){
  var p=$('loginPass').value;
  fetch('/api/login',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({pass:p})})
  .then(function(r){return r.json()}).then(function(d){
    if(d.ok){_adminPass=p;$('loginOverlay').style.display='none';loadConfig().then(loadStatus)}
    else{$('loginErr').style.display='block';...}
  }).catch(function(){...});
}
```
Uses raw `fetch()` (not `j()`) to avoid the 401 interceptor on the login endpoint itself.

**Admin password field in WiFi tab (~line 126):**
```html
<div class="card"><h2>Admin password</h2>
  <label>Web UI password</label>
  <input id="adminPass" type="password" placeholder="1111">
  <small class="hint">Leave blank to disable. Default is <code>1111</code>.</small>
</div>
```

**Config load — reads adminPass (~line 537):**
```javascript
if($('adminPass'))$('adminPass').placeholder=c.adminPass?'(set — blank keeps it)':'(none)';
```

**Config save — includes adminPass (~line 604):**
```javascript
adminPass:gv('adminPass'),
```

---

### 5. `src/main.cpp`

**`settingsBegin()` return value check (~line 241):**
```cpp
if (!settingsBegin()) {
  Serial.println("[boot] LittleFS mount FAILED — running with defaults");
  gfxBoot("SmallTV", "FS error");
  delay(2000);
}
loadSettings(g_settings);
```
Device continues with defaults if FS is corrupted. Shows "FS error" on boot screen for 2 seconds.

---

### 6. `src/features/clock/ClockMode.cpp`

**Weather URLs changed to HTTPS (~lines 94, 98):**
```cpp
url = "https://api.openweathermap.org/data/2.5/weather?q=...";
url = "https://wttr.in/" + city + "?format=j1";
```

**Weather fetch rewritten for HTTPS + JSON filter (~line 83-155):**
- Uses `platformMakeSecureClient(2048)` for HTTPS connections instead of plain `WiFiClient`
- Heap guard: `if (ESP.getFreeHeap() < 14000)` — prevents OOM on tight ESP8266
- `DeserializationOption::Filter` applied to both OWM and wttr.in responses:
  ```cpp
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
  DeserializationError err = deserializeJson(doc, http.getStream(),
      DeserializationOption::Filter(filter));
  ```
- `http.useHTTP10(true)` added to prevent chunked transfer issues

---

### 7. `src/features/ticker/StockClient.cpp`

**GraphQL injection fix (~line 145):**
```cpp
static String sanitizeSymbol(const char* sym) {
  String out;
  for (const char* p = sym; *p; p++) {
    char c = *p;
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '/' || c == ':')
      out += c;
  }
  return out;
}
```
`buildCashQuoteUrl()` and `buildCashChartUrl()` now call `sanitizeSymbol(symbol)` instead of embedding raw symbol.

**Bounds check in `stockAt()` (~line 38):**
```cpp
const StockData& stockAt(uint8_t i) { return (i < g_count) ? g_stocks[i] : g_stocks[0]; }
```

---

### 8. `src/features/ticker/TickerMode.cpp`

**Portfolio name buffer fix (~line 238):**
```cpp
char nm[22];  // was: char nm[10];
```
Matches `MAX_NAME_LEN` (20) + null terminator. Prevents truncation of long stock names.

---

## What was NOT changed (intentional)

| Item | Reason |
|------|--------|
| `setInsecure()` in `Platform.h` | User explicitly said security is not a concern; this saves heap on ESP8266 |
| `/update` OTA endpoint (no auth) | Device must always be flashable, even if admin password is forgotten |
| `BearSslTuning.cpp` | No issues found; TLS tuning is correct for the ESP8266 memory constraints |
| `GithubClient.cpp` token header | Token is sent to user's own LAN bridge; MITM risk accepted |
| `OtaUpdate.cpp` `setInsecure()` | Same reasoning as above; OTA from GitHub stays as-is |
| `Clock.cpp` volatile vars | Single-core ESP8266 has no torn-read issue; ESP32 impact is theoretical |

---

## Potential follow-up items (not bugs, suggestions)

1. **`esc()` in `symHintFor()`** — The innerHTML in `symHintFor` uses `esc()` for cash.ch values; already safe after the fix.
2. **`mascot_frames.h`** — Large PROGMEM array in header; only included from `Mascot.cpp` so no waste currently.
3. **`GalleryMode.h` PhotoItem** — Uses `String` for filenames; could be `char[]` to reduce heap fragmentation, but the gallery is not on the critical path.
4. **`StockClient.cpp:38` `stockAt()`** — Returns `g_stocks[0]` on out-of-bounds; all current callers are safe, but a `StockData::clear()` return would be cleaner.

---

## Verification checklist

- [ ] OTA upload at `/update` works without auth
- [ ] First boot with default password "1111" — login overlay appears
- [ ] Login with correct password → full access
- [ ] Login with wrong password → "Wrong password" message
- [ ] Changing password in WiFi tab → save → reboot → new password required
- [ ] Blank password in settings → auth disabled
- [ ] Weather displays correctly (HTTPS, wttr.in fallback)
- [ ] Photo delete only works for `/photos/` paths
- [ ] Config export does NOT include adminPass (includeSecrets=false)
- [ ] Large POST body (>8KB) rejected with 413
- [ ] LittleFS mount failure shows "FS error" on boot screen
