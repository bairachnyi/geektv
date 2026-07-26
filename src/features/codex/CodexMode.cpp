#include "CodexMode.h"
#include "CodexClient.h"
#include "Gfx.h"
#include "Net.h"
#include "MontserratBold.h"
#include <Arduino_GFX_Library.h>

CodexMode g_codexMode;

static void drawMontserratCentered(const char* txt, int yCenter, const GFXfont* font, uint16_t color) {
  Arduino_GFX* g = gfxDev();
  if (!g || !txt || !txt[0]) return;

  g->setFont((GFXfont*)font);
  g->setTextColor(color);

  int16_t x1 = 0, y1 = 0;
  uint16_t w = 0, h = 0;
  g->getTextBounds((char*)txt, 0, 0, &x1, &y1, &w, &h);

  int x = (240 - (int)w) / 2;
  if (x < 0) x = 0;

  int yBase = yCenter + (int)h / 2 - (int)y1 / 2;
  g->setCursor(x, yBase);
  g->print(txt);
}

static void fmtTokens(uint32_t tokens, char* out, size_t n) {
  if (tokens >= 1000000) {
    snprintf(out, n, "%.2fM", (float)tokens / 1000000.0f);
  } else if (tokens >= 1000) {
    snprintf(out, n, "%.1fK", (float)tokens / 1000.0f);
  } else {
    snprintf(out, n, "%lu", (unsigned long)tokens);
  }
}

void CodexMode::begin(const Settings& s) {
  codexInit(s);
  m_curPage = 0;
  m_lastRotateMs = millis();
  m_renderedLastOk = 0xFFFFFFFF;
  m_renderedError = false;
  m_needRender = true;
}

void CodexMode::invalidate(const Settings& s) {
  (void)s;
  codexForceRefresh();
  m_renderedLastOk = 0xFFFFFFFF;
  m_needRender = true;
}

void CodexMode::wake(const Settings& s) {
  m_lastRotateMs = millis();
  m_needRender = true;
}

void CodexMode::service(const Settings& s) {
  if (s.mode != MODE_CODEX && s.mode != MODE_CAROUSEL) return;

  codexService(s);

  const CodexData& d = codexGet();
  if (d.lastOkMs != m_renderedLastOk || d.error != m_renderedError) {
    m_needRender = true;
    m_renderedLastOk = d.lastOkMs;
    m_renderedError = d.error;
  }

  // Rotate between 3 screens every rotateSec
  uint16_t rotSec = s.codex.rotateSec > 0 ? s.codex.rotateSec : 8;
  if ((int32_t)(millis() - m_lastRotateMs) >= (int32_t)(rotSec * 1000UL)) {
    m_lastRotateMs = millis();
    m_curPage = (m_curPage + 1) % 3;
    m_needRender = true;
  }

  if (m_needRender) {
    m_needRender = false;
    render(s);
  }
}

