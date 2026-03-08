#include "tcl.hpp"

#include "common_esp32.hpp"
#include "keyer.hpp"
#include "ssh.hpp"
#include "typist.hpp"

#include <string>

#include "SPIFFS.h"

extern "C" {
#include <jim.h>
}

namespace atmt {

static Jim_Interp *interp = nullptr;

// --- stdout → typist redirect via funopen ---
// stdout is permanently replaced with a custom FILE* that writes to
// editor.target and wakes the typist. This means puts, printf, and any
// other stdout output appears on the BLE keyboard. Debug output uses
// Serial directly and is unaffected.

static int stdout_write(void *cookie, const char *buf, int len) {
  if (len <= 0)
    return len;

  EditorState &tgt = editor.target;

  for (int i = 0; i < len; i++) {
    char c = buf[i];
    if (c == '\n') {
      // Start a new row
      if (tgt.num_rows < EditorState::kRows) {
        tgt.rows[tgt.num_rows] = "";
        tgt.num_rows++;
      }
      tgt.cursor_row = tgt.num_rows - 1;
      tgt.cursor_col = 0;
    } else {
      int r = tgt.cursor_row;
      if (r < 0 || r >= tgt.num_rows) {
        r = 0;
        tgt.cursor_row = 0;
        if (tgt.num_rows == 0) {
          tgt.rows[0] = "";
          tgt.num_rows = 1;
        }
      }
      tgt.rows[r] += c;
      tgt.cursor_col = tgt.rows[r].size();
    }
  }

  WakeTypist();
  return len;
}

static void RedirectStdout() {
  FILE *fp = funopen(nullptr, nullptr, stdout_write, nullptr, nullptr);
  if (!fp) {
    Debugf("Tcl: failed to create stdout redirect\n");
    return;
  }
  // Unbuffered so output appears immediately
  setvbuf(fp, nullptr, _IONBF, 0);
  fflush(stdout);
  stdout = fp;
}

// --- Tcl eval with output capture ---

static std::string TclEval(const char *text, size_t len) {
  if (!interp)
    return "";

  std::string script(text, len);
  int ret = Jim_Eval(interp, script.c_str());
  const char *result = Jim_String(Jim_GetResult(interp));

  if (ret == JIM_ERR) {
    std::string err = "ERROR: ";
    err += result;
    return err;
  }

  return result ? result : "";
}

// --- Custom Tcl commands ---

static int SshCmd(Jim_Interp *interp, int argc, Jim_Obj *const *argv) {
  StartSSHSession();
  return JIM_OK;
}

static int HeapCmd(Jim_Interp *interp, int argc, Jim_Obj *const *argv) {
  std::string info = DebugHeapStr("tcl");
  Jim_SetResultString(interp, info.c_str(), info.size());
  return JIM_OK;
}

// --- Eval-in-place chord handler ---

static void TclEvalInPlace() {
  EditorState &tgt = editor.target;
  int r = tgt.cursor_row;
  if (r < 0 || r >= tgt.num_rows)
    return;

  std::string &row = tgt.rows[r];
  int col = tgt.cursor_col;
  if (col > (int)row.size())
    col = row.size();

  std::string code = row.substr(0, col);
  Debugf("Tcl eval(%s)", code.c_str());

  // Truncate old output after cursor
  row = code;

  // Evaluate
  auto output = TclEval(code.data(), code.size());
  Debugf(" => '%s'\n", output.c_str());

  // Append output after cursor position
  row += output;

  WakeTypist();
}

// --- Init ---

void TclInit() {
  // Mount SPIFFS for scripts
  if (!SPIFFS.begin(true, "/spiffs", 10)) {
    Debugf("Tcl: SPIFFS mount failed\n");
  } else {
    Debugf("Tcl: SPIFFS mounted (%d/%d bytes used)\n",
           (int)SPIFFS.usedBytes(), (int)SPIFFS.totalBytes());
  }

  RedirectStdout();

  interp = Jim_CreateInterp();
  if (!interp) {
    Debugf("Tcl: failed to create interpreter\n");
    return;
  }

  Jim_RegisterCoreCommands(interp);

  // Register custom commands
  Jim_CreateCommand(interp, "ssh", SshCmd, nullptr, nullptr);
  Jim_CreateCommand(interp, "heap", HeapCmd, nullptr, nullptr);

  Debugf("Tcl: ready\n");

  // Source init script from SPIFFS if it exists
  FILE *f = fopen("/spiffs/init.tcl", "r");
  if (f) {
    fclose(f);
    int ret = Jim_EvalFile(interp, "/spiffs/init.tcl");
    if (ret == JIM_ERR) {
      Debugf("Tcl: init.tcl error: %s\n",
             Jim_String(Jim_GetResult(interp)));
    } else {
      Debugf("Tcl: init.tcl loaded\n");
    }
  }

  RegisterChord(2, 1, 0, 2, 0, TclEvalInPlace);
  RegisterChord(1, 1, 0, 2, 0, DebugDumpEditor);
  RegisterChord(3, 2, 2, 2, 0, [] {
    std::string out = DebugHeapStr("DebugChord");
    out += '\n';
    char tmp[128];
    auto dump = [&](const char *name, TaskHandle_t h) {
      if (!h)
        return;
      auto state = eTaskGetState(h);
      if (state == eRunning)
        return;
      const char *s = "?";
      switch (state) {
      case eReady:
        s = "RDY";
        break;
      case eBlocked:
        s = "BLK";
        break;
      case eSuspended:
        s = "SUS";
        break;
      case eDeleted:
        s = "DEL";
        break;
      default:
        break;
      }
      snprintf(
          tmp, sizeof(tmp), "                          %-16s %s stack_free=%u ",
          name, s,
          (unsigned)(uxTaskGetStackHighWaterMark(h) * sizeof(StackType_t)));
      out += tmp;
      uint32_t *sp = (uint32_t *)pxTCBGetTopOfStack(h);
      uint32_t *end = (uint32_t *)pxTCBGetEndOfStack(h);
      int nwords = (end > sp) ? (end - sp) : 0;
      if (nwords > 256)
        nwords = 256;
      int found = 0;
      for (int i = 0; i < nwords && found < 12; i++) {
        uint32_t w = sp[i];
        uint32_t pc = (w & 0x3FFFFFFF) | 0x40000000;
        if ((w >> 30) != 0 && pc >= 0x40000000 && pc < 0x43000000) {
          snprintf(tmp, sizeof(tmp), "0x%08x ", pc);
          out += tmp;
          found++;
        }
      }
      if (!found)
        out += "(no code addrs)";
      out += "\n";
    };
    extern TaskHandle_t typist_task_handle;
    dump("Typist", typist_task_handle);
    extern TaskHandle_t ssh_task_handle;
    dump("SSH", ssh_task_handle);
    out.pop_back();
    Debugf("%s", out.c_str());
  });
}

} // namespace atmt
