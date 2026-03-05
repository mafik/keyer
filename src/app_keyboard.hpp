#pragma once

#include "BleKeyboard.h"
#include "keyboard.hpp"

namespace atmt {

struct AppKeyboard {
  BleKeyboard keyboard_{"maf.klaw"};

  void Setup();
  void Loop();
  void SendUnicode(uint32_t codepoint, Modifier mods);
  void SendKey(IBM_Key key, Modifier mods);
  void SetBattery(int percent);
  bool IsConnected();
};

extern AppKeyboard ble_keyboard;

} // namespace atmt
