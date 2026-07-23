// smalltv-ultra — custom firmware for the GeekMagic SmallTV family
//
// Three features, each a self-contained DisplayMode (see Mode.h), picked in the
// web UI and dispatched from the registry below:
//   - Ticker (features/ticker):  stock/crypto price, % change, sparkline.
//   - Usage  (features/usage):   Antigravity + Codex usage bars.
//   - GitHub (features/github):  GH//STAT CI/CD dashboard via a trusted bridge.
// Shared plumbing (WiFi, web UI, OTA, display core, settings) lives at src root.
//
// License: WTFPL
#include <Arduino.h>
#include "Platform.h"
#include "config.h"
#include "Settings.h"
#include "Net.h"
#include "Gfx.h"
#include "WebPortal.h"
#include "OtaUpdate.h"
#include "Mode.h"
#include "Clock.h"

#if WITH_TICKER
#include "TickerMode.h"
#endif
#if WITH_USAGE
#include "UsageMode.h"
#endif
#if WITH_GITHUB
#include "GithubMode.h"
#include "GithubClient.h"
#endif
#if WITH_CLOCK
#include "ClockMode.h"
#endif
#if WITH_GALLERY
#include "GalleryMode.h"
#endif

// ---- mode registry --------------------------------------------------------
// The compiled-in features, in display order. main.cpp holds no per-feature
// state of its own — each mode owns its fetch/render/dirty tracking.
static DisplayMode* kModes[] = {
#if WITH_TICKER
  &g_tickerMode,
#endif
#if WITH_USAGE
  &g_usageMode,
#endif
#if WITH_GITHUB
  &g_githubMode,
#endif
#if WITH_CLOCK
  &g_clockMode,
#endif
#if WITH_GALLERY
  &g_galleryMode,
#endif
};
static const size_t kModeCount = sizeof(kModes) / sizeof(kModes[0]);

// ---- carousel -------------------------------------------------------------
// MODE_CAROUSEL rotates through the ticked features. Switches call wake() on
// the incoming mode: repaint from cached data, no refetch.
// For clock, we support multiple themes (Digital/Weather/Modern) cycling as
// separate virtual slots within the same g_clockMode.
static size_t   g_carIdx = 0;
static uint32_t g_carSwitch = 0;
// Carousel clock theme rotation: tracks which theme-index to show next.
// We build the list of enabled carousel clock themes on each advance.
static uint8_t  g_carClockThemeIdx = 0;

static bool carouselHas(const Settings& s, const DisplayMode* m) {
  switch (m->modeConst()) {
    case MODE_STOCKS:  return s.carouselTicker;
    case MODE_USAGE:   return s.carouselUsage;
    case MODE_GITHUB:  return s.carouselGithub;
    case MODE_CLOCK:   return s.carouselClock || s.carouselClockDigital || s.carouselClockWeather || s.carouselClockModern || s.carouselClockForecast;
    case MODE_GALLERY: return s.carouselGallery;
    default:           return true;
  }
}

// Count how many carousel clock themes are enabled.
static uint8_t carouselClockCount(const Settings& s) {
  return (uint8_t)(s.carouselClockDigital ? 1 : 0)
       + (uint8_t)(s.carouselClockWeather ? 1 : 0)
       + (uint8_t)(s.carouselClockModern  ? 1 : 0)
       + (uint8_t)(s.carouselClockForecast? 1 : 0);
}

// Return the Nth enabled carousel clock theme index (0=Fullscreen, 1=Weather, 2=Modern, 3=Forecast).
static uint8_t carouselClockThemeAt(const Settings& s, uint8_t n) {
  uint8_t cur = 0;
  if (s.carouselClockDigital)  { if (n == cur) return 0; cur++; }
  if (s.carouselClockWeather)  { if (n == cur) return 1; cur++; }
  if (s.carouselClockModern)   { if (n == cur) return 2; cur++; }
  if (s.carouselClockForecast) { if (n == cur) return 3; cur++; }
  return 0;  // fallback: fullscreen clock
}

