#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define HEAP_SIZE (1024 * 1024)
#define STACK_CELLS 512

// Disable fault handling
#define forth_faults_setup()
#define FAULT_ENTRY

#define VOCABULARY_LIST V(forth) V(internals)

// FORTH-YIELD: stops forth_run
// CAPTURE-TYPE: prints to stdout for debugging
#define PLATFORM_OPCODE_LIST \
  X("FORTH-YIELD", FORTH_YIELD, PARK; return rp) \
  X("CAPTURE-TYPE", CAPTURE_TYPE, fwrite(c1, 1, n0, stdout); fflush(stdout); NIP; DROP) \
  XV(internals, "RAW-TERMINATE", RAW_TERMINATE, ) \
  YV(internals, MALLOC, SET malloc(n0)) \
  YV(internals, SYSFREE, free(a0); DROP) \
  YV(internals, REALLOC, SET realloc(a1, n0); NIP) \
  CALLING_OPCODE_LIST \
  FLOATING_POINT_LIST

#include "common/tier0_opcodes.h"
#include "common/tier1_opcodes.h"
#include "common/tier2_opcodes.h"
#include "common/calling.h"
#include "common/bits.h"

// Safe evaluate1 — same as ESP32 version
#define evaluate1 evaluate1_stock
#include "common/core.h"
#undef evaluate1

static cell_t *evaluate1(cell_t *rp) {
  cell_t call = 0;
  cell_t tos, *sp, *ip;
  float *fp;
  UNPARK;
  cell_t name;
  cell_t len = parse(' ', &name);
  if (len == 0) { DUP; tos = 0; PARK; return rp; }
  cell_t xt = find((const char *) name, len);
  if (xt) {
    if (g_sys->state && !(*TOFLAGS(xt) & IMMEDIATE)) {
      COMMA(xt);
    } else {
      call = xt;
    }
    if (g_sys->tin > 24900) {
      char dbg[64];
      int dlen = len < 63 ? len : 63;
      memcpy(dbg, (const char *)name, dlen);
      dbg[dlen] = 0;
      fprintf(stderr, "EXEC[%ld]: '%s' xt=%p call=%ld state=%ld\n",
              (long)g_sys->tin, dbg, (void*)xt, (long)call, (long)g_sys->state);
    }
  } else {
    char buf[64];
    int blen = len < 63 ? len : 63;
    memcpy(buf, (const char *)name, blen);
    buf[blen] = 0;
    fprintf(stderr, "NOTFOUND: '%s'\n", buf);
    DUP; tos = 0; PARK; return rp;
  }
  PUSH call;
  PARK;
  return rp;
}

#include "common/calls.h"
#include "common/floats.h"
#include "common/interp.h"

#include "test_boot.h"

int main(int argc, char *argv[]) {
  void *heap = mmap(
      (void *) 0x8000000, HEAP_SIZE,
      PROT_EXEC | PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (heap == MAP_FAILED) {
    heap = malloc(HEAP_SIZE);
  }
  fprintf(stderr, "Heap at %p, size %d\n", heap, HEAP_SIZE);

  forth_init(0, 0, heap, HEAP_SIZE, forth_boot, sizeof(forth_boot) - 1);
  fprintf(stderr, "forth_init done, running boot...\n");

  g_sys->rp = forth_run(g_sys->rp);

  if (!g_sys->rp) {
    fprintf(stderr, "CRASH: forth_run returned NULL rp!\n");
    return 1;
  }

  fprintf(stderr, "Boot OK! heap used = %d bytes\n",
          (int)((char *)g_sys->heap - (char *)heap));

  // Test: evaluate an unknown word — should print error, not crash
  {
    const char *test = "lol forth-yield";
    const char *old_tib = g_sys->tib;
    cell_t old_ntib = g_sys->ntib;
    cell_t old_tin = g_sys->tin;
    g_sys->tib = test;
    g_sys->ntib = strlen(test);
    g_sys->tin = 0;
    fprintf(stderr, "Testing unknown word 'lol'...\n");
    g_sys->rp = forth_run(g_sys->rp);
    g_sys->tib = old_tib;
    g_sys->ntib = old_ntib;
    g_sys->tin = old_tin;
    if (!g_sys->rp) {
      fprintf(stderr, "CRASH: unknown word caused NULL rp!\n");
      return 1;
    }
    fprintf(stderr, "Unknown word test passed!\n");
  }

  // Test: evaluate valid Forth — should work normally
  {
    const char *test = "2 3 + . forth-yield";
    g_sys->tib = test;
    g_sys->ntib = strlen(test);
    g_sys->tin = 0;
    fprintf(stderr, "Testing '2 3 + .'...\n");
    g_sys->rp = forth_run(g_sys->rp);
    if (!g_sys->rp) {
      fprintf(stderr, "CRASH: valid eval caused NULL rp!\n");
      return 1;
    }
    fprintf(stderr, "Valid eval test passed!\n");
  }

  return 0;
}
