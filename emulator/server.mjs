import http from 'node:http';
import os from 'node:os';
import { readFileSync, writeFileSync, mkdirSync, readdirSync, unlinkSync } from 'node:fs';
import { GithubWebhookStore } from './github-webhooks.mjs';

const PORT = Number(process.env.PORT || 8788);
const SETTINGS_PORT = Number(process.env.SETTINGS_PORT || 8789);
const HOST = process.env.HOST || '0.0.0.0';
const DEVICE_TOKEN = process.env.DEVICE_TOKEN || '';
const CONFIG_FILE = new URL('./config.local.json', import.meta.url);
const DEVICE_CONFIG_FILE = new URL('./device-config.local.json', import.meta.url);
const GITHUB_EVENTS_FILE = new URL('./github-events.local.json', import.meta.url);
const WEBUI_FILE = new URL('../src/webui.h', import.meta.url);
const EMULATOR_PHOTOS_DIR = new URL('./photos/', import.meta.url);

try { mkdirSync(EMULATOR_PHOTOS_DIR, { recursive: true }); } catch {}

const logs = [];
let emulatorPhotos = [];
let pvGalleryIdx = 0; // tracks current gallery photo in emulator preview rotation

function loadEmulatorPhotosFromDisk() {
  try {
    const files = readdirSync(EMULATOR_PHOTOS_DIR);
    emulatorPhotos = [];
    for (const f of files) {
      if (/\.(jpg|jpeg|png|gif|raw)$/i.test(f)) {
        const filePath = new URL(`./photos/${f}`, import.meta.url);
        const buf = readFileSync(filePath);
        const mime = f.endsWith('.png') ? 'image/png' : (f.endsWith('.gif') ? 'image/gif' : 'image/jpeg');
        emulatorPhotos.push({ name: f, path: `/photos/${f}`, size: buf.length, buffer: buf, mime });
      }
    }
  } catch {}
}
loadEmulatorPhotosFromDisk();
function record(level, event, details = {}) {
  const entry = { at: new Date().toISOString(), level, event, ...details };
  logs.push(entry); if (logs.length > 250) logs.shift();
  const detail = details.message || details.repo || details.mode || '';
  console.log(`[${entry.at}] ${level.toUpperCase()} ${event}${detail ? `: ${detail}` : ''}`);
  return entry;
}

function extractImageBuffer(rawBuf) {
  if (!rawBuf || !rawBuf.length) return { buffer: Buffer.alloc(0), mime: 'image/jpeg' };
  // Check for JPEG SOI (0xFF 0xD8)
  const jpgStart = rawBuf.indexOf(Buffer.from([0xFF, 0xD8]));
  if (jpgStart >= 0) {
    const jpgEnd = rawBuf.lastIndexOf(Buffer.from([0xFF, 0xD9]));
    if (jpgEnd > jpgStart) {
      return { buffer: rawBuf.subarray(jpgStart, jpgEnd + 2), mime: 'image/jpeg' };
    }
  }
  // Check for PNG header (0x89 0x50 0x4E 0x47)
  const pngStart = rawBuf.indexOf(Buffer.from([0x89, 0x50, 0x4E, 0x47]));
  if (pngStart >= 0) {
    const nextBoundary = rawBuf.indexOf(Buffer.from('\r\n--'), pngStart);
    const end = nextBoundary >= 0 ? nextBoundary : rawBuf.length;
    return { buffer: rawBuf.subarray(pngStart, end), mime: 'image/png' };
  }
  // Check for GIF header (GIF8)
  const gifStart = rawBuf.indexOf(Buffer.from('GIF8'));
  if (gifStart >= 0) {
    const nextBoundary = rawBuf.indexOf(Buffer.from('\r\n--'), gifStart);
    const end = nextBoundary >= 0 ? nextBoundary : rawBuf.length;
    return { buffer: rawBuf.subarray(gifStart, end), mime: 'image/gif' };
  }
  // Fallback: headers end at \r\n\r\n
  const headerEnd = rawBuf.indexOf(Buffer.from('\r\n\r\n'));
  if (headerEnd >= 0) {
    let body = rawBuf.subarray(headerEnd + 4);
    const trail = body.indexOf(Buffer.from('\r\n--'));
    if (trail >= 0) body = body.subarray(0, trail);
    return { buffer: body, mime: 'image/jpeg' };
  }
  return { buffer: rawBuf, mime: 'image/jpeg' };
}

const githubWebhooks = new GithubWebhookStore(GITHUB_EVENTS_FILE, { record });

function lanIps() {
  return Object.values(os.networkInterfaces()).flat().filter(x => x?.family === 'IPv4' && !x.internal).map(x => x.address);
}

const DEVICE_PREVIEW_CSS = String.raw`
<style id="local-device-preview-css">
.pv-lab{position:fixed;right:18px;top:74px;width:306px;z-index:12;color:#d8f7ef;font-family:ui-monospace,SFMono-Regular,Menlo,monospace}
.pv-panel{padding:13px;background:rgba(10,14,19,.96);border:1px solid #2a3946;border-radius:18px;box-shadow:0 22px 70px rgba(0,0,0,.46)}
.pv-head,.pv-foot,.pv-controls{display:flex;align-items:center;justify-content:space-between;gap:8px}
.pv-head{margin-bottom:10px;font-size:10px;letter-spacing:.12em;color:#7d91a0}
.pv-head strong{color:#52f2b8;font-size:11px}.pv-state{padding:3px 6px;border:1px solid #315849;border-radius:8px;color:#63dcae}
.pv-state.dirty{border-color:#80622b;color:#ffc857}.pv-shell{position:relative;margin:auto;width:270px;padding:15px;border-radius:24px;background:linear-gradient(145deg,#27303a,#101419 58%,#07090c);border:1px solid #3a4550;box-shadow:inset 0 1px 1px rgba(255,255,255,.14),inset 0 -7px 14px rgba(0,0,0,.45)}
.pv-brand{text-align:center;margin-bottom:8px;font:700 9px system-ui;letter-spacing:.18em;color:#7f8c98}.pv-screen{position:relative;width:240px;height:240px;overflow:hidden;background:#020406;border:2px solid #050607;border-radius:4px;box-shadow:0 0 0 1px #38444e,0 0 23px rgba(47,129,247,.14);transform-origin:center}
.pv-frame{position:absolute;inset:0;padding:6px 4px;color:#e6edf3;background:#050a12;opacity:0;transform:scale(.992);transition:opacity .22s ease,transform .22s ease}
.pv-frame.pv-in{opacity:1;transform:none}.pv-frame.pv-out{opacity:0;transform:scale(1.008)}
.pv-frame.pv-live-frame{padding:0}.pv-live-frame iframe{display:block;width:240px;height:240px;border:0;background:#050a12}
.pv-top{height:25px;display:flex;justify-content:space-between;align-items:flex-start;padding:1px 3px;border-bottom:1px solid #193047;font-size:9px;letter-spacing:.08em;color:#39e7ff;position:relative}.pv-top:after{content:"";position:absolute;bottom:-1px;left:0;width:54px;height:1px;background:#39e7ff}.pv-top b{font-size:17px;line-height:20px;color:#f2f6fa;letter-spacing:-.04em}
.pv-time{color:#6c7d84}.pv-big{font-size:30px;font-weight:800;letter-spacing:-.05em;color:#fff}.pv-green{color:#4ee69c}.pv-red{color:#ff5c68}.pv-blue{color:#58a9ff}.pv-dim{color:#71858b}.pv-tag{display:inline-block;padding:2px 5px;border-radius:6px;background:#14231f;color:#69ddb2;font-size:8px;letter-spacing:.08em}
.pv-chart{width:100%;height:93px;margin:7px 0 1px}.pv-chart path{fill:none;stroke:#39e7ff;stroke-width:3}.pv-chart .area{fill:url(#pvgrad);stroke:none}.pv-grid{stroke:#193047;stroke-width:1}
.pv-quote{display:flex;justify-content:space-between;align-items:end;margin:8px 4px 0}.pv-quote small{display:block;font-size:9px;color:#718096}.pv-dots{text-align:center;color:#37564d;font-size:8px;letter-spacing:3px}.pv-dots b{color:#39e7ff}
.pv-ai-summary{height:25px;display:flex;align-items:center;justify-content:space-around;color:#718096;font-size:10px;font-weight:800}.pv-ai-summary b{color:#f2f6fa}.pv-meter{height:84px;margin-bottom:6px;padding:8px 10px;background:#0d1828;border:1px solid #193047;border-left:4px solid var(--meter);border-radius:6px;position:relative}.pv-meter strong{font-size:14px;color:#f2f6fa}.pv-meter .pv-percent{position:absolute;right:9px;top:7px;color:var(--meter);font-size:22px;font-weight:900}.pv-meter small{display:block;color:#718096;font-size:10px;margin-top:8px}.pv-meter-track{height:9px;margin-top:9px;border-radius:5px;background:#193047;overflow:hidden}.pv-meter-track i{display:block;height:100%;width:var(--fill);border-radius:5px;background:var(--meter);box-shadow:0 0 8px color-mix(in srgb,var(--meter),transparent 35%);transition:width .5s ease}
.pv-gh-stats{display:flex;gap:5px;margin:7px 0}.pv-gh-stats span{flex:1;padding:5px;border-radius:5px;background:#0b1212;text-align:center;font-size:10px}.pv-event{display:grid;grid-template-columns:30px 1fr auto;gap:7px;align-items:center;min-height:73px;padding:8px 5px;border:1px solid #17362e;border-left:3px solid currentColor;border-radius:7px;margin-top:6px}.pv-event.latest{border-color:#39e7ff;box-shadow:inset 0 0 0 1px #39e7ff55}.pv-icon{display:grid;place-items:center;width:25px;height:25px;border-radius:50%;border:3px solid currentColor;font-weight:900;font-size:13px}.pv-icon.run{border-top-color:transparent;animation:pvSpin 1s linear infinite}.pv-event b{display:block;max-width:130px;overflow:hidden;white-space:nowrap;font-size:13px}.pv-event b.pv-marquee span{display:inline-block;animation:pvTextLoop 8s linear infinite}.pv-event small{display:block;max-width:130px;overflow:hidden;white-space:nowrap;color:#91a49e;font-size:10px;margin-top:5px}.pv-event time{font-size:10px;color:#a2b3ad}
@keyframes pvTextLoop{0%,15%{transform:translateX(0)}100%{transform:translateX(-52%)}}
@keyframes pvSpin{to{transform:rotate(360deg)}}.pv-caption{margin:9px 1px 8px;font:11px/1.45 system-ui;color:#8999a6}.pv-controls button{width:34px;height:30px;border:1px solid #30404d;border-radius:8px;background:#151c23;color:#d9e6ee;cursor:pointer}.pv-controls button:hover{border-color:#52d8aa}.pv-mode{flex:1;text-align:center;font-size:10px;color:#a9bbc5}.pv-foot{margin-top:9px;font-size:9px;color:#657783}.pv-progress{height:2px;flex:1;background:#182229;overflow:hidden}.pv-progress i{display:block;height:100%;width:0;background:#51e4aa}.pv-progress i.go{animation:pvProgress 4s linear infinite}@keyframes pvProgress{from{width:0}to{width:100%}}
.pv-local-badge{position:fixed;right:12px;bottom:12px;background:#2f81f7;color:white;padding:7px 10px;border-radius:8px;font:12px system-ui;z-index:20}
@media(max-width:1100px){.pv-lab{position:relative;right:auto;top:auto;width:min(100% - 32px,680px);margin:16px auto}.pv-panel{display:grid;grid-template-columns:300px 1fr;gap:14px}.pv-head{grid-column:1/-1}.pv-caption,.pv-controls,.pv-foot{grid-column:2}.pv-controls{align-self:end}.pv-foot{align-self:start}.pv-local-badge{display:none}}
@media(max-width:620px){.pv-panel{display:block}.pv-caption{margin-top:12px}.pv-lab{width:calc(100% - 20px)}}
</style>`;

const DEVICE_PREVIEW_HTML = String.raw`
<aside class="pv-lab" id="pvLab" aria-label="Virtual SmallTV preview">
 <div class="pv-panel">
  <div class="pv-head"><strong>VIRTUAL SMALLTV</strong><span>LOCAL PREVIEW</span><span class="pv-state" id="pvState">SAVED</span></div>
  <div class="pv-shell"><div class="pv-brand">SMALLTV · ULTRA</div><div class="pv-screen" id="pvScreen"></div></div>
  <div class="pv-caption">Live preview of the candidate firmware. Changes take effect instantly; they reach the device after build and OTA.</div>
  <div class="pv-controls"><button type="button" id="pvPrev" title="Previous screen">‹</button><div class="pv-mode" id="pvMode">—</div><button type="button" id="pvPlay" title="Pause carousel">Ⅱ</button><button type="button" id="pvNext" title="Next screen">›</button></div>
  <div class="pv-foot"><span id="pvTiming">STATIC</span><div class="pv-progress"><i id="pvProgress"></i></div><span id="pvCount">1/1</span></div>
 </div>
</aside>`;