// Advance g_carIdx to the next ticked mode (stays put if none other is ticked).
// For MODE_CLOCK, also advance g_carClockThemeIdx and apply the theme to settings.
static void carouselNext(Settings& s) {
  // If we're currently on the clock mode and there are more clock themes to show,
  // stay on clock but advance the theme index.
  if (kModes[g_carIdx]->modeConst() == MODE_CLOCK && carouselHas(s, kModes[g_carIdx])) {
    uint8_t count = carouselClockCount(s);
    if (count > 1) {
      uint8_t nextTheme = (g_carClockThemeIdx + 1) % count;
      if (nextTheme != 0) {  // not wrapped back to 0: stay on clock with new theme
        g_carClockThemeIdx = nextTheme;
        s.clock.theme = carouselClockThemeAt(s, g_carClockThemeIdx);
        kModes[g_carIdx]->wake(s);
        return;
      }
      // Wrapped back to theme 0: fall through to advance to next non-clock mode.
      g_carClockThemeIdx = 0;
    }
  }
  // Advance to the next mode slot.
  for (size_t hop = 1; hop <= kModeCount; hop++) {
    size_t cand = (g_carIdx + hop) % kModeCount;
    if (!carouselHas(s, kModes[cand])) continue;
    if (cand != g_carIdx) {
      g_carIdx = cand;
      // When landing on clock, apply the first enabled carousel theme.
      if (kModes[cand]->modeConst() == MODE_CLOCK) {
        g_carClockThemeIdx = 0;
        s.clock.theme = carouselClockThemeAt(s, 0);
      }
      kModes[cand]->wake(s);
    }
    return;
  }
}

static DisplayMode* activeMode(Settings& s) {
  if (s.mode == MODE_CAROUSEL && kModeCount > 0) {
    if (g_carSwitch == 0) g_carSwitch = millis();
    if (!carouselHas(s, kModes[g_carIdx])) carouselNext(s);   // settings changed
#if WITH_GITHUB
    // Once the GitHub mode has observed an active/queued job, keep GH//STAT in
    // front until the next feed refresh reports that all work is complete.
    // Refreshing the dwell timestamp also leaves the final result visible for
    // one complete carousel interval before normal rotation resumes.
    if (s.carouselGithub && githubGet().runningCount > 0) {
      for (size_t i = 0; i < kModeCount; i++) {
        if (kModes[i]->modeConst() != MODE_GITHUB) continue;
        if (g_carIdx != i) { g_carIdx = i; kModes[i]->wake(s); }
        g_carSwitch = millis();
        return kModes[i];
      }
    }
#endif
    if (millis() - g_carSwitch >= (uint32_t)s.carouselSec * 1000UL) {
      g_carSwitch = millis();
      carouselNext(s);
    }
    return kModes[g_carIdx];
  }
  for (size_t i = 0; i < kModeCount; i++)
    if (kModes[i]->modeConst() == s.mode) return kModes[i];
  return kModeCount ? kModes[0] : nullptr;   // fall back to the first compiled mode
}

static Settings g_settings;
static String   g_resetReason;        // why the chip last reset (diagnostics)
static bool     g_safeMode = false;   // last reset was an exception -> don't re-enter the crash
static char     g_epcStr[16] = "";
static char     g_addrStr[16] = "";
static int g_lastBr = -1;        // last effective brightness written (-1 = none yet)
#if HAS_LDR
static uint32_t g_lastAutoBr = 0;
static uint8_t  g_ldrCache   = DEFAULT_BRIGHTNESS;   // last LDR reading (2 s cadence)
#endif

// Single brightness resolver: night mode overrides auto-brightness overrides the
// manual level. Only writes the PWM when the effective target changes.
static uint8_t appEffectiveBrightness() {
  if (clockNightActive()) return g_settings.clock.nightLevel;
#if HAS_LDR
  if (g_settings.autoBrightness) {
    if (millis() - g_lastAutoBr > 2000) {
      g_lastAutoBr = millis();
      int raw = analogRead(LDR_PIN);
      g_ldrCache = (uint8_t)constrain(raw * 100 / ADC_MAX, 5, 100);
    }
    return g_ldrCache;
  }
#endif
  return g_settings.brightness;
}

void appApplyBrightness() {
  uint8_t t = appEffectiveBrightness();
  if ((int)t != g_lastBr) {
    g_lastBr = t;
    gfxSetBrightness(t, g_settings.backlightInverted);
  }
}

// Exposed to the web portal (/api/status) so the last reset reason is visible.
const char* appResetReason() { return g_resetReason.c_str(); }

// Called by the web portal after settings are applied: re-init every mode and
// force a fresh repaint so a mode/URL/symbol change takes effect immediately.
void appInvalidate() {
  for (size_t i = 0; i < kModeCount; i++) kModes[i]->invalidate(g_settings);
  DisplayMode* m = activeMode(g_settings);
  if (m) m->wake(g_settings);
}

