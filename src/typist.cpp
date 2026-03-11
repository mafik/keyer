#include "typist.hpp"

#include "ble_keyboard.hpp"
#include "common_esp32.hpp"
#include "freertos/portmacro.h"
#include "keyboard.hpp"
#include "main_loop.hpp"
#include "shadow_editor.hpp"
#include "ssh.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace atmt {

SemaphoreHandle_t editor_sem;
ShadowEditor editor;

static constexpr TickType_t kMinInterval = pdMS_TO_TICKS(15);

static QueueHandle_t typist_queue = nullptr;

static TickType_t last_send_time_ = 0;

static std::bitset<256> buffered_release = {};
static KeyReport buffered_ = {};

void KeystrokeToHID(Keystroke ks, HID_Key &ibm_key, Modifier &mods) {
  mods = ks.mods;
  if (ks.IsKey()) {
    ibm_key = ks.key;
    return;
  }

  auto codepoint = ks.codepoint;
  // Polish characters via AltGr
  char base = GetOgonekBase(codepoint);
  if (base != 0) {
    mods |= MOD_RIGHT_ALT;
    codepoint = (uint8_t)base;
  }
  switch (codepoint) {
  case '\b':
    ibm_key = HID_Key::BACKSPACE;
    break;
  case '\n':
  case '\r':
    ibm_key = HID_Key::ENTER;
    break;
  case '\t':
    ibm_key = HID_Key::TAB;
    break;
  case 0x1b:
    ibm_key = HID_Key::ESC;
    break;
  case ' ':
    ibm_key = HID_Key::SPACE;
    break;
  case '~':
    mods |= MOD_SHIFT;
  case '`': // Fallthrough
    ibm_key = HID_Key::BACKTICK;
    break;
  case '!':
    mods |= MOD_SHIFT;
  case '1': // Fallthrough
    ibm_key = HID_Key::NUMBER_1;
    break;
  case '@':
    mods |= MOD_SHIFT;
  case '2': // Fallthrough
    ibm_key = HID_Key::NUMBER_2;
    break;
  case '#':
    mods |= MOD_SHIFT;
  case '3': // Fallthrough
    ibm_key = HID_Key::NUMBER_3;
    break;
  case '$':
    mods |= MOD_SHIFT;
  case '4': // Fallthrough
    ibm_key = HID_Key::NUMBER_4;
    break;
  case '%':
    mods |= MOD_SHIFT;
  case '5': // Fallthrough
    ibm_key = HID_Key::NUMBER_5;
    break;
  case '^':
    mods |= MOD_SHIFT;
  case '6': // Fallthrough
    ibm_key = HID_Key::NUMBER_6;
    break;
  case '&':
    mods |= MOD_SHIFT;
  case '7': // Fallthrough
    ibm_key = HID_Key::NUMBER_7;
    break;
  case '*':
    mods |= MOD_SHIFT;
  case '8': // Fallthrough
    ibm_key = HID_Key::NUMBER_8;
    break;
  case '(':
    mods |= MOD_SHIFT;
  case '9': // Fallthrough
    ibm_key = HID_Key::NUMBER_9;
    break;
  case ')':
    mods |= MOD_SHIFT;
  case '0': // Fallthrough
    ibm_key = HID_Key::NUMBER_0;
    break;
  case '_':
    mods |= MOD_SHIFT;
  case '-': // Fallthrough
    ibm_key = HID_Key::MINUS;
    break;
  case '+':
    mods |= MOD_SHIFT;
  case '=': // Fallthrough
    ibm_key = HID_Key::EQUALS;
    break;
  case 'Q':
    mods |= MOD_SHIFT;
  case 'q': // Fallthrough
    ibm_key = HID_Key::LETTER_Q;
    break;
  case 'W':
    mods |= MOD_SHIFT;
  case 'w': // Fallthrough
    ibm_key = HID_Key::LETTER_W;
    break;
  case 'E':
    mods |= MOD_SHIFT;
  case 'e': // Fallthrough
    ibm_key = HID_Key::LETTER_E;
    break;
  case 'R':
    mods |= MOD_SHIFT;
  case 'r': // Fallthrough
    ibm_key = HID_Key::LETTER_R;
    break;
  case 'T':
    mods |= MOD_SHIFT;
  case 't': // Fallthrough
    ibm_key = HID_Key::LETTER_T;
    break;
  case 'Y':
    mods |= MOD_SHIFT;
  case 'y': // Fallthrough
    ibm_key = HID_Key::LETTER_Y;
    break;
  case 'U':
    mods |= MOD_SHIFT;
  case 'u': // Fallthrough
    ibm_key = HID_Key::LETTER_U;
    break;
  case 'I':
    mods |= MOD_SHIFT;
  case 'i': // Fallthrough
    ibm_key = HID_Key::LETTER_I;
    break;
  case 'O':
    mods |= MOD_SHIFT;
  case 'o': // Fallthrough
    ibm_key = HID_Key::LETTER_O;
    break;
  case 'P':
    mods |= MOD_SHIFT;
  case 'p': // Fallthrough
    ibm_key = HID_Key::LETTER_P;
    break;
  case '{':
    mods |= MOD_SHIFT;
  case '[': // Fallthrough
    ibm_key = HID_Key::LEFT_BRACKET;
    break;
  case '}':
    mods |= MOD_SHIFT;
  case ']': // Fallthrough
    ibm_key = HID_Key::RIGHT_BRACKET;
    break;
  case '|':
    mods |= MOD_SHIFT;
  case '\\': // Fallthrough
    ibm_key = HID_Key::BACKSLASH;
    break;
  case 'A':
    mods |= MOD_SHIFT;
  case 'a': // Fallthrough
    ibm_key = HID_Key::LETTER_A;
    break;
  case 'S':
    mods |= MOD_SHIFT;
  case 's': // Fallthrough
    ibm_key = HID_Key::LETTER_S;
    break;
  case 'D':
    mods |= MOD_SHIFT;
  case 'd': // Fallthrough
    ibm_key = HID_Key::LETTER_D;
    break;
  case 'F':
    mods |= MOD_SHIFT;
  case 'f': // Fallthrough
    ibm_key = HID_Key::LETTER_F;
    break;
  case 'G':
    mods |= MOD_SHIFT;
  case 'g': // Fallthrough
    ibm_key = HID_Key::LETTER_G;
    break;
  case 'H':
    mods |= MOD_SHIFT;
  case 'h': // Fallthrough
    ibm_key = HID_Key::LETTER_H;
    break;
  case 'J':
    mods |= MOD_SHIFT;
  case 'j': // Fallthrough
    ibm_key = HID_Key::LETTER_J;
    break;
  case 'K':
    mods |= MOD_SHIFT;
  case 'k': // Fallthrough
    ibm_key = HID_Key::LETTER_K;
    break;
  case 'L':
    mods |= MOD_SHIFT;
  case 'l': // Fallthrough
    ibm_key = HID_Key::LETTER_L;
    break;
  case ':':
    mods |= MOD_SHIFT;
  case ';': // Fallthrough
    ibm_key = HID_Key::SEMICOLON;
    break;
  case '"':
    mods |= MOD_SHIFT;
  case '\'': // Fallthrough
    ibm_key = HID_Key::APOSTROPHE;
    break;
  case 'Z':
    mods |= MOD_SHIFT;
  case 'z': // Fallthrough
    ibm_key = HID_Key::LETTER_Z;
    break;
  case 'X':
    mods |= MOD_SHIFT;
  case 'x': // Fallthrough
    ibm_key = HID_Key::LETTER_X;
    break;
  case 'C':
    mods |= MOD_SHIFT;
  case 'c': // Fallthrough
    ibm_key = HID_Key::LETTER_C;
    break;
  case 'V':
    mods |= MOD_SHIFT;
  case 'v': // Fallthrough
    ibm_key = HID_Key::LETTER_V;
    break;
  case 'B':
    mods |= MOD_SHIFT;
  case 'b': // Fallthrough
    ibm_key = HID_Key::LETTER_B;
    break;
  case 'N':
    mods |= MOD_SHIFT;
  case 'n': // Fallthrough
    ibm_key = HID_Key::LETTER_N;
    break;
  case 'M':
    mods |= MOD_SHIFT;
  case 'm': // Fallthrough
    ibm_key = HID_Key::LETTER_M;
    break;
  case '<':
    mods |= MOD_SHIFT;
  case ',': // Fallthrough
    ibm_key = HID_Key::COMMA;
    break;
  case '>':
    mods |= MOD_SHIFT;
  case '.': // Fallthrough
    ibm_key = HID_Key::PERIOD;
    break;
  case '?':
    mods |= MOD_SHIFT;
  case '/': // Fallthrough
    ibm_key = HID_Key::SLASH;
    break;
  default:
    Debugf("Unsupported codepoint: U+%04X (%d)\n", codepoint, codepoint);
    return;
  }
}