const DEVICE_PREVIEW_JS = String.raw`
<script id="local-device-preview-js">
 (function(){
  var pvNames={stocks:'TICKER',usage:'AI USAGE',github:'GH//STAT',clock:'CLOCK FULLSCREEN',clock_weather:'WEATHER STATION',clock_modern:'MODERN CLOCK',clock_forecast:'3-DAY FORECAST',gallery:'GALLERY'};
  var pvIndex=0,pvTimer=null,pvSyncTimer=null,pvRunId=0,pvPaused=false,pvBaseline='',pvReady=false;
  function pvEl(id){return document.getElementById(id)}
  function pvVal(id,def){var e=pvEl(id);return e?e.value:def}
  function pvChecked(id){var e=pvEl(id);return !!(e&&e.checked)}
  function pvModes(){
   var mode=pvVal('mode','stocks');
   if(mode!=='carousel')return [mode];
   var out=[];
   if(pvChecked('carouselTicker'))out.push('stocks');
   if(pvChecked('carouselUsage'))out.push('usage');
   if(pvChecked('carouselGithub'))out.push('github');
   if(pvChecked('carouselClockDigital'))out.push('clock');
   if(pvChecked('carouselClockWeather'))out.push('clock_weather');
   if(pvChecked('carouselClockModern'))out.push('clock_modern');
   if(pvChecked('carouselClockForecast'))out.push('clock_forecast');
   if(pvChecked('carouselGallery'))out.push('gallery');
   return out.length?out:['github'];
  }
  function pvScreenStocks(){
   return '<div class="pv-frame"><div class="pv-top"><b>MK<span class="pv-blue">//</span>STAT</b><span>AAPL · 1D</span><span class="pv-time">14:32</span></div>'+
    '<div class="pv-quote"><div><small>APPLE INC.</small><div class="pv-big">231.42</div></div><div class="pv-green">▲ 1.84%</div></div>'+
    '<svg class="pv-chart" viewBox="0 0 220 93" aria-hidden="true"><defs><linearGradient id="pvgrad" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#39e7ff" stop-opacity=".24"/><stop offset="1" stop-color="#39e7ff" stop-opacity="0"/></linearGradient></defs><path class="pv-grid" d="M0 23H220M0 46H220M0 69H220"/><path class="area" d="M0 76L19 69L40 73L58 49L76 58L98 38L116 43L138 19L156 31L179 17L199 25L220 9V93H0Z"/><path d="M0 76L19 69L40 73L58 49L76 58L98 38L116 43L138 19L156 31L179 17L199 25L220 9"/></svg>'+
    '<div class="pv-dots"><b>●</b> ● ●</div></div>';
  }
  function pvScreenUsage(){
   return '<div class="pv-frame"><div class="pv-top"><b>AI<span class="pv-blue">//</span>STAT</b><span>LOCAL BRIDGE</span><span class="pv-time">LIVE</span></div>'+
    '<div class="pv-ai-summary"><span>PROVIDERS <b>2</b></span><span>SYNC <b>NOW</b></span></div>'+
    '<div class="pv-meter" style="--meter:#39e7ff;--fill:34%"><strong>ANTIGRAVITY</strong><span class="pv-percent">34%</span><small>Gemini / Claude · reset 02:00</small><div class="pv-meter-track"><i></i></div></div>'+
    '<div class="pv-meter" style="--meter:#59ef9a;--fill:57%"><strong>CODEX</strong><span class="pv-percent">57%</span><small>Tokens / credits · reset 06:00</small><div class="pv-meter-track"><i></i></div></div></div>';
  }
  function pvScreenClock(theme){
   var cityEl = document.getElementById('weatherCity');
   var city = (cityEl && cityEl.value && cityEl.value.trim()) ? cityEl.value.trim().toUpperCase() : 'HUA HIN';
   if(theme===1){
    // Theme 1: Weather & Clock Station (Today Focus, no header title bar, large fonts)
    return '<div class="pv-frame" style="background:#040d1a;padding:8px;display:flex;flex-direction:column;justify-content:space-between">'+
     '<div style="background:#091b30;border:1px solid #1c3b5e;border-radius:12px;padding:12px 14px">'+
      '<div style="display:flex;justify-content:space-between;align-items:center">'+
       '<b style="font-size:18px;color:#70c5ff;letter-spacing:.05em">'+city+'</b>'+
       '<b style="font-size:32px;color:#ff9f43;letter-spacing:-.03em">⛅ +31°C</b>'+
      '</div>'+
      '<div style="font-size:14px;font-weight:800;letter-spacing:.1em;color:#fff;margin-top:6px">TROPICAL CLEAR</div>'+
     '</div>'+
     '<div style="background:#091b30;border:1px solid #1c3b5e;border-radius:12px;padding:12px 14px;text-align:center">'+
      '<div style="font-size:46px;font-weight:900;color:#fff;letter-spacing:-.04em;line-height:1">14:55</div>'+
      '<div style="font-size:14px;font-weight:700;color:#70c5ff;margin-top:6px;letter-spacing:.08em">THU · 23 JUL 2026</div>'+
     '</div>'+
    '</div>';
   }
   if(theme===2){
    // Theme 2: Modern OLED Dashboard Clock (no header title bar, large fonts)
    return '<div class="pv-frame" style="background:#000;padding:8px;display:flex;flex-direction:column;justify-content:space-between">'+
     '<div style="background:rgba(20,12,32,0.9);border:1px solid #a55eea;border-radius:14px;padding:16px 8px;text-align:center;box-shadow:0 0 20px rgba(165,94,234,0.2)">'+
      '<div style="font-size:62px;font-weight:900;letter-spacing:-.05em;color:#fff;line-height:1">14:55</div>'+
      '<div style="font-size:13px;letter-spacing:.15em;color:#a55eea;margin-top:8px;font-weight:800">THURSDAY 23 JUL</div>'+
     '</div>'+
     '<div style="background:#0b0712;border:1px solid #221338;border-radius:10px;padding:12px 14px;display:flex;justify-content:space-between;align-items:center">'+
      '<b style="font-size:16px;color:#fff">📍 '+city+'</b>'+
      '<b style="color:#a55eea;font-size:22px">+31°C ☀️</b>'+
     '</div>'+
    '</div>';
   }
   if(theme===3){
    // Theme 3: 3-Day Forecast Breakdown (full 240x240 screen height, no header title bar)
    return '<div class="pv-frame" style="background:#050914;padding:6px;display:flex;flex-direction:column;gap:6px;justify-content:space-between">'+
     '<div style="background:#091624;border:1px solid #163654;border-radius:10px;padding:10px 14px;display:flex;justify-content:space-between;align-items:center;flex:1">'+
      '<div><b style="font-size:14px;color:#39e7ff;display:block">TODAY · '+city+'</b><span style="font-size:13px;color:#fff;font-weight:700">SUNNY / CLEAR</span></div>'+
      '<b style="font-size:24px;color:#4ee69c">+31°C</b>'+
     '</div>'+
     '<div style="background:#140c20;border:1px solid #2a1b40;border-radius:10px;padding:10px 14px;display:flex;justify-content:space-between;align-items:center;flex:1">'+
      '<div><b style="font-size:14px;color:#a55eea;display:block">TOMORROW</b><span style="font-size:13px;color:#fff;font-weight:700">PARTLY CLOUDY</span></div>'+
      '<b style="font-size:24px;color:#ffb627">+33°C</b>'+
     '</div>'+
     '<div style="background:#051424;border:1px solid #142d4c;border-radius:10px;padding:10px 14px;display:flex;justify-content:space-between;align-items:center;flex:1">'+
      '<div><b style="font-size:14px;color:#70c5ff;display:block">SAT 25 JUL</b><span style="font-size:13px;color:#fff;font-weight:700">MOSTLY SUNNY</span></div>'+
      '<b style="font-size:24px;color:#70c5ff">+30°C</b>'+
     '</div>'+
    '</div>';
   }
   // Theme 0: Giant Fullscreen Clock (No headers, maximum font size)
   return '<div class="pv-frame" style="background:#02050a;padding:8px;display:flex;flex-direction:column;justify-content:center;align-items:center;text-align:center">'+
    '<div style="font-size:74px;font-weight:900;letter-spacing:-.06em;color:#39e7ff;line-height:1;text-shadow:0 0 25px rgba(57,231,255,0.6)">14:55</div>'+
    '<div style="font-size:14px;font-weight:800;letter-spacing:.15em;color:#ffb627;margin-top:16px;background:rgba(255,182,39,0.12);padding:6px 16px;border-radius:14px;border:1px solid rgba(255,182,39,0.3)">THURSDAY · 23 JUL 2026</div>'+
    '<div style="font-size:11px;color:#5a84a2;margin-top:14px">192.168.1.141</div>'+
   '</div>';
  }
  function pvScreenGallery(){
   var photos = window._emulatorPhotos || [];
   if (photos.length > 0) {
    var idx = (window.pvGalleryIdx||0) % photos.length;
    var p = photos[idx];
    return '<div class="pv-frame" style="padding:0;background:#000;overflow:hidden">' +
     '<img src="' + p.path + '?t='+Date.now()+'" style="width:240px;height:240px;object-fit:cover;display:block">' +
     '</div>';
   }
   return '<div class="pv-frame"><div class="pv-top"><b>PHOTO ALBUM</b><span>240x240</span><span class="pv-time">EMPTY</span></div>'+
    '<div style="height:175px;display:grid;place-items:center;background:#0d1828;border-radius:8px;margin:10px 0;border:1px solid #193047"><span class="pv-green" style="font-size:13px;text-align:center">🖼 No photos uploaded yet<br><span style="font-size:11px;opacity:.6">Upload photos in Gallery tab</span></span></div></div>';
  }
  function pvScreenGithub(){
   var src=location.protocol+'//'+location.hostname+':${PORT}/embed';
   return '<div class="pv-frame pv-live-frame"><iframe title="Live GH//STAT preview" src="'+src+'"></iframe></div>';
  }
  function pvMarkup(mode){
   if(mode==='usage') return pvScreenUsage();
   if(mode==='github') return pvScreenGithub();
   if(mode==='clock') return pvScreenClock(0);
   if(mode==='clock_weather') return pvScreenClock(1);
   if(mode==='clock_modern') return pvScreenClock(2);
   if(mode==='clock_forecast') return pvScreenClock(3);
   if(mode==='gallery') return pvScreenGallery();
   return pvScreenStocks();
  }
 function pvSignature(){
  var parts=[];document.querySelectorAll('input,select,textarea').forEach(function(e){
   if(!e.id||e.type==='file'||e.id.indexOf('pv')===0)return;
   var value=e.type==='checkbox'||e.type==='radio'?String(e.checked):(e.type==='password'?(e.value?'secret-present':''):e.value);
   parts.push(e.id+'='+value);
  });return parts.join('|');
 }
 function pvDirty(){
  if(!pvReady)return;
  var dirty=pvSignature()!==pvBaseline,state=pvEl('pvState');
  state.textContent=dirty?'UNSAVED':'SAVED';state.classList.toggle('dirty',dirty);
 }
 function pvRender(animate){
  var modes=pvModes();if(pvIndex>=modes.length)pvIndex=0;if(pvIndex<0)pvIndex=modes.length-1;
  var mode=modes[pvIndex],screen=pvEl('pvScreen'),old=Array.prototype.slice.call(screen.querySelectorAll('.pv-frame'));
  var holder=document.createElement('div');holder.innerHTML=pvMarkup(mode);var next=holder.firstChild;screen.appendChild(next);
  requestAnimationFrame(function(){next.classList.add('pv-in');if(animate)old.forEach(function(frame){frame.classList.add('pv-out')})});
  if(old.length)setTimeout(function(){old.forEach(function(frame){if(frame.parentNode)frame.parentNode.removeChild(frame)})},420);
  pvEl('pvMode').textContent=pvNames[mode]||mode.toUpperCase();
  pvEl('pvCount').textContent=(pvIndex+1)+'/'+modes.length;
  var carousel=pvVal('mode','stocks')==='carousel';
  var actual=Math.max(5,Number(pvVal('carouselSec','30'))||30);
  pvEl('pvTiming').textContent=carousel?('DEMO 4S · DEVICE '+actual+'S'):'STATIC';
  pvEl('pvPlay').style.visibility=carousel&&modes.length>1?'visible':'hidden';
  var bar=pvEl('pvProgress');bar.classList.toggle('go',carousel&&modes.length>1&&!pvPaused);
  pvEl('pvPlay').textContent=pvPaused?'▶':'Ⅱ';
 }
 function pvRestart(resetIndex){
  pvRunId++;var runId=pvRunId;
  if(pvTimer)clearTimeout(pvTimer);pvTimer=null;if(resetIndex!==false)pvIndex=0;pvRender(true);
  var modes=pvModes();
  if(pvVal('mode','stocks')==='carousel'&&modes.length>1&&!pvPaused){
   var step=function(){if(runId!==pvRunId)return;pvIndex=(pvIndex+1)%pvModes().length;
    var m=pvModes()[pvIndex];if(m==='gallery'){window.pvGalleryIdx=(window.pvGalleryIdx||0)+1;}
    pvRender(true);pvTimer=setTimeout(step,4000)};
   pvTimer=setTimeout(step,4000);
  }
 }
 window.pvRestart=pvRestart;
 function pvChanged(e){
  if(e&&e.target&&['mode','carouselSec','carouselTicker','carouselUsage','carouselGithub','carouselClockDigital','carouselClockWeather','carouselClockModern','carouselClockForecast','carouselGallery','brightness','rotation','weatherCity','clockTheme'].indexOf(e.target.id)>=0){
   if(pvSyncTimer)clearTimeout(pvSyncTimer);
   pvSyncTimer=setTimeout(pvRestart,120);
  }
  pvDirty();
 }
 pvEl('pvPrev').addEventListener('click',function(){var n=pvModes().length;pvIndex=(pvIndex-1+n)%n;pvRender(true)});
 pvEl('pvNext').addEventListener('click',function(){pvIndex=(pvIndex+1)%pvModes().length;pvRender(true)});
 pvEl('pvPlay').addEventListener('click',function(){pvPaused=!pvPaused;pvRestart(false)});
 document.addEventListener('input',pvChanged);document.addEventListener('change',pvChanged);
 var originalSave=window.saveAll;
 if(typeof originalSave==='function')window.saveAll=function(){
  var result=originalSave.apply(this,arguments);
  setTimeout(function(){fetch('/api/config').then(function(r){if(!r.ok)throw Error();return r.json()}).then(function(){pvBaseline=pvSignature();pvReady=true;pvDirty()}).catch(function(){})},700);
  return result;
 };
 pvRender(false);
 setTimeout(function(){pvRestart();pvBaseline=pvSignature();pvReady=true;pvDirty()},850);
})();
</script>`;

