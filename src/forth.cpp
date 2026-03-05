#include "forth.hpp"

#include "app.hpp"
#include "common_esp32.hpp"
#include "keyer.hpp"

#include <string>

// File I/O support for Forth dictionary save/restore
#include "SPIFFS.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// --- ueforth integration ---
// We include ueforth headers directly, defining a minimal configuration.

// Capture buffer for Forth output
static std::string *forth_capture_buf = nullptr;

// Boot source (generated from forth_boot.fs)
#include "forth_boot.h"

// File I/O buffers (used by REQUIRED_FILES_SUPPORT opcodes)
static char filename[PATH_MAX];
static char filename2[PATH_MAX];

#define PRINT_ERRORS 0
#define STACK_CELLS 512
#define MINIMUM_FREE_SYSTEM_HEAP (64 * 1024)

// Minimal vocabulary list — just forth and internals
#define VOCABULARY_LIST V(forth) V(internals)

// Custom PLATFORM_OPCODE_LIST
// FORTH-YIELD: like YIELD but in the forth vocabulary (always findable)
#define PLATFORM_OPCODE_LIST                                                   \
  X("MS-TICKS", MS_TICKS, PUSH millis())                                       \
  X("FORTH-YIELD", FORTH_YIELD, PARK; return rp)                               \
  XV(internals, "RAW-YIELD", RAW_YIELD, yield())                               \
  XV(internals, "RAW-TERMINATE", RAW_TERMINATE, )                              \
  X(                                                                           \
      "CAPTURE-TYPE", CAPTURE_TYPE,                                            \
      if (forth_capture_buf) { forth_capture_buf->append(c1, n0); } NIP;       \
      DROP)                                                                    \
  YV(internals, MALLOC, SET malloc(n0))                                        \
  YV(internals, SYSFREE, free(a0); DROP)                                       \
  YV(internals, REALLOC, SET realloc(a1, n0); NIP)                             \
  YV(internals, heap_caps_malloc, SET heap_caps_malloc(n1, n0); NIP)           \
  YV(internals, heap_caps_free, heap_caps_free(a0); DROP)                      \
  YV(internals, heap_caps_realloc,                                             \
     tos = (cell_t)heap_caps_realloc(a2, n1, n0);                              \
     NIPn(2))                                                                  \
  YV(internals, heap_caps_get_total_size, n0 = heap_caps_get_total_size(n0))   \
  YV(internals, heap_caps_get_free_size, n0 = heap_caps_get_free_size(n0))     \
  YV(internals, heap_caps_get_minimum_free_size,                               \
     n0 = heap_caps_get_minimum_free_size(n0))                                 \
  YV(internals, heap_caps_get_largest_free_block,                              \
     n0 = heap_caps_get_largest_free_block(n0))                                \
  REQUIRED_FILES_SUPPORT                                                       \
  CALLING_OPCODE_LIST                                                          \
  FLOATING_POINT_LIST

