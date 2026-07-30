#include "GithubMode.h"
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "GithubClient.h"

GithubMode g_githubMode;

static const uint16_t GH_BG = 0x0000;
static const uint16_t GH_CARD = 0x0000;
static const uint16_t GH_MUTED = 0x6B6D;
static const uint16_t GH_CYAN = 0x07FF;
static const uint16_t GH_GREEN = 0x4FE9;
static const uint16_t GH_RED = 0xF986;
static const uint16_t GH_AMBER = 0xFD20;

static uint16_t stateColor(uint8_t s) {
  if (s == GH_SUCCESS) return GH_GREEN;
  if (s == GH_FAILURE) return GH_RED;
  if (s == GH_RUNNING) return GH_CYAN;
  if (s == GH_QUEUED) return GH_AMBER;
  return GH_MUTED;
}

static const char* stateLabel(uint8_t s) {
  if (s == GH_SUCCESS) return "PASS";
  if (s == GH_FAILURE) return "FAIL";
  if (s == GH_RUNNING) return "RUN";
  if (s == GH_QUEUED) return "WAIT";
  if (s == GH_CANCELLED) return "STOP";
  return "?";
}

static const char* eventLabel(uint8_t type) {
  if (type == GH_EVENT_DEPLOYMENT) return "DEP";
  if (type == GH_EVENT_PULL_REQUEST) return "PR";
  if (type == GH_EVENT_RELEASE) return "REL";
  return "ACT";
}

static const char* shortRepo(const char* repo) {
  const char* slash = strrchr(repo, '/');
  return slash && slash[1] ? slash + 1 : repo;
}

static void fitCopy(const char* src, char* out, size_t n, size_t maxChars) {
  if (!src) src = "";
  size_t len = strlen(src);
  if (len <= maxChars) { strlcpy(out, src, n); return; }
  size_t keep = maxChars > 2 ? maxChars - 2 : maxChars;
  strncpy(out, src, keep); out[keep] = 0;
  strlcat(out, "..", n);
}

static const char* errorTitle(const char* code, bool stale) {
  if (stale) return "DATA STALE";
  if (!strcmp(code, "TOKEN_INVALID") || !strcmp(code, "TOKEN_DENIED") || !strcmp(code, "DEVICE_TOKEN_DENIED")) return "TOKEN DENIED";
  if (!strcmp(code, "REPOSITORY_NOT_FOUND")) return "REPO NOT FOUND";
  if (!strcmp(code, "NO_REPOSITORIES")) return "NO REPOS";
  if (!strcmp(code, "RATE_LIMITED")) return "RATE LIMITED";
  if (!strcmp(code, "GITHUB_TIMEOUT")) return "GITHUB TIMEOUT";
  if (!strcmp(code, "GITHUB_OFFLINE") || !strcmp(code, "BRIDGE_OFFLINE")) return "BRIDGE OFFLINE";
  if (!strcmp(code, "WEBHOOK_SIGNATURE_INVALID")) return "BAD WEBHOOK SIG";
  if (!strcmp(code, "WEBHOOK_NOT_CONFIGURED")) return "WEBHOOK NOT SET";
  if (!strcmp(code, "WEBHOOK_STALE")) return "WEBHOOK STALE";
  if (!strcmp(code, "FEED_TIMEOUT")) return "FEED TIMEOUT";
  if (!strcmp(code, "FEED_URL_LOOP")) return "FEED URL LOOP";
  if (!strcmp(code, "BAD_RESPONSE") || !strcmp(code, "GITHUB_BAD_RESPONSE")) return "BAD RESPONSE";
  if (!strcmp(code, "LOW_MEMORY")) return "LOW MEMORY";
  if (!strcmp(code, "FEED_URL_INVALID")) return "BAD FEED URL";
  return "GITHUB ERROR";
}

static bool activeState(uint8_t state) {
  return state == GH_RUNNING || state == GH_QUEUED;
}

static void drawHeader(Arduino_GFX* gfx, bool priority, bool warning, bool stale) {
  gfx->setTextSize(2); gfx->setTextColor(C_WHITE); gfx->setCursor(7, 7); gfx->print("GH");
  gfx->setTextColor(GH_CYAN); gfx->print("//");
  gfx->setTextColor(C_WHITE); gfx->print("STAT");
  uint16_t liveColor = (warning || stale) ? GH_RED : (priority ? GH_AMBER : GH_CYAN);
  gfx->fillCircle(174, 14, 3, liveColor);
  gfx->setTextSize(1); gfx->setTextColor(liveColor); gfx->setCursor(181, 10); gfx->print(priority ? "FOCUS" : "LIVE");
  if (stale) { gfx->setCursor(181, 10); gfx->print("STALE"); }
  else if (warning && !priority) { gfx->setCursor(181, 10); gfx->print("WARN"); }
  gfx->drawFastHLine(6, 29, 228, 0x1924);
  gfx->drawFastHLine(6, 29, 66, GH_CYAN);
}

