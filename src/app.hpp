#pragma once

#include <memory>

#include "common_esp32.hpp"
#include "keyboard.hpp"

namespace atmt {

struct App {
  enum class Kind { kKeyboard, kTerminal };
  virtual void OnSetup() = 0;
  virtual void OnLoop() = 0;
  virtual void OnUnicode(uint32_t codepoint, Modifier mods) = 0;
  virtual void OnKey(IBM_Key key, Modifier mods) = 0;
  virtual void OnBattery(int percent) {}
  virtual void ShowText(string_view text) {}
  virtual ~App() = default;

  static std::unique_ptr<App> Load();
  static void SaveAndRestart(Kind);
};

extern std::unique_ptr<App> current_app;

} // namespace atmt
