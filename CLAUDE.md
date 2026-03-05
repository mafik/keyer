# Keyer Project

If along the way you execute some command, it doesn't work and then you figure out how to make it work, please record that in CLAUDE.md - this way you'll avoid making the same mistakes over and over.

## Project Overview

Chording keyboard (5 fingers: Thumb/Index/Middle/Ring/Pinky) running on ESP32-S3 with BLE HID. Pinky defaults to shift modifier but individual chords can use it explicitly. The firmware includes a chording keyboard layout, an embedded Forth interpreter (ueforth), SSH client, and terminal emulator.

**Board**: ESP32-S3 with 16MB flash, PSRAM, custom board definition `EyeTerm` in `boards/`
**Framework**: Arduino + ESP-IDF (dual framework)
**Build system**: PlatformIO

## Architecture

### Boot flow
1. `setup()` in `eye_term.cpp` → `InitESP32()` → `InitMainLoop()` → `InitKeyer()` → `App::Load()` → `ForthInit()`
2. `loop()` runs `MainLoopNonBlocking()` repeatedly
3. `ForthInit()` wraps `current_app` in `ForthAppWrapper` to intercept keyboard events

### Key source files
| File | Purpose |
|------|---------|
| `src/eye_term.cpp` | Entry point (setup/loop), calls ForthInit |
| `src/forth.cpp` | Forth VM integration — ueforth embedding, ForthEval, chord handlers |
| `src/forth.hpp` | Just declares `ForthInit()` |
| `src/forth_boot.fs` | Forth boot source — edit this, regenerate forth_boot.h |
| `src/forth_boot.h` | **Generated** from forth_boot.fs — never edit directly |
| `src/keyer.cpp` | Chord detection, layout, RegisterChord API |
| `src/app.hpp` | App interface (OnSetup, OnLoop, OnUnicode, OnKey, OnBattery) |
| `src/app_keyboard.cpp` | BLE HID keyboard app |
| `src/common_esp32.cpp` | Platform init, battery monitoring, Debugf() |
| `platformio.ini` | Build config — note `lib_ignore = ueforth` (we include headers directly) |

### Forth integration architecture

The Forth VM (ueforth) is embedded directly — not as a library, but by `#include`ing its `.h` files into `forth.cpp` with custom `PLATFORM_OPCODE_LIST` and `VOCABULARY_LIST` macros.

**Boot sequence:**
1. `ForthInit()` mounts SPIFFS, allocates 64KB heap (PSRAM preferred), calls `forth_init()` + `forth_run()`
2. Boot source (forth_boot.fs) loads phase1+allocation+phase2+filetools, sets up I/O and vocabularies
3. Boot source ends with `forth-yield` — a custom opcode that makes `forth_run()` return
4. After boot, `interpret0` (`begin +evaluate1 again`) is running and paused via yield
5. Each `ForthEval()` call resumes interpret0 with new TIB text + trailing `forth-yield`

**Custom opcodes:**
- `FORTH-YIELD` — `PARK; return rp` — yields from forth_run back to C++
- `CAPTURE-TYPE` — appends output to `forth_capture_buf` string
- `REQUIRED_FILES_SUPPORT` — full POSIX file I/O (open/read/write/close/etc.)
- Standard memory opcodes (malloc, heap_caps_*, etc.)

**Safe evaluate1:**
The stock `evaluate1` returns NULL on unfound words → UNPARK from NULL → crash. Our replacement skips unfound words and logs them via Debugf. This only matters during the initial C-level `EVALUATE1/BRANCH` boot loop (before `interpret0` takes over at boot.fs:105).

**ForthEval:**
- Appends ` forth-yield` to user input
- Saves/restores TIB state (tib, ntib, tin)
- Captures output via `forth_capture_buf` → `CAPTURE-TYPE` opcode
- Returns captured output as std::string

**Dictionary save/restore:**
- SPIFFS mounted at `/spiffs/` (3.4MB partition in default_16MB.csv)
- `filetools.fs` provides `save-name` / `restore-name` words
- `setup-saving-base` allocates space for the save checkpoint
- On boot, `ForthRestore()` checks for `/spiffs/forth.dat` and restores if present
- User saves with: `s" /spiffs/forth.dat" save-name` (via ForthEval)