#define REQUIRED_FILES_SUPPORT                                                 \
  X("R/O", R_O, PUSH O_RDONLY)                                                 \
  X("W/O", W_O, PUSH O_WRONLY)                                                 \
  X("R/W", R_W, PUSH O_RDWR)                                                   \
  Y(BIN, )                                                                     \
  X("CLOSE-FILE", CLOSE_FILE, tos = close(tos); tos = tos ? errno : 0)         \
  X("FLUSH-FILE", FLUSH_FILE, fsync(tos); tos = 0)                             \
  X("OPEN-FILE", OPEN_FILE, cell_t mode = n0; DROP; cell_t len = n0; DROP;     \
    memcpy(filename, a0, len); filename[len] = 0;                              \
    n0 = open(filename, mode, 0777); PUSH n0 < 0 ? errno : 0)                  \
  X("CREATE-FILE", CREATE_FILE, cell_t mode = n0; DROP; cell_t len = n0; DROP; \
    memcpy(filename, a0, len); filename[len] = 0;                              \
    n0 = open(filename, mode | O_CREAT | O_TRUNC); PUSH n0 < 0 ? errno : 0)    \
  X("DELETE-FILE", DELETE_FILE, cell_t len = n0; DROP;                         \
    memcpy(filename, a0, len); filename[len] = 0; n0 = unlink(filename);       \
    n0 = n0 ? errno : 0)                                                       \
  X("RENAME-FILE", RENAME_FILE, cell_t len = n0; DROP;                         \
    memcpy(filename, a0, len); filename[len] = 0; DROP; cell_t len2 = n0;      \
    DROP; memcpy(filename2, a0, len2); filename2[len2] = 0;                    \
    n0 = rename(filename2, filename); n0 = n0 ? errno : 0)                     \
  X("WRITE-FILE", WRITE_FILE, cell_t fd = n0; DROP; cell_t len = n0; DROP;     \
    n0 = write(fd, a0, len); n0 = n0 != len ? errno : 0)                       \
  X("READ-FILE", READ_FILE, cell_t fd = n0; DROP; cell_t len = n0; DROP;       \
    n0 = read(fd, a0, len); PUSH n0 < 0 ? errno : 0)                           \
  X("FILE-POSITION", FILE_POSITION, n0 = (cell_t)lseek(n0, 0, SEEK_CUR);       \
    PUSH n0 < 0 ? errno : 0)                                                   \
  X("REPOSITION-FILE", REPOSITION_FILE, cell_t fd = n0; DROP;                  \
    n0 = (cell_t)lseek(fd, tos, SEEK_SET); n0 = n0 < 0 ? errno : 0)            \
  X("RESIZE-FILE", RESIZE_FILE, cell_t fd = n0; DROP;                          \
    n0 = ResizeFile(fd, tos))                                                  \
  X("FILE-SIZE", FILE_SIZE, struct stat st; w = fstat(n0, &st);                \
    n0 = (cell_t)st.st_size; PUSH w < 0 ? errno : 0)                           \
  X("NON-BLOCK", NON_BLOCK, n0 = fcntl(n0, F_SETFL, O_NONBLOCK);               \
    n0 = n0 < 0 ? errno : 0)                                                   \
  X("OPEN-DIR", OPEN_DIR, memcpy(filename, a1, n0); filename[n0] = 0;          \
    n1 = (cell_t)opendir(filename); n0 = n1 ? 0 : errno)                       \
  X("CLOSE-DIR", CLOSE_DIR, n0 = closedir((DIR *)n0); n0 = n0 ? errno : 0)     \
  YV(internals, READDIR, struct dirent *ent = readdir((DIR *)n0);              \
     SET(ent ? ent->d_name : 0))

// Disable fault handling
#define forth_faults_setup()
#define FAULT_ENTRY

// ueforth core headers (order matters — matches ESP32forth.ino)

#include "../lib/ueforth/common/tier0_opcodes.h"
#include "../lib/ueforth/common/tier1_opcodes.h"
#include "../lib/ueforth/common/tier2_opcodes.h"

#include "../lib/ueforth/common/calling.h"

#include "../lib/ueforth/common/bits.h"

// Work around lack of ftruncate on ESP32 (used by RESIZE-FILE opcode)
static cell_t ResizeFile(cell_t fd, cell_t size) {
  struct stat st;
  char buf[256];
  cell_t t = fstat(fd, &st);
  if (t < 0) {
    return errno;
  }
  if (size < st.st_size) {
    return ENOSYS;
  }
  cell_t oldpos = lseek(fd, 0, SEEK_CUR);
  if (oldpos < 0) {
    return errno;
  }
  t = lseek(fd, 0, SEEK_END);
  if (t < 0) {
    return errno;
  }
  memset(buf, 0, sizeof(buf));
  while (st.st_size < size) {
    cell_t wlen = sizeof(buf);
    if (size - st.st_size < wlen) {
      wlen = size - st.st_size;
    }
    t = write(fd, buf, wlen);
    if (t != wlen) {
      return errno;
    }
    st.st_size += t;
  }
  t = lseek(fd, oldpos, SEEK_SET);
  if (t < 0) {
    return errno;
  }
  return 0;
}

// Replace evaluate1 with a version that doesn't crash on unfound words.
// The stock evaluate1 returns NULL on error, which causes EVALUATE1 opcode
// to UNPARK from NULL → LoadProhibited crash. Our version returns rp with
// tos=0 (skip), matching the empty-parse behavior.
#define evaluate1 evaluate1_stock
#include "../lib/ueforth/common/core.h"
#undef evaluate1

