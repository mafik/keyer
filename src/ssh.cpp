#include "ssh.hpp"

#include <esp_pm.h>
#include <libssh/libssh.h>
#include <vterm.h>

#include "app.hpp"
#include "common_esp32.hpp"
#include "g1.hpp"
#include "libssh/libssh.h"
#include "libssh/server.h"
#include "main_loop.hpp"
#include "secrets.hpp"

namespace atmt {

ssh_channel ssh_chan = nullptr;
bool ssh_connected = false;

// VTerm for terminal emulation
VTerm *vterm = nullptr;
VTermScreen *vterm_screen = nullptr;
bool vterm_dirty = false;

TaskHandle_t ssh_task_handle = NULL;

constexpr int VTERM_ROWS = G1::DISPLAY_LINES;
constexpr int VTERM_COLS = 60; // between 41 and 144 characters actually

VTermScreenCallbacks vterm_callbacks = {
    .damage =
        [](VTermRect rect, void *user) {
          vterm_dirty = true;
          return 1;
        },
    .moverect = nullptr,
    .movecursor =
        [](VTermPos, VTermPos, int, void *) {
          vterm_dirty = true;
          return 1;
        },
    .settermprop = nullptr,
    .bell = nullptr,
    .resize = nullptr,
    .sb_pushline = nullptr,
    .sb_popline = nullptr,
    .sb_clear = nullptr,
    .sb_pushline4 = nullptr,
};

// Convert VTerm screen to text for G1 display
string GetVTermDisplay() {
  string result;
  VTermPos cursor_pos;
  vterm_state_get_cursorpos(vterm_obtain_state(vterm), &cursor_pos);

  for (int row = 0; row < VTERM_ROWS; ++row) {
    for (int col = 0; col < VTERM_COLS; ++col) {
      VTermPos pos = {.row = row, .col = col};
      VTermScreenCell cell;
      vterm_screen_get_cell(vterm_screen, pos, &cell);

      if (row == cursor_pos.row && col == cursor_pos.col) {
        result += '|';
      }

      if (cell.chars[0] == 0 || cell.chars[0] == ' ') {
        result += ' ';
      } else {
        // Convert Unicode codepoint to UTF-8
        uint32_t cp = cell.chars[0];
        if (cp <= 0x7F) {
          result += (char)cp;
        } else if (cp <= 0x7FF) {
          result += (char)(0xC0 | (cp >> 6));
          result += (char)(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
          result += (char)(0xE0 | (cp >> 12));
          result += (char)(0x80 | ((cp >> 6) & 0x3F));
          result += (char)(0x80 | (cp & 0x3F));
        } else {
          result += (char)(0xF0 | (cp >> 18));
          result += (char)(0x80 | ((cp >> 12) & 0x3F));
          result += (char)(0x80 | ((cp >> 6) & 0x3F));
          result += (char)(0x80 | (cp & 0x3F));
        }
      }
    }

    // Trim trailing spaces
    while (!result.empty() && result.back() == ' ') {
      result.pop_back();
    }
    if (row < VTERM_ROWS - 1) {
      result += '\n';
    }
  }

  return result;
}

void SSH_Loop(void *unused) {
#define DEBUG_LN(line)                                                         \
  if constexpr (kDebug) {                                                      \
    RunOnMain([](uint64_t) { Debugln(line); }, 0);                             \
  }
#define DEBUG_F(line, ...)                                                     \
  if constexpr (kDebug) {                                                      \
    RunOnMain([=]() { Debugf(line, ##__VA_ARGS__); });                         \
  }

  DEBUG_LN("SSH: Task starting...");

reconnect:

  ssh_session ssh_sess = ssh_new();
  if (!ssh_sess) {
    DEBUG_LN("Failed to create SSH session");
    return;
  }

  // Import private key
  ssh_key privkey;
  if (ssh_pki_import_privkey_base64(SSH_PRIVATE_KEY, nullptr, nullptr, nullptr,
                                    &privkey) != SSH_OK) {
    DEBUG_LN("Failed to import private key");
    ssh_free(ssh_sess);
    return;
  }

  ssh_options_set(ssh_sess, SSH_OPTIONS_HOST, SSH_HOST);
  ssh_options_set(ssh_sess, SSH_OPTIONS_PORT, &SSH_PORT);
  ssh_options_set(ssh_sess, SSH_OPTIONS_USER, SSH_USER);
  long timeout = 60;
  ssh_options_set(ssh_sess, SSH_OPTIONS_TIMEOUT, &timeout);
  long nodelay = 1;
  ssh_options_set(ssh_sess, SSH_OPTIONS_NODELAY, &nodelay);

  int verbosity = kDebug ? SSH_LOG_INFO : SSH_LOG_NONE;
  ssh_options_set(ssh_sess, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);

  // Temporarily raise CPU frequency for crypto-heavy operations
  esp_pm_lock_handle_t pm_lock;
  esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "ssh", &pm_lock);
  esp_pm_lock_acquire(pm_lock);

  DEBUG_LN("SSH: Connecting...");
  if (ssh_connect(ssh_sess) != SSH_OK) {
    auto error = ssh_get_error(ssh_sess);
    DEBUG_F("SSH connection failed: %s\n", error);
    ssh_key_free(privkey);
    ssh_free(ssh_sess);
    ssh_sess = nullptr;
    goto maybe_reconnect;
  }

  if (ssh_userauth_publickey(ssh_sess, nullptr, privkey) != SSH_AUTH_SUCCESS) {
    auto error = ssh_get_error(ssh_sess);
    DEBUG_F("Authentication failed: %s\n", error);
    ssh_key_free(privkey);
    ssh_disconnect(ssh_sess);
    ssh_free(ssh_sess);
    ssh_sess = nullptr;
    goto maybe_reconnect;
  }

  ssh_key_free(privkey);

  // Open channel
  ssh_chan = ssh_channel_new(ssh_sess);
  if (!ssh_chan) {
    DEBUG_LN("Failed to create SSH channel");
    ssh_disconnect(ssh_sess);
    ssh_free(ssh_sess);
    ssh_sess = nullptr;
    goto maybe_reconnect;
  }

  if (ssh_channel_open_session(ssh_chan) != SSH_OK) {
    auto error = ssh_get_error(ssh_sess);
    DEBUG_F("Failed to open channel: %s\n", error);
    ssh_channel_free(ssh_chan);
    ssh_chan = nullptr;
    ssh_disconnect(ssh_sess);
    ssh_free(ssh_sess);
    ssh_sess = nullptr;
    goto maybe_reconnect;
  }

  if (ssh_channel_request_pty_size(ssh_chan, "vt220", VTERM_COLS, VTERM_ROWS) !=
      SSH_OK) {
    auto error = ssh_get_error(ssh_sess);
    DEBUG_F("Failed to request PTY: %s\n", error);
    ssh_channel_close(ssh_chan);
    ssh_channel_free(ssh_chan);
    ssh_chan = nullptr;
    ssh_disconnect(ssh_sess);
    ssh_free(ssh_sess);
    ssh_sess = nullptr;
    goto maybe_reconnect;
  }

  if (ssh_channel_request_shell(ssh_chan) != SSH_OK) {
    auto error = ssh_get_error(ssh_sess);
    DEBUG_F("Failed to request shell: %s\n", error);
    ssh_channel_close(ssh_chan);
    ssh_channel_free(ssh_chan);
    ssh_chan = nullptr;
    ssh_disconnect(ssh_sess);
    ssh_free(ssh_sess);
    ssh_sess = nullptr;
    goto maybe_reconnect;
  }

  ssh_connected = true;
  DEBUG_LN("SSH shell session established!");

  // Initialize VTerm
  vterm = vterm_new(VTERM_ROWS, VTERM_COLS);
  vterm_set_utf8(vterm, 1);
  vterm_screen = vterm_obtain_screen(vterm);
  vterm_screen_set_callbacks(vterm_screen, &vterm_callbacks, NULL);
  vterm_screen_reset(vterm_screen, 1);

  esp_pm_lock_release(pm_lock);
  esp_pm_lock_delete(pm_lock);

  // Main SSH read loop
  char buffer[4096];
  while (ssh_connected) {
    if (!ssh_chan || ssh_channel_is_eof(ssh_chan)) {
      ssh_connected = false;
      DEBUG_LN("SSH channel is EOF!");
      break;
    }

    int nbytes =
        ssh_channel_read_timeout(ssh_chan, buffer, sizeof(buffer), 0, 10000);
    if (nbytes == SSH_AGAIN) {
      // Without this, the connection keeps dropping (maybe Android Hotspot
      // kills it?)
      ssh_send_keepalive(ssh_sess);
    } else if (nbytes > 0) {
      // Feed to vterm
      if (vterm) {
        vterm_input_write(vterm, buffer, nbytes);
      }

      // Update G1 display if changed
      if (vterm_dirty) {
        vterm_dirty = false;
        string display = GetVTermDisplay();
        atmt::RunOnMain([display = std::move(display)]() {
          if (current_app)
            current_app->ShowText(display);
        });
      }
    } else if (nbytes < 0) {
      ssh_connected = false;
      DEBUG_F("SSH read %d bytes!\n", nbytes);
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }

  // Cleanup
  if (vterm) {
    vterm_free(vterm);
    vterm = nullptr;
    vterm_screen = nullptr;
  }

  if (ssh_chan) {
    ssh_channel_close(ssh_chan);
    ssh_channel_free(ssh_chan);
    ssh_chan = nullptr;
  }

  if (ssh_sess) {
    ssh_disconnect(ssh_sess);
    ssh_free(ssh_sess);
    ssh_sess = nullptr;
  }

maybe_reconnect:

  if (WiFi.isConnected()) {
    DEBUG_LN("SSH_Loop exited but WiFi is still on - reconnecting...");
    goto reconnect;
  }

  RunOnMain([]() {
    Debugln("SSH_Loop ended");
    ssh_task_handle = NULL;
    // TODO: check if wifi is connected and maybe restart the task?
  });
  vTaskDelete(NULL);
}

void OnNetworkConnectedSSH() {
  if (ssh_task_handle) {
    // ssh task already running
  } else {
    xTaskCreatePinnedToCore(SSH_Loop,  // Task function
                            "SSH",     // Task name (for debugging)
                            32 * 1024, // Stack size in BYTES (not words!)
                            NULL,      // Parameters passed to task
                            5,         // Priority (0-configMAX_PRIORITIES-1)
                            &ssh_task_handle, // Task handle (can be NULL)
                            1 // Core 1 (away from WiFi/BLE on Core 0)
    );
  }
}
} // namespace atmt
