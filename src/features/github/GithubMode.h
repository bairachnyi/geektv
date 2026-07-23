#pragma once
#include "Mode.h"

class GithubMode : public DisplayMode {
 public:
  const char* id() const override { return "github"; }
  uint8_t modeConst() const override { return MODE_GITHUB; }
  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override;
 private:
  uint32_t renderedAt_ = 0;
  uint32_t nextPageMs_ = 0;
  uint32_t nextAnimMs_ = 0;
  uint8_t page_ = 0;
  uint32_t animationFrame_ = 0;
  bool needRender_ = true;
  bool lastStale_ = false;
};

extern GithubMode g_githubMode;
