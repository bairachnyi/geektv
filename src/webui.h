// webui.h — single-page config UI served from PROGMEM
//
// Tabs are segmented per feature: shared Status/WiFi/Display/Update plus one tab
// per feature (Ticker / AI Usage / GitHub). The config JSON
// mirrors the nested Settings layout: { ..shared.., ticker:{...}, usage:{...} }.
#pragma once
#include <Arduino.h>

static const char WEBUI_HTML[] PROGMEM = R"HTMLPAGE(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>GeekTV</title>
<style>
:root{--bg:#0e1116;--card:#171c24;--mut:#8b96a5;--fg:#e6edf3;--acc:#3fb950;--acc2:#2f81f7;--red:#f85149;--bd:#262d38}
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--fg);font-size:15px}
header{padding:14px 16px;border-bottom:1px solid var(--bd);display:flex;align-items:center;gap:10px}
header h1{font-size:17px;margin:0;font-weight:600}
header .dot{width:9px;height:9px;border-radius:50%;background:var(--mut)}
header .dot.ok{background:var(--acc)}
nav{display:flex;gap:4px;padding:8px;overflow-x:auto;border-bottom:1px solid var(--bd);position:sticky;top:0;background:var(--bg);z-index:5}
nav button{background:none;border:0;color:var(--mut);padding:8px 12px;border-radius:8px;font-size:14px;cursor:pointer;white-space:nowrap}
nav button.active{background:var(--card);color:var(--fg)}
main{padding:16px;max-width:680px;margin:0 auto}
.tab{display:none}.tab.active{display:block}
.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:16px;margin-bottom:14px}
h2{font-size:14px;text-transform:uppercase;letter-spacing:.04em;color:var(--mut);margin:0 0 12px}
label{display:block;margin:10px 0 4px;font-size:13px;color:var(--mut)}
input[type=text],input[type=password],input[type=number],input[type=url],select,textarea{
 width:100%;padding:9px 10px;background:#0b0e13;border:1px solid var(--bd);border-radius:8px;color:var(--fg);font-size:15px}
textarea{min-height:76px;resize:vertical;font-family:inherit}
input[type=range]{width:100%}
.row{display:flex;gap:10px}.row>*{flex:1}
.chk{display:flex;align-items:center;gap:8px;margin:8px 0}
.chk input{width:18px;height:18px}
.chk label{margin:0;color:var(--fg);font-size:14px}
button.btn{background:var(--acc);color:#04130a;border:0;padding:10px 16px;border-radius:9px;font-size:15px;font-weight:600;cursor:pointer}
button.btn.sec{background:#222b36;color:var(--fg)}
button.btn.danger{background:var(--red);color:#1a0606}
button.btn:disabled{opacity:.5}
.muted{color:var(--mut);font-size:13px}
table{width:100%;border-collapse:collapse}
td{padding:6px 4px}
.symrow input{margin:0}
.kv{display:flex;justify-content:space-between;padding:5px 0;border-bottom:1px solid var(--bd)}
.kv:last-child{border:0}.kv b{font-weight:600}
.toast{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);background:#0b0e13;border:1px solid var(--bd);padding:10px 16px;border-radius:10px;opacity:0;transition:.3s;pointer-events:none}
.toast.show{opacity:1}
.net{padding:8px;border:1px solid var(--bd);border-radius:8px;margin:4px 0;cursor:pointer;display:flex;justify-content:space-between}
.net:hover{border-color:var(--acc2)}
.bar{height:8px;background:#0b0e13;border-radius:6px;overflow:hidden;margin-top:8px}
.bar>div{height:100%;width:0;background:var(--acc2);transition:.2s}
small.hint{display:block;color:var(--mut);margin-top:4px;font-size:12px}
.chip{display:inline-block;margin-left:8px;padding:2px 8px;border-radius:10px;font-size:11px;font-weight:600;letter-spacing:.03em;background:var(--acc2);color:#fff;vertical-align:middle}
.crop-modal{position:fixed;inset:0;background:rgba(0,0,0,.85);display:none;align-items:center;justify-content:center;z-index:100;padding:16px}
.crop-modal.show{display:flex}
.crop-card{background:var(--card);border:1px solid var(--bd);border-radius:16px;max-width:540px;width:100%;max-height:90vh;display:flex;flex-direction:column;overflow:hidden;box-shadow:0 20px 50px rgba(0,0,0,.6)}
.crop-head{padding:14px 16px;border-bottom:1px solid var(--bd);display:flex;justify-content:space-between;align-items:center}
.crop-head h3{margin:0;font-size:16px}
.crop-body{padding:16px;overflow:auto;display:flex;justify-content:center;align-items:center;background:#05080c;min-height:280px;position:relative}
.crop-wrap{position:relative;user-select:none;touch-action:none;display:inline-block;max-width:100%}
.crop-wrap img{display:block;max-width:100%;max-height:55vh;object-fit:contain}
.crop-box{position:absolute;border:2px solid var(--acc2);box-shadow:0 0 0 9999px rgba(0,0,0,.6);cursor:move;box-sizing:border-box}
.crop-box::after{content:"240×240";position:absolute;top:6px;left:6px;background:rgba(0,0,0,.7);color:#fff;font-size:10px;padding:2px 6px;border-radius:4px;pointer-events:none}
.crop-handle{position:absolute;width:16px;height:16px;background:var(--acc2);border:2px solid #fff;border-radius:50%;z-index:10}
.crop-handle.nw{top:-8px;left:-8px;cursor:nwse-resize}
.crop-handle.ne{top:-8px;right:-8px;cursor:nesw-resize}
.crop-handle.sw{bottom:-8px;left:-8px;cursor:nesw-resize}
.crop-handle.se{bottom:-8px;right:-8px;cursor:nwse-resize}
.crop-foot{padding:14px 16px;border-top:1px solid var(--bd);display:flex;justify-content:flex-end;gap:10px}
</style></head>
<body>
<header><span id="dot" class="dot"></span><h1>GeekTV</h1><span id="chip" class="chip" style="display:none"></span><span id="hi" class="muted"></span></header>
<nav>
 <button data-t="status" class="active">Status</button>
 <button data-t="wifi">WiFi</button>
 <button data-t="display">Display</button>
 <button data-t="clocktab">Clock &amp; Weather</button>
 <button data-t="gallerytab">Gallery</button>
 <button data-t="ticker">Ticker</button>
 <button data-t="usage">AI Usage</button>
 <button data-t="github">GitHub</button>
 <button data-t="update">Update</button>
</nav>
<main>
 <!-- STATUS -->
 <section id="status" class="tab active">
  <div class="card"><h2>Device</h2><div id="statusBox" class="muted">Loading...</div></div>
  <div class="card"><h2>Tickers</h2><div id="tickBox" class="muted">-</div>
   <button class="btn sec" style="margin-top:10px" onclick="refreshNow()">Refresh data now</button></div>
  <div class="card"><h2>Documentation</h2>
   <small class="hint">Complete references for <a href="https://github.com/bairachnyi/smalltv-ultra/blob/main/docs/src/content/docs/reference/settings.md" target="_blank">all settings</a>, <a href="https://github.com/bairachnyi/smalltv-ultra/blob/main/docs/src/content/docs/features/github.md" target="_blank">GitHub GH//STAT</a>, <a href="https://github.com/bairachnyi/smalltv-ultra/blob/main/docs/src/content/docs/reference/architecture.md" target="_blank">firmware architecture</a> and the <a href="https://github.com/bairachnyi/smalltv-ultra/blob/main/docs/src/content/docs/reference/http-api.md" target="_blank">HTTP API</a>.</small>
  </div>
  <div class="card"><h2>Factory V9.0.51 compatibility</h2>
   <div class="hint" style="line-height:1.55">
    <p><b>Clock themes:</b> not yet ported to this candidate.</p>
    <p><b>Weather and forecast:</b> not yet ported to this candidate.</p>
    <p><b>Photo gallery:</b> not yet ported to this candidate.</p>
    <p style="color:#ffb627"><b>Do not flash this candidate over the factory firmware yet.</b> These three factory features must be restored and tested first. The current local UI is an emulator, not a complete V9.0.51 replacement.</p>
   </div>
  </div>
 </section>

 <!-- WIFI -->
 <section id="wifi" class="tab">
  <div class="card"><h2>Saved networks</h2>
   <button class="btn sec" onclick="scan()">Scan networks</button>
   <div id="scanList"></div>
   <table id="wifiTable"></table>
   <button class="btn sec" style="margin-top:10px" onclick="addWifi()">+ Add network</button>
   <div style="margin-top:14px"><button class="btn" onclick="saveWifi()">Save &amp; connect (reboots)</button></div>
   <small class="hint">2.4&nbsp;GHz only. Up to 4 networks; at boot the device joins the strongest one it can see. Tap a scan result to fill a row. Leave a password blank to keep the stored one.</small>
  </div>
  <div class="card"><h2>Device name</h2>
   <label>Hostname</label><input id="hostname" type="text" placeholder="smalltv">
   <small class="hint">Reachable as <code>http://&lt;hostname&gt;.local</code> via mDNS. Running several SmallTVs? Give each its own name (<code>smalltv-desk</code>, <code>smalltv-shelf</code>) so local bridges reach the right device. Saving a new name reboots the device.</small>
  </div>
  <div class="card"><h2>Setup hotspot (AP)</h2>
   <label>AP name</label><input id="apSsid" type="text">
   <label>AP password <span class="muted">(blank = open, else min 8 chars)</span></label>
   <input id="apPass" type="text" placeholder="(unchanged)">
   <small class="hint">The AP appears when no WiFi is configured or the connection fails.</small>
  </div>
   <div class="card"><h2>Admin password</h2>
    <label>Web UI password</label>
    <input id="adminPass" type="password" placeholder="1111">
    <small class="hint">Leave blank to disable password protection. Default is <code>1111</code>. After changing, you'll need to enter the new password on next page load.</small>
   </div>
  </section>
 <section id="display" class="tab">
  <div class="card"><h2>Mode</h2>
   <label>What this device shows</label>
   <select id="mode" onchange="modeChanged()">
    <option value="stocks">Stock / crypto ticker</option>
    <option value="usage">AI usage (Antigravity + Codex)</option>
    <option value="github">GitHub deploys</option>
    <option value="clock">Clock: Giant Fullscreen</option>
    <option value="clock_weather">Clock: Weather Station (Today)</option>
    <option value="clock_modern">Clock: Modern Status Bar</option>
    <option value="clock_forecast">Clock: 3-Day Forecast Station</option>
    <option value="gallery">Photo Gallery (240x240)</option>
    <option value="carousel">Carousel (rotate modes)</option>
   </select>
   <div id="carouselRow">
    <label>Switch mode every (s)</label><input id="carouselSec" type="number" min="5" max="3600">
    <div class="chk"><input id="carouselTicker" type="checkbox"><label>Ticker</label></div>
    <div class="chk"><input id="carouselUsage" type="checkbox"><label>AI usage</label></div>
    <div class="chk"><input id="carouselGithub" type="checkbox"><label>GitHub deploys</label></div>
    <div class="chk"><input id="carouselClockDigital" type="checkbox"><label>Clock: Giant Fullscreen</label></div>
    <div class="chk"><input id="carouselClockWeather" type="checkbox"><label>Clock: Weather Station (Today)</label></div>
    <div class="chk"><input id="carouselClockModern" type="checkbox"><label>Clock: Modern Status Bar</label></div>
    <div class="chk"><input id="carouselClockForecast" type="checkbox"><label>Clock: 3-Day Forecast Station</label></div>
    <div class="chk"><input id="carouselGallery" type="checkbox"><label>Photo Gallery</label></div>
   </div>
   <small class="hint">Pick the active feature, then configure it in its own tab. Carousel rotates through the ticked features.</small>
  </div>
  <div class="card"><h2>Screen</h2>
   <label>Brightness: <span id="brVal"></span>%</label>
   <input id="brightness" type="range" min="0" max="100" oninput="brVal.textContent=this.value">
   <div class="chk"><input id="autoBrightness" type="checkbox"><label>Auto-brightness (light sensor on A0)</label></div>
   <label>Orientation</label>
   <select id="rotation"><option value="0">0&deg;</option><option value="1">90&deg;</option>
    <option value="2">180&deg;</option><option value="3">270&deg;</option></select>
   <div class="chk"><input id="backlightInverted" type="checkbox"><label>Backlight is active-low (try if screen stays dark)</label></div>
  </div>
  <div class="card"><h2>Clock &amp; night mode</h2>
   <label>Timezone</label>
   <select id="tz"></select>
   <div class="muted" id="clockNow" style="margin:8px 0">Clock: -</div>
   <div class="chk"><input id="nightEnabled" type="checkbox"><label>Dim or blank the screen on a nightly schedule</label></div>
   <div class="row">
    <div><label>From</label><input id="nightStart" type="time"></div>
    <div><label>To</label><input id="nightEnd" type="time"></div>
   </div>
   <label>Night brightness: <span id="nlVal"></span>% <span class="muted">(0 = screen off)</span></label>
   <input id="nightLevel" type="range" min="0" max="100" oninput="nlVal.textContent=this.value">
   <small class="hint">Needs internet once to set the clock over NTP (no on-screen clock, this just drives the schedule). While the window is active it overrides the brightness and auto-brightness above. Times are local to the selected timezone; DST is handled automatically. After a reboot the schedule resumes once the clock re-syncs, so the screen may show normal brightness for a few seconds.</small>
  </div>
 </section>

  <!-- CLOCK & WEATHER (feature) -->
  <section id="clocktab" class="tab">
    <div class="card"><h2>Clock Display Settings</h2>
     <div class="chk"><input id="format24h" type="checkbox"><label>24-hour time format (HH:MM)</label></div>
     <div class="chk"><input id="showSeconds" type="checkbox"><label>Show seconds (HH:MM:SS)</label></div>
     <div class="chk"><input id="showDate" type="checkbox"><label>Show day &amp; date</label></div>
     <label>Clock Theme</label>
     <select id="clockTheme">
      <option value="0">Giant Fullscreen Clock (Time Focus)</option>
      <option value="1">Weather Station (Today Focus)</option>
      <option value="2">Modern Status Bar (OLED Glass)</option>
      <option value="3">3-Day Weather Forecast Breakdown</option>
     </select>
     <label>Font Size</label>
     <select id="fontScale" onchange="pvRestart();updateClockPreview()">
      <option value="0">Theme default</option>
      <option value="1">Small</option>
      <option value="2">Medium</option>
      <option value="3">Large</option>
      <option value="4">Extra Large</option>
      <option value="5">Giant</option>
     </select>
     <div class="chk" style="margin-top:8px"><input id="boldText" type="checkbox" onchange="pvRestart();updateClockPreview()"><label>Bold text (thicker, smoother lines)</label></div>
    </div>
    <div class="card"><h2>Colors</h2>
     <div class="row">
      <div><label>Time color</label><input id="timeColor" type="color" value="#39e7ff" onchange="pvRestart();updateClockPreview()"></div>
      <div><label>Date color</label><input id="dateColor" type="color" value="#ffb6c1" onchange="pvRestart();updateClockPreview()"></div>
     </div>
     <div class="row">
      <div><label>Accent color</label><input id="accentColor" type="color" value="#58a9ff" onchange="pvRestart();updateClockPreview()"></div>
      <div><label>Background</label><input id="bgColor" type="color" value="#000000" onchange="pvRestart();updateClockPreview()"></div>
     </div>
     <div style="margin-top:10px">
      <button class="btn sec" onclick="resetClockColors()">Reset to defaults</button>
     </div>
    </div>
    <div class="card"><h2>Preview</h2>
     <canvas id="clockPreview" width="240" height="240" style="width:240px;height:240px;border:1px solid var(--bd);border-radius:8px;display:block;margin:0 auto"></canvas>
     <small class="hint">Live preview of the clock appearance. Updates as you change settings above.</small>
    </div>
   <div class="card"><h2>Weather Forecast Settings</h2>
    <label>City Name</label>
    <input id="weatherCity" type="text" placeholder="Moscow, London, New York">
    <label>OpenWeatherMap API Key <span class="muted">(Optional, blank uses wttr.in fallback)</span></label>
    <input id="weatherApiKey" type="text" placeholder="e.g. 8a7f9... (32 char hex)">
    <div class="row">
     <div>
      <label>Units</label>
      <select id="weatherUnits">
       <option value="c">Celsius (&deg;C)</option>
       <option value="f">Fahrenheit (&deg;F)</option>
      </select>
     </div>
     <div>
      <label>Refresh weather every (s)</label>
      <input id="weatherPollSec" type="number" min="60" max="86400" value="900">
     </div>
    </div>
    <small class="hint">The device shows time, date, local weather, and its current <b>IP address</b> on the clock screen for easy access.</small>
   </div>
  </section>

  <!-- GALLERY (feature) -->
  <section id="gallerytab" class="tab">
   <div class="card"><h2>Photo Album Settings</h2>
    <div class="row">
     <div><label>Switch photo every (s)</label><input id="galleryRotateSec" type="number" min="2" max="3600" value="10"></div>
     <div style="display:flex;align-items:flex-end">
      <div class="chk"><input id="galleryRandom" type="checkbox"><label>Shuffle photo order</label></div>
     </div>
    </div>
   </div>
   <div class="card"><h2>Upload 240x240 Image / GIF</h2>
    <label>Select image file <span class="muted">(JPG, PNG, GIF — opens 1:1 cropper)</span></label>
    <input id="photoFile" type="file" accept="image/*,.raw" onchange="handlePhotoSelect(event)">
    <div style="margin-top:10px">
     <button id="photoUpBtn" class="btn" onclick="openCropModal()">Crop &amp; Upload (240x240)</button>
    </div>
    <div class="bar" style="margin-top:10px"><div id="photoBar"></div></div>
    <small id="photoMsg" class="hint">Photos are automatically cropped 1:1, resized to 240x240 and compressed as JPEG (10-30 KB) for instant display on the device.</small>
   </div>
   <div class="card">
    <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:10px">
     <h2 style="margin:0">Saved Photos on Device</h2>
     <div id="storageBadge" style="font-size:11px;font-weight:700;color:#52d8aa;background:rgba(82,216,170,0.12);padding:4px 10px;border-radius:12px;border:1px solid rgba(82,216,170,0.25)">Storage: Calculating...</div>
    </div>
    <div class="bar" style="margin-bottom:12px"><div id="storageBar" style="width:0%;background:#52d8aa;height:6px;border-radius:3px"></div></div>
    <div id="photoList" class="muted">Loading photos...</div>
    <button class="btn sec" style="margin-top:10px" onclick="loadPhotos()">Refresh Photo List</button>
   </div>
  </section>

 <!-- TICKER (feature) -->
 <section id="ticker" class="tab">
  <div class="card"><h2>Rotation &amp; data</h2>
   <div class="row">
    <div><label>Show each ticker (s)</label><input id="rotateSec" type="number" min="2" max="300"></div>
    <div><label>Refresh data (s)</label><input id="pollSec" type="number" min="10" max="3600"></div>
   </div>
   <div class="row">
    <div><label>Chart timeframe</label>
     <select id="range">
      <option value="1d">1 day</option><option value="5d">5 days</option>
      <option value="1mo">1 month</option><option value="3mo">3 months</option>
      <option value="6mo">6 months</option><option value="ytd">Year to date</option>
      <option value="1y">1 year</option><option value="2y">2 years</option>
      <option value="5y">5 years</option><option value="max">Max</option>
     </select></div>
    <div><label>Chart points</label><input id="points" type="number" min="0" max="60"></div>
   </div>
   <div class="row">
    <div><label>Change &amp; % basis</label>
     <select id="changeOnRange">
      <option value="true">Chart timeframe</option>
      <option value="false">1 day</option>
     </select></div>
   </div>
   <small class="hint">Chart timeframe: the change, arrow, colors, and chart cover the same span, so they agree. Needs chart data (2+ points); without it the device falls back to the 1-day change. At 1 day it measures from the session's first data point, so overnight gaps are not counted. 1 day: the classic change vs the previous close, which can point the other way than a longer chart.</small>
   <label>Webhook URL <span class="muted">(only for tickers set to Webhook)</span></label>
   <input id="webhookUrl" type="url" placeholder="http://n8n.local:5678/webhook/stock">
  </div>
  <div class="card"><h2>Color scheme</h2>
   <select id="colorInverted"><option value="false">Green up / Red down</option>
    <option value="true">Red up / Green down</option></select>
  </div>
  <div class="card"><h2>What to show</h2>
   <div class="chk"><input id="showName" type="checkbox"><label>Name / symbol</label></div>
   <div class="chk"><input id="showPrice" type="checkbox"><label>Price</label></div>
   <div class="chk"><input id="showChange" type="checkbox"><label>Change &amp; % change</label></div>
   <div class="chk"><input id="showChart" type="checkbox"><label>Sparkline chart</label></div>
   <div class="chk"><input id="showRangeLabel" type="checkbox"><label>Timeframe label</label></div>
   <div class="chk"><input id="showUpdatedAgo" type="checkbox"><label>"Updated N s ago"</label></div>
   <div class="chk"><input id="showPageDots" type="checkbox"><label>Rotation dots</label></div>
   <div class="chk"><input id="showPortfolio" type="checkbox"><label>Position P/L &amp; portfolio page</label></div>
  </div>
  <div class="card"><h2>Tickers (rotate on screen)</h2>
   <table id="symTable"></table>
   <button class="btn sec" style="margin-top:10px" onclick="addSym()">+ Add ticker</button>
   <small class="hint" id="symHint"></small>
  </div>
  <div class="card"><h2>cash.ch symbol finder</h2>
   <label>Instrument <span class="muted">(paste a cash.ch link, ISIN, valor, or a name)</span></label>
   <div class="row">
    <input id="cashQ" type="text" placeholder="https://www.cash.ch/... or EU0009654078">
    <button class="btn sec" style="flex:0 0 auto" onclick="cashFind()">Find</button>
   </div>
   <div id="cashRes"></div>
   <small class="hint">Searches cash.ch from your browser and turns the result into the listing key the ticker needs. Click a match to add it as a ticker.</small>
  </div>
 </section>

 <!-- USAGE (feature) -->
 <section id="usage" class="tab">
  <div class="card"><h2>AI usage</h2>
   <label>AI Usage bridge URL</label>
   <input id="usageUrl" type="url" placeholder="http://192.168.1.10:8788/api/ai-usage">
   <label>Refresh data (s)</label><input id="usagePollSec" type="number" min="10" max="3600">
   <small class="hint"><b>What it shows:</b> reported Antigravity and Codex usage percentages, plus reset timers. The device receives only compact percentages from a trusted bridge on your LAN; provider credentials stay on the Mac. <b>Pull:</b> enter the bridge endpoint. <b>Push:</b> leave it blank and POST the same JSON to <code>http://&lt;device&gt;/api/ai-usage</code>.</small>
  </div>
  <div class="card"><h2>Data availability</h2>
   <small class="hint">Google does not currently publish a personal Antigravity allowance API, and OpenAI does not publish an API for personal ChatGPT/Codex plan usage. Their values therefore require a local adapter or manual bridge input. Official organization/admin usage APIs measure API or enterprise activity and are not the same as the personal app quota. No Google, OpenAI, Anthropic, or GitHub secret should be stored on this device.</small>
   <small class="hint">Bridge JSON: <code>{"a":34,"ar":120,"c":57,"cr":360,"st":"ok","ok":true}</code>. Percentages are 0–100; reset values are minutes. The legacy Claude <code>s/sr/w/wr</code> contract remains accepted during migration.</small>
  </div>
 </section>

 <!-- GITHUB (feature) -->
 <section id="github" class="tab">
  <div class="card"><h2>GitHub setup — quick guide</h2>
   <ol class="hint" style="padding-left:20px;line-height:1.55">
    <li>Copy each repository name from its URL: <code>github.com/OWNER/REPOSITORY</code> becomes <code>OWNER/REPOSITORY</code>.</li>
    <li>Open <a href="https://github.com/settings/personal-access-tokens/new" target="_blank">GitHub → New fine-grained token</a>. Use a clear name such as <code>SmallTV read only</code>, select the resource owner, an expiration date, and only the repositories this screen should monitor.</li>
    <li>Under <b>Repository permissions</b>, grant read-only access for Actions, Deployments, Pull requests, Checks, and Contents. Do not grant write access.</li>
    <li>Click <b>Generate token</b>, copy the value beginning with <code>github_pat_</code>, then add one account row below for each resource owner. A private organization may need to approve the token in its Personal access token settings.</li>
    <li>Enter the Mac/bridge LAN feed URL, owners, tokens and repositories; choose <b>Live GitHub</b>, then click <b>Save bridge</b> and <b>Test feed</b>.</li>
   </ol>
   <small class="hint">Tokens go directly from this browser to the bridge, are not returned by it, and are never stored on the ESP8266. The bridge must remain reachable on your Wi-Fi.</small>
  </div>
  <div class="card"><h2>How to read the GH//STAT screen</h2>
   <div class="hint" style="line-height:1.55">
    <p><b>GH//STAT</b> is the GitHub status dashboard. <b>LIVE</b> means the screen is using a fresh bridge response; <code>1/2</code> is the current page and total page count.</p>
    <p><b>R</b> counts running or queued events, <b>P</b> successful events, and <b>F</b> failed events. Each page contains up to two large cards. <b>FOCUS</b> means active builds/checks have priority and completed history is temporarily hidden.</p>
    <p>The animated ring and live <code>MM:SS</code> counter mean work is active or waiting. A static green check means success. A static red circle means failure.</p>
    <p>Each card shows: repository, workflow/environment/PR/release name, event type (<code>ACT</code>, <code>DEP</code>, <code>PR</code>, <code>REL</code>), branch or ref, and the last event date/time. Date/time uses the bridge computer's local timezone. A double cyan frame marks the newest event.</p>
    <p><a href="https://github.com/bairachnyi/smalltv-ultra/blob/main/docs/src/content/docs/features/github.md" target="_blank">Open the complete GH//STAT documentation</a></p>
   </div>
  </div>
 <div class="card"><h2>GitHub deploy dashboard</h2>
   <label>Status feed URL</label>
   <input id="githubStatusUrl" type="url" placeholder="http://192.168.1.10:8788/api/github">
   <small class="hint">Complete LAN address printed by the bridge, ending in <code>/api/github</code>. Use the Mac's LAN IP, not <code>localhost</code>; the ESP8266 must be able to open it.</small>
   <label>Device token <span class="muted">(blank keeps the saved token)</span></label>
   <input id="githubToken" type="password" autocomplete="off" placeholder="optional">
   <small class="hint">Optional shared secret protecting bridge endpoints (<code>X-Device-Token</code>). This is not a GitHub personal token. It must match the bridge's <code>DEVICE_TOKEN</code>.</small>
   <div class="row">
    <div><label>Refresh data (s)</label><input id="githubPollSec" type="number" min="5" max="3600"></div>
    <div><label>Rotate pages (s)</label><input id="githubRotateSec" type="number" min="3" max="300"></div>
   </div>
   <small class="hint"><b>Refresh</b> controls how often the device asks the bridge for new data. <b>Rotate</b> controls how long each two-card page stays visible. Active timers animate locally between refreshes. In Carousel mode an active or queued GitHub job keeps GH//STAT in front until the next refresh reports completion.</small>
  </div>
  <div class="card"><h2>GitHub accounts &amp; repositories</h2>
   <small class="hint">These controls configure the bridge at the feed URL. GitHub access tokens are sent from this browser directly to the bridge and are never saved on the ESP8266.</small>
   <label>Bridge mode</label>
   <select id="githubBridgeMode"><option value="mock">Mock data</option><option value="live">Live GitHub</option></select>
   <small class="hint"><b>Mock</b> uses safe demonstration events. <b>Live</b> uses the webhook or polling delivery mechanism selected below.</small>
   <label>Live delivery</label>
   <select id="githubDelivery"><option value="webhook">GitHub webhooks — near real time</option><option value="polling">REST polling — compatibility</option></select>
   <small class="hint"><b>Webhooks</b> are recommended for many repositories: GitHub pushes changes immediately and normal display refreshes do not consume REST quota. Polling remains available as a fallback.</small>
   <label>Public webhook delivery URL</label>
   <input id="githubWebhookUrl" type="url" placeholder="https://github-smalltv.example.com/api/github/webhook">
   <small class="hint">GitHub must reach this permanent HTTPS address. It should forward the unchanged request body and headers to the bridge endpoint <code>/api/github/webhook</code>. A LAN or localhost URL will not work.</small>
   <label>Webhook secret <span class="muted">(blank keeps the saved secret)</span></label>
   <div class="row"><div><input id="githubWebhookSecret" type="password" autocomplete="off" placeholder="Generate a secret"></div><div style="align-self:end"><button class="btn sec" onclick="generateGithubWebhookSecret()">Generate</button></div></div>
   <small class="hint">Copy the generated value into the GitHub App or organization webhook <b>Secret</b> field. The bridge verifies every delivery using <code>X-Hub-Signature-256</code> and ignores duplicate delivery IDs.</small>
   <details style="margin-top:10px"><summary>GitHub App setup for all repositories</summary>
    <div class="hint" style="line-height:1.55">
     <p>1. Open <a href="https://github.com/settings/apps/new" target="_blank">GitHub → Settings → Developer settings → New GitHub App</a>.</p>
     <p>2. Enable webhooks, paste the permanent HTTPS URL and the exact secret generated above.</p>
     <p>3. Grant read-only repository permissions: <b>Actions</b>, <b>Checks</b>, <b>Contents</b>, <b>Deployments</b>, and <b>Pull requests</b>. No write permission is required.</p>
     <p>4. Subscribe to <code>workflow_run</code>, <code>deployment</code>, <code>deployment_status</code>, <code>pull_request</code>, <code>check_suite</code>, and <code>release</code>.</p>
     <p>5. Install the app for <b>All repositories</b> on <code>bairachnyi</code> and <code>ananas-it</code>. New repositories will then be included automatically.</p>
    </div>
   </details>
   <label>Events to monitor</label>
   <div class="chk"><input id="githubEventActions" type="checkbox" checked><label>GitHub Actions — latest workflow run (<code>ACT</code>)</label></div>
   <div class="chk"><input id="githubEventDeployments" type="checkbox" checked><label>Deployments &amp; environments — latest deployment status (<code>DEP</code>)</label></div>
   <div class="chk"><input id="githubEventPullRequests" type="checkbox" checked><label>Pull request checks — checks for recent open PRs (<code>PR</code>)</label></div>
   <div class="chk"><input id="githubEventReleases" type="checkbox" checked><label>Releases — latest published release (<code>REL</code>)</label></div>
   <label>Owners and optional polling tokens</label>
   <table id="githubAccounts"></table>
   <button class="btn sec" style="margin-top:8px" onclick="addGithubAccount()">+ Add account</button>
   <small class="hint"><b>Owner</b> filters accepted webhook repositories, for example <code>bairachnyi</code> or <code>ananas-it</code>. Webhook mode does not need PATs. A fine-grained <code>github_pat_…</code> token is only needed for legacy REST polling or future reconciliation.</small>
   <label>Repository allowlist <span class="muted">(one owner/repo per line; blank = all configured owners)</span></label>
   <textarea id="githubRepositories" placeholder="bairachnyi/smalltv-ultra"></textarea>
   <small class="hint">In webhook mode leave this blank to accept every installed repository belonging to the owners above. Enter exact <code>owner/repository</code> names only when you want to restrict the screen.</small>
   <label>Bridge cache / GitHub refresh (s)</label><input id="githubCacheSec" type="number" min="60" max="3600" value="120">
   <small class="hint">Used only for REST polling. In webhook mode the device may read the bridge every 5–10 seconds without causing GitHub API requests.</small>
   <div style="margin-top:12px">
    <button class="btn sec" onclick="loadGithubBridge(true)">Load bridge</button>
    <button class="btn" onclick="saveGithubBridge()">Save bridge</button>
    <button class="btn sec" onclick="testGithubFeed()">Test feed</button>
   </div>
   <small class="hint"><b>Load bridge</b> reads saved non-secret settings. <b>Save bridge</b> validates and stores them on the bridge computer. <b>Test feed</b> performs the same request the device will make and reports a specific error.</small>
   <div id="githubBridgeState" class="muted" style="margin-top:8px">Enter a feed URL, then load the bridge settings.</div>
  </div>
 </section>

 <!-- UPDATE -->
 <section id="update" class="tab">
  <div class="card"><h2>Update from GitHub</h2>
   <div class="muted">Installed: <b id="fwVer">-</b></div>
   <div style="margin-top:10px">
    <button class="btn sec" onclick="checkUpdate()" id="chkBtn">Check for latest</button>
    <button class="btn" style="margin-left:8px" onclick="selfUpdate()" id="ghUpBtn" disabled>Update now</button>
   </div>
   <div id="ghMsg" class="muted" style="margin-top:8px"></div>
   <small class="hint">Pulls the newest release straight from <a id="repoLink" href="https://github.com/bairachnyi/smalltv-ultra/releases" target="_blank">the GitHub repo</a>. HTTPS OTA is tight on the ESP8266; if it fails, use the manual upload below.</small>
  </div>
  <div class="card"><h2>Manual update (OTA)</h2>
   <input id="fw" type="file" accept=".bin">
   <div style="margin-top:12px"><button class="btn" onclick="upload()" id="upBtn">Upload &amp; flash</button></div>
   <div class="bar"><div id="upBar"></div></div>
   <div id="upMsg" class="muted" style="margin-top:8px"></div>
   <small class="hint">Upload <code>smalltv-ultra-firmware.bin</code> from the <a href="https://github.com/bairachnyi/smalltv-ultra/releases" target="_blank">releases page</a> or a local build. The device reboots when done.</small>
  </div>
  <div class="card"><h2>Settings backup</h2>
   <button class="btn sec" onclick="location.href='/api/export'">Export settings</button>
   <input id="cfgFile" type="file" accept=".json,application/json" style="margin-top:10px">
   <div style="margin-top:10px"><button class="btn" onclick="importCfg()">Import &amp; reboot</button></div>
   <small class="hint">The export is the device's <code>config.json</code>, including WiFi passwords in clear text; treat the file accordingly. Import applies everything and reboots.</small>
  </div>
  <div class="card"><h2>Maintenance</h2>
   <button class="btn sec" onclick="reboot()">Reboot</button>
   <button class="btn danger" style="margin-left:8px" onclick="factory()">Factory reset</button>
  </div>
 </section>
</main>

<div style="text-align:center;padding:0 0 16px"><button class="btn" onclick="saveAll()">Save settings</button></div>
<div style="text-align:center;padding:0 0 24px;font-size:12px">
 <a id="footRepo" href="https://github.com/bairachnyi/smalltv-ultra" target="_blank" style="color:var(--acc2);text-decoration:none">GitHub: bairachnyi/smalltv-ultra</a>
 <span id="footVer" class="muted"></span>
</div>
<div id="toast" class="toast"></div>

<div id="loginOverlay" style="position:fixed;inset:0;background:var(--bg);z-index:200;display:none;align-items:center;justify-content:center">
 <div class="card" style="max-width:320px;width:100%">
  <h2>Enter Password</h2>
  <input id="loginPass" type="password" placeholder="Password" onkeydown="if(event.key==='Enter')doLogin()">
  <div style="margin-top:12px"><button class="btn" onclick="doLogin()">Login</button></div>
  <div id="loginErr" class="muted" style="color:var(--red);margin-top:8px;display:none">Wrong password</div>
 </div>
</div>

<script>
var C={};
function $(id){return document.getElementById(id)}
// null-safe field helpers: a lean build removes some feature tabs entirely
function sv(id,v){var e=$(id);if(e)e.value=(v!=null?v:'')}
function sc(id,v){var e=$(id);if(e)e.checked=!!v}
function gv(id){var e=$(id);return e?e.value:''}
function gc(id){var e=$(id);return e?e.checked:false}
function toast(m){var t=$('toast');t.textContent=m;t.classList.add('show');setTimeout(function(){t.classList.remove('show')},2200)}
function j(url,opt){
 if(!opt)opt={};
 if(!opt.headers)opt.headers={};
 if(_adminPass)opt.headers['X-Admin-Pass']=_adminPass;
 return fetch(url,opt).then(function(r){
  if(r.status===401){showLogin();throw Error('auth required')}
  return r.json()
 });
}

// Auth
var _adminPass='';
function showLogin(){$('loginOverlay').style.display='flex';setTimeout(function(){$('loginPass').focus()},100)}
function doLogin(){
 var p=$('loginPass').value;
 fetch('/api/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({pass:p})})
 .then(function(r){return r.json()}).then(function(d){
  if(d.ok){_adminPass=p;$('loginOverlay').style.display='none';loadConfig().then(loadStatus)}
  else{$('loginErr').style.display='block';setTimeout(function(){$('loginErr').style.display='none'},3000)}
 }).catch(function(){$('loginErr').style.display='block';setTimeout(function(){$('loginErr').style.display='none'},3000)});
}

// tabs
document.querySelectorAll('nav button').forEach(function(b){b.onclick=function(){
 document.querySelectorAll('nav button').forEach(function(x){x.classList.remove('active')});
 document.querySelectorAll('.tab').forEach(function(x){x.classList.remove('active')});
 b.classList.add('active');$(b.dataset.t).classList.add('active');
}});

// field groups by their location in the nested config
var T_TEXT=['webhookUrl','range'];                   // ticker strings
var T_NUM=['rotateSec','pollSec','points'];          // ticker numbers
var T_BOOL=['showName','showPrice','showChange','showChart','showRangeLabel','showUpdatedAgo','showPageDots','showPortfolio'];

// IANA -> POSIX TZ. The device stores/uses the POSIX rule; this map lives in the
// browser so the firmware carries no tz database (same idea as the cash finder).
var TZMAP={
 '':'UTC0','UTC':'UTC0',
 'Europe/London':'GMT0BST,M3.5.0/1,M10.5.0','Europe/Dublin':'GMT0IST,M3.5.0/1,M10.5.0',
 'Europe/Lisbon':'WET0WEST,M3.5.0/1,M10.5.0',
 'Europe/Rome':'CET-1CEST,M3.5.0,M10.5.0/3','Europe/Paris':'CET-1CEST,M3.5.0,M10.5.0/3',
 'Europe/Berlin':'CET-1CEST,M3.5.0,M10.5.0/3','Europe/Madrid':'CET-1CEST,M3.5.0,M10.5.0/3',
 'Europe/Amsterdam':'CET-1CEST,M3.5.0,M10.5.0/3','Europe/Brussels':'CET-1CEST,M3.5.0,M10.5.0/3',
 'Europe/Zurich':'CET-1CEST,M3.5.0,M10.5.0/3','Europe/Vienna':'CET-1CEST,M3.5.0,M10.5.0/3',
 'Europe/Warsaw':'CET-1CEST,M3.5.0,M10.5.0/3','Europe/Prague':'CET-1CEST,M3.5.0,M10.5.0/3',
 'Europe/Stockholm':'CET-1CEST,M3.5.0,M10.5.0/3','Europe/Oslo':'CET-1CEST,M3.5.0,M10.5.0/3',
 'Europe/Copenhagen':'CET-1CEST,M3.5.0,M10.5.0/3',
 'Europe/Athens':'EET-2EEST,M3.5.0/3,M10.5.0/4','Europe/Helsinki':'EET-2EEST,M3.5.0/3,M10.5.0/4',
 'Europe/Bucharest':'EET-2EEST,M3.5.0/3,M10.5.0/4','Europe/Kyiv':'EET-2EEST,M3.5.0/3,M10.5.0/4',
 'Europe/Istanbul':'<+03>-3','Europe/Moscow':'MSK-3',
 'America/New_York':'EST5EDT,M3.2.0,M11.1.0','America/Toronto':'EST5EDT,M3.2.0,M11.1.0',
 'America/Chicago':'CST6CDT,M3.2.0,M11.1.0','America/Denver':'MST7MDT,M3.2.0,M11.1.0',
 'America/Phoenix':'MST7','America/Los_Angeles':'PST8PDT,M3.2.0,M11.1.0',
 'America/Anchorage':'AKST9AKDT,M3.2.0,M11.1.0','America/Sao_Paulo':'<-03>3',
 'America/Mexico_City':'CST6','America/Bogota':'<-05>5','America/Argentina/Buenos_Aires':'<-03>3',
 'Asia/Dubai':'<+04>-4','Asia/Karachi':'PKT-5','Asia/Kolkata':'IST-5:30',
 'Asia/Dhaka':'<+06>-6','Asia/Bangkok':'<+07>-7','Asia/Jakarta':'WIB-7',
 'Asia/Shanghai':'CST-8','Asia/Hong_Kong':'HKT-8','Asia/Singapore':'<+08>-8',
 'Asia/Taipei':'CST-8','Asia/Tokyo':'JST-9','Asia/Seoul':'KST-9',
 'Australia/Perth':'AWST-8','Australia/Sydney':'AEST-10AEDT,M10.1.0,M4.1.0/3',
 'Australia/Adelaide':'ACST-9:30ACDT,M10.1.0,M4.1.0/3','Australia/Brisbane':'AEST-10',
 'Pacific/Auckland':'NZST-12NZDT,M9.5.0,M4.1.0/3','Pacific/Honolulu':'HST10'};
function fillTz(){var s=$('tz');if(!s)return;var keys=Object.keys(TZMAP).filter(function(k){return k!==''});
 keys.sort();s.innerHTML='<option value="">UTC</option>'+keys.map(function(k){return '<option value="'+k+'">'+k+'</option>'}).join('');}

var MODEOPT={ticker:'stocks',usage:'usage',github:'github'};
var CAROPT={ticker:'carouselTicker',usage:'carouselUsage',github:'carouselGithub'};
function hideFeat(name){
 var b=document.querySelector('nav button[data-t="'+name+'"]'); if(b)b.remove();
 var sec=$(name); if(sec)sec.remove();
 var o=document.querySelector('#mode option[value="'+MODEOPT[name]+'"]'); if(o)o.remove();
 var c=$(CAROPT[name]); if(c)c.closest('.chk').remove();
}
function modeChanged(){if(!$('mode'))return;
 $('carouselRow').style.display=$('mode').value==='carousel'?'block':'none';}
function loadConfig(){return j('/api/config').then(function(c){C=c;
 var f=c.features||{}; ['ticker','usage','github'].forEach(function(k){if(f[k]===false)hideFeat(k)});
 var t=c.ticker||{}, u=c.usage||{};
 // shared
  ['apSsid','apPass','hostname'].forEach(function(k){$(k).value=c[k]!=null?c[k]:''});
  if($('adminPass'))$('adminPass').placeholder=c.adminPass?'(set — blank keeps it)':'(none)';
  renderWifi(c.wifi||(c.staSsid?[{ssid:c.staSsid,passSet:c.staPassSet}]:[]));
 $('brightness').value=c.brightness; $('brVal').textContent=c.brightness;
 $('rotation').value=c.rotation;
 $('autoBrightness').checked=!!c.autoBrightness;
 $('backlightInverted').checked=!!c.backlightInverted;
 // header chip = which chip this firmware was built for
 var chipName={esp8266:'ESP8266',esp32c2:'ESP32-C2',esp32:'ESP32'}[c.chip]||'';
 var chE=$('chip'); if(chE&&chipName){chE.textContent=chipName;chE.style.display='inline-block';}
 // clock slice
 fillTz(); var ck=c.clock||{};
 if(ck.tz && !(ck.tz in TZMAP)){var _ts=$('tz'); if(_ts){var _o=document.createElement('option');_o.value=ck.tz;_o.textContent=ck.tz;_ts.appendChild(_o);}}
 sv('tz',ck.tz||''); sc('nightEnabled',!!ck.nightEnabled);
 sv('nightStart',ck.nightStart||'22:00'); sv('nightEnd',ck.nightEnd||'07:00');
 sv('nightLevel',ck.nightLevel!=null?ck.nightLevel:0); $('nlVal')&&($('nlVal').textContent=(ck.nightLevel!=null?ck.nightLevel:0));
 sc('format24h',ck.format24h!==false); sc('showSeconds',!!ck.showSeconds); sc('showDate',ck.showDate!==false);
 sv('clockTheme',ck.theme||0); sv('weatherCity',ck.weatherCity||'Moscow'); sv('weatherApiKey',ck.weatherApiKey||'');
  sv('weatherUnits',ck.weatherUnits||'c'); sv('weatherPollSec',ck.weatherPollSec||900);
  var formatHex = function(v, def) {
    if (!v) return def;
    if (typeof v === 'string') return v.startsWith('#') ? v : ('#' + v);
    return rgb565ToHex(v);
  };
  sv('timeColor', formatHex(ck.timeColor, '#39e7ff'));
  sv('dateColor', formatHex(ck.dateColor, '#ffb6c1'));
  sv('accentColor', formatHex(ck.accentColor, '#58a9ff'));
  sv('bgColor', formatHex(ck.bgColor, '#000000'));
  sv('fontScale', ck.fontScale || 0);
  sc('boldText', !!ck.boldText);
  updateClockPreview();
  // gallery slice
 var gl=c.gallery||{}; sv('galleryRotateSec',gl.rotateSec||10); sc('galleryRandom',!!gl.randomOrder);
 loadPhotos();
 $('mode').value=c.mode||'stocks'; modeChanged();
  sv('carouselSec',c.carouselSec||30);
  sc('carouselTicker',c.carouselTicker!==false); sc('carouselUsage',c.carouselUsage!==false);
  sc('carouselGithub',c.carouselGithub!==false);
  sc('carouselClockDigital',c.carouselClockDigital!==false);
  sc('carouselClockWeather',!!c.carouselClockWeather);
  sc('carouselClockModern',!!c.carouselClockModern);
  sc('carouselClockForecast',!!c.carouselClockForecast);
  sc('carouselGallery',c.carouselGallery!==false);
 // ticker slice
 T_TEXT.forEach(function(k){sv(k,t[k])});
 T_NUM.forEach(function(k){sv(k,t[k])});
 T_BOOL.forEach(function(k){sc(k,t[k])});
 sv('colorInverted',t.colorInverted?'true':'false');
 sv('changeOnRange',t.changeOnRange===false?'false':'true');
 renderSyms(t.symbols||[]); symHintFor('yahoo');
 // usage slice
 sv('usageUrl',u.usageUrl);
 sv('usagePollSec',u.pollSec);
 // GitHub slice
 var gh=c.github||{};
 sv('githubStatusUrl',gh.statusUrl); sv('githubPollSec',gh.pollSec||15); sv('githubRotateSec',gh.rotateSec||8);
 var gt=$('githubToken');if(gt)gt.placeholder=gh.tokenSet?'(saved — blank keeps it)':'optional';
 if(gh.statusUrl)loadGithubBridge(false);
 var ap=$('apPass'); if(ap)ap.placeholder=c.apPassSet?'(unchanged)':'(open)';
})}

function esc(s){return (''+(s==null?'':s)).replace(/[<>&"']/g,function(c){return {'<':'&lt;','>':'&gt;','&':'&amp;','"':'&quot;',"'":'&#39;'}[c]})}

// RGB565 <-> hex color conversion for the web UI color pickers
function rgb565ToHex(v){
 var r=((v>>11)&0x1F)<<3, g=((v>>5)&0x3F)<<2, b=(v&0x1F)<<3;
 return '#'+((1<<24)+(r<<16)+(g<<8)+b).toString(16).slice(1);
}
function hexToRgb565(hex){
 hex=hex.replace('#','');
 var r=parseInt(hex.substring(0,2),16), g=parseInt(hex.substring(2,4),16), b=parseInt(hex.substring(4,6),16);
 return ((r>>3)<<11)|((g>>2)<<5)|(b>>3);
}

// Live clock preview on a 240x240 canvas
function updateClockPreview(){
 var cv=$('clockPreview'); if(!cv||!cv.getContext) return;
 var ctx=cv.getContext('2d');
 var theme=parseInt(gv('clockTheme'))||0;
 var tc=gv('timeColor')||'#39e7ff';
 var dc=gv('dateColor')||'#ffb6c1';
 var ac=gv('accentColor')||'#58a9ff';
 var bg=gv('bgColor')||'#000000';
 var timeSz=parseInt(gv('timeScale'))||5;
 var dateSz=parseInt(gv('dateScale'))||2;
 var fontSt=gv('fontStyle')||'0';
 var isBold=gc('boldText')||(fontSt==='1');
 var showSec=gc('showSeconds');
 var showDt=gc('showDate');
 var city=(gv('weatherCity')||'MOSCOW').toUpperCase();

 if(showSec && timeSz > 5) timeSz = 5;

 ctx.fillStyle=bg; ctx.fillRect(0,0,240,240);

 var fontFamily = (fontSt==='2' ? '"Courier New", monospace' : (fontSt==='3' ? 'system-ui, sans-serif' : 'monospace'));
 var fontWeight = isBold ? 'bold ' : 'normal ';

 function drawText(txt,x,y,sz,color){
  ctx.fillStyle=color;
  ctx.font=fontWeight+(sz*8)+'px '+fontFamily;
  ctx.textAlign='left';
  ctx.fillText(txt,x,y+(sz*8));
  if(isBold){ctx.fillText(txt,x+1,y+(sz*8));}
 }
 function drawCentered(txt,y,sz,color){
  ctx.fillStyle=color;
  ctx.font=fontWeight+(sz*8)+'px '+fontFamily;
  ctx.textAlign='center';
  ctx.fillText(txt,120,y+(sz*8));
  if(isBold){ctx.fillText(txt,121,y+(sz*8));}
 }

 var timeStr=showSec?'12:34:56':'12:34';
 var dateStr='MON, 23 JUL 2026';

 if(theme===0){
  var yOff=(timeSz>=7)?36:(timeSz===6?44:(timeSz===5?52:62));
  if (fontSt === '2') {
   ctx.strokeStyle='rgba(24,198,198,0.3)';ctx.lineWidth=2;ctx.strokeRect(8,yOff-4,224,timeSz*8+8);
  }
  drawCentered(timeStr,yOff,timeSz,tc);
  if(showDt) {
   var dateY=(timeSz>=6)?168:142;
   ctx.fillStyle='rgba(24,198,198,0.15)';ctx.fillRect(10,dateY,220,32);
   ctx.strokeStyle=dc;ctx.lineWidth=1;ctx.strokeRect(10,dateY,220,32);
   drawCentered(dateStr,dateY+4,dateSz,dc);
  }
  drawCentered('IP: 192.168.1.141',218,1,ac);
 } else if(theme===1){
  ctx.fillStyle='rgba(1,134,200,0.2)';ctx.fillRect(8,8,224,120);
  ctx.strokeStyle='#1C17';ctx.strokeRect(8,8,224,120);
  drawText(city,18,18,2,ac);
  drawText('+22.5C',18,46,4,tc);
  drawText('CLEAR SKY',18,94,2,dc);
  ctx.fillStyle='rgba(8,201,150,0.2)';ctx.fillRect(8,134,224,98);
  ctx.strokeStyle='#22F3';ctx.strokeRect(8,134,224,98);
  drawCentered(timeStr,148,timeSz,tc);
  if(showDt) drawCentered(dateStr,198,dateSz,dc);
 } else if(theme===2){
  ctx.fillStyle='rgba(16,132,200,0.2)';ctx.fillRect(8,8,224,130);
  ctx.strokeStyle=ac;ctx.strokeRect(8,8,224,130);
  drawCentered(timeStr,35,timeSz,tc);
  if(showDt) drawCentered(dateStr,98,dateSz,dc);
  ctx.fillStyle='rgba(8,66,100,0.2)';ctx.fillRect(8,144,224,88);
  ctx.strokeStyle='#2126';ctx.strokeRect(8,144,224,88);
  drawText(city,18,160,2,dc);
  drawText('+22.5C',135,155,3,ac);
  drawText('LIVE WEATHER',18,198,1,ac);
 } else {
  ctx.fillStyle='rgba(9,68,120,0.2)';ctx.fillRect(6,6,228,72);
  ctx.strokeStyle='#1390';ctx.strokeRect(6,6,228,72);
  drawText('TODAY ('+city+')',16,16,2,tc);
  drawText('+22.5C',135,12,3,ac);
  drawText('CLEAR SKY',16,45,2,dc);
  ctx.fillStyle='rgba(16,132,200,0.2)';ctx.fillRect(6,84,228,72);
  ctx.strokeStyle='#2126';ctx.strokeRect(6,84,228,72);
  drawText('TOMORROW',16,94,2,ac);
  drawText('+24.0C',135,90,3,dc);
  drawText('PARTLY CLOUDY',16,123,2,dc);
  ctx.fillStyle='rgba(1,134,200,0.2)';ctx.fillRect(6,162,228,72);
  ctx.strokeStyle='#1C17';ctx.strokeRect(6,162,228,72);
  drawText('SAT 25 JUL',16,172,2,ac);
  drawText('+20.5C',135,168,3,ac);
  drawText('MOSTLY SUNNY',16,201,2,dc);
 }
}
function resetClockColors(){
 sv('timeColor','#39e7ff');sv('dateColor','#ffb6c1');sv('accentColor','#58a9ff');sv('bgColor','#000000');
 updateClockPreview();
}

// Listen for color/font/theme changes to update preview
document.addEventListener('input',function(e){
 if(e.target.id&&['timeColor','dateColor','accentColor','bgColor','timeScale','dateScale'].indexOf(e.target.id)>=0) updateClockPreview();
});
document.addEventListener('change',function(e){
 if(e.target.id&&['clockTheme','fontStyle','timeScale','dateScale','showSeconds','showDate','boldText'].indexOf(e.target.id)>=0) updateClockPreview();
});
function symHintFor(v){var h=$('symHint');if(!h)return;
 h.innerHTML=(v==='cash'
  ?'<b>cash.ch</b>: fetched directly by the device. The symbol is a listing key like <code>147478611-246-333</code>; the finder below turns a cash.ch link, ISIN, or name into one.'
   +(C.chip==='esp8266'?' <b>On this ESP8266</b>, cash.ch\'s TLS is beyond this chip &mdash; use the <b>GitHub</b> source for the same listing key instead (a scheduled workflow publishes it). The ESP32 boards fetch cash.ch directly.':'')
  :v==='github'
  ?'<b>GitHub</b>: reads a listing key\'s quote from a small JSON file the repo\'s <code>quotes</code> workflow publishes (a proxy for cash.ch on chips that can\'t reach it directly). The symbol is the cash.ch listing key, and it must be listed in <code>quotes-config.json</code>.'
  :v==='webhook'
  ?'<b>Webhook</b>: the device asks the webhook URL above and passes the symbol through as-is, so use whatever your endpoint understands.'
  :'<b>Yahoo Finance</b>: fetched directly by the device. Use Yahoo symbols: <code>AAPL</code>, <code>NESN.SW</code> (Swiss stocks end in <code>.SW</code>), <code>BTC-USD</code>, <code>EURUSD=X</code>.')
  +' Name is optional; if set it overrides the source\'s name. Qty and per-unit cost are optional too: set both and the ticker shows your P/L plus a portfolio summary page.';}

// cash.ch symbol finder: runs in YOUR browser (cash.ch answers cross-origin),
// the device itself is not involved in the search.
function cashFind(){var q=gv('cashQ').trim();if(!q){toast('Paste a link, ISIN, or name first');return}
 var m=q.match(/^https?:\/\/\S*?(\d{5,12})/); if(m)q=m[1];   // a cash.ch link carries the valor in its slug
 $('cashRes').innerHTML='<div class="muted">Searching cash.ch...</div>';
 var gq='query{textSearch(publication:CASH,search:"'+q.replace(/["\\]/g,'')+'",sort:Relevance,sortOrder:Descending,limit:10,offset:0){'+
  'equity{items{...on Equity{listingId mName market mCur mIsin}}} fund{items{...on Fund{listingId mName market mCur mIsin}}} '+
  'derivative{items{...on Derivative{listingId mName market mCur mIsin}}} bond{items{...on Bond{listingId mName market mCur mIsin}}} '+
  'index{items{...on Index{listingId mName market mCur}}} diverse{items{...on Diverse{listingId mName market mCur mIsin}}} '+
  'cryptoCurrency{items{...on CryptoCurrency{listingId mName market mCur}}}}}';
 fetch('https://www.cash.ch/_/api/graphql/prod?query='+encodeURIComponent(gq))
 .then(function(r){return r.json()})
 .then(function(d){var out=[];var b=(d.data&&d.data.textSearch)||{};
  ['equity','derivative','fund','bond','index','diverse','cryptoCurrency'].forEach(function(k){
   ((b[k]&&b[k].items)||[]).forEach(function(it){if(it&&it.listingId)out.push(it)});});
  if(!out.length){$('cashRes').innerHTML='<div class="muted">Nothing found on cash.ch</div>';return}
  var h='';out.slice(0,10).forEach(function(it){
   h+='<div class="net" onclick="cashPick(this.dataset.k)" data-k="'+esc(it.listingId)+'"><span>'+esc(it.mName||'?')+
    ' <span class="muted">'+esc(it.mIsin||'')+'</span></span><span class="muted">'+esc(it.market||'')+' '+esc(it.mCur||'')+'</span></div>';});
  $('cashRes').innerHTML=h;
 }).catch(function(){$('cashRes').innerHTML='<div class="muted">cash.ch not reachable from this browser</div>'});}
function cashPick(k){var rows=document.querySelectorAll('#symTable tr');var tr=null;
 for(var i=0;i<rows.length;i++){if(!rows[i].querySelector('.s').value.trim()){tr=rows[i];break}}
 if(!tr){if(rows.length>=8){toast('Max 8');return}addRow({});tr=$('symTable').lastChild}
 tr.querySelector('.s').value=k;tr.querySelector('.src').value='cash';symHintFor('cash');
 toast('Added '+k+'. Set a name, then Save.');}
function collect(){
 var o={mode:gv('mode'),
  carouselSec:parseInt(gv('carouselSec'))||30,
  carouselTicker:gc('carouselTicker'), carouselUsage:gc('carouselUsage'), carouselGithub:gc('carouselGithub'),
  carouselClockDigital:gc('carouselClockDigital'), carouselClockWeather:gc('carouselClockWeather'), carouselClockModern:gc('carouselClockModern'), carouselClockForecast:gc('carouselClockForecast'),
  carouselClock:gc('carouselClockDigital')||gc('carouselClockWeather')||gc('carouselClockModern')||gc('carouselClockForecast'),
  carouselGallery:gc('carouselGallery'),
  brightness:parseInt(gv('brightness'))||0,
  rotation:parseInt(gv('rotation')),
  autoBrightness:gc('autoBrightness'),
  backlightInverted:gc('backlightInverted'),
   hostname:gv('hostname'), apSsid:gv('apSsid'), apPass:gv('apPass'),
   adminPass:gv('adminPass'),
   wifi:collectWifi()};
 // ticker slice (only if compiled in)
 if($('ticker')){
  var t={colorInverted:gv('colorInverted')==='true',changeOnRange:gv('changeOnRange')==='true'};
  T_TEXT.forEach(function(k){t[k]=gv(k)});
  T_NUM.forEach(function(k){t[k]=parseInt(gv(k))||0});
  T_BOOL.forEach(function(k){t[k]=gc(k)});
  t.symbols=[];
  document.querySelectorAll('#symTable tr').forEach(function(tr){
   var s=tr.querySelector('.s').value.trim();
   if(s)t.symbols.push({symbol:s,name:tr.querySelector('.n').value.trim(),source:tr.querySelector('.src').value,
    qty:parseFloat(tr.querySelector('.q').value)||0,cost:parseFloat(tr.querySelector('.c').value)||0});
  });
  o.ticker=t;
 }
 // usage slice
 if($('usage')) o.usage={usageUrl:gv('usageUrl'), pollSec:parseInt(gv('usagePollSec'))||0};
 // GitHub slice
 if($('github')) o.github={statusUrl:gv('githubStatusUrl'),accessToken:gv('githubToken'),
  pollSec:parseInt(gv('githubPollSec'))||15,rotateSec:parseInt(gv('githubRotateSec'))||8};
 // clock slice
 if($('tz')){var _tzn=gv('tz'); var _tzp=(_tzn in TZMAP)?TZMAP[_tzn]:((C.clock&&C.clock.tz===_tzn&&C.clock.tzPosix)?C.clock.tzPosix:'UTC0');
  o.clock={tz:_tzn,tzPosix:_tzp,
  nightEnabled:gc('nightEnabled'),nightStart:gv('nightStart')||'22:00',
  nightEnd:gv('nightEnd')||'07:00',nightLevel:parseInt(gv('nightLevel'))||0,
  format24h:gc('format24h'),showSeconds:gc('showSeconds'),showDate:gc('showDate'),
  theme:parseInt(gv('clockTheme'))||0,weatherCity:gv('weatherCity'),weatherApiKey:gv('weatherApiKey'),
  weatherUnits:gv('weatherUnits')||'c',weatherPollSec:parseInt(gv('weatherPollSec'))||900,
  timeColor:gv('timeColor')||'#39e7ff',
  dateColor:gv('dateColor')||'#ffb6c1',
  accentColor:gv('accentColor')||'#58a9ff',
  bgColor:gv('bgColor')||'#000000',
  timeScale:parseInt(gv('timeScale'))||5,
  dateScale:parseInt(gv('dateScale'))||2,
  fontStyle:parseInt(gv('fontStyle'))||0,
  fontScale:parseInt(gv('timeScale'))||5,
  boldText:gc('boldText')};}
 // gallery slice
 if($('galleryRotateSec')){
  o.gallery={rotateSec:parseInt(gv('galleryRotateSec'))||10,randomOrder:gc('galleryRandom')};
 }
 return o;
}

function loadPhotos(){
 var pl=$('photoList');
 if(!pl)return;
 j('/api/photos').then(function(res){
  var arr = Array.isArray(res) ? res : (res.photos || []);
  window._emulatorPhotos=arr;
  if(typeof pvRestart==='function') pvRestart(false);

  // Update Storage Free Space Indicator
  var sb=$('storageBadge');
  var sBar=$('storageBar');
  var totalBytes=(res && res.fsTotal)?res.fsTotal:(3*1024*1024);
  var freeBytes=(res && typeof res.fsFree!=='undefined')?res.fsFree:Math.max(0,totalBytes-(res.fsUsed||arr.reduce(function(acc,p){return acc+(p.size||0);},0)));
  var usedBytes=totalBytes-freeBytes;
  var pct=Math.min(100,Math.max(0,Math.round((usedBytes/totalBytes)*100)));

  if(sb) sb.textContent='Free Space: '+(freeBytes/(1024*1024)).toFixed(2)+' MB / '+(totalBytes/(1024*1024)).toFixed(2)+' MB ('+(100-pct)+'% free)';
  if(sBar) sBar.style.width=pct+'%';

  if(!arr||!arr.length){
   pl.innerHTML='<div class="muted">No photos on device yet. Upload 240x240 image above.</div>';
   return;
  }
  var h='<table><tr><th>File</th><th>Size</th><th>Action</th></tr>';
  arr.forEach(function(p){
   h+='<tr><td><a href="'+esc(p.path)+'" target="_blank">'+esc(p.name)+'</a></td>'+
    '<td>'+Math.round(p.size/1024)+' KB</td>'+
    '<td><button class="btn danger" style="padding:4px 8px" onclick="deletePhoto(\''+esc(p.path)+'\')">Delete</button></td></tr>';
  });
  h+='</table>';
  pl.innerHTML=h;
 }).catch(function(){pl.innerHTML='<div class="muted">Could not load photo list</div>';});
}

function uploadPhoto(){
 var f=$('photoFile').files[0];
 if(!f){toast('Select an image file first');return;}
 var fd=new FormData();fd.append('photo',f,f.name);
 var p=(window.C&&window.C.adminPass)||'';
 var u='/api/photos/upload'+(p?'?pass='+encodeURIComponent(p):'');
 var x=new XMLHttpRequest();x.open('POST',u);
 $('photoUpBtn').disabled=true;
 x.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);$('photoBar').style.width=p+'%';$('photoMsg').textContent='Uploading '+p+'%'}};
 x.onload=function(){$('photoUpBtn').disabled=false;if(x.status==200){$('photoMsg').textContent='Photo uploaded successfully!';$('photoBar').style.width='100%';loadPhotos();}else{$('photoMsg').textContent='Upload failed: '+x.responseText}};
 x.onerror=function(){$('photoUpBtn').disabled=false;$('photoMsg').textContent='Upload error';};
 x.send(fd);
}

function deletePhoto(path){
 if(!confirm('Delete this photo?'))return;
 j('/api/photos/delete',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({path:path})})
 .then(function(res){toast(res.ok?'Photo deleted':'Could not delete photo');loadPhotos();});
}
function saveAll(){var ghMode=gv('mode');var ghUrl=gv('githubStatusUrl').trim();
 if((ghMode==='github'||(ghMode==='carousel'&&gc('carouselGithub'))||ghUrl)&&!validateGithubFeed(true))return;
 j('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(collect())})
 .then(function(r){toast(r.reboot?'Saved — rebooting...':'Saved');if(r.reboot)setTimeout(function(){location.reload()},6000);loadStatus()})}

function saveWifi(){
 var o={wifi:collectWifi()};
 j('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)}).then(function(){
  toast('Saved, rebooting to connect...');j('/api/reboot',{method:'POST'});
 });
}

// GitHub bridge management. Tokens travel browser -> bridge and are never
// included in the device's /api/config payload or LittleFS settings file.
function githubBridgeUrl(path){var raw=gv('githubStatusUrl').trim();if(!raw)return '';
 try{var u=new URL(raw,location.href);u.pathname=u.pathname.replace(/\/api\/github\/?$/,'/api/'+path);u.search='';u.hash='';return u.toString()}
 catch(e){return ''}}
function githubBridgeHeaders(json){var h={};if(json)h['Content-Type']='application/json';var t=gv('githubToken').trim();if(t)h['X-Device-Token']=t;return h}
function githubProblem(message,id){setGithubBridgeState(message,true);toast(message);var e=id&&$(id);if(e){e.style.borderColor='var(--red)';e.focus()}return false}
var githubWebhookSecretSet=false;
function clearGithubProblems(){['githubStatusUrl','githubRepositories','githubCacheSec','githubWebhookUrl','githubWebhookSecret'].forEach(function(id){var e=$(id);if(e)e.style.borderColor='' });document.querySelectorAll('#githubAccounts input').forEach(function(e){e.style.borderColor=''})}
function validateGithubFeed(required){clearGithubProblems();var raw=gv('githubStatusUrl').trim();
 if(!raw)return required?githubProblem('Enter the bridge feed URL, for example http://192.168.1.10:8788/api/github','githubStatusUrl'):true;
 var u;try{u=new URL(raw)}catch(e){return githubProblem('Feed URL is invalid. Copy the complete http://…/api/github address.','githubStatusUrl')}
 if(u.protocol!=='http:'&&u.protocol!=='https:')return githubProblem('Feed URL must start with http:// or https://.','githubStatusUrl');
 if(u.hostname==='localhost'||u.hostname==='127.0.0.1')return githubProblem('Use the Mac LAN IP, not localhost: the device cannot reach your Mac loopback address.','githubStatusUrl');
 if(!/\/api\/github\/?$/.test(u.pathname))return githubProblem('Feed URL must end with /api/github.','githubStatusUrl');return true}
function validateGithubBridge(){if(!validateGithubFeed(true))return false;var rows=document.querySelectorAll('#githubAccounts .ghacct'),seen={};
 if(!rows.length)return githubProblem('Add at least one GitHub account owner.','githubAccounts');
 for(var i=0;i<rows.length;i++){var owner=rows[i].querySelector('.ghowner'),token=rows[i].querySelector('.ghtoken'),name=owner.value.trim();
  if(!/^[A-Za-z0-9_.-]+$/.test(name)){owner.style.borderColor='var(--red)';owner.focus();return githubProblem('Account row '+(i+1)+': enter only the GitHub owner login, for example ananas-it.',null)}
  if(seen[name.toLowerCase()]){owner.style.borderColor='var(--red)';return githubProblem('Account '+name+' is listed more than once.',null)}seen[name.toLowerCase()]=1;
  if(token.value.trim()&&!/^github_pat_/.test(token.value.trim())){token.style.borderColor='var(--red)';token.focus();return githubProblem('Token for '+name+' must be a fine-grained token beginning with github_pat_.',null)}}
 var repos=gv('githubRepositories').split(/[,\n]/).map(function(x){return x.trim()}).filter(Boolean);if(repos.length>50)return githubProblem('Use no more than 50 repositories.','githubRepositories');
 for(var j=0;j<repos.length;j++)if(!/^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(repos[j]))return githubProblem('Repository '+(j+1)+' must use owner/repository format.','githubRepositories');
 if(!gc('githubEventActions')&&!gc('githubEventDeployments')&&!gc('githubEventPullRequests')&&!gc('githubEventReleases'))return githubProblem('Enable at least one GitHub event type.',null);
 if(gv('githubDelivery')==='webhook'){var ws=gv('githubWebhookSecret').trim(),wu=gv('githubWebhookUrl').trim();
  if(!ws&&!githubWebhookSecretSet)return githubProblem('Generate a webhook secret before enabling webhook delivery.','githubWebhookSecret');
  if(ws&&ws.length<20)return githubProblem('Webhook secret must contain at least 20 characters.','githubWebhookSecret');
  if(wu&&!/^https:\/\/[^/\s]+/i.test(wu))return githubProblem('Webhook delivery URL must be a public https:// address.','githubWebhookUrl')}
 var cache=Number(gv('githubCacheSec'));if(!Number.isFinite(cache)||cache<60||cache>3600)return githubProblem('Bridge cache must be between 60 and 3600 seconds.','githubCacheSec');return true}
function bridgeRequest(url,opt){return fetch(url,opt).then(function(r){return r.json().catch(function(){throw Error('Bridge returned invalid JSON (HTTP '+r.status+').')}).then(function(d){if(!r.ok||d.ok===false)throw Error((d.error&&d.error.message)||d.message||('Bridge returned HTTP '+r.status+'.'));return d})})}
function addGithubAccount(a){var t=$('githubAccounts');if(!t)return;var tr=document.createElement('tr');tr.className='ghacct';
 tr.innerHTML='<td><input class="ghowner" type="text" placeholder="owner" value="'+esc((a&&a.owner)||'')+'"></td>'+
  '<td><input class="ghtoken" type="password" autocomplete="off" placeholder="'+((a&&a.tokenSet)?'(saved — blank keeps it)':'github_pat_...')+'"></td>'+
  '<td style="width:34px"><button class="btn sec" style="padding:6px 10px" onclick="this.closest(\'tr\').remove()">&times;</button></td>';t.appendChild(tr)}
function renderGithubAccounts(arr){var t=$('githubAccounts');if(!t)return;t.innerHTML='';(arr||[]).forEach(addGithubAccount);if(!arr||!arr.length)addGithubAccount({})}
function generateGithubWebhookSecret(){var bytes=new Uint8Array(32);crypto.getRandomValues(bytes);var value=Array.from(bytes).map(function(x){return x.toString(16).padStart(2,'0')}).join('');sv('githubWebhookSecret',value);var e=$('githubWebhookSecret');if(e){e.type='text';e.select()}toast('Webhook secret generated — copy it to GitHub, then save')}
function setGithubBridgeState(s,bad){var e=$('githubBridgeState');if(e){e.textContent=s;e.style.color=bad?'var(--red)':'var(--mut)'}}
function loadGithubBridge(show){if(!validateGithubFeed(true))return Promise.resolve();var url=githubBridgeUrl('config');
 setGithubBridgeState('Loading bridge...');return bridgeRequest(url,{headers:githubBridgeHeaders(false)}).then(function(c){var e=c.events||{};
  sv('githubBridgeMode',c.mode||'mock');sv('githubDelivery',c.delivery||'polling');sv('githubCacheSec',c.cacheSec||120);sv('githubRepositories',(c.repositories||[]).join('\n'));renderGithubAccounts(c.accounts||[]);
  var wh=c.webhook||{};sv('githubWebhookUrl',wh.publicUrl||'');sv('githubWebhookSecret','');githubWebhookSecretSet=!!wh.secretSet;var se=$('githubWebhookSecret');if(se){se.type='password';se.placeholder=wh.secretSet?'(saved — blank keeps it)':'Generate a secret'}
  sc('githubEventActions',e.actions!==false);sc('githubEventDeployments',e.deployments!==false);sc('githubEventPullRequests',e.pullRequests!==false);sc('githubEventReleases',e.releases!==false);
  var p=c.polling||{},rl=c.rateLimit||{},state='Bridge connected';
  if(c.delivery==='webhook')state+=' · webhook '+(wh.secretSet?'secured':'secret missing')+' · '+(wh.received||0)+' deliveries · '+(wh.tracked||0)+' tracked';
  else if(p.effectiveCacheSec)state+=' · GitHub refresh '+p.effectiveCacheSec+'s · ~'+p.requestsPerRefresh+' requests/cycle';
  if(rl.blocked&&rl.blockedUntil)state+=' · paused until '+new Date(rl.blockedUntil).toLocaleTimeString();
  setGithubBridgeState(state,c.delivery==='webhook'?!wh.secretSet:!!rl.blocked);if(show)toast('Bridge settings loaded')
 }).catch(function(e){setGithubBridgeState('Bridge unavailable: '+e.message,true);if(show)toast('Bridge unavailable')})}
function saveGithubBridge(){if(!validateGithubBridge())return;var url=githubBridgeUrl('config'),accounts=[];
 document.querySelectorAll('#githubAccounts .ghacct').forEach(function(tr){var owner=tr.querySelector('.ghowner').value.trim();if(owner)accounts.push({owner:owner,token:tr.querySelector('.ghtoken').value.trim()})});
 var body={mode:gv('githubBridgeMode'),delivery:gv('githubDelivery'),cacheSec:parseInt(gv('githubCacheSec'))||120,accounts:accounts,
  events:{actions:gc('githubEventActions'),deployments:gc('githubEventDeployments'),pullRequests:gc('githubEventPullRequests'),releases:gc('githubEventReleases')},
  repositories:gv('githubRepositories').split(/[,\n]/).map(function(x){return x.trim()}).filter(Boolean),
  webhook:{publicUrl:gv('githubWebhookUrl').trim(),secret:gv('githubWebhookSecret').trim()}};
 setGithubBridgeState('Saving bridge...');bridgeRequest(url,{method:'POST',headers:githubBridgeHeaders(true),body:JSON.stringify(body)}).then(function(c){
  renderGithubAccounts(c.accounts||[]);var p=c.polling||{},wh=c.webhook||{};githubWebhookSecretSet=!!wh.secretSet;sv('githubWebhookSecret','');
  setGithubBridgeState('Bridge settings saved'+(c.delivery==='webhook'?' · webhook waiting for GitHub':(p.effectiveCacheSec?' · GitHub refresh '+p.effectiveCacheSec+'s':'')));
  toast('GitHub bridge saved')
 }).catch(function(e){setGithubBridgeState('Could not save: '+e.message,true);toast('Bridge save failed')})}
function testGithubFeed(){if(!validateGithubBridge())return;var url=githubBridgeUrl('github');setGithubBridgeState('Testing feed...');
 bridgeRequest(url,{headers:githubBridgeHeaders(false)}).then(function(d){
  setGithubBridgeState('Feed OK — '+((d.items||d.runs||[]).length)+' GitHub events');toast('GitHub feed works')
 }).catch(function(e){setGithubBridgeState('Feed failed: '+e.message,true);toast('Feed test failed')})}

// wifi networks (up to 4)
function renderWifi(arr){var t=$('wifiTable');if(!t)return;t.innerHTML='';arr.forEach(addWifiRow);if(!arr.length)addWifiRow({})}
function addWifiRow(o){var t=$('wifiTable');var tr=document.createElement('tr');tr.className='symrow';
 tr.innerHTML='<td style="width:44%"><input class="ws" type="text" autocomplete="off" placeholder="SSID" value="'+esc(o.ssid||'')+'"></td>'+
  '<td><input class="wp" type="password" autocomplete="off" placeholder="'+(o.passSet?'(unchanged)':'password')+'"></td>'+
  '<td style="width:34px"><button class="btn sec" style="padding:6px 10px" onclick="this.closest(\'tr\').remove()">&times;</button></td>';
 t.appendChild(tr);}
function addWifi(){if(document.querySelectorAll('#wifiTable tr').length>=4){toast('Max 4');return}addWifiRow({})}
function collectWifi(){var w=[];document.querySelectorAll('#wifiTable tr').forEach(function(tr){
 var s=tr.querySelector('.ws').value.trim();if(!s)return;
 var e={ssid:s};var p=tr.querySelector('.wp').value;if(p)e.pass=p;w.push(e);});return w}
function scanPick(ssid){var rows=document.querySelectorAll('#wifiTable tr');var tr=null;
 for(var i=0;i<rows.length;i++){if(!rows[i].querySelector('.ws').value.trim()){tr=rows[i];break}}
 if(!tr){if(rows.length>=4){toast('Max 4');return}addWifiRow({});tr=$('wifiTable').lastChild}
 tr.querySelector('.ws').value=ssid;tr.querySelector('.wp').focus();}

// symbols
function renderSyms(arr){var t=$('symTable');if(!t)return;t.innerHTML='';arr.forEach(addRow);if(!arr.length)addRow({})}
function addRow(o){var t=$('symTable');var tr=document.createElement('tr');tr.className='symrow';
 tr.innerHTML='<td style="width:24%"><input class="s" type="text" placeholder="AAPL" value="'+esc(o.symbol||'')+'"></td>'+
  '<td><input class="n" type="text" placeholder="name" value="'+esc(o.name||'')+'"></td>'+
  '<td style="width:118px"><select class="src" onchange="symHintFor(this.value)">'+
   '<option value="yahoo">Yahoo Finance</option><option value="cash">cash.ch</option><option value="github">GitHub</option><option value="webhook">Webhook</option></select></td>'+
  '<td style="width:58px"><input class="q" type="number" step="any" min="0" placeholder="qty" value="'+(o.qty>0?o.qty:'')+'"></td>'+
  '<td style="width:70px"><input class="c" type="number" step="any" min="0" placeholder="cost" value="'+(o.cost>0?o.cost:'')+'"></td>'+
  '<td style="width:34px"><button class="btn sec" style="padding:6px 10px" onclick="this.closest(\'tr\').remove()">&times;</button></td>';
 tr.querySelector('.src').value=o.source||'yahoo';
 t.appendChild(tr);}
function addSym(){if(document.querySelectorAll('#symTable tr').length>=8){toast('Max 8');return}addRow({})}

// wifi scan
function scan(){$('scanList').innerHTML='<div class="muted">Scanning...</div>';
 j('/api/scan').then(function(l){var h='';l.sort(function(a,b){return b.rssi-a.rssi});
  l.forEach(function(n){h+='<div class="net" onclick="scanPick(this.dataset.s)" data-s="'+
   esc(n.ssid)+'"><span>'+(n.enc?'🔒 ':'')+esc(n.ssid)+'</span><span class="muted">'+n.rssi+' dBm</span></div>'});
  $('scanList').innerHTML=h||'<div class="muted">No networks found</div>';})}

// status
function loadStatus(){j('/api/status').then(function(s){
 $('dot').className='dot'+(s.connected?' ok':'');
 $('hi').textContent=s.mode==='ap'?'setup mode':(s.ip||'');
 var cn=$('clockNow'); if(cn){var ne=!!(C.clock&&C.clock.nightEnabled);var ns=s.night?'  · night mode active':(s.nightHeld?'  · night mode waiting for NTP':'');cn.textContent=!ne?'Clock: NTP runs only when night mode is on':('Clock: '+(s.synced?(s.time||'synced')+(s.tz?' ('+s.tz+')':''):'waiting for NTP...')+ns);}
 var fw=$('fwVer'); if(fw)fw.textContent=s.fw+' '+s.version;
 // Surface the result of a boot-time GitHub update (ESP8266) once on first load,
 // so a failure that happened across the reboot is visible even if the original
 // Update tab was closed. Don't clobber an in-progress check/update message.
 if(!window._otaShown){window._otaShown=1;var gm=$('ghMsg');if(gm&&!gm.textContent&&s.updateMsg&&s.updateMsg!=='updating...')gm.textContent='Last update: '+s.updateMsg}
 var fv=$('footVer'); if(fv)fv.textContent=' v'+s.version;
 if(s.repo){var rl=$('repoLink'); if(rl)rl.href=s.repo+'/releases'; var fr=$('footRepo'); if(fr)fr.href=s.repo;}
 $('statusBox').innerHTML=
  kv('Firmware',s.fw+' '+s.version)+kv('Mode',s.mode.toUpperCase())+
  kv('Network',s.ssid||'-')+kv('IP',s.ip||'-')+kv('mDNS','http://'+(C.hostname||'smalltv')+'.local')+
  kv('Signal',s.rssi?s.rssi+' dBm':'-')+
  kv('Free heap',s.heap+' B')+kv('Uptime',fmtUp(s.uptime))+kv('Last reset',s.reset||'-');
 var h='';(s.tickers||[]).forEach(function(t){
  var c=t.error?'var(--red)':(t.valid?'var(--acc)':'var(--mut)');
  var pc=t.changePct!=null?(t.changePct>=0?'+':'')+t.changePct.toFixed(2)+'%':'';
  h+='<div class="kv"><b style="color:'+c+'">'+t.symbol+'</b><span>'+
   (t.valid?(t.price+'  '+pc):(t.error?'error':'...'))+'</span></div>';});
 $('tickBox').innerHTML=h||'<span class="muted">No tickers configured</span>';
})}
function kv(k,v){return '<div class="kv"><span class="muted">'+k+'</span><b>'+esc(v)+'</b></div>'}
function fmtUp(s){var d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);
 return (d?d+'d ':'')+(h?h+'h ':'')+m+'m'}
function refreshNow(){j('/api/refresh',{method:'POST'}).then(function(){toast('Refreshing...');setTimeout(loadStatus,1500)})}

// GitHub self-update
function checkUpdate(){$('ghMsg').textContent='Checking GitHub...';$('chkBtn').disabled=true;
 j('/api/checkupdate').then(function(u){$('chkBtn').disabled=false;
  if(!u.ok){$('ghMsg').textContent='Check failed: '+(u.error||'unknown');return}
  if(u.newer){$('ghMsg').innerHTML='Version <b>'+u.latest+'</b> is available (installed '+u.current+').';$('ghUpBtn').disabled=false}
  else{$('ghMsg').textContent='Up to date ('+u.current+').';$('ghUpBtn').disabled=true}
 }).catch(function(){$('chkBtn').disabled=false;$('ghMsg').textContent='Check failed'})}
function selfUpdate(){if(!confirm('Download and flash the latest release from GitHub? The device reboots if it succeeds.'))return;
 $('ghUpBtn').disabled=true;$('chkBtn').disabled=true;
 $('ghMsg').textContent='Downloading and flashing... this can take a couple of minutes and the device may reboot twice.';
 // Installed version, read synchronously from the already-loaded status so the
 // poller below can recognise success (new version) without racing a fetch.
 var cur=(($('fwVer').textContent||'').trim().split(' ').pop())||'';
 j('/api/selfupdate',{method:'POST'}).then(function(){
  var n=0;var t=setInterval(function(){n++;
   j('/api/status').then(function(s){
    if(cur&&s.version&&s.version!==cur){clearInterval(t);$('ghMsg').textContent='Updated to '+s.version+'.';$('chkBtn').disabled=false;return}
    var m=s.updateMsg||'';
    if(m&&m!=='starting...'&&m!=='updating...'){clearInterval(t);$('ghMsg').textContent='Update failed: '+m;$('chkBtn').disabled=false}
   }).catch(function(){});
   if(n>100)clearInterval(t);
  },3000);
 }).catch(function(){$('ghMsg').textContent='Could not start update';$('chkBtn').disabled=false})}

// settings backup
function importCfg(){var f=$('cfgFile').files[0];if(!f){toast('Pick a config .json first');return}
 var r=new FileReader();
 r.onload=function(){var txt=r.result;
  try{JSON.parse(txt)}catch(e){toast('Not valid JSON');return}
  if(!confirm('Apply this configuration and reboot?'))return;
  j('/api/import',{method:'POST',headers:{'Content-Type':'application/json'},body:txt})
   .then(function(){toast('Imported, rebooting...');setTimeout(function(){location.reload()},8000)})
   .catch(function(){toast('Import failed')});
 };
 r.readAsText(f);}

// maintenance
function reboot(){if(confirm('Reboot device?'))j('/api/reboot',{method:'POST'}).then(function(){toast('Rebooting...')})}
function factory(){if(confirm('Erase ALL settings and reboot?'))j('/api/factory',{method:'POST'}).then(function(){toast('Reset, rebooting...')})}

// OTA
function upload(){var f=$('fw').files[0];if(!f){toast('Pick a .bin first');return}
 var fd=new FormData();fd.append('firmware',f,f.name);
 var x=new XMLHttpRequest();x.open('POST','/update');
 $('upBtn').disabled=true;
 x.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);$('upBar').style.width=p+'%';$('upMsg').textContent='Uploading '+p+'%'}};
 x.onload=function(){$('upBtn').disabled=false;if(x.status==200){$('upMsg').textContent='Done. Rebooting...';$('upBar').style.width='100%';setTimeout(function(){location.reload()},9000)}else{$('upMsg').textContent='Failed: '+x.responseText}};
 x.onerror=function(){$('upBtn').disabled=false;$('upMsg').textContent='Upload error'};
 x.send(fd);
}

// Crop Modal Logic
var cropData={x:0,y:0,size:100,imgW:0,imgH:0,dispW:0,dispH:0,activeFile:null};

function openCropModal(){
 var f=$('photoFile').files[0];
 if(!f){toast('Select an image file first');return;}
 handlePhotoSelect({target:$('photoFile')});
}

function handlePhotoSelect(e){
 var file=e.target.files[0];
 if(!file)return;
 cropData.activeFile=file;
 var reader=new FileReader();
 reader.onload=function(evt){
  var img=$('cropImg');
  img.onload=function(){
   cropData.imgW=img.naturalWidth;
   cropData.imgH=img.naturalHeight;
   $('cropModal').classList.add('show');
   setTimeout(initCropBox, 50);
  };
  img.src=evt.target.result;
 };
 reader.readAsDataURL(file);
}

function initCropBox(){
 var img=$('cropImg');
 cropData.dispW=img.clientWidth || img.offsetWidth;
 cropData.dispH=img.clientHeight || img.offsetHeight;

 if (!cropData.dispW || !cropData.dispH) {
  setTimeout(initCropBox, 50);
  return;
 }

 var maxSquare=Math.min(cropData.dispW, cropData.dispH);
 cropData.size=maxSquare;
 cropData.x=Math.round((cropData.dispW-maxSquare)/2);
 cropData.y=Math.round((cropData.dispH-maxSquare)/2);
 updateCropBoxUI();
}

function updateCropBoxUI(){
 var box=$('cropBox');
 box.style.left=cropData.x+'px';
 box.style.top=cropData.y+'px';
 box.style.width=cropData.size+'px';
 box.style.height=cropData.size+'px';
}

function closeCropModal(){
 $('cropModal').classList.remove('show');
 $('photoFile').value='';
}

(function setupCropEvents(){
 var isDragging=false, activeHandle=null;
 var startX=0, startY=0, origX=0, origY=0, origSize=0;

 document.addEventListener('pointerdown',function(e){
  if(e.target.classList.contains('crop-handle')){
   activeHandle=e.target.dataset.h || 'se';
   startX=e.clientX; startY=e.clientY;
   origX=cropData.x; origY=cropData.y; origSize=cropData.size;
   e.preventDefault();
  } else if(e.target===$('cropBox') || e.target.parentNode===$('cropBox')){
   isDragging=true; startX=e.clientX; startY=e.clientY;
   origX=cropData.x; origY=cropData.y;
   e.preventDefault();
  }
 });

 document.addEventListener('pointermove',function(e){
  if(isDragging){
   var dx=e.clientX-startX, dy=e.clientY-startY;
   cropData.x=Math.max(0,Math.min(cropData.dispW-cropData.size,origX+dx));
   cropData.y=Math.max(0,Math.min(cropData.dispH-cropData.size,origY+dy));
   updateCropBoxUI();
  } else if(activeHandle){
   var dx=e.clientX-startX, dy=e.clientY-startY;
   if (activeHandle==='se'){
    var maxSz=Math.min(cropData.dispW-origX, cropData.dispH-origY);
    var delta=Math.max(dx, dy);
    cropData.size=Math.max(40, Math.min(maxSz, origSize+delta));
   } else if (activeHandle==='nw'){
    var delta=Math.max(-dx, -dy);
    var maxSz=origSize+Math.min(origX, origY);
    var newSz=Math.max(40, Math.min(maxSz, origSize+delta));
    cropData.x=origX-(newSz-origSize);
    cropData.y=origY-(newSz-origSize);
    cropData.size=newSz;
   } else if (activeHandle==='ne'){
    var delta=Math.max(dx, -dy);
    var maxSz=origSize+Math.min(cropData.dispW-(origX+origSize), origY);
    var newSz=Math.max(40, Math.min(maxSz, origSize+delta));
    cropData.y=origY-(newSz-origSize);
    cropData.size=newSz;
   } else if (activeHandle==='sw'){
    var delta=Math.max(-dx, dy);
    var maxSz=origSize+Math.min(origX, cropData.dispH-(origY+origSize));
    var newSz=Math.max(40, Math.min(maxSz, origSize+delta));
    cropData.x=origX-(newSz-origSize);
    cropData.size=newSz;
   }
   updateCropBoxUI();
  }
 });

 document.addEventListener('pointerup',function(){ isDragging=false; activeHandle=null; });
})();

function applyCropAndUpload(){
 var img=$('cropImg');
 var scale=cropData.imgW/cropData.dispW;
 var sourceX=cropData.x*scale;
 var sourceY=cropData.y*scale;
 var sourceSize=cropData.size*scale;

 var canvas=document.createElement('canvas');
 canvas.width=240; canvas.height=240;
 var ctx=canvas.getContext('2d');
 ctx.imageSmoothingEnabled=true;
 ctx.imageSmoothingQuality='high';
 ctx.drawImage(img,sourceX,sourceY,sourceSize,sourceSize,0,0,240,240);

 canvas.toBlob(function(blob){
  if(!blob){toast('Error processing image');return;}
  var fileName=cropData.activeFile?cropData.activeFile.name:'photo.jpg';
  var lastDot=fileName.lastIndexOf('.');
  if(lastDot>0){
   var ext=fileName.substring(lastDot+1).toLowerCase();
   if(ext!=='jpg'&&ext!=='jpeg') fileName=fileName.substring(0,lastDot)+'.jpg';
  } else {
   fileName+='.jpg';
  }

  var fd=new FormData();
  fd.append('photo',blob,fileName);

  closeCropModal();

  var p=(window.C&&window.C.adminPass)||'';
  var u='/api/photos/upload'+(p?'?pass='+encodeURIComponent(p):'');
  var x=new XMLHttpRequest();
  x.open('POST',u);
  $('photoUpBtn').disabled=true;
  $('photoMsg').textContent='Uploading cropped 240x240 image ('+Math.round(blob.size/1024)+' KB)...';

  x.upload.onprogress=function(e){
   if(e.lengthComputable){
    var p=Math.round(e.loaded/e.total*100);
    $('photoBar').style.width=p+'%';
   }
  };
  x.onload=function(){
   $('photoUpBtn').disabled=false;
   if(x.status==200){
    $('photoMsg').textContent='Photo uploaded successfully! ('+Math.round(blob.size/1024)+' KB)';
    $('photoBar').style.width='100%';
    loadPhotos();
   } else {
    $('photoMsg').textContent='Upload failed: '+x.responseText;
   }
  };
  x.onerror=function(){
   $('photoUpBtn').disabled=false;
   $('photoMsg').textContent='Upload error';
  };
   x.send(fd);
  },'image/jpeg',0.88);
}

loadConfig().then(loadStatus);
setInterval(loadStatus,5000);
</script>

<!-- Crop Modal -->
<div id="cropModal" class="crop-modal">
 <div class="crop-card">
  <div class="crop-head">
   <h3>Crop Photo for SmallTV (1:1 Aspect Ratio)</h3>
   <button class="btn sec" style="padding:4px 8px" onclick="closeCropModal()">&times;</button>
  </div>
  <div class="crop-body">
   <div id="cropWrap" class="crop-wrap">
    <img id="cropImg" src="" alt="Crop target">
    <div id="cropBox" class="crop-box">
     <div class="crop-handle nw" data-h="nw"></div>
     <div class="crop-handle ne" data-h="ne"></div>
     <div class="crop-handle sw" data-h="sw"></div>
     <div class="crop-handle se" data-h="se"></div>
    </div>
   </div>
  </div>
  <div class="crop-foot">
   <button class="btn sec" onclick="closeCropModal()">Cancel</button>
   <button class="btn" onclick="applyCropAndUpload()">Crop &amp; Upload (240x240)</button>
  </div>
 </div>
</div>
</body></html>)HTMLPAGE";