function extractWebUi() {
  const source = readFileSync(WEBUI_FILE, 'utf8');
  const match = source.match(/R"HTMLPAGE\(([\s\S]*?)\)HTMLPAGE"/);
  if (!match) throw new Error('Could not extract WEBUI_HTML from src/webui.h');
  return match[1]
    .replace('</head>', `${DEVICE_PREVIEW_CSS}</head>`)
    .replace('</body>', `${DEVICE_PREVIEW_HTML}<div class="pv-local-badge">LOCAL DEVICE EMULATOR :${SETTINGS_PORT}</div>${DEVICE_PREVIEW_JS}</body>`);
}

const DEVICE_WEB_UI = extractWebUi();

function mergeConfig(base, update) {
  if (!update || typeof update !== 'object' || Array.isArray(update)) return base;
  const out = { ...base };
  for (const [key, value] of Object.entries(update)) {
    if (value && typeof value === 'object' && !Array.isArray(value) && base[key] && typeof base[key] === 'object' && !Array.isArray(base[key])) out[key] = mergeConfig(base[key], value);
    else out[key] = value;
  }
  return out;
}

const initialFeed = `http://127.0.0.1:${PORT}/api/github`;
const initialAiUsageFeed = `http://${lanIps()[0] || '127.0.0.1'}:${PORT}/api/ai-usage`;
const DEFAULT_DEVICE_CONFIG = {
  wifi: [{ ssid: 'Local test WiFi', passSet: true }], apSsid: 'SmallTV-Setup', apPass: '', apPassSet: false,
  hostname: 'smalltv-emulator', adminPass: '1111', mode: 'github', carouselSec: 30,
  carouselTicker: true, carouselUsage: true, carouselGithub: true,
  carouselClock: true, carouselClockDigital: true, carouselClockWeather: false, carouselClockModern: false,
  carouselGallery: true,
  httpTimeout: 8000, brightness: 90, autoBrightness: false, backlightInverted: false, rotation: 0,
  features: { ticker: true, usage: true, github: true, clock: true, gallery: true }, chip: 'esp8266',
  ticker: { webhookUrl: '', range: '1d', points: 48, pollSec: 120, rotateSec: 10, colorInverted: false, changeOnRange: true, showName: true, showPrice: true, showChange: true, showChart: true, showRangeLabel: true, showUpdatedAgo: false, showPageDots: true, showPortfolio: true, symbols: [] },
  usage: { usageUrl: initialAiUsageFeed, pollSec: 120 },
  github: { statusUrl: initialFeed, tokenSet: false, pollSec: 15, rotateSec: 8 },
  clock: { tz: 'Asia/Bangkok', tzPosix: '<+07>-7', nightEnabled: false, nightStart: '22:00', nightEnd: '07:00', nightLevel: 0, format24h: true, showSeconds: false, showDate: true, theme: 0, weatherCity: 'Hua Hin', weatherApiKey: '', weatherUnits: 'c', weatherPollSec: 900 },
  gallery: { rotateSec: 10, randomOrder: false },
};
let deviceConfig = structuredClone(DEFAULT_DEVICE_CONFIG);

try {
  const savedDevice = JSON.parse(readFileSync(DEVICE_CONFIG_FILE, 'utf8'));
  deviceConfig = mergeConfig(deviceConfig, savedDevice);
} catch {}
if (deviceConfig.mode === 'radar') deviceConfig.mode = 'stocks';
delete deviceConfig.radar;
delete deviceConfig.carouselRadar;

function list(value) {
  return String(value || '').split(',').map(x => x.trim()).filter(Boolean);
}

const envOwners = list(process.env.GITHUB_OWNERS || 'bairachnyi,ananas-it');
const envGithubToken = process.env.GITHUB_TOKEN || '';
let bridgeConfig = {
  mode: process.env.GITHUB_MODE || 'mock',
  delivery: process.env.GITHUB_DELIVERY || 'polling',
  cacheSec: Math.max(60, Number(process.env.CACHE_SEC || 120)),
  accounts: envOwners.map(owner => ({ owner, token: envGithubToken })),
  repositories: list(process.env.GITHUB_REPOS),
  events: { actions: true, deployments: true, pullRequests: true, releases: true },
  webhook: { publicUrl: process.env.GITHUB_WEBHOOK_URL || '', secret: process.env.GITHUB_WEBHOOK_SECRET || '' },
};

try {
  const saved = JSON.parse(readFileSync(CONFIG_FILE, 'utf8'));
  if (saved && typeof saved === 'object') bridgeConfig = normalizeConfig(saved, bridgeConfig);
} catch {}

let scenario = 'mixed';
let cache = { at: 0, data: null };
let livePromise = null;
let githubBlockedUntil = 0;
let githubRate = { limit: 0, remaining: 0, resetAt: 0, resource: 'core' };

const scenarios = {
  idle: [
    item('bairachnyi/smalltv-ultra', 'action', 'Build firmware', 'main', 'completed', 'success', 180),
    item('ananas-it/web', 'deployment', 'production', 'main', 'completed', 'success', 620),
    item('ananas-it/api', 'release', 'Release v2.4.1', 'main', 'completed', 'success', 980),
  ],
  deploying: [
    item('ananas-it/web', 'deployment', 'production', 'main', 'in_progress', '', 44),
    item('ananas-it/api', 'action', 'Tests', 'feature/auth', 'queued', '', 12),
    item('bairachnyi/smalltv-ultra', 'action', 'Build firmware', 'main', 'completed', 'success', 210),
    item('ananas-it/mobile', 'pull_request', 'PR #82 checks', 'feature/login', 'in_progress', '', 31),
  ],
  failure: [
    item('ananas-it/api', 'deployment', 'production', 'main', 'completed', 'failure', 75),
    item('ananas-it/web', 'pull_request', 'PR #41 checks', 'release', 'completed', 'failure', 132),
    item('bairachnyi/smalltv-ultra', 'action', 'Build firmware', 'main', 'completed', 'success', 420),
  ],
  mixed: [
    item('ananas-it/customer-portal-production', 'deployment', 'Deploy production with database migrations and cache warmup', 'feature/github-deployment-dashboard', 'in_progress', '', 51),
    item('ananas-it/api', 'action', 'Tests', 'feature/auth', 'completed', 'failure', 95),
    item('bairachnyi/smalltv-ultra', 'action', 'Build firmware', 'main', 'completed', 'success', 190),
    item('ananas-it/mobile', 'pull_request', 'PR #82 checks', 'feature/login', 'queued', '', 18),
    item('ananas-it/infrastructure', 'deployment', 'staging', 'main', 'completed', 'success', 720),
    item('ananas-it/web', 'release', 'Release v3.8.0', 'main', 'completed', 'success', 1400),
  ],
};

const errorScenarios = {
  token_error: { code: 'TOKEN_DENIED', message: 'The token lacks permission or organization approval.', repo: 'ananas-it/api', source: 'github' },
  repo_error: { code: 'REPOSITORY_NOT_FOUND', message: 'Repository not found or the token cannot access it.', repo: 'ananas-it/private-app', source: 'github' },
  bridge_offline: { code: 'BRIDGE_OFFLINE', message: 'Cannot connect to the GitHub bridge.', repo: '', source: 'network' },
  stale_data: { code: 'DATA_STALE', message: 'Last successful data is too old.', repo: '', source: 'bridge' },
};

function item(repo, type, workflow, branch, status, conclusion, occurredAt, startedAt = occurredAt) {
  const parsed = typeof occurredAt === 'number' ? Date.now() - occurredAt * 1000 : Date.parse(occurredAt || '');
  const started = typeof startedAt === 'number' ? Date.now() - startedAt * 1000 : Date.parse(startedAt || '');
  const eventMs = Number.isFinite(parsed) ? parsed : Date.now();
  const startedMs = Number.isFinite(started) ? started : eventMs;
  const event = new Date(eventMs);
  const months = ['JAN', 'FEB', 'MAR', 'APR', 'MAY', 'JUN', 'JUL', 'AUG', 'SEP', 'OCT', 'NOV', 'DEC'];
  const pad = value => String(value).padStart(2, '0');
  const when = `${pad(event.getDate())} ${months[event.getMonth()]} ${pad(event.getHours())}:${pad(event.getMinutes())}`;
  return {
    repo, type, workflow, branch, status, conclusion,
    age: Math.max(0, Math.round((Date.now() - startedMs) / 1000)),
    at: Math.floor(startedMs / 1000),
    updatedAt: Math.floor(eventMs / 1000),
    when,
  };
}

function response(runs, message = '') {
  const latestAt = Math.max(0, ...runs.map(run => Number(run.updatedAt || run.at || 0)));
  return {
    ok: true,
    version: '0.5.0',
    updatedAt: new Date().toISOString(),
    message,
    items: runs.map(run => ({ ...run, latest: Number(run.updatedAt || run.at || 0) === latestAt })),
  };
}

function refreshAges(data) {
  if (!Array.isArray(data.items)) return data;
  const now = Math.floor(Date.now() / 1000);
  return { ...data, updatedAt: new Date().toISOString(), items: data.items.map(event => ({ ...event, age: event.at ? Math.max(0, now - event.at) : event.age })) };
}

function mockItems() {
  const key = { action: 'actions', deployment: 'deployments', pull_request: 'pullRequests', release: 'releases' };
  return scenarios[scenario].filter(event => bridgeConfig.events[key[event.type]] !== false);
}

function mockResponse() {
  if (errorScenarios[scenario]) return { ok: false, version: '0.5.0', updatedAt: new Date().toISOString(), items: [], error: errorScenarios[scenario] };
  return response(mockItems(), `Mock scenario: ${scenario}`);
}

function cleanName(value) {
  return String(value || '').trim().replace(/[^a-zA-Z0-9_.-]/g, '').slice(0, 64);
}

function cleanRepo(value) {
  const repo = String(value || '').trim().replace(/[^a-zA-Z0-9_.\/-]/g, '').slice(0, 130);
  return /^[^/]+\/[^/]+$/.test(repo) ? repo : '';
}

class BridgeError extends Error {
  constructor(code, message, status = 400, details = {}) {
    super(message); this.code = code; this.status = status; Object.assign(this, details);
  }
  json() {
    return {
      ok: false,
      error: {
        code: this.code,
        message: this.message,
        field: this.field || '',
        repo: this.repo || '',
        source: this.source || '',
        retryAt: this.retryAt || 0,
        remaining: this.remaining ?? null,
        limit: this.limit ?? null,
      },
    };
  }
}

function invalid(code, message, field) {
  throw new BridgeError(code, message, 400, { field });
}

