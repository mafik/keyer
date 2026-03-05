#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>

#define HEAP_SIZE (1024 * 1024)
#define STACK_CELLS 512

#define forth_faults_setup()
#define FAULT_ENTRY

#define VOCABULARY_LIST V(forth) V(internals)

static char *capture_buf = NULL;
static int capture_len = 0;
static int capture_cap = 0;

static void capture_append(const char *s, int n) {
  if (capture_len + n >= capture_cap) {
    capture_cap = (capture_len + n + 1) * 2;
    capture_buf = (char *)realloc(capture_buf, capture_cap);
  }
  memcpy(capture_buf + capture_len, s, n);
  capture_len += n;
  capture_buf[capture_len] = 0;
}

#define PLATFORM_OPCODE_LIST \
  X("FORTH-YIELD", FORTH_YIELD, PARK; return rp) \
  X("CAPTURE-TYPE", CAPTURE_TYPE, capture_append(c1, n0); NIP; DROP) \
  XV(internals, "RAW-TERMINATE", RAW_TERMINATE, ) \
  YV(internals, MALLOC, SET malloc(n0)) \
  YV(internals, SYSFREE, free(a0); DROP) \
  YV(internals, REALLOC, SET realloc(a1, n0); NIP) \
  X("R/O", R_O, PUSH 0) \
  X("W/O", W_O, PUSH 1) \
  X("R/W", R_W, PUSH 2) \
  Y(BIN, ) \
  X("CLOSE-FILE", CLOSE_FILE, tos = close(tos); tos = tos ? 0 : 0) \
  X("OPEN-FILE", OPEN_FILE, cell_t mode = n0; DROP; cell_t len = n0; DROP; \
    n0 = open("/dev/null", mode, 0777); PUSH n0 < 0 ? 1 : 0) \
  X("CREATE-FILE", CREATE_FILE, cell_t mode = n0; DROP; cell_t len = n0; DROP; \
    n0 = open("/dev/null", mode | 0100 | 01000); PUSH n0 < 0 ? 1 : 0) \
  X("DELETE-FILE", DELETE_FILE, cell_t len = n0; DROP; n0 = -1) \
  X("RENAME-FILE", RENAME_FILE, DROPn(3); n0 = -1) \
  X("WRITE-FILE", WRITE_FILE, cell_t fd = n0; DROP; cell_t len = n0; DROP; \
    n0 = write(fd, a0, len); n0 = n0 != len ? 1 : 0) \
  X("READ-FILE", READ_FILE, cell_t fd = n0; DROP; cell_t len = n0; DROP; \
    n0 = read(fd, a0, len); PUSH n0 < 0 ? 1 : 0) \
  X("FILE-POSITION", FILE_POSITION, n0 = 0; PUSH 0) \
  X("REPOSITION-FILE", REPOSITION_FILE, cell_t fd = n0; DROP; n0 = 0) \
  X("RESIZE-FILE", RESIZE_FILE, cell_t fd = n0; DROP; n0 = 0) \
  X("FILE-SIZE", FILE_SIZE, n0 = 0; PUSH 0) \
  X("NON-BLOCK", NON_BLOCK, n0 = 0) \
  X("OPEN-DIR", OPEN_DIR, n1 = 0; n0 = -1) \
  X("CLOSE-DIR", CLOSE_DIR, n0 = 0) \
  YV(internals, READDIR, SET 0) \
  CALLING_OPCODE_LIST \
  FLOATING_POINT_LIST

#include "common/tier0_opcodes.h"
#include "common/tier1_opcodes.h"
#include "common/tier2_opcodes.h"
#include "common/calling.h"
#include "common/bits.h"

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
  } else {
    char dbg[64];
    int dlen = len < 63 ? len : 63;
    memcpy(dbg, (const char *)name, dlen);
    dbg[dlen] = 0;
    fprintf(stderr, "NOTFOUND: '%s'\n", dbg);
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

static void forth_eval(const char *text) {
  char input[1024];
  snprintf(input, sizeof(input), "%s forth-yield", text);

  capture_len = 0;
  if (capture_buf) capture_buf[0] = 0;

  const char *old_tib = g_sys->tib;
  cell_t old_ntib = g_sys->ntib;
  cell_t old_tin = g_sys->tin;

  g_sys->tib = input;
  g_sys->ntib = strlen(input);
  g_sys->tin = 0;

  g_sys->rp = forth_run(g_sys->rp);

  g_sys->tib = old_tib;
  g_sys->ntib = old_ntib;
  g_sys->tin = old_tin;

  if (capture_buf && capture_len > 0) {
    printf("Output: [%s]\n", capture_buf);
  } else {
    printf("Output: (none)\n");
  }
}

int main(int argc, char *argv[]) {
  void *heap = mmap(
      (void *) 0x8000000, HEAP_SIZE,
      PROT_EXEC | PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (heap == MAP_FAILED) heap = malloc(HEAP_SIZE);

  forth_init(0, 0, heap, HEAP_SIZE, forth_boot, sizeof(forth_boot) - 1);
  fprintf(stderr, "Running boot...\n");
  g_sys->rp = forth_run(g_sys->rp);
  if (!g_sys->rp) { fprintf(stderr, "BOOT FAILED\n"); return 1; }
  fprintf(stderr, "Boot OK!\n");

  // Check stack right after boot
  printf("--- After boot ---\n");
  forth_eval(".s");

  printf("--- 4 4 + .s ---\n");
  forth_eval("4 4 + .s");

  printf("--- .s again ---\n");
  forth_eval(".s");

  return 0;
}
