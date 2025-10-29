#include "app_keyboard.hpp"

#include "common_esp32.hpp"
#include "eye_term.hpp"

#include <NimBLEDevice.h>

// Defined at global scope in BleKeyboard.cpp
extern const uint8_t _asciimap[128];

namespace atmt {

static uint8_t BuildHIDModifiers(Modifier mods) {
  uint8_t result = 0;
  if (mods & MOD_CTRL)
    result |= 0x01; // Left Ctrl
  if (mods & MOD_SHIFT)
    result |= 0x02; // Left Shift
  if (mods & MOD_ALT)
    result |= 0x04; // Left Alt
  if (mods & MOD_SUPER)
    result |= 0x08; // Left GUI
  if (mods & MOD_RIGHT_ALT)
    result |= 0x40; // Right Alt
  return result;
}

static void SendAndRelease(BleKeyboard &kb, KeyReport &report) {
  kb.sendReport(&report);
  KeyReport empty = {};
  kb.sendReport(&empty);
}

void AppKeyboard::OnSetup() {
  keyboard_.setName("𝖒𝖆𝖋.🎹");
  keyboard_.begin();

  int num_bonds = NimBLEDevice::getNumBonds();
  Log("> %d bonded device(s):\n", num_bonds);
  for (int i = 0; i < num_bonds; i++) {
    auto addr = NimBLEDevice::getBondedAddress(i);
    Log(">   %d: %s\n", i, addr.toString().c_str());
  }
}

void AppKeyboard::OnLoop() { delay(1); }

void AppKeyboard::OnUnicode(uint32_t codepoint, Modifier mods) {
  if (!keyboard_.isConnected())
    return;

  // Polish characters via AltGr
  char base = GetOgonekBase(codepoint);
  if (base != 0) {
    mods |= MOD_RIGHT_ALT;
    codepoint = (uint8_t)base;
  }

  // Special control characters
  if (codepoint == '\n' || codepoint == '\r') {
    keyboard_.write(KEY_RETURN);
    return;
  }
  if (codepoint == '\t') {
    keyboard_.write(KEY_TAB);
    return;
  }
  if (codepoint == 0x1b) {
    keyboard_.write(KEY_ESC);
    return;
  }

  // ASCII 32-126
  if (codepoint >= 32 && codepoint <= 126) {
    uint8_t map_val = _asciimap[codepoint];
    if (map_val == 0)
      return;
    bool needs_shift = (map_val & 0x80) != 0;
    uint8_t keycode = map_val & 0x7F;

    Modifier effective_mods = mods;
    if (needs_shift)
      effective_mods |= MOD_SHIFT;

    KeyReport report = {};
    report.modifiers = BuildHIDModifiers(effective_mods);
    report.keys[0] = keycode;
    SendAndRelease(keyboard_, report);
    return;
  }

  // Non-ASCII: skip
}

void AppKeyboard::OnKey(IBM_Key key, Modifier mods) {
  if (!keyboard_.isConnected())
    return;

  uint8_t keycode = 0;
  switch (key) {
  case IBM_Key::BACKSPACE:
    keycode = 0x2A;
    break;
  case IBM_Key::DELETE:
    keycode = 0x4C;
    break;
  case IBM_Key::ENTER:
    keycode = 0x28;
    break;
  case IBM_Key::TAB:
    keycode = 0x2B;
    break;
  case IBM_Key::ESC:
    keycode = 0x29;
    break;
  case IBM_Key::UP_ARROW:
    keycode = 0x52;
    break;
  case IBM_Key::DOWN_ARROW:
    keycode = 0x51;
    break;
  case IBM_Key::LEFT_ARROW:
    keycode = 0x50;
    break;
  case IBM_Key::RIGHT_ARROW:
    keycode = 0x4F;
    break;
  case IBM_Key::HOME:
    keycode = 0x4A;
    break;
  case IBM_Key::END:
    keycode = 0x4D;
    break;
  case IBM_Key::PAGE_UP:
    keycode = 0x4B;
    break;
  case IBM_Key::PAGE_DOWN:
    keycode = 0x4E;
    break;
  case IBM_Key::INSERT:
    keycode = 0x49;
    break;
  default:
    return;
  }

  KeyReport report = {};
  report.modifiers = BuildHIDModifiers(mods);
  report.keys[0] = keycode;
  SendAndRelease(keyboard_, report);
}

void AppKeyboard::OnBattery(int percent) { keyboard_.setBatteryLevel(percent); }

} // namespace atmt
