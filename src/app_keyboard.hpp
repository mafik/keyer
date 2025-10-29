#pragma once

#include "BleKeyboard.h"
#include "app.hpp"

namespace atmt {

struct AppKeyboard : App {
  BleKeyboard keyboard_{"maf.klaw"};

  void OnSetup() override;
  void OnLoop() override;
  void OnUnicode(uint32_t codepoint, Modifier mods) override;
  void OnKey(IBM_Key key, Modifier mods) override;
  void OnBattery(int percent) override;
};

} // namespace atmt
