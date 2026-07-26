#pragma once
#include "CodexData.h"
#include "Settings.h"

void codexInit(const Settings& s);
void codexForceRefresh();
void codexService(const Settings& s);
const CodexData& codexGet();
bool codexFresh(uint32_t withinMs = 1200000);
bool codexApply(const String& jsonBody);
