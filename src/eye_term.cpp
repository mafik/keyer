#include "eye_term.hpp"

#include "app_keyboard.hpp"
#include "common_esp32.hpp"
#include "forth.hpp"
#include "keyer.hpp"
#include "main_loop.hpp"
#include "typist.hpp"

namespace atmt {

void LogAndDeleteOnMain(uint64_t str_ptr) {
  auto str = std::unique_ptr<string>(reinterpret_cast<string *>(str_ptr));
  Debug(str->c_str());
}

} // namespace atmt

void setup() {
  InitESP32();

  const char *reset_reasons[] = {
      "UNKNOWN",  "POWERON", "EXT",        "SW",         "PANIC", "INT_WDT",
      "TASK_WDT", "WDT",     "DEEPSLEEP",  "BROWNOUT",   "SDIO",  "USB",
      "JTAG",     "EFUSE",   "PWR_GLITCH", "CPU_LOCKUP",
  };
  auto reason = esp_reset_reason();
  Debugf("Reset reason: %d (%s)\n", reason,
         reason < sizeof(reset_reasons) / sizeof(reset_reasons[0])
             ? reset_reasons[reason]
             : "?");

  Log("> maf.klaw booting up...\n");
  DebugHeap("after-InitESP32");
  InitMainLoop();
  DebugHeap("after-InitMainLoop");
  InitKeyer();
  DebugHeap("after-InitKeyer");
  ble_keyboard.Setup();
  DebugHeap("after-BLE-Setup");
  InitTypist();
  DebugHeap("after-InitTypist");
  ForthInit();
  DebugHeap("after-ForthInit");
}

void loop() {
  while (atmt::MainLoopNonBlocking()) {
  }
  ble_keyboard.Loop();
}