function normalizeConfig(input, previous = bridgeConfig, strict = false) {
  if (!input || typeof input !== 'object' || Array.isArray(input)) invalid('INVALID_CONFIG', 'Settings must be a JSON object.');
  if (strict && !['mock', 'live'].includes(input.mode)) invalid('INVALID_MODE', 'Choose Mock data or Live GitHub.', 'mode');
  if (strict && !Array.isArray(input.accounts)) invalid('ACCOUNTS_REQUIRED', 'Add at least one GitHub account owner.', 'accounts');
  if (strict && (input.accounts.length < 1 || input.accounts.length > 6)) invalid('ACCOUNTS_LIMIT', 'Add between 1 and 6 GitHub account owners.', 'accounts');
  const oldTokens = new Map((previous.accounts || []).map(a => [a.owner.toLowerCase(), a.token || '']));
  const seenOwners = new Set();
  const accounts = Array.isArray(input.accounts) ? input.accounts.slice(0, 6).map((item, index) => {
    const rawOwner = String(item?.owner || '').trim();
    const owner = cleanName(item?.owner);
    if (strict && (!owner || owner !== rawOwner)) invalid('INVALID_OWNER', `Account row ${index + 1}: use only the GitHub login, for example ananas-it.`, `accounts.${index}.owner`);
    if (strict && seenOwners.has(owner.toLowerCase())) invalid('DUPLICATE_OWNER', `Account ${owner} is listed more than once.`, `accounts.${index}.owner`);
    seenOwners.add(owner.toLowerCase());
    const old = oldTokens.get(owner.toLowerCase()) || '';
    const enteredToken = String(item.token || '').trim();
    if (strict && enteredToken && !enteredToken.startsWith('github_pat_')) invalid('INVALID_TOKEN', `Token for ${owner} must be a fine-grained token starting with github_pat_.`, `accounts.${index}.token`);
    const token = item.clearToken ? '' : (enteredToken || old);
    return owner ? { owner, token } : null;
  }).filter(Boolean) : previous.accounts;
  if (strict && !Array.isArray(input.repositories)) invalid('REPOSITORIES_REQUIRED', 'Repository allowlist must be a list.', 'repositories');
  if (strict && input.repositories.length > 50) invalid('REPOSITORIES_LIMIT', 'No more than 50 repositories are supported.', 'repositories');
  const rawRepositories = Array.isArray(input.repositories) ? input.repositories : previous.repositories;
  const repositories = Array.isArray(rawRepositories)
    ? [...new Set(rawRepositories.map((value, index) => {
      const repo = cleanRepo(value);
      if (strict && !repo) invalid('INVALID_REPOSITORY', `Repository row ${index + 1}: use owner/repository, for example ananas-it/web.`, 'repositories');
      return repo;
    }).filter(Boolean))].slice(0, 50)
    : previous.repositories;
  if (strict && (!input.events || typeof input.events !== 'object')) invalid('EVENTS_REQUIRED', 'Choose at least one event type.', 'events');
  const events = {
    actions: input.events?.actions !== false,
    deployments: input.events?.deployments !== false,
    pullRequests: input.events?.pullRequests !== false,
    releases: input.events?.releases !== false,
  };
  if (strict && !Object.values(events).some(Boolean)) invalid('NO_EVENTS', 'Enable at least one event type.', 'events');
  const cacheSec = strict ? Number(input.cacheSec) : Number(input.cacheSec || previous.cacheSec || 120);
  if (strict && (!Number.isFinite(cacheSec) || cacheSec < 60 || cacheSec > 3600)) invalid('INVALID_CACHE', 'Cache must be between 60 and 3600 seconds.', 'cacheSec');
  const delivery = ['polling', 'webhook'].includes(input.delivery) ? input.delivery : (previous.delivery || 'polling');
  const enteredWebhookSecret = String(input.webhook?.secret || '').trim();
  const webhookSecret = input.webhook?.clearSecret ? '' : (enteredWebhookSecret || previous.webhook?.secret || '');
  const webhookPublicUrl = String(input.webhook?.publicUrl ?? previous.webhook?.publicUrl ?? '').trim();
  if (strict && enteredWebhookSecret && enteredWebhookSecret.length < 20) invalid('WEBHOOK_SECRET_WEAK', 'Webhook secret must contain at least 20 characters.', 'webhook.secret');
  if (strict && webhookPublicUrl && !/^https:\/\/[^/\s]+/i.test(webhookPublicUrl)) invalid('WEBHOOK_URL_INVALID', 'Webhook delivery URL must be a public https:// address.', 'webhook.publicUrl');
  if (strict && delivery === 'webhook' && !webhookSecret) invalid('WEBHOOK_SECRET_REQUIRED', 'Generate and save a webhook secret before enabling webhook delivery.', 'webhook.secret');
  return {
    mode: input.mode === 'live' ? 'live' : input.mode === 'mock' ? 'mock' : previous.mode,
    delivery,
    cacheSec: Math.max(60, Math.min(3600, cacheSec)),
    accounts,
    repositories,
    events,
    webhook: { publicUrl: webhookPublicUrl, secret: webhookSecret },
  };
}

function publicConfig() {
  const estimate = estimateGithubLoad();
  return {
    ok: true,
    mode: bridgeConfig.mode,
    delivery: bridgeConfig.delivery || 'polling',
    cacheSec: bridgeConfig.cacheSec,
    accounts: bridgeConfig.accounts.map(a => ({ owner: a.owner, tokenSet: Boolean(a.token) })),
    repositories: bridgeConfig.repositories,
    events: bridgeConfig.events,
    polling: estimate,
    rateLimit: {
      ...githubRate,
      blockedUntil: githubBlockedUntil || 0,
      blocked: githubBlockedUntil > Date.now(),
    },
    webhook: {
      publicUrl: bridgeConfig.webhook?.publicUrl || '',
      endpoint: '/api/github/webhook',
      ...githubWebhooks.status(Boolean(bridgeConfig.webhook?.secret)),
    },
  };
}

function publicDeviceConfig() {
  const copy = structuredClone(deviceConfig);
  copy.features = { ticker: true, usage: true, github: true, clock: true, gallery: true };
  if (copy.mode === 'radar') copy.mode = 'stocks';
  delete copy.radar;
  delete copy.carouselRadar;
  copy.chip = 'esp8266';
  copy.wifi = (copy.wifi || []).map(row => ({ ssid: row.ssid || '', passSet: Boolean(row.pass || row.passSet) }));
  copy.apPassSet = Boolean(copy.apPass || copy.apPassSet);
  copy.apPass = '';
  copy.adminPass = copy.adminPass || '1111';
  copy.github = copy.github || {};
  copy.github.tokenSet = Boolean(copy.github.accessToken || copy.github.tokenSet);
  delete copy.github.accessToken;
  // Ensure clock color/font fields exist with defaults
  copy.clock = copy.clock || {};
  if (copy.clock.timeColor == null) copy.clock.timeColor = '39E7';
  if (copy.clock.dateColor == null) copy.clock.dateColor = 'FFB6';
  if (copy.clock.accentColor == null) copy.clock.accentColor = '58A9';
  if (copy.clock.bgColor == null) copy.clock.bgColor = '0000';
  if (copy.clock.fontScale == null) copy.clock.fontScale = 0;
  if (copy.clock.boldText == null) copy.clock.boldText = false;
  return copy;
}

function saveDeviceConfig(next) {
  const oldMode = deviceConfig.mode;
  const oldGithub = deviceConfig.github || {};
  const update = structuredClone(next || {});
  if (update.mode === 'radar') update.mode = 'stocks';
  delete update.radar;
  delete update.carouselRadar;
  if (update.apPass === '') delete update.apPass;
  if (update.github?.accessToken === '') delete update.github.accessToken;
  if (Array.isArray(update.wifi)) {
    update.wifi = update.wifi.map(row => {
      if (row.pass) return row;
      const saved = (deviceConfig.wifi || []).find(item => item.ssid === row.ssid);
      return saved ? { ...row, ...(saved.pass ? { pass: saved.pass } : {}), passSet: Boolean(saved.pass || saved.passSet) } : row;
    });
  }
  deviceConfig = mergeConfig(deviceConfig, update);
  writeFileSync(DEVICE_CONFIG_FILE, `${JSON.stringify(deviceConfig, null, 2)}\n`, { mode: 0o600 });
  record('info', 'device.config.saved', {
    mode: deviceConfig.mode,
    message: `mode=${deviceConfig.mode}; feed=${deviceConfig.github?.statusUrl || '(empty)'}`,
    changed: { mode: oldMode !== deviceConfig.mode, github: JSON.stringify(oldGithub) !== JSON.stringify(deviceConfig.github || {}) },
  });
}

function saveConfig(next) {
  const credentialsChanged = Array.isArray(next?.accounts)
    && next.accounts.some(account => String(account?.token || '').trim() || account?.clearToken);
  bridgeConfig = normalizeConfig(next, bridgeConfig, true);
  writeFileSync(CONFIG_FILE, `${JSON.stringify(bridgeConfig, null, 2)}\n`, { mode: 0o600 });
  cache = { at: 0, data: null };
  livePromise = null;
  if (credentialsChanged) {
    githubBlockedUntil = 0;
    githubRate = { limit: 0, remaining: 0, resetAt: 0, resource: 'core' };
  }
  record('info', 'bridge.config.saved', { mode: bridgeConfig.mode, message: `${bridgeConfig.mode}; ${bridgeConfig.accounts.length} owners; ${bridgeConfig.repositories.length || 'auto'} repositories` });
}

async function gh(path, token = '') {
  if (githubBlockedUntil > Date.now()) {
    throw new BridgeError('RATE_LIMITED', `GitHub polling is paused until ${new Date(githubBlockedUntil).toLocaleTimeString()}.`, 429, {
      retryAt: githubBlockedUntil,
      remaining: githubRate.remaining,
      limit: githubRate.limit,
    });
  }
  const headers = { Accept: 'application/vnd.github+json', 'User-Agent': 'smalltv-github-bridge', 'X-GitHub-Api-Version': '2022-11-28' };
  if (token) headers.Authorization = `Bearer ${token}`;
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 12_000);
  let r;
  try { r = await fetch(`https://api.github.com${path}`, { headers, signal: controller.signal }); }
  catch (e) {
    const timeout = e.name === 'AbortError';
    throw new BridgeError(timeout ? 'GITHUB_TIMEOUT' : 'GITHUB_OFFLINE', timeout ? 'GitHub did not respond within 12 seconds.' : 'Cannot connect to GitHub.', 502);
  } finally { clearTimeout(timer); }
  const limit = Number(r.headers.get('x-ratelimit-limit') || 0);
  const remaining = Number(r.headers.get('x-ratelimit-remaining') || 0);
  const resetAt = Number(r.headers.get('x-ratelimit-reset') || 0) * 1000;
  githubRate = { limit, remaining, resetAt, resource: r.headers.get('x-ratelimit-resource') || 'core' };
  if (!r.ok) {
    let body = {};
    try { body = await r.json(); } catch {}
    let code = `GITHUB_HTTP_${r.status}`;
    let message = `GitHub returned HTTP ${r.status}.`;
    if (r.status === 401) { code = 'TOKEN_INVALID'; message = 'GitHub rejected the token. Generate a new fine-grained token.'; }
    if (r.status === 403 || r.status === 429) {
      const retryAfter = Math.max(0, Number(r.headers.get('retry-after') || 0)) * 1000;
      const secondary = /secondary rate limit|abuse/i.test(String(body.message || ''));
      const rateLimited = remaining === 0 || r.status === 429 || secondary || retryAfter > 0;
      code = rateLimited ? 'RATE_LIMITED' : 'TOKEN_DENIED';
      if (rateLimited) {
        githubBlockedUntil = Math.max(
          Date.now() + 60_000,
          resetAt || 0,
          Date.now() + retryAfter,
        );
        message = `GitHub API rate limit was reached. Automatic retry after ${new Date(githubBlockedUntil).toLocaleTimeString()}.`;
        record('warn', 'github.rate_limit.paused', {
          code,
          message,
          remaining,
          limit,
          retryAt: githubBlockedUntil,
        });
      } else {
        message = 'The token lacks permission or organization approval.';
      }
    }
    if (r.status === 404) { code = 'REPOSITORY_NOT_FOUND'; message = 'Repository not found or the token cannot access it.'; }
    throw new BridgeError(code, message, code === 'RATE_LIMITED' ? 429 : (r.status >= 500 ? 502 : 400), {
      retryAt: code === 'RATE_LIMITED' ? githubBlockedUntil : 0,
      remaining,
      limit,
    });
  }
  try { return await r.json(); }
  catch { throw new BridgeError('GITHUB_BAD_RESPONSE', 'GitHub returned invalid JSON.', 502); }
}

function tokenForRepo(repo) {
  const owner = repo.split('/')[0].toLowerCase();
  return bridgeConfig.accounts.find(a => a.owner.toLowerCase() === owner)?.token
    || bridgeConfig.accounts.find(a => a.token)?.token
    || '';
}