**Chord handler:**
- Chord 2102/mod0 → ForthEvalInPlace: deletes text after cursor (old results), evaluates text before cursor, emits output via BLE AND appends to internal buffer, then moves host caret back to the code/results boundary. Repeated presses re-evaluate; cursor stays between code and output so user can edit code and re-run.

**Error handling (safe-notfound):**
- Boot source overrides `'notfound` with `safe-notfound` which prints `ERROR: <word> NOT FOUND!` instead of calling `throw`. This prevents crashes from unknown words during ForthEval (since `interpret0` has no `catch` handler on the stack).

## Build Commands

```bash
# Regenerate forth_boot.h from forth_boot.fs (MUST do after editing .fs)
python3 lib/ueforth/tools/importation.py -i src/forth_boot.fs -o src/forth_boot.h \
  -I lib/ueforth -I lib/ueforth/esp32 --name forth_boot --header cpp

# Build
pio run -e EyeTerm

# Upload
pio run -e EyeTerm -t upload --upload-port /dev/ttyACM0

# Monitor serial (debug output)
stty -F /dev/ttyACM0 115200 raw -echo && timeout 10 cat /dev/ttyACM0

# Decode crash backtrace
/home/maf/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line \
  -pfiaC -e .pio/build/EyeTerm/firmware.elf 0xADDR1 0xADDR2
```

## Desktop Forth Test Harness

There's a desktop test harness in `test_forth/` that reproduces the Forth VM without needing the ESP32. **Use this for debugging Forth boot issues** — it's orders of magnitude faster than flash-test-reflash cycles.

```bash
# Generate boot header
python3 lib/ueforth/tools/importation.py -i test_forth/test_boot.fs -o test_forth/test_boot.h \
  -I lib/ueforth -I lib/ueforth/esp32 --name forth_boot --header cpp

# Build
gcc -g -O0 -I lib/ueforth -I lib/ueforth/posix -I test_forth \
  -o test_forth/test_boot test_forth/test_boot.c -lm -ldl

# Run (stderr = debug, stdout = Forth output)
./test_forth/test_boot

# Debug with GDB
gdb -batch -ex run -ex bt ./test_forth/test_boot
```

**Important:** The desktop test uses CAPTURE-TYPE writing to stdout. If output isn't visible, add `fflush(stdout)` to the CAPTURE-TYPE opcode — stdout may be buffered and a crash loses unflushed output.

## ESP32 Device Operations

### Bootloader mode (for crash-looping devices)
Hold BOOT button → press RESET → release BOOT. Then upload. After upload, press RESET (without BOOT) to start normally.

Port is usually `/dev/ttyACM0`, sometimes `/dev/ttyACM1` after reboot.

### Serial monitoring pitfalls
- **DTR toggle puts ESP32 in bootloader mode!** Don't use pyserial's DTR toggle for reset — it triggers download mode. Just press the physical RESET button.
- Boot messages appear in the first ~2 seconds. If you start monitoring later, you'll only see periodic battery reports.
- No serial output = either booted fine (messages already passed) or crash-looping. Check for repeated `Guru Meditation Error` lines to distinguish.

## Lessons Learned / Pitfalls

### ueforth boot source structure
The stock ESP32 boot (`lib/ueforth/esp32/esp32_boot.fs`) includes:
```
phase1.fs → allocation.fs → bindings.fs → phase2.fs → phase_filetools.fs → platform.fs → ... → fini.fs
```
**Our boot only needs**: `phase1.fs` → `allocation.fs` → `phase2.fs` → `filetools.fs` → vocab setup → `forth-yield`

Key files NOT in phase1/phase2:
- `filetools.fs` — NOT in phase2.fs! It's in `phase_filetools.fs`. Must be included explicitly if you need `save-name`/`restore-name`/`setup-saving-base`.
- `bindings.fs` — vocabulary transfers for ESP/WiFi/Wire/etc. We don't need these.
- `platform.fs` — Serial-based type/key. We use capture-type instead.
- `fini.fs` — calls `setup-saving-base` + `execute` (autoboot) + `ok`. We inline what we need.

