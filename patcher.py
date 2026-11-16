import re

with open('src/features/clock/ClockMode.cpp', 'r') as f:
    c = f.read()

# 1. Remove github variables in begin/invalidate/wake
c = re.sub(
    r'm_lastGithubRevision = UINT32_MAX;\s*m_nextGithubFrameMs = 0;\s*m_githubFrame = 0;',
    r'm_nextSysUpdateMs = 0;',
    c
)

# 2. Replace service github tracking with system update
c = re.sub(
    r'if \(s\.github\.statusUrl\.length\(\) >= 8\) githubService\(s\);\s*const GithubData& github = githubGet\(\);\s*bool githubChanged = github\.revision != m_lastGithubRevision;\s*if \(s\.clock\.theme == 0 &&\s*\(githubChanged \|\| \(int32_t\)\(nowMs - m_nextGithubFrameMs\) >= 0\)\) \{\s*m_lastGithubRevision = github\.revision;\s*m_githubFrame\+\+;\s*renderGithubSummary\(s\);\s*m_nextGithubFrameMs = nowMs \+ 1000UL;\s*\}',
    r'if (s.clock.theme == 0 && (int32_t)(nowMs - m_nextSysUpdateMs) >= 0) {\n    renderSystemSummary(s);\n    m_nextSysUpdateMs = nowMs + 10000UL;\n  }',
    c
)

# 3. Replace renderGithubSummary implementation
c = re.sub(
    r'void ClockMode::renderGithubSummary\(const Settings& s\) \{[\s\S]*?gfxPrint\(126, y - 8, repo, UI_WHITE, 2\);\s*\}\s*\}',
    r'''void ClockMode::renderSystemSummary(const Settings& s) {
  if (s.clock.theme != 0) return;
  Arduino_GFX* g = gfxDev();
  if (!g) return;
  g->fillRect(104, 176, 136, 64, s.clock.bgColor);

  char wifiStr[16];
  if (netConnected()) snprintf(wifiStr, sizeof(wifiStr), "WIFI %d", netRSSI());
  else strlcpy(wifiStr, "OFFLINE", sizeof(wifiStr));
  gfxPrint(112, 194, wifiStr, netConnected() ? s.clock.accentColor : UI_MUTED, 2);

  uint32_t up = millis() / 1000;
  uint32_t d = up / 86400, h = (up % 86400) / 3600, m = (up % 3600) / 60;
  char upStr[16];
  if (d > 0) snprintf(upStr, sizeof(upStr), "UP %ud %02uh", (unsigned)d, (unsigned)h);
  else snprintf(upStr, sizeof(upStr), "UP %uh %02um", (unsigned)h, (unsigned)m);
  gfxPrint(112, 218, upStr, s.clock.dateColor, 2);
}''',
    c
)

# 4. Replace github render initialization
c = re.sub(
    r'm_lastGithubRevision = UINT32_MAX;\s*m_githubFrame = 0;\s*renderGithubSummary\(s\);\s*m_nextGithubFrameMs = millis\(\) \+ 1000UL;',
    r'renderSystemSummary(s);\n    m_nextSysUpdateMs = millis() + 10000UL;',
    c
)

# 5. Remove unused functions
c = re.sub(
    r'static const char\* compactRepo\(const char\* repo\) \{[\s\S]*?static void drawCompactGithubIcon\(Arduino_GFX\* g, int x, int y, const GithubRun& run, uint16_t frame\) \{[\s\S]*?g->fillRect\(x, y, 6, 6, c\);\s*\}',
    r'',
    c
)

with open('src/features/clock/ClockMode.cpp', 'w') as f:
    f.write(c)

