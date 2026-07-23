// UsageClient.h — pulls AI usage from a trusted local bridge.
#pragma once
#include "Settings.h"
#include "UsageData.h"

void usageInit(const Settings& s);
void usageService(const Settings& s);     // call each loop; fetches on the poll schedule
void usageForceRefresh();                 // poll again on the next service() call
const UsageData& usageGet();
bool usageFresh(uint32_t withinMs);       // true if the last good update is recent enough

// Apply a payload PUSHED to the device. The old /api/usage route stays compatible.
bool usageApply(const String& body);      // parse {a,ar,c,cr,st,ok}; true on success
