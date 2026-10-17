#pragma once
// ESPHome copies version/ alongside main.cpp. Source archives build without git.
#if __has_include("version/generated.h")
#include "version/generated.h"
#endif
#ifndef CYD_GIT_REV
#define CYD_GIT_REV "unknown"
#endif