static void bootProgress(const char* msg) {
  gfxBoot("SmallTV", msg);
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(FW_NAME " " FW_VERSION);

  // Capture why we (re)booted. On a reboot loop this is the key clue, and the
  // device's UART isn't exposed — so we also show it on screen below. On the
  // ESP8266 we also keep the crash PC (epc1) for addr2line decoding; the
  // ESP32-C2 (RISC-V) doesn't expose it, so epc/addr come back empty there.
  PlatformReset pr = platformResetInfo();
  Serial.print("[boot] reset reason: ");
  Serial.println(pr.reason);

  if (pr.wasCrash) {
    g_safeMode = true;                   // crashed last boot -> stay out of the crash path
    strlcpy(g_epcStr,  pr.epc,  sizeof(g_epcStr));
    strlcpy(g_addrStr, pr.addr, sizeof(g_addrStr));
    char rich[80];
    snprintf(rich, sizeof(rich), "%s epc %s addr %s", pr.reason.c_str(),
             g_epcStr[0] ? g_epcStr : "-", g_addrStr[0] ? g_addrStr : "-");
    g_resetReason = rich;
  } else {
    g_resetReason = pr.reason;
  }

  Serial.println("[boot] settings");
  if (!settingsBegin()) {
    Serial.println("[boot] LittleFS mount FAILED — running with defaults");
    gfxBoot("SmallTV", "FS error");
    delay(2000);
  }
  loadSettings(g_settings);

  Serial.println("[boot] display");
  gfxBegin(g_settings);
  gfxBoot(g_safeMode ? "Crashed" : "SmallTV", FW_VERSION);

  Serial.println("[boot] net");
  netBegin(g_settings, bootProgress);
  // Arm SNTP now that WiFi (STA) is up — but only if night mode is enabled, so a
  // ticker-only device doesn't pay the SNTP heap cost (which can starve the cash.ch
  // TLS handshake on the ESP8266). clockReapply arms it iff needed. Skipped after a
  // crash so a fault in here can't boot-loop before the web server starts (the
  // device then comes up in safe mode, OTA-recoverable, instead of needing UART).
  if (!g_safeMode) clockReapply(g_settings);

  // A GitHub update queued from the web UI runs now, before the features claim
  // the heap (the download needs a 16 KB TLS buffer that only fits at boot).
  // On success it reboots into the new image; a no-op stub on the ESP32 targets.
  if (otaBootRequested()) {
    Serial.println("[boot] github update");
    gfxBoot("SmallTV", "updating...");
    otaBootUpdate(g_settings);
    gfxBoot("SmallTV", "update failed");   // still here -> failed; details in the web UI
    delay(1200);
  }

  Serial.println("[boot] web");
  webPortalBegin(g_settings);

  Serial.println("[boot] modes");
  for (size_t i = 0; i < kModeCount; i++) kModes[i]->begin(g_settings);
  Serial.println("[boot] done");

  if (netMode() == NET_AP) {
    gfxApInfo(g_settings.apSsid.c_str(), g_settings.apPass.c_str(), netIP().c_str());
  } else if (g_safeMode) {
    // Last boot crashed: show the crash address (persistent) and keep the web
    // server up for OTA recovery — don't enter the render path that crashed.
    gfxCrash(g_epcStr, g_addrStr, netIP().c_str());
  } else {
    // Show which network we joined and how to reach the web UI, long enough to read.
    gfxStaInfo(netSSID().c_str(), netIP().c_str(), g_settings.hostname.c_str());
    delay(3500);
  }
}

void loop() {
  netLoop();
  webPortalLoop();

  if (webPortalRebootDue()) {
    delay(120);
    ESP.restart();
  }

  if (g_safeMode) {
    delay(5);
    return;  // crashed last boot: web UI stays up for OTA recovery, no rendering
  }

  if (netMode() == NET_AP) {
    delay(5);
    return;  // setup mode: AP info stays on screen
  }

  // --- STA mode: the active feature fetches + renders itself ---

  // Night-mode state machine (NTP-trust gate), then apply the effective brightness
  // (night override / auto-brightness / manual level).
  clockService(g_settings);
  appApplyBrightness();

  DisplayMode* m = activeMode(g_settings);
  if (m) m->service(g_settings);

  delay(5);
}
