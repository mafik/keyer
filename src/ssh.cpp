#include "ssh.hpp"

#include "libssh/server.h"
#include <esp_pm.h>
#include <libssh/libssh.h>
#include <vterm.h>

#include <WiFi.h>

#include "common_esp32.hpp"
#include "keyboard.hpp"
#include "main_loop.hpp"
#include "secrets.hpp"
#include "typist.hpp"

namespace atmt {

ssh_channel ssh_chan = nullptr;

static SemaphoreHandle_t ssh_start_sem = nullptr;
TaskHandle_t ssh_task_handle = nullptr;

// VTerm for terminal emulation
static VTerm *vterm = nullptr;
static VTermScreen *vterm_screen = nullptr;
static bool vterm_dirty = false;

constexpr int VTERM_ROWS = 7;
constexpr int VTERM_COLS = 80;
static VTermScreenCallbacks vterm_callbacks = {
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

// Set a status message in the editor target (single line)
static void SetStatusMessage(const char *text) {
  EditorState &tgt = editor.target;
  tgt.num_rows = 0;
  const char *p = text;
  while (*p && tgt.num_rows < EditorState::kRows) {
    const char *nl = strchr(p, '\n');
    if (!nl)
      nl = p + strlen(p);
    tgt.rows[tgt.num_rows++].assign(p, nl);
    p = *nl ? nl + 1 : nl;
  }
  // Strip empty leading/trailing rows
  while (tgt.num_rows > 1 && tgt.rows[0].empty()) {
    for (int i = 0; i < tgt.num_rows - 1; ++i)
      tgt.rows[i] = std::move(tgt.rows[i + 1]);
    tgt.num_rows--;
  }
  while (tgt.num_rows > 1 && tgt.rows[tgt.num_rows - 1].empty())
    tgt.num_rows--;
  if (tgt.num_rows == 0) {
    tgt.num_rows = 1;
    tgt.rows[0].clear();
  }
  tgt.cursor_row = tgt.num_rows - 1;
  tgt.cursor_col = tgt.rows[tgt.cursor_row].size();
  WakeTypist();
}

// Copy VTerm screen content into editor.target
static void UpdateEditorFromVTerm() {
  if (!vterm || !vterm_screen)
    return;

  EditorState &tgt = editor.target;
  VTermPos cpos;
  vterm_state_get_cursorpos(vterm_obtain_state(vterm), &cpos);

  tgt.num_rows = VTERM_ROWS;
  for (int row = 0; row < VTERM_ROWS; ++row) {
    tgt.rows[row].clear();
    for (int col = 0; col < VTERM_COLS; ++col) {
      VTermPos pos = {.row = row, .col = col};
      VTermScreenCell cell;
      vterm_screen_get_cell(vterm_screen, pos, &cell);
      char ch = (cell.chars[0] >= 32 && cell.chars[0] <= 126)
                    ? (char)cell.chars[0]
                    : ' ';
      tgt.rows[row] += ch;
    }
    // Trim trailing spaces, but keep spaces up to the cursor on the cursor row
    int keep = (row == cpos.row) ? cpos.col : 0;
    while ((int)tgt.rows[row].size() > keep && tgt.rows[row].back() == ' ')
      tgt.rows[row].pop_back();
  }
  tgt.cursor_row = cpos.row;
  tgt.cursor_col = cpos.col;
  WakeTypist();
}

static void RunOneSSHSession() {
  DebugHeap("ssh-task-start");

  // Allocate VTerm before WiFi — there's ~100KB free here, enough for both.
  // VTerm (~34KB) is kept alive across sessions, so this only runs once.
  // Must NOT be on the main thread (would starve WiFi init).
  if (!vterm) {
    vterm = vterm_new(VTERM_ROWS, VTERM_COLS);
    vterm_set_utf8(vterm, 1);
    vterm_screen = vterm_obtain_screen(vterm);
    vterm_screen_set_callbacks(vterm_screen, &vterm_callbacks, NULL);
    DebugHeap("after-VTerm-alloc");
  }

  SetStatusMessage("SSH: Connecting WiFi...\n");

  ssh_session ssh_sess = nullptr;
  ssh_key privkey = nullptr;
  esp_pm_lock_handle_t pm_lock = nullptr;
  esp_pm_config_esp32s3_t old_pm_config;
  bool ssh_connected = false;
  bool pm_modified = false;

  // --- WiFi connection ---
  WiFi.mode(WIFI_STA);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  {
    int wifi_attempts = 0;
    while (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(100));
      if (++wifi_attempts > 100) {
        SetStatusMessage("SSH: WiFi timeout!\n");
        Debugln("SSH: WiFi timeout");
        goto cleanup;
      }
    }
  }

  Debugf("SSH: WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());
  DebugHeap("after-WiFi-connect");

  // --- SSH connection ---
  // VTerm is allocated AFTER ssh auth completes — ssh_connect needs large
  // contiguous blocks for crypto buffers, and VTerm (~34KB) would leave
  // insufficient heap (largest block < 8KB → server resets during key
  // exchange).
  Debugln("SSH: Connecting...");

  ssh_sess = ssh_new();
  if (!ssh_sess) {
    SetStatusMessage("SSH: Failed to create session!\n");
    goto cleanup;
  }

  if (ssh_pki_import_privkey_base64(SSH_PRIVATE_KEY, nullptr, nullptr, nullptr,
                                    &privkey) != SSH_OK) {
    SetStatusMessage("SSH: Failed to import key!\n");
    goto cleanup;
  }

  ssh_options_set(ssh_sess, SSH_OPTIONS_HOST, SSH_HOST);
  ssh_options_set(ssh_sess, SSH_OPTIONS_PORT, &SSH_PORT);
  ssh_options_set(ssh_sess, SSH_OPTIONS_USER, SSH_USER);
  {
    long timeout = 60;
    ssh_options_set(ssh_sess, SSH_OPTIONS_TIMEOUT, &timeout);
  }
  {
    long nodelay = 1;
    ssh_options_set(ssh_sess, SSH_OPTIONS_NODELAY, &nodelay);
  }
  {
    int verbosity = kDebug ? SSH_LOG_INFO : SSH_LOG_NONE;
    ssh_options_set(ssh_sess, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
  }

  // Raise CPU to 240MHz for crypto
  esp_pm_get_configuration(&old_pm_config);
  {
    esp_pm_config_esp32s3_t ssh_pm = {
        .max_freq_mhz = 240, .min_freq_mhz = 240, .light_sleep_enable = false};
    esp_pm_configure(&ssh_pm);
  }
  esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "ssh", &pm_lock);
  esp_pm_lock_acquire(pm_lock);
  pm_modified = true;

  if (ssh_connect(ssh_sess) != SSH_OK) {
    Debugf("SSH: Connection failed: %s\n", ssh_get_error(ssh_sess));
    SetStatusMessage("SSH: Connection failed!\n");
    goto cleanup;
  }
  ssh_connected = true;

  if (ssh_userauth_publickey(ssh_sess, nullptr, privkey) != SSH_AUTH_SUCCESS) {
    Debugf("SSH: Auth failed: %s\n", ssh_get_error(ssh_sess));
    SetStatusMessage("SSH: Auth failed!\n");
    goto cleanup;
  }

  ssh_key_free(privkey);
  privkey = nullptr;

  ssh_chan = ssh_channel_new(ssh_sess);
  if (!ssh_chan || ssh_channel_open_session(ssh_chan) != SSH_OK) {
    Debugf("SSH: Channel open failed: %s\n", ssh_get_error(ssh_sess));
    SetStatusMessage("SSH: Channel failed!\n");
    goto cleanup;
  }

  if (ssh_channel_request_pty_size(ssh_chan, "vt220", VTERM_COLS, VTERM_ROWS) !=
      SSH_OK) {
    Debugf("SSH: PTY failed: %s\n", ssh_get_error(ssh_sess));
    SetStatusMessage("SSH: PTY request failed!\n");
    goto cleanup;
  }

  if (ssh_channel_request_shell(ssh_chan) != SSH_OK) {
    Debugf("SSH: Shell failed: %s\n", ssh_get_error(ssh_sess));
    SetStatusMessage("SSH: Shell request failed!\n");
    goto cleanup;
  }

  // Restore PM after crypto-heavy phase
  esp_pm_lock_release(pm_lock);
  esp_pm_lock_delete(pm_lock);
  esp_pm_configure(&old_pm_config);
  pm_lock = nullptr;
  pm_modified = false;

  vterm_screen_reset(vterm_screen, 1);

  DebugHeap("after-SSH-shell");
  Debugln("SSH: Shell session established!");
  SetStatusMessage("SSH: Connected!\n");

  // Main SSH read loop
  {
    char buffer[4096];
    while (true) {
      if (!ssh_chan || ssh_channel_is_eof(ssh_chan)) {
        Debugln("SSH: Channel EOF");
        break;
      }

      int nbytes =
          ssh_channel_read_timeout(ssh_chan, buffer, sizeof(buffer), 0, 10000);
      if (nbytes == SSH_AGAIN) {
        ssh_send_keepalive(ssh_sess);
      } else if (nbytes > 0) {
        vterm_input_write(vterm, buffer, nbytes);
        if (vterm_dirty) {
          vterm_dirty = false;
          UpdateEditorFromVTerm();
        }
      } else if (nbytes < 0) {
        Debugf("SSH: Read error %d\n", nbytes);
        break;
      }

      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }

cleanup:
  if (ssh_chan) {
    ssh_channel_close(ssh_chan);
    ssh_channel_free(ssh_chan);
    ssh_chan = nullptr;
  }
  if (ssh_connected)
    ssh_disconnect(ssh_sess);
  if (privkey)
    ssh_key_free(privkey);
  if (ssh_sess)
    ssh_free(ssh_sess);
  if (pm_lock) {
    esp_pm_lock_release(pm_lock);
    esp_pm_lock_delete(pm_lock);
  }
  if (pm_modified)
    esp_pm_configure(&old_pm_config);

  WiFi.disconnect();

  DebugHeap("after-cleanup");
  Debugln("SSH: Session ended");
  SetStatusMessage("\nSSH: Disconnected.\n");
}

// Persistent task that waits for a semaphore signal to start an SSH session.
// Created once at boot, never deleted — avoids task create/delete memory
// issues.
static void SSHTask(void *) {
  for (;;) {
    xSemaphoreTake(ssh_start_sem, portMAX_DELAY);
    Debugln("SSH: Task woke up");
    RunOneSSHSession();
    ssh_chan = nullptr;
    Debugln("SSH: Task going back to sleep");
  }
}

void StartSSHSession() {
  if (ssh_chan) {
    Debugln("SSH: Session already running");
    return;
  }
  if (!ssh_start_sem) {
    // First call — create semaphore and persistent task
    ssh_start_sem = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(SSHTask, "SSH", 17 * 1024, nullptr, 5,
                            &ssh_task_handle, 1);
  }
  xSemaphoreGive(ssh_start_sem);
}

} // namespace atmt
