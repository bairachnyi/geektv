#include "GithubMode.h"
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "GithubClient.h"

GithubMode g_githubMode;

static const uint16_t GH_BG = 0x0208;
static const uint16_t GH_CARD = 0x08D2;
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

// Character-based marquee for the fixed-width built-in font. Long values pause
// briefly, move left through a three-space gap, then restart seamlessly.
static void marqueeCopy(const char* src, char* out, size_t n, size_t visibleChars, uint32_t frame) {
  if (!src) src = "";
  size_t len = strlen(src);
  if (len <= visibleChars) { strlcpy(out, src, n); return; }
  const uint8_t gap = 3;
  const uint8_t holdSteps = 6;
  size_t period = len + gap;
  size_t phase = (frame / 2) % (holdSteps + period);
  size_t start = phase < holdSteps ? 0 : phase - holdSteps;
  size_t count = min(visibleChars, n - 1);
  for (size_t i = 0; i < count; i++) {
    size_t at = (start + i) % period;
    out[i] = at < len ? src[at] : ' ';
  }
  out[count] = 0;
}

static const char* errorTitle(const char* code, bool stale) {
  if (stale) return "DATA STALE";
  if (!strcmp(code, "TOKEN_INVALID") || !strcmp(code, "TOKEN_DENIED") || !strcmp(code, "DEVICE_TOKEN_DENIED")) return "TOKEN DENIED";
  if (!strcmp(code, "REPOSITORY_NOT_FOUND")) return "REPO NOT FOUND";
  if (!strcmp(code, "NO_REPOSITORIES")) return "NO REPOS";
  if (!strcmp(code, "RATE_LIMITED")) return "RATE LIMITED";
  if (!strcmp(code, "GITHUB_TIMEOUT")) return "GITHUB TIMEOUT";
  if (!strcmp(code, "GITHUB_OFFLINE") || !strcmp(code, "BRIDGE_OFFLINE")) return "BRIDGE OFFLINE";
  if (!strcmp(code, "BAD_RESPONSE") || !strcmp(code, "GITHUB_BAD_RESPONSE")) return "BAD RESPONSE";
  if (!strcmp(code, "LOW_MEMORY")) return "LOW MEMORY";
  if (!strcmp(code, "FEED_URL_INVALID")) return "BAD FEED URL";
  return "GITHUB ERROR";
}

static bool activeState(uint8_t state) {
  return state == GH_RUNNING || state == GH_QUEUED;
}

static uint8_t visibleRunCount(const GithubData& d) {
  return d.runningCount ? d.runningCount : d.runCount;
}

static const GithubRun* visibleRunAt(const GithubData& d, uint8_t visibleIndex) {
  bool activeOnly = d.runningCount > 0;
  for (uint8_t i = 0, seen = 0; i < d.runCount; i++) {
    if (activeOnly && !activeState(d.runs[i].state)) continue;
    if (seen++ == visibleIndex) return &d.runs[i];
  }
  return nullptr;
}

