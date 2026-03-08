#pragma once

#include <cstdint>

#include "common.hpp"

namespace atmt {

using Modifier = uint8_t;
constexpr Modifier MOD_SHIFT = 0x02, MOD_ALT = 0x04, MOD_CTRL = 0x01,
                   MOD_SUPER = 0x08, MOD_RIGHT_CTRL = 0x10,
                   MOD_RIGHT_SHIFT = 0x20, MOD_RIGHT_ALT = 0x40,
                   MOD_RIGHT_SUPER = 0x80;

enum class HID_Key {
  NONE,

  LETTER_A = 0x04,
  LETTER_B = 0x05,
  LETTER_C = 0x06,
  LETTER_D = 0x07,
  LETTER_E = 0x08,
  LETTER_F = 0x09,
  LETTER_G = 0x0A,
  LETTER_H = 0x0B,
  LETTER_I = 0x0C,
  LETTER_J = 0x0D,
  LETTER_K = 0x0E,
  LETTER_L = 0x0F,
  LETTER_M = 0x10,
  LETTER_N = 0x11,
  LETTER_O = 0x12,
  LETTER_P = 0x13,
  LETTER_Q = 0x14,
  LETTER_R = 0x15,
  LETTER_S = 0x16,
  LETTER_T = 0x17,
  LETTER_U = 0x18,
  LETTER_V = 0x19,
  LETTER_W = 0x1A,
  LETTER_X = 0x1B,
  LETTER_Y = 0x1C,
  LETTER_Z = 0x1D,
  NUMBER_1 = 0x1E,
  NUMBER_2 = 0x1F,
  NUMBER_3 = 0x20,
  NUMBER_4 = 0x21,
  NUMBER_5 = 0x22,
  NUMBER_6 = 0x23,
  NUMBER_7 = 0x24,
  NUMBER_8 = 0x25,
  NUMBER_9 = 0x26,
  NUMBER_0 = 0x27,
  ENTER = 0x28,
  ESC = 0x29,
  BACKSPACE = 0x2A,
  TAB = 0x2B,
  SPACE = 0x2C,
  MINUS = 0x2D,
  EQUALS = 0x2E,
  LEFT_BRACKET = 0x2F,
  RIGHT_BRACKET = 0x30,
  BACKSLASH = 0x31,
  // ???
  SEMICOLON = 0x33,
  APOSTROPHE = 0x34,
  BACKTICK = 0x35,
  COMMA = 0x36,
  PERIOD = 0x37,
  SLASH = 0x38,
  CAPS_LOCK = 0x39,
  F1 = 0x3A,
  F2 = 0x3B,
  F3 = 0x3C,
  F4 = 0x3D,
  F5 = 0x3E,
  F6 = 0x3F,
  F7 = 0x40,
  F8 = 0x41,
  F9 = 0x42,
  F10 = 0x43,
  F11 = 0x44,
  F12 = 0x45,

  PRINT_SCREEN = 0x46,
  SCROLL_LOCK = 0x47,
  PAUSE_BREAK = 0x48,

  INSERT = 0x49,
  HOME = 0x4A,
  PAGE_UP = 0x4B,
  DELETE = 0x4C,
  END = 0x4D,
  PAGE_DOWN = 0x4E,

  // Arrows:
  RIGHT_ARROW = 0x4F,
  LEFT_ARROW = 0x50,
  DOWN_ARROW = 0x51,
  UP_ARROW = 0x52,

  // Modifiers:
  LEFT_CTRL = 0xE0,
  LEFT_SHIFT = 0xE1,
  LEFT_ALT = 0xE2,
  LEFT_SUPER = 0xE3,
  RIGHT_CTRL = 0xE4,
  RIGHT_SHIFT = 0xE5,
  RIGHT_ALT = 0xE6,
  RIGHT_SUPER = 0xE7,
};

// An terminal input sequence. For simple characters (like ^A or 'a') it can be
// a single byte. For longer various key combinations it can use ANSI terminal
// escape sequences (like \e[97;13u ).
using TerminalSequence = string;

// Converts the given unicode code point & given modifiers into a sequence that
// can be sent over SSH.
string TerminalSequenceFromUnicode(int codepoint, Modifier mod_mask);

// Converts the given IBM_Key & given modifiers into a sequence that
// can be sent over SSH.
string TerminalSequenceFromIBM_Key(HID_Key key, Modifier mod_mask);

const char *ToStr(HID_Key);

// Returns the base ASCII character for a Polish ogonek codepoint,
// or 0 if codepoint is not a Polish character.
char GetOgonekBase(uint32_t codepoint);

// Apply US keyboard shift to an ASCII codepoint (e.g. '=' → '+', 'a' → 'A').
// Returns the character unchanged if it has no shifted form.
uint32_t ApplyShift(uint32_t codepoint);

} // namespace atmt
