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

  // For everything else, use CSI u sequence (kitty keyboard protocol)
  return CSI_u_sequence(codepoint, mod_mask);
}

string TerminalSequenceFromIBM_Key(IBM_Key key, Modifier mod_mask) {
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
  case IBM_Key::ESC:
    return "\x1b";

  // Function keys
  case IBM_Key::F1:
    return csi_tilde_seq("1");
  case IBM_Key::F2:
    return csi_tilde_seq("2");
  case IBM_Key::F3:
    return csi_tilde_seq("3");
  case IBM_Key::F4:
    return csi_tilde_seq("4");
  case IBM_Key::F5:
    return csi_tilde_seq("5");
  case IBM_Key::F6:
    return csi_tilde_seq("17");
  case IBM_Key::F7:
    return csi_tilde_seq("18");
  case IBM_Key::F8:
    return csi_tilde_seq("19");
  case IBM_Key::F9:
    return csi_tilde_seq("20");
  case IBM_Key::F10:
    return csi_tilde_seq("21");
  case IBM_Key::F11:
    return csi_tilde_seq("23");
  case IBM_Key::F12:
    return csi_tilde_seq("24");

  // Arrow keys
  case IBM_Key::UP_ARROW:
    return csi_letter_seq('A');
  case IBM_Key::DOWN_ARROW:
    return csi_letter_seq('B');
  case IBM_Key::RIGHT_ARROW:
    return csi_letter_seq('C');
  case IBM_Key::LEFT_ARROW:
    return csi_letter_seq('D');

  // Navigation keys
  case IBM_Key::HOME:
    return csi_letter_seq('H');
  case IBM_Key::END:
    return csi_letter_seq('F');
  case IBM_Key::PAGE_UP:
    return csi_tilde_seq("5");
  case IBM_Key::PAGE_DOWN:
    return csi_tilde_seq("6");
  case IBM_Key::INSERT:
    return csi_tilde_seq("2");
  case IBM_Key::DELETE:
    return csi_tilde_seq("3");

  // Special keys
  case IBM_Key::BACKSPACE:
    return (mod_mask == 0) ? "\x7f" : CSI_u_sequence(127, mod_mask);
  case IBM_Key::TAB:
    return (mod_mask == 0) ? "\t" : CSI_u_sequence(9, mod_mask);
  case IBM_Key::ENTER:
    return (mod_mask == 0) ? "\r" : CSI_u_sequence(13, mod_mask);
  case IBM_Key::BACKTICK:
    return (mod_mask == 0) ? "`" : CSI_u_sequence('`', mod_mask);

  // Keys without standard sequences (fallback to CSI u)
  case IBM_Key::PRINT_SCREEN:
    return CSI_u_sequence(0xE00A, mod_mask);
  case IBM_Key::SCROLL_LOCK:
    return CSI_u_sequence(0xE00B, mod_mask);
  case IBM_Key::PAUSE_BREAK:
    return CSI_u_sequence(0xE00C, mod_mask);

  default:
    return "";
  }
}

const char *ToStr(IBM_Key key) {
  switch (key) {
  case IBM_Key::ESC:
    return "ESC";
  case IBM_Key::F1:
    return "F1";
  case IBM_Key::F2:
    return "F2";
  case IBM_Key::F3:
    return "F3";
  case IBM_Key::F4:
    return "F4";
  case IBM_Key::F5:
    return "F5";
  case IBM_Key::F6:
    return "F6";
  case IBM_Key::F7:
    return "F7";
  case IBM_Key::F8:
    return "F8";
  case IBM_Key::F9:
    return "F9";
  case IBM_Key::F10:
    return "F10";
  case IBM_Key::F11:
    return "F11";
  case IBM_Key::F12:
    return "F12";
  case IBM_Key::PRINT_SCREEN:
    return "PRINT_SCREEN";
  case IBM_Key::SCROLL_LOCK:
    return "SCROLL_LOCK";
  case IBM_Key::PAUSE_BREAK:
    return "PAUSE_BREAK";
  case IBM_Key::BACKTICK:
    return "BACKTICK";
  case IBM_Key::INSERT:
    return "INSERT";
  case IBM_Key::DELETE:
    return "DELETE";
  case IBM_Key::BACKSPACE:
    return "BACKSPACE";
  case IBM_Key::TAB:
    return "TAB";
  case IBM_Key::ENTER:
    return "ENTER";
  case IBM_Key::UP_ARROW:
    return "UP_ARROW";
  case IBM_Key::RIGHT_ARROW:
    return "RIGHT_ARROW";
  case IBM_Key::DOWN_ARROW:
    return "DOWN_ARROW";
  case IBM_Key::LEFT_ARROW:
    return "LEFT_ARROW";
  case IBM_Key::PAGE_UP:
    return "PAGE_UP";
  case IBM_Key::PAGE_DOWN:
    return "PAGE_DOWN";
  case IBM_Key::HOME:
    return "HOME";
  case IBM_Key::END:
    return "END";
  default: {
    static char buf[50];
    snprintf(buf, sizeof(buf), "IBM_Key(%d)", (int)key);
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
  case U'ą': return U'Ą';
  case U'ć': return U'Ć';
  case U'ę': return U'Ę';
  case U'ł': return U'Ł';
  case U'ń': return U'Ń';
  case U'ó': return U'Ó';
  case U'ś': return U'Ś';
  case U'ź': return U'Ź';
  case U'ż': return U'Ż';
  }
  return c;
}

} // namespace atmt
