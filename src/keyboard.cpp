#include "keyboard.hpp"
#include <sstream>

namespace atmt {

// Helper to format CSI u sequence with modifiers
static string CSI_u_sequence(int codepoint, Modifier mod_mask) {
  std::ostringstream oss;
  oss << "\x1b[" << codepoint;
  if (mod_mask != 0) {
    // Convert to 1-based modifier parameter
    int mod_param = 1;
    if (mod_mask & MOD_SHIFT)
      mod_param += 1;
    if (mod_mask & MOD_ALT)
      mod_param += 2;
    if (mod_mask & MOD_CTRL)
      mod_param += 4;
    if (mod_mask & MOD_SUPER)
      mod_param += 8;
    oss << ";" << mod_param;
  }
  oss << "u";
  return oss.str();
}

string TerminalSequenceFromUnicode(int codepoint, Modifier mod_mask) {
  // Resolve shift for printable ASCII — most terminals don't support CSI u
  if ((mod_mask & MOD_SHIFT) && codepoint >= 32 && codepoint <= 126) {
    uint32_t shifted = ApplyShift(codepoint);
    if (shifted != (uint32_t)codepoint) {
      codepoint = shifted;
      mod_mask &= ~MOD_SHIFT;
    }
  }

  // Handle simple ASCII characters without modifiers
  if (mod_mask == 0 && codepoint >= 32 && codepoint <= 126) {
    return string(1, static_cast<char>(codepoint));
  }

  // Handle control characters with Ctrl modifier
  if ((mod_mask == MOD_CTRL) && codepoint >= 'a' && codepoint <= 'z') {
    // Ctrl+letter produces control codes (Ctrl+A = 0x01, etc.)
    return string(1, static_cast<char>(codepoint - 'a' + 1));
  }
  if ((mod_mask == MOD_CTRL) && codepoint >= 'A' && codepoint <= 'Z') {
    return string(1, static_cast<char>(codepoint - 'A' + 1));
  }

  // Handle special control characters
  if (mod_mask == 0) {
    switch (codepoint) {
    case '\n':
    case '\r':
      return "\r";
    case '\t':
      return "\t";
    case 0x1b: // ESC
      return "\x1b";
    case 0x7f: // DEL
      return "\x7f";
    }
  }

  // Send Unicode codepoints as UTF-8
  if (mod_mask == 0 && codepoint >= 0x80) {
    char buf[4];
    int len;
    if (codepoint < 0x800) {
      buf[0] = 0xC0 | (codepoint >> 6);
      buf[1] = 0x80 | (codepoint & 0x3F);
      len = 2;
    } else if (codepoint < 0x10000) {
      buf[0] = 0xE0 | (codepoint >> 12);
      buf[1] = 0x80 | ((codepoint >> 6) & 0x3F);
      buf[2] = 0x80 | (codepoint & 0x3F);
      len = 3;
    } else {
      buf[0] = 0xF0 | (codepoint >> 18);
      buf[1] = 0x80 | ((codepoint >> 12) & 0x3F);
      buf[2] = 0x80 | ((codepoint >> 6) & 0x3F);
      buf[3] = 0x80 | (codepoint & 0x3F);
      len = 4;
    }
    return string(buf, len);
  }

  // Fallback: CSI u sequence (kitty keyboard protocol)
  return CSI_u_sequence(codepoint, mod_mask);
}

string TerminalSequenceFromIBM_Key(HID_Key key, Modifier mod_mask) {
  // Calculate modifier parameter (1-based)
  int mod_param = 1;
  if (mod_mask & MOD_SHIFT)
    mod_param += 1;
  if (mod_mask & MOD_ALT)
    mod_param += 2;
  if (mod_mask & MOD_CTRL)
    mod_param += 4;
  if (mod_mask & MOD_SUPER)
    mod_param += 8;

  // Helper lambda for tilde-terminated CSI sequences (F1-F12, Insert, Delete,
  // etc.)
  auto csi_tilde_seq = [&](const string &code) -> string {
    if (mod_mask == 0) {
      return "\x1b[" + code + "~";
    } else {
      return "\x1b[" + code + ";" + std::to_string(mod_param) + "~";
    }
  };

  // Helper lambda for letter-terminated CSI sequences (arrow keys, Home, End)
  auto csi_letter_seq = [&](char letter) -> string {
    if (mod_mask == 0) {
      return string("\x1b[") + letter;
    } else {
      return "\x1b[1;" + std::to_string(mod_param) + string(1, letter);
    }
  };

  switch (key) {
  case HID_Key::ESC:
    return "\x1b";

  // Function keys
  case HID_Key::F1:
    return csi_tilde_seq("1");
  case HID_Key::F2:
    return csi_tilde_seq("2");
  case HID_Key::F3:
    return csi_tilde_seq("3");
  case HID_Key::F4:
    return csi_tilde_seq("4");
  case HID_Key::F5:
    return csi_tilde_seq("5");
  case HID_Key::F6:
    return csi_tilde_seq("17");
  case HID_Key::F7:
    return csi_tilde_seq("18");
  case HID_Key::F8:
    return csi_tilde_seq("19");
  case HID_Key::F9:
    return csi_tilde_seq("20");
  case HID_Key::F10:
    return csi_tilde_seq("21");
  case HID_Key::F11:
    return csi_tilde_seq("23");
  case HID_Key::F12:
    return csi_tilde_seq("24");

  // Arrow keys
  case HID_Key::UP_ARROW:
    return csi_letter_seq('A');
  case HID_Key::DOWN_ARROW:
    return csi_letter_seq('B');
  case HID_Key::RIGHT_ARROW:
    return csi_letter_seq('C');
  case HID_Key::LEFT_ARROW:
    return csi_letter_seq('D');

  // Navigation keys
  case HID_Key::HOME:
    return csi_letter_seq('H');
  case HID_Key::END:
    return csi_letter_seq('F');
  case HID_Key::PAGE_UP:
    return csi_tilde_seq("5");
  case HID_Key::PAGE_DOWN:
    return csi_tilde_seq("6");
  case HID_Key::INSERT:
    return csi_tilde_seq("2");
  case HID_Key::DELETE:
    return csi_tilde_seq("3");

  // Special keys
  case HID_Key::BACKSPACE:
    return (mod_mask == 0) ? "\x7f" : CSI_u_sequence(127, mod_mask);
  case HID_Key::TAB:
    return (mod_mask == 0) ? "\t" : CSI_u_sequence(9, mod_mask);
  case HID_Key::ENTER:
    return (mod_mask == 0) ? "\r" : CSI_u_sequence(13, mod_mask);
  case HID_Key::BACKTICK:
    return (mod_mask == 0) ? "`" : CSI_u_sequence('`', mod_mask);

  // Keys without standard sequences (fallback to CSI u)
  case HID_Key::PRINT_SCREEN:
    return CSI_u_sequence(0xE00A, mod_mask);
  case HID_Key::SCROLL_LOCK:
    return CSI_u_sequence(0xE00B, mod_mask);
  case HID_Key::PAUSE_BREAK:
    return CSI_u_sequence(0xE00C, mod_mask);

  default:
    return "";
  }
}

const char *ToStr(HID_Key key) {
  switch (key) {
  case HID_Key::NONE:
    return "NONE";
  case HID_Key::LETTER_A:
    return "LETTER_A";
  case HID_Key::LETTER_B:
    return "LETTER_B";
  case HID_Key::LETTER_C:
    return "LETTER_C";
  case HID_Key::LETTER_D:
    return "LETTER_D";
  case HID_Key::LETTER_E:
    return "LETTER_E";
  case HID_Key::LETTER_F:
    return "LETTER_F";
  case HID_Key::LETTER_G:
    return "LETTER_G";
  case HID_Key::LETTER_H:
    return "LETTER_H";
  case HID_Key::LETTER_I:
    return "LETTER_I";
  case HID_Key::LETTER_J:
    return "LETTER_J";
  case HID_Key::LETTER_K:
    return "LETTER_K";
  case HID_Key::LETTER_L:
    return "LETTER_L";
  case HID_Key::LETTER_M:
    return "LETTER_M";
  case HID_Key::LETTER_N:
    return "LETTER_N";
  case HID_Key::LETTER_O:
    return "LETTER_O";
  case HID_Key::LETTER_P:
    return "LETTER_P";
  case HID_Key::LETTER_Q:
    return "LETTER_Q";
  case HID_Key::LETTER_R:
    return "LETTER_R";
  case HID_Key::LETTER_S:
    return "LETTER_S";
  case HID_Key::LETTER_T:
    return "LETTER_T";
  case HID_Key::LETTER_U:
    return "LETTER_U";
  case HID_Key::LETTER_V:
    return "LETTER_V";
  case HID_Key::LETTER_W:
    return "LETTER_W";
  case HID_Key::LETTER_X:
    return "LETTER_X";
  case HID_Key::LETTER_Y:
    return "LETTER_Y";
  case HID_Key::LETTER_Z:
    return "LETTER_Z";
  case HID_Key::NUMBER_1:
    return "NUMBER_1";
  case HID_Key::NUMBER_2:
    return "NUMBER_2";
  case HID_Key::NUMBER_3:
    return "NUMBER_3";
  case HID_Key::NUMBER_4:
    return "NUMBER_4";
  case HID_Key::NUMBER_5:
    return "NUMBER_5";
  case HID_Key::NUMBER_6:
    return "NUMBER_6";
  case HID_Key::NUMBER_7:
    return "NUMBER_7";
  case HID_Key::NUMBER_8:
    return "NUMBER_8";
  case HID_Key::NUMBER_9:
    return "NUMBER_9";
  case HID_Key::NUMBER_0:
    return "NUMBER_0";
  case HID_Key::ENTER:
    return "ENTER";
  case HID_Key::ESC:
    return "ESC";
  case HID_Key::BACKSPACE:
    return "BACKSPACE";
  case HID_Key::TAB:
    return "TAB";
  case HID_Key::SPACE:
    return "SPACE";
  case HID_Key::MINUS:
    return "MINUS";
  case HID_Key::EQUALS:
    return "EQUALS";
  case HID_Key::LEFT_BRACKET:
    return "LEFT_BRACKET";
  case HID_Key::RIGHT_BRACKET:
    return "RIGHT_BRACKET";
  case HID_Key::BACKSLASH:
    return "BACKSLASH";
  case HID_Key::SEMICOLON:
    return "SEMICOLON";
  case HID_Key::APOSTROPHE:
    return "APOSTROPHE";
  case HID_Key::BACKTICK:
    return "BACKTICK";
  case HID_Key::COMMA:
    return "COMMA";
  case HID_Key::PERIOD:
    return "PERIOD";
  case HID_Key::SLASH:
    return "SLASH";
  case HID_Key::CAPS_LOCK:
    return "CAPS_LOCK";
  case HID_Key::F1:
    return "F1";
  case HID_Key::F2:
    return "F2";
  case HID_Key::F3:
    return "F3";
  case HID_Key::F4:
    return "F4";
  case HID_Key::F5:
    return "F5";
  case HID_Key::F6:
    return "F6";
  case HID_Key::F7:
    return "F7";
  case HID_Key::F8:
    return "F8";
  case HID_Key::F9:
    return "F9";
  case HID_Key::F10:
    return "F10";
  case HID_Key::F11:
    return "F11";
  case HID_Key::F12:
    return "F12";
  case HID_Key::PRINT_SCREEN:
    return "PRINT_SCREEN";
  case HID_Key::SCROLL_LOCK:
    return "SCROLL_LOCK";
  case HID_Key::PAUSE_BREAK:
    return "PAUSE_BREAK";
  case HID_Key::INSERT:
    return "INSERT";
  case HID_Key::HOME:
    return "HOME";
  case HID_Key::PAGE_UP:
    return "PAGE_UP";
  case HID_Key::DELETE:
    return "DELETE";
  case HID_Key::END:
    return "END";
  case HID_Key::PAGE_DOWN:
    return "PAGE_DOWN";
  case HID_Key::RIGHT_ARROW:
    return "RIGHT_ARROW";
  case HID_Key::LEFT_ARROW:
    return "LEFT_ARROW";
  case HID_Key::DOWN_ARROW:
    return "DOWN_ARROW";
  case HID_Key::UP_ARROW:
    return "UP_ARROW";
  case HID_Key::LEFT_CTRL:
    return "LEFT_CTRL";
  case HID_Key::LEFT_SHIFT:
    return "LEFT_SHIFT";
  case HID_Key::LEFT_ALT:
    return "LEFT_ALT";
  case HID_Key::LEFT_SUPER:
    return "LEFT_SUPER";
  case HID_Key::RIGHT_CTRL:
    return "RIGHT_CTRL";
  case HID_Key::RIGHT_SHIFT:
    return "RIGHT_SHIFT";
  case HID_Key::RIGHT_ALT:
    return "RIGHT_ALT";
  case HID_Key::RIGHT_SUPER:
    return "RIGHT_SUPER";
  default: {
    static char buf[50];
    snprintf(buf, sizeof(buf), "HID_Key(%d)", (int)key);
    return buf;
  }
  }
}

char GetOgonekBase(uint32_t codepoint) {
  switch (codepoint) {
  case U'ć':
    return 'c';
  case U'ł':
    return 'l';
  case U'ń':
    return 'n';
  case U'ó':
    return 'o';
  case U'ś':
    return 's';
  case U'ż':
    return 'z';
  case U'ź':
    return 'x';
  case U'ą':
    return 'a';
  case U'ę':
    return 'e';
  case U'Ć':
    return 'C';
  case U'Ł':
    return 'L';
  case U'Ń':
    return 'N';
  case U'Ó':
    return 'O';
  case U'Ś':
    return 'S';
  case U'Ż':
    return 'Z';
  case U'Ź':
    return 'X';
  case U'Ą':
    return 'A';
  case U'Ę':
    return 'E';
  default:
    return 0;
  }
}

uint32_t ApplyShift(uint32_t c) {
  if (c >= 'a' && c <= 'z')
    return c - 32;
  static const char unshifted[] = "`1234567890-=[]\\;',./";
  static const char shifted[] = "~!@#$%^&*()_+{}|:\"<>?";
  for (int i = 0; unshifted[i]; ++i)
    if (c == (uint32_t)unshifted[i])
      return shifted[i];
  // Polish ogoneks
  switch (c) {
  case U'ą':
    return U'Ą';
  case U'ć':
    return U'Ć';
  case U'ę':
    return U'Ę';
  case U'ł':
    return U'Ł';
  case U'ń':
    return U'Ń';
  case U'ó':
    return U'Ó';
  case U'ś':
    return U'Ś';
  case U'ź':
    return U'Ź';
  case U'ż':
    return U'Ż';
  }
  return c;
}

} // namespace atmt
