#pragma once
#include "Mode.h"
#include "config.h"

class CodexMode : public DisplayMode {
 public:
  const char* id() const override { return "codex"; }
  uint8_t modeConst() const override { return MODE_CODEX; }

  void begin(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override;
  void service(const Settings& s) override;

  void render(const Settings& s);

 private:
  uint8_t  m_curPage = 0;
  uint32_t m_lastRotateMs = 0;
  uint32_t m_renderedLastOk = 0xFFFFFFFF;
  bool     m_renderedError = false;
  bool     m_needRender = true;
};

extern CodexMode g_codexMode;
