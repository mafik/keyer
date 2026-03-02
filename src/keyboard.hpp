#pragma once

#include <cstdint>

#include "common.hpp"

namespace atmt {

using Modifier = uint32_t;
constexpr Modifier MOD_SHIFT = 1, MOD_ALT = 2, MOD_CTRL = 4, MOD_SUPER = 8,
                   MOD_RIGHT_ALT = 16;

enum class IBM_Key {
  NONE,
  ESC,
  F1,
  F2,
  F3,
  F4,
  F5,
  F6,
  F7,
  F8,
  F9,
  F10,
  F11,
  F12,
  PRINT_SCREEN,
  SCROLL_LOCK,
  PAUSE_BREAK,
  BACKTICK,
  INSERT,
  DELETE,
  BACKSPACE,
  TAB,
  ENTER,
  UP_ARROW,
  RIGHT_ARROW,
  DOWN_ARROW,
  LEFT_ARROW,
  PAGE_UP,
  PAGE_DOWN,
  HOME,
  END,
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
string TerminalSequenceFromIBM_Key(IBM_Key key, Modifier mod_mask);

const char *ToStr(IBM_Key);

// Returns the base ASCII character for a Polish ogonek codepoint,
// or 0 if codepoint is not a Polish character.
char GetOgonekBase(uint32_t codepoint);

} // namespace atmt