async function discoverRepos() {
  if (bridgeConfig.repositories.length) return bridgeConfig.repositories;
  const found = [];
  for (const account of bridgeConfig.accounts) {
    let repos;
    const token = account.token || bridgeConfig.accounts.find(a => a.token)?.token || '';
    if (account.token) {
      repos = await gh('/user/repos?affiliation=owner,organization_member&sort=pushed&per_page=100', token);
      repos = repos.filter(r => r.owner.login.toLowerCase() === account.owner.toLowerCase());
    } else {
      try { repos = await gh(`/orgs/${account.owner}/repos?type=all&sort=pushed&per_page=100`, token); }
      catch { repos = await gh(`/users/${account.owner}/repos?sort=pushed&per_page=100`, token); }
    }
    found.push(...repos.filter(r => !r.archived).map(r => r.full_name));
  }
  return [...new Set(found)].slice(0, 12);
}

function estimateGithubLoad() {
  if (bridgeConfig.delivery === 'webhook') {
    return { repoCount: 0, requestsPerRefresh: 0, recommendedCacheSec: 0, effectiveCacheSec: 0 };
  }
  const repoCount = bridgeConfig.repositories.length || 12;
  const perRepo =
    (bridgeConfig.events.actions ? 1 : 0)
    + (bridgeConfig.events.deployments ? 2 : 0)
    + (bridgeConfig.events.pullRequests ? 3 : 0)
    + (bridgeConfig.events.releases ? 1 : 0);
  const requestsPerRefresh = (bridgeConfig.repositories.length ? 0 : bridgeConfig.accounts.length) + repoCount * perRepo;
  const recommendedCacheSec = Math.max(60, Math.ceil(requestsPerRefresh * 3600 / 4000));
  return { repoCount, requestsPerRefresh, recommendedCacheSec, effectiveCacheSec: Math.max(bridgeConfig.cacheSec, recommendedCacheSec) };
}

function webhookData() {
  if (!bridgeConfig.webhook?.secret) {
    return {
      ok: false,
      version: '0.5.0',
      message: 'Generate and save a webhook secret.',
      items: [],
      error: {
        code: 'WEBHOOK_SECRET_MISSING',
        message: 'Generate a webhook secret in GitHub settings.',
        repo: '',
        source: 'webhook',
      },
      warnings: [],
    };
  }
  const stored = githubWebhooks.events({
    repositories: bridgeConfig.repositories,
    owners: bridgeConfig.accounts.map(account => account.owner),
    enabled: bridgeConfig.events,
    limit: 16,
  });
  const items = stored.map(event => item(
    event.repo,
    event.type,
    event.workflow,
    event.branch,
    event.status,
    event.conclusion,
    event.occurredAt,
    event.startedAt,
  ));
  const status = githubWebhooks.status(Boolean(bridgeConfig.webhook?.secret));
  const data = response(items, items.length
    ? `Webhook live; ${status.tracked} events tracked`
    : 'Webhook ready; waiting for the first GitHub event');
  data.delivery = 'webhook';
  data.webhook = status;
  return data;
}

async function refreshLiveData() {
  const effectiveCacheSec = estimateGithubLoad().effectiveCacheSec;
  if (Date.now() - cache.at < effectiveCacheSec * 1000 && cache.data) return cache.data;
  const repos = await discoverRepos();
  if (!repos.length) {
    return { ok: false, version: '0.5.0', message: 'No repositories were found. Add owner/repository entries or check token access.', items: [], error: { code: 'NO_REPOSITORIES', message: 'Add at least one accessible repository.', repo: '', source: 'config' }, warnings: [] };
  }
  const batches = [];
  const warnings = [];
  for (const repo of repos) {
    if (githubBlockedUntil > Date.now()) break;
    batches.push(...await collectRepo(repo, warnings));
  }
  const priority = r => ['in_progress', 'queued', 'waiting', 'pending'].includes(r.status) ? 0 : r.conclusion === 'failure' ? 1 : 2;
  batches.sort((a, b) => priority(a) - priority(b) || a.age - b.age);
  const data = response(batches.slice(0, 8), warnings.length
    ? `Watching ${repos.length}; ${warnings.length} source errors`
    : `Watching ${repos.length} repositories`);
  data.warnings = warnings.slice(0, 8);
  if (warnings.length) data.error = warnings[0];
  if (warnings.length && !batches.length) { data.ok = false; data.error = warnings[0]; }
  cache = { at: Date.now(), data };
  return cache.data;
}

async function liveData() {
  const effectiveCacheSec = estimateGithubLoad().effectiveCacheSec;
  if (Date.now() - cache.at < effectiveCacheSec * 1000 && cache.data) return cache.data;
  if (githubBlockedUntil > Date.now() && cache.data) return cache.data;
  if (githubBlockedUntil > Date.now()) {
    return {
      ok: false,
      version: '0.5.0',
      message: `GitHub polling is paused until ${new Date(githubBlockedUntil).toLocaleTimeString()}.`,
      items: [],
      error: {
        code: 'RATE_LIMITED',
        message: `Automatic retry after ${new Date(githubBlockedUntil).toLocaleTimeString()}.`,
        repo: '',
        source: 'github',
        retryAt: githubBlockedUntil,
        remaining: githubRate.remaining,
        limit: githubRate.limit,
      },
      warnings: [],
    };
  }
  if (livePromise) return livePromise;
  livePromise = refreshLiveData().catch(error => {
    if (cache.data) {
      cache.data.warnings = [{ code: error.code || 'GITHUB_ERROR', message: error.message, source: 'github' }];
      return cache.data;
    }
    if (error instanceof BridgeError && error.code === 'RATE_LIMITED') {
      return {
        ok: false,
        version: '0.5.0',
        message: error.message,
        items: [],
        error: {
          code: error.code,
          message: error.message,
          repo: '',
          source: 'github',
          retryAt: error.retryAt || githubBlockedUntil,
          remaining: error.remaining ?? githubRate.remaining,
          limit: error.limit ?? githubRate.limit,
        },
        warnings: [],
      };
    }
    throw error;
  }).finally(() => { livePromise = null; });
  return livePromise;
}

function completedState(state) {
  return ['success', 'failure', 'error', 'inactive', 'cancelled'].includes(state) ? 'completed' : state;
}

function conclusionFor(state) {
  if (state === 'error') return 'failure';
  if (state === 'inactive') return 'cancelled';
  return state;
}

async function collectRepo(repo, warnings) {
  const token = tokenForRepo(repo);
  const tasks = [];
  if (bridgeConfig.events.actions) tasks.push(async () => {
    const d = await gh(`/repos/${repo}/actions/runs?per_page=1`, token);
    const w = d.workflow_runs?.[0];
    const happenedAt = w?.status === 'completed' ? (w.updated_at || w.run_started_at || w.created_at) : (w?.run_started_at || w?.created_at);
    return w ? [item(repo, 'action', w.name, w.head_branch || '', w.status, w.conclusion || '', happenedAt, w.run_started_at || w.created_at)] : [];
  });
  if (bridgeConfig.events.deployments) tasks.push(async () => {
    const deployments = await gh(`/repos/${repo}/deployments?per_page=1`, token);
    const deployment = deployments?.[0];
    if (!deployment) return [];
    const statuses = await gh(`/repos/${repo}/deployments/${deployment.id}/statuses?per_page=1`, token);
    const latest = statuses?.[0];
    const state = latest?.state || 'pending';
    return [item(repo, 'deployment', latest?.environment || deployment.environment || 'deployment', deployment.ref || '', completedState(state), conclusionFor(state), latest?.updated_at || deployment.updated_at || deployment.created_at, deployment.created_at)];
  });
  if (bridgeConfig.events.pullRequests) tasks.push(async () => {
    const pulls = await gh(`/repos/${repo}/pulls?state=open&sort=updated&direction=desc&per_page=2`, token);
    const out = [];
    for (const pr of pulls.slice(0, 2)) {
      const checks = await gh(`/repos/${repo}/commits/${pr.head.sha}/check-runs?filter=latest&per_page=100`, token);
      const runs = checks.check_runs || [];
      const active = runs.some(c => c.status !== 'completed');
      const failed = runs.some(c => ['failure', 'timed_out', 'action_required', 'startup_failure'].includes(c.conclusion));
      const status = active || !runs.length ? (active ? 'in_progress' : 'pending') : 'completed';
      const conclusion = failed ? 'failure' : status === 'completed' ? 'success' : '';
      const updatedAt = runs.map(c => c.completed_at || c.started_at).filter(Boolean).sort().at(-1) || pr.updated_at;
      const startedAt = runs.filter(c => c.status !== 'completed').map(c => c.started_at).filter(Boolean).sort()[0] || pr.updated_at;
      out.push(item(repo, 'pull_request', `PR #${pr.number} checks`, pr.head.ref || '', status, conclusion, updatedAt, startedAt));
    }
    return out;
  });
  if (bridgeConfig.events.releases) tasks.push(async () => {
    try {
      const release = await gh(`/repos/${repo}/releases/latest`, token);
      return [item(repo, 'release', `Release ${release.tag_name}`, release.target_commitish || '', 'completed', 'success', release.published_at || release.created_at)];
    } catch (e) {
      if (e instanceof BridgeError && e.code === 'REPOSITORY_NOT_FOUND') return [];
      throw e;
    }
  });
  const out = [];
  for (const task of tasks) {
    try {
      out.push(...await task());
    } catch (error) {
      const reason = error instanceof BridgeError ? error : new BridgeError('SOURCE_FAILED', error.message || 'GitHub source failed.', 502);
      const warning = { code: reason.code, message: reason.message, repo, source: 'github' };
      warnings.push(warning); record('warn', 'github.source.failed', { code: reason.code, message: reason.message, repo, source: 'github' });
      if (reason.code === 'RATE_LIMITED') break;
    }
  }
  return out;
}

