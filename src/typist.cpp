#include "typist.hpp"

#include "ble_keyboard.hpp"
#include "common_esp32.hpp"
#include "main_loop.hpp"
#include "shadow_editor.hpp"
#include "ssh.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace atmt {

ShadowEditor editor;

static SemaphoreHandle_t typist_wake_sem = nullptr;

static void TypistTask(void *) {
  for (;;) {
    Keystroke ks = editor.NextKeystroke();
    if (ks.IsNone()) {
      xSemaphoreTake(typist_wake_sem, portMAX_DELAY);
      continue;
    }
    // Send via BLE on main thread
    RunOnMain([ks]() { SendKeystrokeDirect(ks); });
    ApplyKeystroke(editor.current, ks);
  }
}

TaskHandle_t typist_task_handle = nullptr;

void InitTypist() {
  typist_wake_sem = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(TypistTask, "Typist", 4 * 1024, nullptr, 3,
                          &typist_task_handle, 1);
}

void WakeTypist() {
  if (typist_wake_sem)
    xSemaphoreGive(typist_wake_sem);
}

void SendKeystroke(Keystroke ks) {
  bool immediate = editor.HandleKeypress(ks);
  if (immediate) {
    SendKeystrokeDirect(ks);
  } else {
    WakeTypist();
  }
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
  SendKeystroke(Keystroke::Char(codepoint, mods));
}

void HandleKey(HID_Key key, Modifier mods) {
  if (ssh_chan) {
    auto seq = TerminalSequenceFromIBM_Key(key, mods);
    if (seq.size())
      ssh_channel_write(ssh_chan, seq.data(), seq.size());
    return;
  }

  SendKeystroke(Keystroke::Key(key, mods));
}

void SendKeystrokeDirect(Keystroke ks) {
  if (ks.IsChar()) {
    ble_keyboard.SendUnicode(ks.codepoint, ks.mods);
  } else if (ks.IsKey()) {
    ble_keyboard.SendKey(ks.key, ks.mods);
  }
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