bool AddToBuffer(HID_Key key, Modifier mods) {

  uint8_t k = (uint8_t)key;

  int n_buffered = buffered_.Count();
  if (n_buffered == 6) {
    // Buffer full
    return false;
  }

  if (buffered_release[k]) {
    // Key must be released before it's pressed
    return false;
  }
  if (buffered_.Contains(key)) {
    // Already pressed
    return false;
  }

  if (n_buffered && (buffered_.modifiers != mods)) {
    // Can't change modifiers on already buffered keys
    return false;
  }

  // Success!
  buffered_.modifiers = mods;
  buffered_.keys[n_buffered] = k;
  buffered_release.set((int)key); // also mark it for release later

  return true;
}

bool AddToBuffer(Keystroke ks) {
  HID_Key key;
  Modifier mods;
  KeystrokeToHID(ks, key, mods);
  return AddToBuffer(key, mods);
}

static void TypistTask(void *) {
  last_send_time_ = xTaskGetTickCount();
  for (;;) {
    Keystroke from_queue;
    bool was_idle = editor.IsIdle();
    if (xQueueReceive(typist_queue, &from_queue, 0)) {
      if (from_queue.IsNone()) {
        continue;
      }
      ApplyKeystroke(editor.target, from_queue);
      // A keystroke can only be added directly to the buffer if the editor is
      // idle. Otherwise it will mess up with synthesized events.

      // IF a keystroke is successfully added to the buffer, then it should also
      // be updated in the editor's current state!
      if (was_idle && AddToBuffer(from_queue)) {
        ApplyKeystroke(editor.current, from_queue);
      }
      continue;
    }

    // At this point there are no more events from the user to process.

    if (!was_idle) {
      Keystroke synthesized_ks = editor.NextKeystroke();
      if (synthesized_ks.IsNone()) {
        Debugln("ERROR: Non-idle editor didn't synthesize an event!");
      }
      if (AddToBuffer(synthesized_ks)) {
        ApplyKeystroke(editor.current, synthesized_ks);
        continue;
      }
      // At this point there is a synthesized event that we can't really send
      // because the buffer is full.
    }

    // At this point either we have nothing to do, or the buffer is full and
    // must be sent.

    if (buffered_release.none() && buffered_.Count() == 0) {
      xQueuePeek(typist_queue, &from_queue, portMAX_DELAY);
      continue;
    }

    // Finally! We can now send the buffer :)

    Debugf("Sending buffer_: modifiers=0x%02x "
           "keys=[%s %s %s %s %s %s]\n",
           buffered_.modifiers, ToStr((HID_Key)buffered_.keys[0]),
           ToStr((HID_Key)buffered_.keys[1]), ToStr((HID_Key)buffered_.keys[2]),
           ToStr((HID_Key)buffered_.keys[3]), ToStr((HID_Key)buffered_.keys[4]),
           ToStr((HID_Key)buffered_.keys[5]));

    if constexpr (false) { // Rate limit to avoid dropping notifications.
      // Note: we could actually try to process more of the messages from the
      // queue here, but that's asking for more bugs.
      auto can_send_time = last_send_time_ + kMinInterval;
      if (auto now = xTaskGetTickCount(); can_send_time > now) {
        Debugf("Waiting %d ms before sending the next report\n",
               (can_send_time - now) * portTICK_PERIOD_MS);
        vTaskDelay(can_send_time - now);
      }
    }

    last_send_time_ = xTaskGetTickCount();
    ble_keyboard.SendReportWithRetries(&buffered_);
    for (int i = 0; i < buffered_release.size(); ++i) {
      if (buffered_release.test(i) && !buffered_.Contains((HID_Key)i)) {
        buffered_release.reset(i);
      }
    }
    buffered_ = {};
  }
}