function screenHtml() {
  const scenarioButtons = [...Object.keys(scenarios), ...Object.keys(errorScenarios)].map(s => `<button onclick="pick('${s}')">${s}</button>`).join('');
  return `<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>SmallTV GitHub emulator</title>
<style>*{box-sizing:border-box}body{margin:0;background:#0e1117;color:#e6edf3;font:14px system-ui;min-height:100vh;padding:40px}.wrap{display:flex;gap:32px;align-items:flex-start;flex-wrap:wrap;justify-content:center}.device{padding:22px;background:#272b31;border-radius:22px;box-shadow:0 16px 50px #0008;position:sticky;top:30px}.screen{width:240px;height:240px;background:#050a12;padding:6px 4px;overflow:hidden;font-family:ui-monospace,SFMono-Regular,Menlo,monospace}.opshead{height:25px;border-bottom:1px solid #193047;display:flex;align-items:flex-start;padding:1px 3px;font-size:17px;font-weight:900;position:relative}.opshead:after{content:'';position:absolute;bottom:-1px;left:0;width:54px;height:1px;background:#39e7ff}.slash{color:#39e7ff}.live{margin-left:auto;color:#718096;font-size:8px;padding-top:5px}.live:before{content:'';display:inline-block;width:6px;height:6px;border-radius:50%;background:#39e7ff;box-shadow:0 0 7px #39e7ff;margin-right:5px}.page{color:#718096;font-size:8px;padding:5px 0 0 9px}.sum{height:25px;display:flex;align-items:center;justify-content:space-around;color:#718096;font-size:9px}.metric i{display:inline-block;width:6px;height:6px;border-radius:50%;margin-right:5px}.metric b{color:#f2f6fa;margin-left:3px}.list{display:grid;gap:4px}.item,.panel,.error-card{background:#0d1828;border:1px solid #193047;border-radius:6px}.item{height:55px;padding:4px 7px 3px 29px;position:relative;border-left:3px solid var(--state)}.status-icon{position:absolute;left:7px;top:8px;width:18px;height:18px;border-radius:50%}.status-icon.run{border:2px solid #345065;border-top-color:#39e7ff;animation:spin .75s linear infinite}.status-icon.wait{border:2px dotted #ffb627;animation:spin 1.25s linear infinite}.status-icon.ok{border:2px solid #59ef9a;animation:successGlow 1.6s ease-in-out infinite}.status-icon.ok:after{content:'';position:absolute;width:8px;height:4px;border-left:2px solid #59ef9a;border-bottom:2px solid #59ef9a;transform:rotate(-45deg);left:3px;top:4px}.status-icon.fail{background:#ff5d68;color:#050a12;text-align:center;font:bold 14px/18px system-ui;animation:failPulse 1.1s ease-out infinite}.repo{font-weight:800;font-size:13px;max-width:145px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.detail{color:#718096;font-size:8px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;margin-top:2px}.state{position:absolute;right:7px;top:6px;color:var(--state);font-size:9px;font-weight:900}.meta{position:absolute;left:7px;right:7px;bottom:4px;height:11px;font-size:8px;color:#718096}.tag{display:inline-block;background:#193047;color:var(--state);padding:1px 3px;border-radius:3px;margin-right:5px}.branch{display:inline-block;max-width:92px;overflow:hidden;white-space:nowrap;vertical-align:bottom;text-overflow:ellipsis}.when{float:right;color:#aab5c2}.error-card{height:169px;margin:18px 6px;padding:22px 10px;text-align:center}.error-icon{width:20px;height:20px;border-radius:50%;background:#ff5d68;box-shadow:0 0 0 5px #ff5d6833;margin:auto;animation:failPulse 1.1s ease-out infinite}.error-title{font-size:17px;font-weight:800;margin:12px 0}.error-message,.error-repo{font-size:9px;color:#718096;margin:6px}.error-repo{color:#e6edf3}.error-action{color:#39e7ff;margin-top:20px;font-size:9px}@keyframes spin{to{transform:rotate(360deg)}}@keyframes successGlow{50%{box-shadow:0 0 9px #59ef9a;transform:scale(1.08)}}@keyframes failPulse{70%{box-shadow:0 0 0 7px #ff5d6800}}.controls{width:420px}.panel{padding:16px;margin:12px 0}.controls button{padding:9px 12px;margin:0 5px 7px 0;background:#21262d;color:#fff;border:1px solid #30363d;border-radius:7px;cursor:pointer}.controls button.primary{background:#238636;border-color:#2ea043}.controls button:hover{border-color:#58a6ff}label{display:block;color:#8b949e;font-size:12px;margin:9px 0 4px}input,select,textarea{width:100%;padding:9px;background:#0d1117;color:#e6edf3;border:1px solid #30363d;border-radius:6px}input[type=checkbox]{width:auto;margin-right:7px}textarea{min-height:65px;resize:vertical}.account{display:grid;grid-template-columns:1fr 1.4fr 34px;gap:6px;margin:6px 0}.account button{margin:0;padding:5px}.hint{color:#8b949e;font-size:12px}code,a{color:#79c0ff}</style></head>
<body><div class="wrap"><div class="device"><div id="deviceScreen" class="screen"></div></div><div class="controls"><h1>GitHub dashboard emulator <small class="hint">v0.5.0</small></h1><p>240×240 preview for SmallTV Ultra.</p><div>${scenarioButtons}</div><div class="panel"><h2>GitHub bridge settings</h2><p class="hint">Create a <a href="https://github.com/settings/personal-access-tokens/new" target="_blank">fine-grained token</a>, choose the resource owner and selected repositories, then grant read-only Actions, Deployments, Pull requests, Checks and Contents. Use one approved token per private owner; it must begin with <code>github_pat_</code>.</p><label>Mode</label><select id="bridgeMode"><option value="mock">Mock data</option><option value="live">Live GitHub</option></select><label>Delivery</label><select id="delivery"><option value="webhook">GitHub webhooks — near real time</option><option value="polling">REST polling</option></select><label>Public webhook URL</label><input id="webhookUrl" type="url" placeholder="https://example.com/api/github/webhook"><label>Webhook secret</label><div class="account"><input id="webhookSecret" type="password" placeholder="blank keeps saved secret"><button onclick="generateWebhookSecret()">Generate</button></div><p class="hint">The public HTTPS endpoint must forward the unchanged body and GitHub headers here. Webhook mode does not poll GitHub REST for normal updates.</p><label>Events</label><div><label><input id="evActions" type="checkbox"> Actions</label><label><input id="evDeployments" type="checkbox"> Deployments / environments</label><label><input id="evPullRequests" type="checkbox"> Pull request checks</label><label><input id="evReleases" type="checkbox"> Releases</label></div><label>Accounts and access tokens</label><div id="accounts"></div><button onclick="addAccount()">+ account</button><p class="hint">Blank keeps the saved token. A different saved token can authenticate public organization requests, but private organization repositories require a token for that resource owner and may require administrator approval.</p><label>Repository allowlist (one owner/repo per line; blank = all webhook repositories for listed owners)</label><textarea id="repositories" placeholder="bairachnyi/smalltv-ultra"></textarea><p class="hint">In webhook mode a blank list accepts every installed repository belonging to the configured owners.</p><label>GitHub refresh / bridge cache (seconds)</label><input id="cacheSec" type="number" min="60" max="3600"><p class="hint">Used only by REST polling. The display may always read this local bridge every 10 seconds.</p><p id="rateState" class="hint"></p><button class="primary" onclick="saveBridge()">Save bridge settings</button><span id="saveState" class="hint"></span></div><p>Device feed: <code id="feedUrl"></code></p></div></div>
<script>const el=id=>document.getElementById(id);const active=s=>['in_progress','queued','waiting','pending'].includes(s);const stateInfo=s=>s==='success'?{color:'#59ef9a',label:'PASS',icon:'ok'}:s==='failure'?{color:'#ff5d68',label:'FAIL',icon:'fail'}:s==='in_progress'?{color:'#39e7ff',label:'RUN',icon:'run'}:['queued','waiting','pending'].includes(s)?{color:'#ffb627',label:'WAIT',icon:'wait'}:{color:'#718096',label:'STOP',icon:'stop'};
function esc(s){return String(s||'').replace(/[&<>\"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;'}[c]))}function addAccount(a={}){let row=document.createElement('div');row.className='account';row.innerHTML='<input class="owner" placeholder="owner" value="'+esc(a.owner)+'"><input class="token" type="password" placeholder="'+(a.tokenSet?'saved — blank keeps it':'github_pat_…')+'"><button onclick="this.parentElement.remove()">×</button>';el('accounts').appendChild(row)}function generateWebhookSecret(){let bytes=new Uint8Array(32);crypto.getRandomValues(bytes);el('webhookSecret').value=[...bytes].map(x=>x.toString(16).padStart(2,'0')).join('');el('webhookSecret').type='text';el('webhookSecret').select()}
async function loadConfig(){let c=await fetch('/api/config').then(r=>r.json()),e=c.events||{},p=c.polling||{},rl=c.rateLimit||{},wh=c.webhook||{};el('bridgeMode').value=c.mode;el('delivery').value=c.delivery||'polling';el('webhookUrl').value=wh.publicUrl||'';el('webhookSecret').value='';el('webhookSecret').placeholder=wh.secretSet?'saved — blank keeps it':'Generate a secret';el('cacheSec').value=c.cacheSec;el('repositories').value=(c.repositories||[]).join('\\n');el('evActions').checked=e.actions!==false;el('evDeployments').checked=e.deployments!==false;el('evPullRequests').checked=e.pullRequests!==false;el('evReleases').checked=e.releases!==false;el('accounts').innerHTML='';(c.accounts||[]).forEach(addAccount);if(!c.accounts?.length)addAccount();el('rateState').textContent=c.delivery==='webhook'?('Webhook: '+(wh.secretSet?'secured':'secret missing')+' · '+(wh.received||0)+' deliveries · '+(wh.tracked||0)+' tracked'):(p.effectiveCacheSec?'Effective GitHub refresh: '+p.effectiveCacheSec+'s · about '+p.requestsPerRefresh+' requests/cycle. ':'')+(rl.blocked&&rl.blockedUntil?'Paused until '+new Date(rl.blockedUntil).toLocaleTimeString()+'.':rl.limit?'Quota: '+rl.remaining+'/'+rl.limit+' remaining.':'')}
async function saveBridge(){let accounts=[...document.querySelectorAll('#accounts .account')].map(r=>({owner:r.querySelector('.owner').value.trim(),token:r.querySelector('.token').value.trim()})).filter(a=>a.owner);let body={mode:el('bridgeMode').value,delivery:el('delivery').value,webhook:{publicUrl:el('webhookUrl').value.trim(),secret:el('webhookSecret').value.trim()},cacheSec:Number(el('cacheSec').value),repositories:el('repositories').value.split(/[,\\n]/).map(x=>x.trim()).filter(Boolean),accounts,events:{actions:el('evActions').checked,deployments:el('evDeployments').checked,pullRequests:el('evPullRequests').checked,releases:el('evReleases').checked}};try{let response=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}),r=await response.json();if(!response.ok||!r.ok)throw Error(r.error?.message||'Could not save settings.');el('saveState').style.color='#3fb950';el('saveState').textContent='Saved';await loadConfig();await load()}catch(e){el('saveState').style.color='#f85149';el('saveState').textContent=e.message}}
const errorTitle=c=>({TOKEN_INVALID:'TOKEN DENIED',TOKEN_DENIED:'TOKEN DENIED',DEVICE_TOKEN_DENIED:'TOKEN DENIED',REPOSITORY_NOT_FOUND:'REPO NOT FOUND',NO_REPOSITORIES:'NO REPOS',RATE_LIMITED:'RATE LIMITED',WEBHOOK_SECRET_MISSING:'WEBHOOK NOT SET',WEBHOOK_SIGNATURE_INVALID:'BAD WEBHOOK',GITHUB_TIMEOUT:'GITHUB TIMEOUT',GITHUB_OFFLINE:'BRIDGE OFFLINE',BRIDGE_OFFLINE:'BRIDGE OFFLINE',BAD_RESPONSE:'BAD RESPONSE',GITHUB_BAD_RESPONSE:'BAD RESPONSE',LOW_MEMORY:'LOW MEMORY',DATA_STALE:'DATA STALE'}[c]||'GITHUB ERROR');
let latestData=null,page=0,latestFetchedAt=Date.now();const header=(page,pages)=>'<div class="opshead"><span>GH<span class="slash">//</span>OPS</span><span class="live">LIVE</span>'+(pages>1?'<span class="page">'+(page+1)+'/'+pages+'</span>':'')+'</div>';function elapsed(seconds){seconds=Math.max(0,Math.floor(seconds));return seconds<3600?String(Math.floor(seconds/60)).padStart(2,'0')+':'+String(seconds%60).padStart(2,'0'):Math.floor(seconds/3600)+'h'+String(Math.floor(seconds/60)%60).padStart(2,'0')}function updateTimers(){document.querySelectorAll('.active-timer').forEach(x=>x.textContent=elapsed(Number(x.dataset.age)+(Date.now()-latestFetchedAt)/1000))}function render(d){if(d.ok===false||d.error){let e=d.error||{};el('deviceScreen').innerHTML=header(0,1)+'<div class="error-card"><div class="error-icon"></div><div class="error-title">'+esc(errorTitle(e.code))+'</div>'+(e.repo?'<div class="error-repo">'+esc(e.repo)+'</div>':'')+'<div class="error-message">'+esc(e.message||'GitHub data unavailable.')+'</div><div class="error-action">[ open GitHub settings ]</div></div>';return}let runs=d.items||d.runs||[],cnt={run:0,ok:0,err:0},pages=Math.max(1,Math.ceil(runs.length/3));if(page>=pages)page=0;runs.forEach(r=>{if(active(r.status))cnt.run++;else if(r.conclusion==='success')cnt.ok++;else if(r.conclusion==='failure')cnt.err++});let h=header(page,pages)+'<div class="sum">'+[['RUN',cnt.run,'#39e7ff'],['PASS',cnt.ok,'#59ef9a'],['FAIL',cnt.err,'#ff5d68']].map(x=>'<span class="metric"><i style="background:'+x[2]+'"></i>'+x[0]+' <b>'+x[1]+'</b></span>').join('')+'</div><div class="list">',types={action:'ACT',deployment:'DEP',pull_request:'PR',release:'REL'};runs.slice(page*3,page*3+3).forEach(r=>{let st=r.status==='completed'?r.conclusion:r.status,info=stateInfo(st),name=r.repo.split('/').pop(),tp=types[r.type]||'ACT',isActive=active(st),right=isActive?elapsed(r.age):info.label;h+='<div class="item" style="--state:'+info.color+'"><span class="status-icon '+info.icon+'">'+(info.icon==='fail'?'!':'')+'</span><div class="repo">'+esc(name)+'</div><div class="detail">'+esc(r.workflow)+'</div><span class="state '+(isActive?'active-timer':'')+'" data-age="'+Number(r.age||0)+'">'+right+'</span><div class="meta"><span class="tag">'+tp+'</span><span class="branch">'+esc(r.branch)+'</span><span class="when">'+esc(r.when||'-- --- --:--')+'</span></div></div>'});el('deviceScreen').innerHTML=h+'</div>'}async function load(){latestData=await fetch('/api/github').then(r=>r.json());latestFetchedAt=Date.now();render(latestData)}async function pick(s){await fetch('/api/scenario',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({scenario:s})});page=0;load()}el('feedUrl').textContent=location.origin+'/api/github';loadConfig();load();setInterval(load,3000);setInterval(updateTimers,1000);setInterval(()=>{if(latestData&&(latestData.items||[]).length>3){page=(page+1)%Math.ceil(latestData.items.length/3);render(latestData)}},5000)</script></body></html>`;
}

