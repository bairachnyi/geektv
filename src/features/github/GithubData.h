#pragma once
#include <Arduino.h>
#include "config.h"

enum GithubRunState : uint8_t {
  GH_UNKNOWN = 0, GH_QUEUED, GH_RUNNING, GH_SUCCESS, GH_FAILURE, GH_CANCELLED
};

enum GithubEventType : uint8_t {
  GH_EVENT_ACTION = 0, GH_EVENT_DEPLOYMENT, GH_EVENT_PULL_REQUEST, GH_EVENT_RELEASE
};

struct GithubRun {
  char repo[64];
  char workflow[48];
  char branch[40];
  char when[13];
  uint8_t state;
  uint8_t type;
  uint32_t ageSec;
  bool latest;
};

struct GithubData {
  GithubRun runs[MAX_GITHUB_RUNS];
  uint8_t runCount;
  uint8_t runningCount;
  uint8_t successCount;
  uint8_t failureCount;
  bool valid;
  bool error;
  uint32_t lastOkMs;
  uint32_t revision;
  char message[48];
  char errorCode[24];
  char errorMessage[64];
  char errorSource[16];
  char errorRepo[34];

  void clear() {
    memset(this, 0, sizeof(*this));
  }
};
