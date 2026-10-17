#pragma once
// Device facts that differ between the ESP32 and the desktop preview (preview.yaml, host).
#include "esphome/core/defines.h"
#include <cstdint>
#ifdef USE_ESP32
#include <esp_heap_caps.h>
#include <esp_system.h>
#endif
namespace cyd_device {
static uint32_t started_at = 0;
inline void begin() { started_at = millis(); }
inline uint32_t uptime() {
#ifdef USE_ESP32
  return millis() / 1000;
#else
  return (millis() - started_at) / 1000;
#endif
}
#ifdef USE_ESP32
inline const char *reset_name(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "reset-pin";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int-wdt";
    case ESP_RST_TASK_WDT: return "task-wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    default: return "unknown";
  }
}
inline const char *reset_name() { return reset_name(esp_reset_reason()); }
// The same numbers as Arduino's ESP.getFreeHeap() and ESP.getMinFreeHeap().
inline uint32_t free_heap() { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL); }
inline uint32_t min_free_heap() { return heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL); }
#else
inline const char *reset_name() { return "preview"; }
inline uint32_t free_heap() { return 0; }
inline uint32_t min_free_heap() { return 0; }
#endif
}  // namespace cyd_device