function enhanceScreenHtml(html, embedded = false) {
  const extraCss = `<style>
.controls{width:520px}.logs{max-height:230px;overflow:auto;background:#080c12;border:1px solid #30363d;border-radius:6px;padding:8px;font:11px ui-monospace,monospace}.log{padding:5px;border-bottom:1px solid #21262d}.log-error{color:#ff7b72}.log-warn{color:#d29922}.log-info{color:#8b949e}.device-link{display:inline-block;padding:9px 12px;border:1px solid #2f81f7;border-radius:7px;text-decoration:none}
.live.focus{color:#ffb627}.live.focus:before{background:#ffb627;box-shadow:0 0 7px #ffb627}
.sum{height:25px;font-size:12px;font-weight:800}.list{gap:6px}.item{height:84px;min-width:0;padding:7px 7px 4px 38px;border-left-width:4px}.item.latest{border-color:#39e7ff;box-shadow:inset 0 0 0 1px #39e7ff66,0 0 8px #39e7ff22}.status-icon{left:8px;top:10px;width:22px;height:22px}.status-icon.ok{border:0;animation:none}.status-icon.ok:after{width:13px;height:7px;left:3px;top:3px;border-width:3px}.status-icon.fail{font-size:17px;line-height:22px;animation:none}.repo{max-width:120px;font-size:15px}.detail{max-width:214px;margin:9px 0 0 -26px;font-size:12px}.state{font-size:13px;top:8px}.meta{left:11px;right:7px;bottom:7px;height:16px;font-size:12px}.tag{padding:1px 4px;margin-right:8px}.branch{max-width:88px}.when{font-size:11px}
.marquee{overflow:hidden;text-overflow:clip}.detail.marquee{max-width:214px}.marquee-track{display:inline-block;white-space:nowrap}.marquee.scrolling .marquee-track{animation:marqueeLoop var(--marquee-duration,8s) linear infinite}.marquee-copy{margin-left:24px}@keyframes marqueeLoop{0%,15%{transform:translateX(0)}100%{transform:translateX(var(--marquee-shift))}}
${embedded ? 'body{min-height:240px;padding:0;overflow:hidden;background:#050a12}.wrap{display:block}.device{position:static;top:auto;width:240px;height:240px;padding:0;border-radius:0;box-shadow:none}.controls{display:none!important}' : ''}
</style>`;
  html = html.replace('</head>', `${extraCss}</head>`);
  html = html.replace('GH<span class="slash">//</span>OPS', 'GH<span class="slash">//</span>STAT');
  html = html.replace('const header=(page,pages)=>', 'const header=(page,pages,focus=false)=>');
  html = html.replace(
    '<span class="live">LIVE</span>',
    '<span class="live \'+(focus?\'focus\':\'\')+\'">\'+(focus?\'FOCUS\':\'LIVE\')+\'</span>',
  );
  html = html.replace("DATA_STALE:'DATA STALE'", "DATA_STALE:'DATA STALE',MODE_INACTIVE:'MODE INACTIVE',FEED_URL_INVALID:'FEED NOT SET',FEED_TIMEOUT:'FEED TIMEOUT',FEED_URL_LOOP:'FEED LOOP'");
  if (!embedded) {
    html = html.replace(
      '<p>240×240 preview for SmallTV Ultra.</p>',
      `<p>240×240 preview for SmallTV Ultra.</p><p><a class="device-link" href="http://localhost:${SETTINGS_PORT}/settings.html" target="_blank">Open device settings ↗</a></p><p id="deviceState" class="hint">Reading device settings…</p>`,
    );
    html = html.replace(
      '<p>Device feed: <code id="feedUrl"></code></p>',
      '<p>Device feed: <code id="feedUrl"></code></p><div class="panel"><h2>Live diagnostics</h2><p class="hint">Configuration, GitHub, feed and emulator errors. Tokens and Wi-Fi passwords are never written here.</p><button onclick="loadLogs()">Refresh logs</button><button onclick="clearLogs()">Clear logs</button><div id="logs" class="logs">No events yet.</div></div>',
    );
  }
  html = html.replace('<div class="repo">\'+esc(name)+\'</div>', '<div class="repo marquee"><span class="marquee-track">\'+esc(name)+\'</span></div>');
  html = html.replace('<div class="detail">\'+esc(r.workflow)+\'</div>', '<div class="detail marquee"><span class="marquee-track">\'+esc(r.workflow)+\'</span></div>');
  html = html.replace('<span class="branch">\'+esc(r.branch)+\'</span>', '<span class="branch marquee"><span class="marquee-track">\'+esc(r.branch)+\'</span></span>');
  html = html.replace(
    "let runs=d.items||d.runs||[],cnt={run:0,ok:0,err:0},pages=Math.max(1,Math.ceil(runs.length/3));if(page>=pages)page=0;runs.forEach(r=>{if(active(r.status))cnt.run++;else if(r.conclusion==='success')cnt.ok++;else if(r.conclusion==='failure')cnt.err++});let h=",
    "let runs=d.items||d.runs||[],cnt={run:0,ok:0,err:0};runs.forEach(r=>{if(active(r.status))cnt.run++;else if(r.conclusion==='success')cnt.ok++;else if(r.conclusion==='failure')cnt.err++});let shown=cnt.run?runs.filter(r=>active(r.status)):runs,pages=Math.max(1,Math.ceil(shown.length/2));if(page>=pages)page=0;let h=",
  );
  html = html.replace("header(page,pages)+'<div class=\"sum\">'+[['RUN'", "header(page,pages,cnt.run>0)+'<div class=\"sum\">'+[['R'");
  html = html.replace("['PASS',cnt.ok", "['P',cnt.ok");
  html = html.replace("['FAIL',cnt.err", "['F',cnt.err");
  html = html.replace('runs.slice(page*3,page*3+3)', 'shown.slice(page*2,page*2+2)');
  html = html.replace(
    '\'<div class="item" style="--state:\'+info.color+\'">',
    '\'<div class="item \'+(r.latest?\'latest\':\'\')+\'" style="--state:\'+info.color+\'">',
  );
  html = html.replace("el('deviceScreen').innerHTML=h+'</div>'}", "el('deviceScreen').innerHTML=h+'</div>';requestAnimationFrame(activateMarquees)}");
  const oldBoot = "el('feedUrl').textContent=location.origin+'/api/github';loadConfig();load();setInterval(load,3000);setInterval(updateTimers,1000);setInterval(()=>{if(latestData&&(latestData.items||[]).length>3){page=(page+1)%Math.ceil(latestData.items.length/3);render(latestData)}},5000)";
  const newBoot = `let simulatedDevice={mode:'github',github:{pollSec:15,rotateSec:8}},deviceSignature='',nextPollAt=0,nextPageAt=0;
async function loadDeviceConfig(){try{let c=await fetch('/api/device-config').then(r=>r.json()),sig=JSON.stringify(c);if(sig!==deviceSignature){deviceSignature=sig;simulatedDevice=c;page=0;nextPollAt=0;nextPageAt=Date.now()+Math.max(1000,Number(c.github?.rotateSec||8)*1000);el('deviceState').textContent='Device mode: '+String(c.mode||'unknown').toUpperCase()+' · refresh '+Number(c.github?.pollSec||15)+'s · rotate '+Number(c.github?.rotateSec||8)+'s';await load()}}catch(e){el('deviceState').textContent='Settings link error: '+e.message}}
load=async function(){let githubActive=simulatedDevice.mode==='github'||(simulatedDevice.mode==='carousel'&&simulatedDevice.carouselGithub!==false);if(!githubActive){latestData={ok:false,error:{code:'MODE_INACTIVE',message:'GitHub screen is disabled. Selected mode: '+String(simulatedDevice.mode||'unknown').toUpperCase()}};latestFetchedAt=Date.now();render(latestData);return}try{latestData=await fetch('/api/device-feed').then(r=>r.json())}catch(e){latestData={ok:false,error:{code:'BRIDGE_OFFLINE',message:e.message}}}latestFetchedAt=Date.now();render(latestData)};
async function loadLogs(){try{let d=await fetch('/api/logs').then(r=>r.json());el('logs').innerHTML=(d.items||[]).slice().reverse().map(x=>'<div class="log log-'+esc(x.level)+'">'+esc(new Date(x.at).toLocaleTimeString())+' ['+esc(x.level.toUpperCase())+'] '+esc(x.event)+(x.message?' — '+esc(x.message):'')+(x.repo?' · '+esc(x.repo):'')+'</div>').join('')||'No events yet.'}catch(e){el('logs').textContent='Log error: '+e.message}}
async function clearLogs(){await fetch('/api/logs/clear',{method:'POST'});loadLogs()}
function activateMarquees(){document.querySelectorAll('.marquee').forEach(function(box){let track=box.querySelector('.marquee-track');if(!track||box.classList.contains('scrolling'))return;let value=track.textContent,width=track.scrollWidth;if(width<=box.clientWidth+1)return;track.innerHTML='<span>'+esc(value)+'</span><span class="marquee-copy">'+esc(value)+'</span>';box.style.setProperty('--marquee-shift','-'+(width+24)+'px');box.style.setProperty('--marquee-duration',Math.max(6,(width+24)/18)+'s');box.classList.add('scrolling')})}
function emulatorTick(){let now=Date.now(),poll=Math.max(1000,Number(simulatedDevice.github?.pollSec||15)*1000),rotate=Math.max(1000,Number(simulatedDevice.github?.rotateSec||8)*1000);if(now>=nextPollAt){nextPollAt=now+poll;load()}if(now>=nextPageAt){nextPageAt=now+rotate;if(latestData){let all=latestData.items||[],focused=all.some(r=>active(r.status))?all.filter(r=>active(r.status)):all;if(focused.length>2){page=(page+1)%Math.ceil(focused.length/2);render(latestData)}}}}
el('feedUrl').textContent='configured in device settings';loadConfig();loadDeviceConfig();loadLogs();setInterval(loadDeviceConfig,1000);setInterval(emulatorTick,500);setInterval(updateTimers,1000);setInterval(loadLogs,2000)`;
  if (embedded) {
    const embeddedBoot = "function activateMarquees(){document.querySelectorAll('.marquee').forEach(function(box){let track=box.querySelector('.marquee-track');if(!track||box.classList.contains('scrolling'))return;let value=track.textContent,width=track.scrollWidth;if(width<=box.clientWidth+1)return;track.innerHTML='<span>'+esc(value)+'</span><span class=\"marquee-copy\">'+esc(value)+'</span>';box.style.setProperty('--marquee-shift','-'+(width+24)+'px');box.style.setProperty('--marquee-duration',Math.max(6,(width+24)/18)+'s');box.classList.add('scrolling')})}el('feedUrl').textContent=location.origin+'/api/github';loadConfig();load();setInterval(load,3000);setInterval(updateTimers,1000);setInterval(()=>{if(latestData){let all=latestData.items||[],focused=all.some(r=>active(r.status))?all.filter(r=>active(r.status)):all;if(focused.length>2){page=(page+1)%Math.ceil(focused.length/2);render(latestData)}}},5000)";
    return html.replace(oldBoot, embeddedBoot);
  }
  return html.replace(oldBoot, newBoot);
}

function json(res, status, value) {
  res.writeHead(status, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store', 'Access-Control-Allow-Origin': '*' });
  res.end(JSON.stringify(value));
}

async function readBody(req) {
  let body = '';
  for await (const chunk of req) {
    body += chunk;
    if (body.length > 64_000) throw new Error('request too large');
  }
  return JSON.parse(body || '{}');
}

async function readRawBody(req) {
  const chunks = [];
  let length = 0;
  for await (const chunk of req) {
    length += chunk.length;
    if (length > 2_000_000) throw new BridgeError('WEBHOOK_TOO_LARGE', 'Webhook payload exceeds 2 MB.', 413);
    chunks.push(chunk);
  }
  return Buffer.concat(chunks);
}

function authorized(req) {
  return !DEVICE_TOKEN || req.headers['x-device-token'] === DEVICE_TOKEN;
}

function dashboardApi(path) {
  return ['/api/device-config', '/api/device-feed', '/api/logs', '/api/logs/clear'].includes(path);
}

function githubWebhookApi(path) {
  return path === '/api/github/webhook';
}

async function fetchDeviceFeed() {
  const url = String(deviceConfig.github?.statusUrl || '').trim();
  if (!/^https?:\/\//i.test(url)) throw new BridgeError('FEED_URL_INVALID', 'Enter a complete GitHub status URL beginning with http:// or https://.', 400, { field: 'github.statusUrl', source: 'config' });
  if (url.includes(`/api/device-feed`)) throw new BridgeError('FEED_URL_LOOP', 'The device feed URL points back to the emulator proxy.', 400, { field: 'github.statusUrl', source: 'config' });
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), Math.max(1000, Number(deviceConfig.httpTimeout || 8000)));
  try {
    const headers = { Accept: 'application/json', 'User-Agent': 'smalltv-device-emulator' };
    if (deviceConfig.github?.accessToken) headers['X-Device-Token'] = deviceConfig.github.accessToken;
    const response = await fetch(url, { headers, signal: controller.signal });
    let data;
    try { data = await response.json(); }
    catch { throw new BridgeError('BAD_RESPONSE', 'The configured feed did not return JSON.', 502, { source: 'bridge' }); }
    if (!response.ok) throw new BridgeError(data.error?.code || `FEED_HTTP_${response.status}`, data.error?.message || `The feed returned HTTP ${response.status}.`, response.status >= 500 ? 502 : 400, { source: 'bridge' });
    return data;
  } catch (e) {
    if (e instanceof BridgeError) throw e;
    const timeout = e.name === 'AbortError';
    throw new BridgeError(timeout ? 'FEED_TIMEOUT' : 'BRIDGE_OFFLINE', timeout ? 'The configured feed timed out.' : 'Cannot connect to the configured GitHub feed.', 502, { source: 'network' });
  } finally { clearTimeout(timer); }
}

