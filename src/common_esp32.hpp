// Adds ESP32-specific defines on top of common.hpp
#pragma once

#include <Arduino.h>           // IWYU pragma: export
#include <WiFi.h>              // IWYU pragma: export
#include <freertos/FreeRTOS.h> // IWYU pragma: export
#include <mutex>

#include "common.hpp" // IWYU pragma: export

namespace atmt {

// Change to true to enable serial debug output
//
// The default is false because when device is not connected to a computer but
// is printing to the serial port, it causes the device to become laggy (weird).
constexpr bool kDebug = true;

extern bool debug_line_start;

extern std::mutex serial_mtx;

inline void _AdjustDebugLineStart(char c) { debug_line_start = c == '\n'; }
inline void _AdjustDebugLineStart(const char *c) {
  _AdjustDebugLineStart(c[strlen(c) - 1]);
}

template <typename... Args> void Debugf(const char *format, Args... args) {
  if constexpr (kDebug) {
    if (!Serial.isConnected())
      return;
    auto guard = std::lock_guard(serial_mtx);
    if (debug_line_start) {
      auto millis = esp_timer_get_time() / 1000;
      Serial.printf("%d| %3lld.%03lld ", xPortGetCoreID(), millis / 1000,
                    millis % 1000);
    }
    Serial.printf(format, args...);
    _AdjustDebugLineStart(format);
  } else {
    (void)format;
  }
}

inline void Debugln(const char *line) {
  if constexpr (kDebug) {
    if (!Serial.isConnected())
      return;
    auto guard = std::lock_guard(serial_mtx);
    if (debug_line_start) {
      auto millis = esp_timer_get_time() / 1000;
      Serial.printf("%d| %3lld.%03lld ", xPortGetCoreID(), millis / 1000,
                    millis % 1000);
    }
    Serial.println(line);
    debug_line_start = true;
  }
}

template <typename T> inline void Debug(T x) {
  if constexpr (kDebug) {
    if (!Serial.isConnected())
      return;
    auto guard = std::lock_guard(serial_mtx);
    if (debug_line_start) {
      auto millis = esp_timer_get_time() / 1000;
      Serial.printf("%d| %3lld.%03lld ", xPortGetCoreID(), millis / 1000,
                    millis % 1000);
    }
    Serial.print(x);
    _AdjustDebugLineStart(x);
  } else {
    (void)x;
  }
}

// GPIO pins for buttons
using GPIO_Pin = uint8_t;

// Lilygo T-Energy S3 uses pin 3 for battery voltage reading
const GPIO_Pin BATTERY_PIN = 3;

void InitESP32();

} // namespace atmt