static void drawErrorScreen(Arduino_GFX* gfx, const GithubData& d, bool configured, bool stale, uint8_t frame) {
  const char* title = configured ? errorTitle(d.errorCode, stale) : "GITHUB NOT SET";
  gfx->fillRoundRect(10, 48, 220, 169, 8, GH_CARD);
  gfx->drawRoundRect(10, 48, 220, 169, 8, 0x1924);
  gfx->fillCircle(120, 79, 9, GH_RED);
  gfx->drawCircle(120, 79, 12 + ((frame >> 1) & 1), GH_RED);
  gfx->setTextSize(2); gfx->setTextColor(C_WHITE); gfxDrawCentered(title, 104, 2, C_WHITE);
  if (!configured) {
    gfxDrawCentered("Open GitHub tab", 142, 1, GH_MUTED);
    gfxDrawCentered("Add bridge feed URL", 158, 1, GH_MUTED);
  } else {
    char repo[29], msg[33];
    fitCopy(d.errorRepo, repo, sizeof(repo), 27);
    fitCopy(stale ? "Last successful data is too old" : d.errorMessage, msg, sizeof(msg), 31);
    if (repo[0]) gfxDrawCentered(repo, 137, 1, C_WHITE);
    gfxDrawCentered(msg, repo[0] ? 155 : 143, 1, GH_MUTED);
    gfxDrawCentered("[ open GitHub settings ]", 188, 1, GH_CYAN);
  }
}

static void drawStateIcon(Arduino_GFX* gfx, int cx, int cy, uint8_t state, uint8_t frame) {
  if (state == GH_RUNNING || state == GH_QUEUED) {
    static const int8_t dx[8] = {0, 5, 7, 5, 0, -5, -7, -5};
    static const int8_t dy[8] = {-7, -5, 0, 5, 7, 5, 0, -5};
    uint16_t color = state == GH_RUNNING ? GH_CYAN : GH_AMBER;
    for (uint8_t i = 0; i < 8; i++) {
      uint8_t distance = (i + 8 - (frame & 7)) & 7;
      gfx->fillCircle(cx + dx[i], cy + dy[i], distance == 0 ? 2 : 1, distance < 3 ? color : GH_MUTED);
    }
    return;
  }
  if (state == GH_SUCCESS) {
    gfx->drawLine(cx - 5, cy, cx - 1, cy + 4, GH_GREEN);
    gfx->drawLine(cx - 1, cy + 4, cx + 6, cy - 5, GH_GREEN);
    return;
  }
  if (state == GH_FAILURE) {
    gfx->fillCircle(cx, cy, 8, GH_RED);
    gfx->drawFastVLine(cx, cy - 4, 6, GH_BG);
    gfx->fillCircle(cx, cy + 4, 1, GH_BG);
    return;
  }
  gfx->drawCircle(cx, cy, 8, GH_MUTED);
  gfx->drawFastHLine(cx - 4, cy, 8, GH_MUTED);
}

static void formatElapsed(const GithubRun& r, const GithubData& d, char* out, size_t n) {
  uint32_t seconds = r.ageSec;
  if ((r.state == GH_RUNNING || r.state == GH_QUEUED) && d.lastOkMs)
    seconds += (millis() - d.lastOkMs) / 1000UL;
  uint8_t minutes = (uint8_t)((seconds / 60) % 60);
  uint8_t remainder = (uint8_t)(seconds % 60);
  if (seconds < 3600) snprintf(out, n, "%02u:%02u", minutes, remainder);
  else {
    uint16_t hours = (uint16_t)min<uint32_t>(seconds / 3600, 999);
    snprintf(out, n, "%uh%02u", hours, minutes);
  }
}

