#include "app.hpp"

#include <Preferences.h>
#include <esp_system.h>
#include <memory>

#include "app_keyboard.hpp"
#include "app_terminal.hpp"

namespace atmt {

std::unique_ptr<App> current_app;

std::unique_ptr<App> App::Load() {
  Preferences prefs;
  prefs.begin("keyer", true);
  auto app_int = prefs.getInt("app", 0); // default 0 = kKeyboard
  prefs.end();
  switch (Kind(app_int)) {
  case Kind::kKeyboard:
    Debugln("Loading Keyboard app...");
    return std::make_unique<AppKeyboard>();
  case Kind::kTerminal:
    Debugln("Loading Terminal app...");
    return std::make_unique<AppTerminal>();
  }
  Debugf("Unknown app kind: %d. Falling back to Keyboard...\n", app_int);
  return std::make_unique<AppKeyboard>();
}

void App::SaveAndRestart(Kind kind) {
  Preferences prefs;
  prefs.begin("keyer", false);
  prefs.putInt("app", kind == Kind::kTerminal ? 1 : 0);
  prefs.end();
  esp_restart();
}

} // namespace atmt
