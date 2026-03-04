#include "forth.hpp"

#include "app.hpp"
#include "common_esp32.hpp"
#include "keyer.hpp"

#include <string>

// --- ueforth integration ---
// We include ueforth headers directly, defining a minimal configuration.

// Capture buffer for Forth output
static std::string *forth_capture_buf = nullptr;

// Boot source (generated from forth_boot.fs)
#include "forth_boot.h"

// Minimal configuration — no WiFi, SPIFFS, Serial, etc.
#define STACK_CELLS 512
#define MINIMUM_FREE_SYSTEM_HEAP (64 * 1024)

// Minimal vocabulary list — just forth and internals
#define VOCABULARY_LIST V(forth) V(internals)

// Custom PLATFORM_OPCODE_LIST with only what we need
#define PLATFORM_OPCODE_LIST                                                   \
  X("MS-TICKS", MS_TICKS, PUSH millis())                                       \
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
  CALLING_OPCODE_LIST                                                          \
  FLOATING_POINT_LIST

// Disable fault handling
#define forth_faults_setup()
#define FAULT_ENTRY

// ueforth core headers (order matters — matches ESP32forth.ino)

#include "../lib/ueforth/common/tier0_opcodes.h"
#include "../lib/ueforth/common/tier1_opcodes.h"
#include "../lib/ueforth/common/tier2_opcodes.h"

#include "../lib/ueforth/common/calling.h"

#include "../lib/ueforth/common/bits.h"

#include "../lib/ueforth/common/core.h"

#include "../lib/ueforth/common/calls.h"

#include "../lib/ueforth/common/floats.h"

#include "../lib/ueforth/common/interp.h"

// --- Text buffer ---
namespace atmt {

static std::string text_buffer;
static int cursor_pos = 0;
static constexpr int kMaxBufferLen = 200;
static bool forth_ready = false;

// --- Forth VM ---

static std::string ForthEval(const char *text, size_t len) {
  if (!forth_ready || !g_sys)
    return "";
  std::string output;
  forth_capture_buf = &output;

  // Save and restore TIB state
  const char *old_tib = g_sys->tib;
  cell_t old_ntib = g_sys->ntib;
  cell_t old_tin = g_sys->tin;

  g_sys->tib = text;
  g_sys->ntib = len;
  g_sys->tin = 0;

  // Run the interpreter loop until all input is consumed
  while (g_sys->tin < g_sys->ntib) {
    g_sys->rp = forth_run(g_sys->rp);
    if (!g_sys->rp) {
      Debugf("Forth eval error\n");
      break;
    }
  }

  g_sys->tib = old_tib;
  g_sys->ntib = old_ntib;
  g_sys->tin = old_tin;

  forth_capture_buf = nullptr;
  return output;
}

// --- Chord handlers ---

std::unique_ptr<App> old_app;

struct ForthAppWrapper : App {
  void OnSetup() override { old_app->OnSetup(); }
  void OnLoop() override { old_app->OnLoop(); }
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

    text_buffer.insert(text_buffer.begin() + cursor_pos, (char)codepoint);
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

static void ForthEvalAppend() {
  auto output = ForthEval(text_buffer.data(), text_buffer.size());
  for (char c : output) {
    old_app->OnUnicode(c, 0);
  }
}

static void ForthEvalReplace() {
  int after = text_buffer.size() - cursor_pos;
  for (int i = 0; i < after; i++) {
    old_app->OnKey(IBM_Key::DELETE, 0);
  }
  for (int i = 0; i < cursor_pos; i++) {
    old_app->OnKey(IBM_Key::BACKSPACE, 0);
  }

  std::string saved = std::move(text_buffer);
  text_buffer.clear();
  cursor_pos = 0;

  auto output = ForthEval(saved.data(), saved.size());
  for (char c : output) {
    old_app->OnUnicode(c, 0);
  }
}

// --- Init ---

void ForthInit() {
  Debugln("Forth init 1");
  old_app = std::move(current_app);
  current_app = std::make_unique<ForthAppWrapper>();

  Debugln("Forth init 2");
  constexpr size_t kHeapSize = 64 * 1024;
  cell_t *heap = (cell_t *)heap_caps_malloc(kHeapSize, MALLOC_CAP_SPIRAM);

  Debugln("Forth init 3");
  if (!heap) {
    heap = (cell_t *)malloc(kHeapSize);
  }
  if (!heap) {
    Debugf("Forth: failed to allocate heap\n");
    return;
  }

  Debugln("Forth init 4");
  forth_init(0, 0, heap, kHeapSize, forth_boot, sizeof(forth_boot) - 1);

  Debugln("Forth init 5");
  // Run boot code to completion
  while (g_sys->rp) {
    g_sys->rp = forth_run(g_sys->rp);
  }

  Debugln("Forth init 6");

  // Set up type to use our capture builtin via evaluate
  const char *setup = "' capture-type is type";
  g_sys->tib = setup;
  g_sys->ntib = strlen(setup);
  g_sys->tin = 0;

  Debugln("Forth init 7");

  // Build a small interpreter loop in the heap
  cell_t *start = g_sys->heap;
  cell_t evaluate1_xt = find("EVALUATE1", 9);
  cell_t branch_xt = find("BRANCH", 6);
  if (evaluate1_xt && branch_xt) {
    COMMA(evaluate1_xt);
    COMMA(branch_xt);
    COMMA(start);

    // Allocate stacks for evaluation
    float *fp_s = (float *)(g_sys->heap + 1);
    g_sys->heap += STACK_CELLS;
    cell_t *rp_s = g_sys->heap + 1;
    g_sys->heap += STACK_CELLS;
    cell_t *sp_s = g_sys->heap + 1;
    g_sys->heap += STACK_CELLS;

    *++rp_s = (cell_t)start;
    *++rp_s = (cell_t)fp_s;
    *sp_s = 0;
    *++rp_s = (cell_t)sp_s;

    cell_t *rp = rp_s;
    while (g_sys->tin < g_sys->ntib) {
      rp = forth_run(rp);
      if (!rp)
        break;
    }
    // Save rp for future evaluations
    g_sys->rp = rp;
  }

  Debugln("Forth init 8");

  forth_ready = true;
  Debugf("Forth: initialized, heap used %d bytes\n",
         (int)((uint8_t *)g_sys->heap - (uint8_t *)g_sys->heap_start));

  RegisterChord(2, 1, 0, 2, 0, ForthEvalAppend);
  RegisterChord(2, 1, 0, 2, 1, ForthEvalReplace);
}

} // namespace atmt