static void drawGithubRow(Arduino_GFX* gfx, const GithubRun& r, const GithubData& d,
                          uint8_t row, uint32_t frame) {
  int y = 35 + row * 66;
  uint16_t color = stateColor(r.state);
  gfx->fillRect(4, y, 232, 62, GH_BG);
  gfx->drawRoundRect(4, y, 232, 62, 5, 0x1924);
  gfx->fillRoundRect(4, y + 5, 3, 52, 1, color);
  drawStateIcon(gfx, 19, y + 17, r.state, frame + row * 2);

  char repo[13], workflow[37], branch[18], right[9];
  fitCopy(shortRepo(r.repo), repo, sizeof(repo), 12);
  fitCopy(r.workflow, workflow, sizeof(workflow), 36);
  fitCopy(r.branch, branch, sizeof(branch), 17);
  if (activeState(r.state)) formatElapsed(r, d, right, sizeof(right));
  else strlcpy(right, stateLabel(r.state), sizeof(right));

  gfx->setTextSize(2);
  gfx->setTextColor(C_WHITE);
  gfx->setCursor(35, y + 7);
  gfx->print(repo);
  gfx->setTextColor(color);
  gfx->setCursor(232 - gfxTextW(right, 2), y + 7);
  gfx->print(right);

  gfx->setTextSize(1);
  gfx->setTextColor(GH_MUTED);
  gfx->setCursor(12, y + 31);
  gfx->print(workflow);
  gfx->setTextColor(0xA534);
  gfx->setCursor(12, y + 47);
  gfx->print(eventLabel(r.type));
  gfx->print(' ');
  gfx->print(branch);
  gfx->setTextColor(C_WHITE);
  gfx->setCursor(232 - gfxTextW(r.when, 1), y + 47);
  gfx->print(r.when);
}

static void drawGithub(const GithubData& d, bool configured, bool stale, uint32_t frame) {
  Arduino_GFX* gfx = gfxDev(); if (!gfx) return;
  gfx->fillScreen(GH_BG);
  drawHeader(gfx, d.runningCount > 0, d.error, stale);
  // Keep displaying the last valid events when one source is degraded or the
  // feed is stale. A fatal error screen is reserved for cases with no usable
  // event data at all.
  if (!configured || !d.valid || (d.error && !d.runCount)) {
    drawErrorScreen(gfx, d, configured, stale, frame);
    return;
  }

  if (!d.runCount) { gfxDrawCentered("No workflow runs", 125, 2, GH_MUTED); return; }
  uint8_t count = min<uint8_t>(3, d.runCount);
  for (uint8_t row = 0; row < count; row++) drawGithubRow(gfx, d.runs[row], d, row, frame);
}

static void drawActiveIndicators(const GithubData& d, uint32_t frame) {
  Arduino_GFX* gfx = gfxDev(); if (!gfx) return;
  uint8_t count = min<uint8_t>(3, d.runCount);
  for (uint8_t row = 0; row < count; row++) {
    const GithubRun& r = d.runs[row];
    if (!activeState(r.state)) continue;
    int y = 35 + row * 66;
    gfx->fillRect(9, y + 7, 21, 21, GH_BG);
    drawStateIcon(gfx, 19, y + 17, r.state, frame + row * 2);
    char right[9];
    formatElapsed(r, d, right, sizeof(right));
    gfx->fillRect(180, y + 5, 55, 20, GH_BG);
    gfx->setTextSize(2);
    gfx->setTextColor(stateColor(r.state));
    gfx->setCursor(232 - gfxTextW(right, 2), y + 7);
    gfx->print(right);
  }
}

void GithubMode::begin(const Settings& s) {
  githubInit(s);
  renderedAt_ = 0;
  nextAnimMs_ = millis();
  animationFrame_ = 0;
  needRender_ = true;
  lastStale_ = false;
}
void GithubMode::invalidate(const Settings& s) {
  githubInit(s);
  githubForceRefresh();
  nextAnimMs_ = millis();
  animationFrame_ = 0;
  needRender_ = true;
}
void GithubMode::wake(const Settings&) { needRender_ = true; }

void GithubMode::service(const Settings& s) {
  bool configured = s.github.statusUrl.length() >= 8;
  if (configured) githubService(s);
  const GithubData& d = githubGet();
  bool stale = d.valid && !githubFresh(((uint32_t)s.github.pollSec * 3UL + 10UL) * 1000UL);
  if (stale != lastStale_) { lastStale_ = stale; needRender_ = true; }
  if (d.revision != renderedAt_) { renderedAt_ = d.revision; needRender_ = true; }
  if ((int32_t)(millis() - nextAnimMs_) >= 0) {
    nextAnimMs_ = millis() + 200;
    animationFrame_++;
    if (!needRender_ && d.valid && d.runningCount) drawActiveIndicators(d, animationFrame_);
  }
  if (needRender_) { drawGithub(d, configured, stale, animationFrame_); needRender_ = false; }
}
