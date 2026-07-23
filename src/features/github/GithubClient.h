#pragma once
#include "Settings.h"
#include "GithubData.h"

void githubInit(const Settings& s);
void githubService(const Settings& s);
void githubForceRefresh();
const GithubData& githubGet();
bool githubFresh(uint32_t withinMs);