static void drawHeader(Arduino_GFX* gfx, uint8_t page, uint8_t pages, bool priority) {
  gfx->setTextSize(2); gfx->setTextColor(C_WHITE); gfx->setCursor(7, 7); gfx->print("GH");
  gfx->setTextColor(GH_CYAN); gfx->print("//");
  gfx->setTextColor(C_WHITE); gfx->print("STAT");
  uint16_t liveColor = priority ? GH_AMBER : GH_CYAN;
  gfx->fillCircle(174, 14, 3, liveColor);
  gfx->setTextSize(1); gfx->setTextColor(liveColor); gfx->setCursor(181, 10); gfx->print(priority ? "FOCUS" : "LIVE");
  if (pages > 1) {
    gfx->setTextColor(GH_MUTED); gfx->setCursor(216, 10); gfx->print(page + 1); gfx->print('/'); gfx->print(pages);
  }
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

static void drawSummary(Arduino_GFX* gfx, int x, const char* label, uint8_t value, uint16_t color) {
  gfx->fillCircle(x, 44, 4, color);
  gfx->setTextSize(2); gfx->setTextColor(GH_MUTED); gfx->setCursor(x + 8, 36); gfx->print(label);
  gfx->setTextColor(C_WHITE); gfx->print(value);
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

static void drawGithub(const GithubData& d, uint8_t page, bool configured, bool stale, uint32_t frame) {
  Arduino_GFX* gfx = gfxDev(); if (!gfx) return;
  gfx->fillScreen(GH_BG);
  uint8_t visibleCount = visibleRunCount(d);
  uint8_t pages = (visibleCount + 1) / 2; if (!pages) pages = 1;
  drawHeader(gfx, page, pages, d.runningCount > 0);
  if (!configured || !d.valid || d.error || stale) { drawErrorScreen(gfx, d, configured, stale, frame); return; }

  drawSummary(gfx, 8, "R", d.runningCount, GH_CYAN);
  drawSummary(gfx, 83, "P", d.successCount, GH_GREEN);
  drawSummary(gfx, 164, "F", d.failureCount, GH_RED);

  if (!d.runCount) { gfxDrawCentered("No workflow runs", 125, 2, GH_MUTED); return; }

  const uint8_t perPage = 2;
  uint8_t start = page * perPage;
  for (uint8_t row = 0; row < perPage && start + row < visibleCount; row++) {
    const GithubRun* selected = visibleRunAt(d, start + row);
    if (!selected) continue;
    const GithubRun& r = *selected;
    int y = 55 + row * 90;
    uint16_t color = stateColor(r.state);
    gfx->fillRoundRect(4, y, 232, 84, 7, GH_CARD);
    gfx->drawRoundRect(4, y, 232, 84, 7, r.latest ? GH_CYAN : 0x1924);
    if (r.latest) gfx->drawRoundRect(6, y + 2, 228, 80, 6, 0x34BF);
    gfx->fillRoundRect(4, y + 7, 4, 70, 2, color);
    drawStateIcon(gfx, 19, y + 18, r.state, frame + row * 2);

    char repo[11], workflow[19], branch[19], right[9];
    marqueeCopy(shortRepo(r.repo), repo, sizeof(repo), 10, frame + row * 3);
    marqueeCopy(r.workflow, workflow, sizeof(workflow), 18, frame + row * 3);
    marqueeCopy(r.branch, branch, sizeof(branch), 18, frame + row * 3);
    if (r.state == GH_RUNNING || r.state == GH_QUEUED) formatElapsed(r, d, right, sizeof(right));
    else strlcpy(right, stateLabel(r.state), sizeof(right));

    gfx->setTextSize(2); gfx->setTextColor(C_WHITE); gfx->setCursor(38, y + 6); gfx->print(repo);
    gfx->setTextColor(color); gfx->setCursor(232 - gfxTextW(right, 2), y + 6); gfx->print(right);
    gfx->setTextColor(GH_MUTED); gfx->setCursor(12, y + 28); gfx->print(workflow);
    gfx->setTextColor(0xA534); gfx->setCursor(12, y + 48); gfx->print(branch);
    gfx->fillRoundRect(9, y + 65, 39, 16, 3, 0x1924);
    gfx->setTextColor(color); gfx->setCursor(11, y + 67); gfx->print(eventLabel(r.type));
    gfx->setTextColor(C_WHITE); gfx->setCursor(87, y + 67); gfx->print(r.when);
  }
}

void GithubMode::begin(const Settings& s) {
  githubInit(s);
  renderedAt_ = 0;
  page_ = 0;
  nextPageMs_ = millis() + (uint32_t)s.github.rotateSec * 1000UL;
  nextAnimMs_ = millis();
  animationFrame_ = 0;
  needRender_ = true;
  lastStale_ = false;
}
void GithubMode::invalidate(const Settings& s) {
  githubInit(s);
  githubForceRefresh();
  page_ = 0;
  nextPageMs_ = millis() + (uint32_t)s.github.rotateSec * 1000UL;
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
  uint8_t pages = (visibleRunCount(d) + 1) / 2;
  if (!pages) pages = 1;
  if (page_ >= pages) { page_ = 0; needRender_ = true; }
  if (pages > 1 && (int32_t)(millis() - nextPageMs_) >= 0) {
    page_ = (page_ + 1) % pages;
    nextPageMs_ = millis() + (uint32_t)s.github.rotateSec * 1000UL;
    needRender_ = true;
  }
  if (d.revision != renderedAt_) { renderedAt_ = d.revision; needRender_ = true; }
  if ((int32_t)(millis() - nextAnimMs_) >= 0) {
    nextAnimMs_ = millis() + 200;
    animationFrame_++;
    needRender_ = true;
  }
  if (needRender_) { drawGithub(d, page_, configured, stale, animationFrame_); needRender_ = false; }
}