const server = http.createServer(async (req, res) => {
  try {
    if (req.method === 'OPTIONS') {
      res.writeHead(204, { 'Access-Control-Allow-Origin': '*', 'Access-Control-Allow-Headers': 'Content-Type, X-Device-Token', 'Access-Control-Allow-Methods': 'GET, POST, OPTIONS' });
      return res.end();
    }
    if (req.url.startsWith('/api/') && !dashboardApi(req.url) && !githubWebhookApi(req.url) && !authorized(req)) return json(res, 401, { ok: false, error: { code: 'DEVICE_TOKEN_DENIED', message: 'Bridge rejected the device token.', field: 'deviceToken', repo: '', source: 'bridge' } });
    if (req.method === 'POST' && req.url === '/api/github/webhook') {
      const result = githubWebhooks.ingest({
        event: String(req.headers['x-github-event'] || ''),
        delivery: String(req.headers['x-github-delivery'] || ''),
        signature: String(req.headers['x-hub-signature-256'] || ''),
        rawBody: await readRawBody(req),
        secret: bridgeConfig.webhook?.secret || '',
      });
      return json(res, result.status, result.body);
    }
    if (req.method === 'GET' && req.url === '/api/device-config') return json(res, 200, publicDeviceConfig());
    if (req.method === 'GET' && req.url === '/api/device-feed') return json(res, 200, refreshAges(await fetchDeviceFeed()));
    if (req.method === 'GET' && req.url === '/api/logs') return json(res, 200, { ok: true, items: logs });
    if (req.method === 'POST' && req.url === '/api/logs/clear') { logs.length = 0; return json(res, 200, { ok: true }); }
    if (req.method === 'GET' && req.url === '/api/ai-usage') {
      return json(res, 200, {
        ok: true,
        a: Math.max(0, Math.min(100, Number(process.env.AI_ANTIGRAVITY_PCT || 34))),
        ar: Math.max(0, Number(process.env.AI_ANTIGRAVITY_RESET_MIN || 120)),
        c: Math.max(0, Math.min(100, Number(process.env.AI_CODEX_PCT || 57))),
        cr: Math.max(0, Number(process.env.AI_CODEX_RESET_MIN || 360)),
        st: 'mock',
        updatedAt: new Date().toISOString(),
      });
    }
    if (req.method === 'GET' && req.url.startsWith('/api/github')) {
      const data = bridgeConfig.mode === 'live'
        ? (bridgeConfig.delivery === 'webhook' ? webhookData() : await liveData())
        : mockResponse();
      return json(res, 200, refreshAges(data));
    }
    if (req.method === 'GET' && req.url === '/api/config') return json(res, 200, publicConfig());
    if (req.method === 'POST' && req.url === '/api/config') {
      saveConfig(await readBody(req));
      return json(res, 200, publicConfig());
    }
    if (req.method === 'POST' && req.url === '/api/scenario') {
      const next = (await readBody(req)).scenario;
      if (scenarios[next] || errorScenarios[next]) scenario = next;
      record('info', 'mock.scenario.changed', { message: scenario });
      return json(res, 200, { ok: true, scenario });
    }
    if (req.method === 'GET' && req.url === '/') {
      res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
      return res.end(enhanceScreenHtml(screenHtml()));
    }
    if (req.method === 'GET' && req.url === '/embed') {
      res.writeHead(200, {
        'Content-Type': 'text/html; charset=utf-8',
        'Cache-Control': 'no-store',
        'Access-Control-Allow-Origin': '*',
      });
      return res.end(enhanceScreenHtml(screenHtml(), true));
    }
    res.writeHead(404); res.end('Not found');
  } catch (e) {
    record('error', 'bridge.request.failed', { code: e.code || 'BRIDGE_INTERNAL', message: e.message || 'Unexpected bridge error.', path: req.url });
    if (e instanceof BridgeError) return json(res, e.status, e.json());
    json(res, 500, { ok: false, error: { code: 'BRIDGE_INTERNAL', message: e.message || 'Unexpected bridge error.', field: '', repo: '', source: 'bridge' } });
  }
});

const startedAt = Date.now();
function deviceStatus() {
  return {
    ok: true, fw: 'smalltv-ultra', version: '0.6.0', repo: 'https://github.com/bairachnyi/smalltv-ultra',
    mode: 'sta', connected: true, ssid: 'Local test WiFi', ip: `127.0.0.1:${SETTINGS_PORT}`, rssi: -42,
    heap: 49296, maxblk: 30000, contstk: 4096, uptime: Math.floor((Date.now() - startedAt) / 1000),
    reset: 'Local emulator', synced: true, time: new Date().toISOString(), tz: deviceConfig.clock?.tz || 'Asia/Bangkok', night: false, tickers: [],
  };
}

const deviceServer = http.createServer(async (req, res) => {
  try {
    if (req.method === 'OPTIONS') {
      res.writeHead(204, { 'Access-Control-Allow-Origin': '*', 'Access-Control-Allow-Headers': 'Content-Type', 'Access-Control-Allow-Methods': 'GET, POST, OPTIONS' });
      return res.end();
    }
    if (req.method === 'GET' && (req.url === '/' || req.url === '/settings.html')) {
      res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' });
      return res.end(DEVICE_WEB_UI);
    }
    if (req.method === 'POST' && req.url === '/api/login') {
      const body = await readBody(req);
      const pass = body.pass || '';
      const setPass = deviceConfig.adminPass !== undefined ? deviceConfig.adminPass : '1111';
      if (!setPass || pass === setPass) return json(res, 200, { ok: true });
      return json(res, 200, { ok: false });
    }
    if (req.method === 'GET' && req.url === '/api/config') return json(res, 200, publicDeviceConfig());
    if (req.method === 'POST' && req.url === '/api/config') { saveDeviceConfig(await readBody(req)); return json(res, 200, { ok: true, reboot: false }); }
    if (req.method === 'GET' && req.url === '/api/status') return json(res, 200, deviceStatus());
    if (req.method === 'GET' && req.url === '/api/scan') return json(res, 200, [{ ssid: 'Local test WiFi', rssi: -42, enc: true }, { ssid: 'Guest 2.4G', rssi: -68, enc: true }]);
    if (req.method === 'GET' && req.url === '/api/photos') {
      const fsTotal = 3 * 1024 * 1024;
      const photosSize = emulatorPhotos.reduce((sum, p) => sum + (p.size || 0), 0);
      const fsUsed = photosSize + 128 * 1024;
      const fsFree = Math.max(0, fsTotal - fsUsed);
      return json(res, 200, {
        photos: emulatorPhotos.map(p => ({ name: p.name, path: p.path, size: p.size })),
        fsTotal,
        fsUsed,
        fsFree
      });
    }
    if (req.method === 'POST' && req.url === '/api/photos/upload') {
      const rawBuf = await readRawBody(req);
      const parsed = extractImageBuffer(rawBuf);
      const ext = parsed.mime === 'image/png' ? 'png' : (parsed.mime === 'image/gif' ? 'gif' : 'jpg');
      const photoName = `photo_${Date.now()}.${ext}`;
      const filePath = new URL(`./photos/${photoName}`, import.meta.url);
      try { writeFileSync(filePath, parsed.buffer); } catch {}
      const photoObj = { name: photoName, path: `/photos/${photoName}`, size: parsed.buffer.length, buffer: parsed.buffer, mime: parsed.mime };
      emulatorPhotos.push(photoObj);
      record('info', 'device.photos.uploaded', { message: `Uploaded photo ${photoName} (${parsed.buffer.length} B)` });
      return json(res, 200, { ok: true });
    }
    if (req.method === 'POST' && req.url === '/api/photos/delete') {
      const body = await readBody(req);
      const path = body.path || '';
      const name = path.replace('/photos/', '').replaceAll('/', '');
      if (name) {
        try { unlinkSync(new URL(`./photos/${name}`, import.meta.url)); } catch {}
      }
      emulatorPhotos = emulatorPhotos.filter(p => p.path !== path);
      record('info', 'device.photos.deleted', { message: `Deleted photo ${path}` });
      return json(res, 200, { ok: true });
    }
    if (req.method === 'GET' && req.url.startsWith('/photos/')) {
      const cleanPath = req.url.split('?')[0];
      let photo = emulatorPhotos.find(p => p.path === cleanPath);
      if (!photo) {
        const name = cleanPath.replace('/photos/', '').replaceAll('/', '');
        if (name) {
          try {
            const filePath = new URL(`./photos/${name}`, import.meta.url);
            const buf = readFileSync(filePath);
            const mime = name.endsWith('.png') ? 'image/png' : (name.endsWith('.gif') ? 'image/gif' : 'image/jpeg');
            photo = { name, path: cleanPath, size: buf.length, buffer: buf, mime };
            emulatorPhotos.push(photo);
          } catch {}
        }
      }
      if (photo && photo.buffer) {
        res.writeHead(200, { 'Content-Type': photo.mime || 'image/jpeg', 'Cache-Control': 'no-store' });
        return res.end(photo.buffer);
      }
      res.writeHead(404, { 'Content-Type': 'text/plain' });
      return res.end('Photo not found');
    }
    if (req.method === 'GET' && req.url === '/api/export') {
      res.writeHead(200, { 'Content-Type': 'application/json', 'Content-Disposition': 'attachment; filename="geektv-emulator-settings.json"', 'Cache-Control': 'no-store' });
      return res.end(JSON.stringify(deviceConfig, null, 2));
    }
    if (req.method === 'POST' && req.url === '/api/import') { saveDeviceConfig(await readBody(req)); return json(res, 200, { ok: true, reboot: true }); }
    if (req.method === 'GET' && req.url === '/api/checkupdate') return json(res, 200, { ok: true, current: '0.7.0', latest: '0.7.0', newer: false });
    if (req.method === 'POST' && ['/api/reboot', '/api/refresh', '/api/usage', '/api/ai-usage'].includes(req.url)) { record('info', `device${req.url.replace('/api', '').replaceAll('/', '.')}`, { message: 'Simulated locally' }); return json(res, 200, { ok: true, simulated: true }); }
    if (req.method === 'POST' && req.url === '/api/factory') { deviceConfig = structuredClone(DEFAULT_DEVICE_CONFIG); saveDeviceConfig({}); record('warn', 'device.factory_reset', { message: 'Local emulator defaults restored' }); return json(res, 200, { ok: true, reboot: true, simulated: true }); }
    if (req.method === 'POST' && req.url === '/api/selfupdate') { record('warn', 'device.self_update.skipped', { message: 'Firmware update is disabled in the local emulator' }); return json(res, 200, { ok: true, simulated: true }); }
    if (req.url === '/update') { record('warn', 'device.ota.skipped', { message: 'OTA is disabled in the local emulator' }); res.writeHead(501, { 'Content-Type': 'text/plain; charset=utf-8' }); return res.end('Firmware upload is disabled in the local emulator.'); }
    res.writeHead(404); return res.end('Not found');
  } catch (e) {
    record('error', 'device.request.failed', { code: e.code || 'DEVICE_INTERNAL', message: e.message || 'Unexpected device emulator error.', path: req.url });
    if (e instanceof BridgeError) return json(res, e.status, e.json());
    return json(res, 500, { ok: false, error: { code: 'DEVICE_INTERNAL', message: e.message || 'Unexpected device emulator error.' } });
  }
});

server.listen(PORT, HOST, () => {
  const ips = Object.values(os.networkInterfaces()).flat().filter(x => x?.family === 'IPv4' && !x.internal).map(x => x.address);
  console.log(`GeekTV emulator: http://localhost:${PORT}`);
  for (const ip of ips) console.log(`Device feed: http://${ip}:${PORT}/api/github`);
  console.log(`Mode: ${bridgeConfig.mode}; owners: ${bridgeConfig.accounts.map(a => a.owner).join(', ')}`);
});

deviceServer.listen(SETTINGS_PORT, HOST, () => {
  console.log(`Device settings: http://localhost:${SETTINGS_PORT}/settings.html`);
  record('info', 'emulator.ready', { message: `screen=:${PORT}; settings=:${SETTINGS_PORT}` });
});