void CodexMode::render(const Settings& s) {
  Arduino_GFX* g = gfxDev();
  if (!g) return;

  g->setFont(nullptr);
  g->fillScreen(C_BLACK);

  const CodexData& d = codexGet();

  uint32_t staleMs = (uint32_t)s.codex.pollSec * 3000UL;
  if (staleMs < 120000UL) staleMs = 120000UL;
  bool stale = d.valid && !codexFresh(staleMs);
  if (!d.valid || (d.error && stale)) {
    gfxDrawCentered("CODEX TRACKER", 70, 3, 0x1FE0); // Cyan
    if (d.error) {
      gfxDrawCentered(stale ? "DATA STALE" : "SYNC ERROR", 116, 2, C_RED);
      gfxDrawCentered(d.errorCode[0] ? d.errorCode : "FETCH FAILED", 145, 1, C_YELLOW);
      gfxDrawCentered(d.errorMessage[0] ? d.errorMessage : "Open Codex settings", 164, 1, C_GRAY);
    } else {
      gfxDrawCentered("Connecting...", 120, 2, C_YELLOW);
      gfxDrawCentered(netIP().c_str(), 160, 2, C_WHITE);
    }
    return;
  }

  uint16_t cyan   = 0x1FE0;
  uint16_t green  = 0x3E00;
  uint16_t orange = 0xFD20;
  uint16_t pink   = 0xF81F;

  if (m_curPage == 0) {
    // Page 0: Codex Primary & Secondary Quota Window
    gfxFillRoundRect(6, 6, 228, 228, 12, 0x0842);
    gfxDrawRoundRect(6, 6, 228, 228, 12, cyan);

    gfxDrawCentered("CODEX QUOTA", 18, 2, cyan);

    char planBuf[32];
    snprintf(planBuf, sizeof(planBuf), "%s PLAN", d.planType[0] ? d.planType : "PLUS");
    gfxDrawCentered(planBuf, 42, 1, C_GRAY);

    char pctStr[16];
    snprintf(pctStr, sizeof(pctStr), "%.0f%%", d.primaryPct);
    drawMontserratCentered(pctStr, 95, &MontserratBold40pt7b, d.primaryPct > 30 ? green : orange);
    gfxDrawCentered("REMAINING", 132, 1, C_WHITE);

    // Primary quota progress bar
    int barW = (int)(d.primaryPct * 200.0f / 100.0f);
    if (barW < 0) barW = 0;
    if (barW > 200) barW = 200;
    gfxFillRoundRect(20, 146, 200, 14, 7, 0x2104);
    if (barW > 0) gfxFillRoundRect(20, 146, barW, 14, 7, d.primaryPct > 30 ? green : orange);

    if (d.primaryReset[0]) {
      char rstBuf[32];
      snprintf(rstBuf, sizeof(rstBuf), "Resets: %s", d.primaryReset);
      gfxDrawCentered(rstBuf, 168, 1, C_YELLOW);
    }

    // Secondary window if present
    char secBuf[40];
    snprintf(secBuf, sizeof(secBuf), "5h Quota: %.0f%% avail", d.secondaryPct);
    gfxDrawCentered(secBuf, 196, 1, C_GRAY);

  } else if (m_curPage == 1) {
    // Page 1: Today Usage Breakdown
    gfxFillRoundRect(6, 6, 228, 228, 12, 0x1084);
    gfxDrawRoundRect(6, 6, 228, 228, 12, green);

    gfxDrawCentered("TODAY CODEX", 18, 2, green);

    char tokBuf[20];
    fmtTokens(d.todayTokens, tokBuf, sizeof(tokBuf));
    drawMontserratCentered(tokBuf, 80, &MontserratBold40pt7b, C_WHITE);
    gfxDrawCentered("TOKENS USED", 112, 1, C_GRAY);

    char callsBuf[32];
    snprintf(callsBuf, sizeof(callsBuf), "%lu API Calls Today", (unsigned long)d.todayCalls);
    gfxDrawCentered(callsBuf, 134, 2, C_YELLOW);

    // Token breakdown: Input / Output / Cached
    gfxDrawCentered("Breakdown (In/Out/Cache):", 168, 1, C_GRAY);
    char inBuf[12], outBuf[12], cacheBuf[12];
    fmtTokens(d.todayInput, inBuf, sizeof(inBuf));
    fmtTokens(d.todayOutput, outBuf, sizeof(outBuf));
    fmtTokens(d.todayCached, cacheBuf, sizeof(cacheBuf));

    char mixBuf[48];
    snprintf(mixBuf, sizeof(mixBuf), "In:%s Out:%s Cch:%s", inBuf, outBuf, cacheBuf);
    gfxDrawCentered(mixBuf, 192, 1, cyan);

  } else {
    // Page 2: Weekly & Models Breakdown
    gfxFillRoundRect(6, 6, 228, 228, 12, 0x0010);
    gfxDrawRoundRect(6, 6, 228, 228, 12, pink);

    gfxDrawCentered("7-DAY & MODELS", 18, 2, pink);

    char wBuf[20];
    fmtTokens(d.weekTokens, wBuf, sizeof(wBuf));
    drawMontserratCentered(wBuf, 75, &MontserratBold28pt7b, C_WHITE);
    gfxDrawCentered("7-Day Rolling Tokens", 98, 1, C_GRAY);

    g->drawFastHLine(20, 118, 200, 0x31A6);

    gfxPrint(20, 128, "TOP MODELS", cyan, 1);

    if (d.model1[0]) {
      char m1Buf[16];
      fmtTokens(d.model1Tokens, m1Buf, sizeof(m1Buf));
      gfxPrint(20, 150, d.model1, C_WHITE, 1);
      gfxPrint(160, 150, m1Buf, green, 1);
    } else gfxPrint(20, 150, "No model data", C_GRAY, 1);

    if (d.model2[0]) {
      char m2Buf[16];
      fmtTokens(d.model2Tokens, m2Buf, sizeof(m2Buf));
      gfxPrint(20, 180, d.model2, C_WHITE, 1);
      gfxPrint(160, 180, m2Buf, green, 1);
    }

    gfxDrawCentered("GeekTV Codex Tracker", 212, 1, C_GRAY);
  }

  g->setFont(nullptr);
}