static cell_t *evaluate1(cell_t *rp) {
  cell_t call = 0;
  cell_t tos, *sp, *ip;
  float *fp;
  UNPARK;
  cell_t name;
  cell_t len = parse(' ', &name);
  if (len == 0) {
    DUP;
    tos = 0;
    PARK;
    return rp;
  }
  cell_t xt = find((const char *)name, len);
  if (xt) {
    if (g_sys->state && !(*TOFLAGS(xt) & IMMEDIATE)) {
      COMMA(xt);
    } else {
      call = xt;
    }
  } else {
    char buf[32];
    int blen = len < 31 ? len : 31;
    memcpy(buf, (const char *)name, blen);
    buf[blen] = 0;
    Debugf("Forth: '%s' not found\n", buf);
    DUP;
    tos = 0;
    PARK;
    return rp;
  }
  PUSH call;
  PARK;
  return rp;
}

#include "../lib/ueforth/common/calls.h"

#include "../lib/ueforth/common/floats.h"

#include "../lib/ueforth/common/interp.h"

// --- Text buffer ---
namespace atmt {

static std::string text_buffer;
static int cursor_pos = 0;
static constexpr int kMaxBufferLen = 200;
static bool forth_ready = false;

static constexpr const char *kDictFile = "/spiffs/forth.dat";

// --- Forth VM ---

// ForthEval: evaluate Forth text synchronously.
// Appends " forth-yield" so that interpret0 yields back to us after processing.
static std::string ForthEval(const char *text, size_t len) {
  if (!forth_ready || !g_sys || !g_sys->rp)
    return "";

  // Build input with trailing yield
  std::string input(text, len);
  input += " forth-yield";

  std::string output;
  forth_capture_buf = &output;

  // Save and restore TIB state
  const char *old_tib = g_sys->tib;
  cell_t old_ntib = g_sys->ntib;
  cell_t old_tin = g_sys->tin;

  g_sys->tib = input.c_str();
  g_sys->ntib = input.size();
  g_sys->tin = 0;

  // forth_run resumes interpret0, processes input, hits forth-yield → returns
  g_sys->rp = forth_run(g_sys->rp);

  g_sys->tib = old_tib;
  g_sys->ntib = old_ntib;
  g_sys->tin = old_tin;

  forth_capture_buf = nullptr;
  return output;
}

// Save user dictionary to SPIFFS
static void ForthSave() {
  if (!forth_ready)
    return;
  auto result = ForthEval("s\" "
                          "/spiffs/forth.dat"
                          "\" save-name",
                          strlen("s\" "
                                 "/spiffs/forth.dat"
                                 "\" save-name"));
  Debugf("Forth: save → '%s'\n", result.c_str());
}

// Restore user dictionary from SPIFFS (if file exists)
static void ForthRestore() {
  struct stat st;
  if (stat(kDictFile, &st) != 0) {
    Debugf("Forth: no saved dictionary\n");
    return;
  }
  Debugf("Forth: restoring dictionary (%d bytes)\n", (int)st.st_size);
  auto result = ForthEval("s\" "
                          "/spiffs/forth.dat"
                          "\" restore-name",
                          strlen("s\" "
                                 "/spiffs/forth.dat"
                                 "\" restore-name"));
  Debugf("Forth: restore → '%s'\n", result.c_str());
}

// --- Chord handlers ---

std::unique_ptr<App> old_app;

struct ForthAppWrapper : App {
  void OnSetup() override { old_app->OnSetup(); }
  void OnLoop() override { old_app->OnLoop(); }
  // Apply shift to ASCII codepoint (US keyboard layout)
  static uint32_t ApplyShift(uint32_t c) {
    static const char unshifted[] = "`1234567890-=[]\\;',./";
    static const char shifted[] = "~!@#$%^&*()_+{}|:\"<>?";
    if (c >= 'a' && c <= 'z')
      return c - 32;
    for (int i = 0; unshifted[i]; i++) {
      if (c == (uint32_t)unshifted[i])
        return shifted[i];
    }
    return c;
  }

  void OnUnicode(uint32_t codepoint, Modifier mods) override {
    old_app->OnUnicode(codepoint, mods);

    if (codepoint == '\n' || codepoint == '\r' || codepoint == '\t' ||
        codepoint == 0x1b) {
      text_buffer.clear();
      cursor_pos = 0;
      return;
    }

    if (codepoint < 32 || codepoint > 126)
      return;
    if ((int)text_buffer.size() >= kMaxBufferLen)
      return;

    // Apply shift modifier so text_buffer matches what the host sees
    uint32_t ch = (mods & MOD_SHIFT) ? ApplyShift(codepoint) : codepoint;
    text_buffer.insert(text_buffer.begin() + cursor_pos, (char)ch);
    cursor_pos++;
  }