TaskHandle_t typist_task_handle = nullptr;

void InitTypist() {
  editor_sem = xSemaphoreCreateBinary();
  xSemaphoreGive(editor_sem);

  typist_queue = xQueueCreate(2, sizeof(Keystroke));

  xTaskCreatePinnedToCore(TypistTask, "Typist", 4 * 1024, nullptr, 3,
                          &typist_task_handle, 1);
}

void WakeTypist() {
  DebugDumpEditor();
  Keystroke ks = Keystroke::None();
  xQueueSendToBack(typist_queue, &ks, 0);
}

void HandleUnicode(uint32_t codepoint, Modifier mods) {
  if (ssh_chan) {
    auto seq = TerminalSequenceFromUnicode(codepoint, mods);
    if (seq.size())
      ssh_channel_write(ssh_chan, seq.data(), seq.size());
    return;
  }
  // Resolve shifted character so the editor tracks what the host displays.
  // e.g. ('=', MOD_SHIFT) → ('+', 0), (U'ą', MOD_SHIFT) → (U'Ą', 0)
  if (mods & MOD_SHIFT) {
    uint32_t shifted = ApplyShift(codepoint);
    if (shifted != codepoint) {
      codepoint = shifted;
      mods &= ~MOD_SHIFT;
    }
  }
  Keystroke ks = Keystroke::Char(codepoint, mods);
  xQueueSendToBack(typist_queue, &ks, 0);
}

