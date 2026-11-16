const fs = require('fs');
let content = fs.readFileSync('src/features/clock/ClockMode.cpp', 'utf8');

// 1. Remove GH functions
content = content.replace(/static const char\* compactRepo[\s\S]*?drawCompactGithubIcon[\s\S]*?\}\n/g, '');

// 2. Fix begin/invalidate/wake
content = content.replace(/m_lastGithubRevision = UINT32_MAX;\s*m_nextGithubFrameMs = 0;\s*m_githubFrame = 0;/g, 'm_nextSysUpdateMs = 0;');

// 3. Fix service loop
content = content.replace(/if \(s\.github\.statusUrl\.length\(\) >= 8\) githubService\(s\);[\s\S]*?m_nextGithubFrameMs = nowMs \+ 1000UL;\s*\}/, 
`if (s.clock.theme == 0 && (int32_t)(nowMs - m_nextSysUpdateMs) >= 0) {
    renderSystemSummary(s);
    m_nextSysUpdateMs = nowMs + 10000UL;
  }`);

// 4. Replace renderGithubSummary
content = content.replace(/void ClockMode::renderGithubSummary\([\s\S]*?\n\}\n/g, 
`void ClockMode::renderSystemSummary(const Settings& s) {
  if (s.clock.theme != 0) return;
  Arduino_GFX* g = gfxDev();
  if (!g) return;
  g->fillRect(104, 176, 136, 64, s.clock.bgColor);

  // WiFi
  char wifiStr[16];
  if (netConnected()) snprintf(wifiStr, sizeof(wifiStr), "WIFI %d", netRSSI());
  else strlcpy(wifiStr, "OFFLINE", sizeof(wifiStr));
  gfxPrint(112, 194, wifiStr, netConnected() ? s.clock.accentColor : UI_MUTED, 2);

  // Uptime
  uint32_t up = millis() / 1000;
  uint32_t d = up / 86400;
  uint32_t h = (up % 86400) / 3600;
  uint32_t m = (up % 3600) / 60;
  char upStr[16];
  if (d > 0) snprintf(upStr, sizeof(upStr), "UP %ud %02uh", (unsigned)d, (unsigned)h);
  else snprintf(upStr, sizeof(upStr), "UP %uh %02um", (unsigned)h, (unsigned)m);
  gfxPrint(112, 218, upStr, s.clock.dateColor, 2);
}
`);

// 5. Replace in render()
content = content.replace(/m_lastGithubRevision = UINT32_MAX;\s*m_githubFrame = 0;\s*renderGithubSummary\(s\);\s*m_nextGithubFrameMs = millis\(\) \+ 1000UL;/g, 
`renderSystemSummary(s);
    m_nextSysUpdateMs = millis() + 10000UL;`);

fs.writeFileSync('src/features/clock/ClockMode.cpp', content);