  void OnKey(IBM_Key key, Modifier mods) override {
    old_app->OnKey(key, mods);
    bool ctrl = mods & MOD_CTRL;
    switch (key) {
    case IBM_Key::BACKSPACE:
      if (cursor_pos > 0) {
        cursor_pos--;
        text_buffer.erase(cursor_pos, 1);
      }
      break;
    case IBM_Key::DELETE:
      if (cursor_pos < (int)text_buffer.size()) {
        text_buffer.erase(cursor_pos, 1);
      }
      break;
    case IBM_Key::LEFT_ARROW:
      if (ctrl) {
        while (cursor_pos > 0 && text_buffer[cursor_pos - 1] == ' ')
          cursor_pos--;
        while (cursor_pos > 0 && text_buffer[cursor_pos - 1] != ' ')
          cursor_pos--;
      } else if (cursor_pos > 0) {
        cursor_pos--;
      }
      break;
    case IBM_Key::RIGHT_ARROW:
      if (ctrl) {
        int len = text_buffer.size();
        while (cursor_pos < len && text_buffer[cursor_pos] != ' ')
          cursor_pos++;
        while (cursor_pos < len && text_buffer[cursor_pos] == ' ')
          cursor_pos++;
      } else if (cursor_pos < (int)text_buffer.size()) {
        cursor_pos++;
      }
      break;
    case IBM_Key::HOME:
      cursor_pos = 0;
      break;
    case IBM_Key::END:
      cursor_pos = text_buffer.size();
      break;
    case IBM_Key::ENTER:
    case IBM_Key::TAB:
    case IBM_Key::ESC:
      text_buffer.clear();
      cursor_pos = 0;
      break;
    default:
      break;
    }
  }
  void OnBattery(int percent) override { old_app->OnBattery(percent); }
};

static void ForthEvalInPlace() {
  // 1. Delete everything after cursor (old results)
  int after = text_buffer.size() - cursor_pos;
  text_buffer.erase(cursor_pos);
  Debugf("Forth eval(%s)", text_buffer.c_str());
  for (int i = 0; i < after; i++) {
    old_app->OnKey(IBM_Key::DELETE, 0);
  }

  // 2. Evaluate the code (everything before cursor = whole buffer now)
  auto output = ForthEval(text_buffer.data(), text_buffer.size());
  Debugf(" => '%s'\n", output.c_str());

  // 3. Emit output via BLE and append to internal buffer
  for (int i = output.size() - 1; i >= 0; --i) {
    char c = output[i];
    old_app->OnUnicode(c, 0);
    old_app->OnKey(IBM_Key::LEFT_ARROW, 0);
  }
  text_buffer.append(output);
}

// --- Init ---

void ForthInit() {
  old_app = std::move(current_app);
  current_app = std::make_unique<ForthAppWrapper>();

  // Mount SPIFFS for dictionary save/restore
  if (!SPIFFS.begin(true, "/spiffs", 10)) {
    Debugf("Forth: SPIFFS mount failed\n");
  } else {
    Debugf("Forth: SPIFFS mounted (%d/%d bytes used)\n",
           (int)SPIFFS.usedBytes(), (int)SPIFFS.totalBytes());
  }

  constexpr size_t kHeapSize = 64 * 1024;
  cell_t *heap = (cell_t *)heap_caps_malloc(kHeapSize, MALLOC_CAP_SPIRAM);
  if (!heap) {
    heap = (cell_t *)malloc(kHeapSize);
  }
  if (!heap) {
    Debugf("Forth: failed to allocate heap\n");
    return;
  }

  // Boot source ends with "forth-yield", so forth_run returns after boot.
  // After boot, interpret0 is running (begin +evaluate1 again).
  // We resume it via forth_run for each ForthEval call.
  forth_init(0, 0, heap, kHeapSize, forth_boot, sizeof(forth_boot) - 1);
  g_sys->rp = forth_run(g_sys->rp);

  if (!g_sys->rp) {
    Debugf("Forth: boot failed\n");
    return;
  }

  forth_ready = true;
  Debugf("Forth: ready, heap used %d bytes\n",
         (int)((uint8_t *)g_sys->heap - (uint8_t *)g_sys->heap_start));

  // Restore saved dictionary if available
  ForthRestore();

  RegisterChord(2, 1, 0, 2, 0, ForthEvalInPlace);
}

} // namespace atmt