void HandleKey(HID_Key key, Modifier mods) {
  if (ssh_chan) {
    auto seq = TerminalSequenceFromIBM_Key(key, mods);
    if (seq.size())
      ssh_channel_write(ssh_chan, seq.data(), seq.size());
    return;
  }
  Keystroke ks = Keystroke::Key(key, mods);
  xQueueSendToBack(typist_queue, &ks, 0);
}

static void AppendState(std::string &buf, const char *label,
                        const EditorState &s) {
  char tmp[128];
  snprintf(tmp, sizeof(tmp), "== %s (%d rows, cursor %d:%d) ==\n", label,
           s.num_rows, s.cursor_row, s.cursor_col);
  buf += tmp;
  for (int r = 0; r < s.num_rows; ++r) {
    buf += " \"";
    buf += s.rows[r];
    buf += "\",\n";
  }
}

void DebugDumpEditor() {
  ShadowEditor copy = editor;
  Keystroke ks = copy.NextKeystroke();

  std::string buf = "Editor dump\n";
  if (ks.IsNone()) {
    buf += "Next: (none -- idle)\n";
  } else if (ks.IsChar()) {
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "Next: '%c' (U+%04X)\n",
             ks.codepoint <= 126 ? (char)ks.codepoint : '?', ks.codepoint);
    buf += tmp;
  } else {
    buf += "Next: ";
    buf += ToStr(ks.key);
    buf += '\n';
  }
  AppendState(buf, "CURRENT", editor.current);
  AppendState(buf, "TARGET", editor.target);
  buf += "==\n";
  Debug(buf.c_str());
}

} // namespace atmt