### The setup-saving-base crash (biggest gotcha)
`setup-saving-base` is defined in `filetools.fs` (in the `internals` vocabulary). It uses `to saving-base` internally. The `to` word (from `locals.fs`) calls `' saving-base` at runtime (it's immediate). If `saving-base` is not in the search order, `'` calls `notfound` which does `throw`. With no `catch` handler during boot, `throw` does `0 rp!` → null dereference → LoadProhibited crash.

**The fix**: Include `filetools.fs` before calling `setup-saving-base`. Without it, the word doesn't exist.

### interpret0 vs evaluate1
After boot.fs line 105 (`interpret0`), the Forth-level `+evaluate1` takes over word processing. From that point:
- Our safe C `evaluate1` is **no longer called** — it only runs during the initial `EVALUATE1/BRANCH` C-level boot loop
- Errors go through the Forth-level `notfound` handler
- We override `'notfound` with `safe-notfound` in the boot source so unknown words print an error message instead of crashing (the stock `notfound` calls `throw`, which with no `catch` handler does `0 rp!` → null deref)

### Vocabulary search order matters
After phase2 finishes, `only forth definitions` runs (end of `locals.fs`). Search order = `[forth]` only. Words in `internals` are NOT findable unless you do `also internals` or `internals definitions`.

Words transferred to internals (by vocabulary.fs lines 53-65): `value-bind`, `aliteral`, `+evaluate1`, `interpret0`, `notfound`, and many others. These are still callable from compiled code (xt is baked in), but not findable by name.

### C++ build order issues with ueforth
- `cell_t` is defined in `bits.h`. Any code using `cell_t` must come after `#include "bits.h"`.
- `ResizeFile()` is referenced in REQUIRED_FILES_SUPPORT macro but the function must be defined before `core.h`/`interp.h` expand the macros. Put it between `bits.h` and `core.h`.
- `filename`/`filename2` are static char arrays used by REQUIRED_FILES_SUPPORT opcodes. Must be declared before the macro expansion.
- Avoid naming local variables `b2` — it's a macro in `calling.h` (`#define b2 (*(uint8_t **) &n2)`). Other reserved names: `a0`-`a7`, `b0`-`b7`, `c0`-`c7`, `n0`-`n7`, `w`.
- The `#define evaluate1 evaluate1_stock` / `#include core.h` / `#undef evaluate1` trick renames the stock function so we can provide our own.

### SPIFFS setup
- Partition table `default_16MB.csv` has a `spiffs` partition at offset 0xc90000, size 0x360000 (3.4MB)
- `SPIFFS.begin(true, "/spiffs", 10)` — `true` = format on first use
- Files are accessed via POSIX paths: `/spiffs/filename`
- Include `"SPIFFS.h"` in forth.cpp

### Desktop test harness limitations
- The test harness (`test_forth/`) does NOT have `REQUIRED_FILES_SUPPORT` opcodes (no `w/o`, `r/o`, file I/O). So `filetools.fs` cannot be included in the test boot source — it will crash because those words are missing. The test boot omits filetools and `setup-saving-base`.
- Both `test_boot.fs` and `forth_boot.fs` must be kept in sync for the `safe-notfound` override and any other shared boot logic.

### Forth boot source (forth_boot.fs) conventions
- Uses `needs` directive for includes — paths are relative to the file or resolved via `-I` flags
- Our paths use `../lib/ueforth/common/` prefix since forth_boot.fs is in `src/`
- Must end with `forth-yield` so `forth_run()` returns control to C++
- `' capture-type is type` — redirects Forth output through our capture mechanism
- `nop-key` / `nop-key?` — dummy implementations since we don't use Forth's REPL input
- Vocabulary setup block must come AFTER `filetools.fs` (if included) so `setup-saving-base` is available

### lib/libvterm
The `lib/libvterm/library.json` may need a fix for `-Werror` builds. If you see libvterm compile errors, check that file.
