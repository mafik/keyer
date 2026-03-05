# Keyer Project

If along the way you execute some command, it doesn't work and then you figure out how to make it work, please record that in CLAUDE.md - this way you'll avoid making the same mistakes over and over.

**Do NOT use the auto memory feature** (files in `.claude/projects/.../memory/`). Store all learnings in this CLAUDE.md file instead.

## CRITICAL: Never revert working directory files

**NEVER run `git stash`, `git checkout .`, `git restore`, `git reset --hard`, or ANY command that reverts working directory files.** This has destroyed user data TWICE already. The repo has tracked files (e.g. `src/secrets.cpp`) where the working copy contains real credentials over placeholder values checked into git. Reverting the working directory replaces these with useless placeholders and the real values are unrecoverable.

If you need to check whether a build error is pre-existing, ask the user or inspect `git log` / `git diff` — NEVER revert files.

**NEVER read or access `src/secrets.cpp`** — it contains real credentials.

## Project Overview

Chording keyboard (5 fingers: Thumb/Index/Middle/Ring/Pinky) running on ESP32-S3 with BLE HID. Pinky defaults to shift modifier but individual chords can use it explicitly. The firmware includes a chording keyboard layout, an embedded Forth interpreter (ueforth), SSH client with VTerm terminal emulation, and BLE HID keyboard output.

**Board**: ESP32-S3 with 16MB flash, PSRAM, custom board definition `EyeTerm` in `boards/`
**Framework**: Arduino + ESP-IDF (dual framework)
**Build system**: PlatformIO

## Architecture

### Data flow
```
keyer.cpp (buttons) → HandleUnicode/HandleKey (typist.cpp) → ShadowEditor → ble_keyboard
                                                            → SSH channel (when ssh_active)
```

### Boot flow
1. `setup()` in `eye_term.cpp` → `InitESP32()` → `InitMainLoop()` → `InitKeyer()` → `ble_keyboard.Setup()` → `InitTypist()` → `ForthInit()`
2. `loop()` runs `MainLoopNonBlocking()` repeatedly, then `ble_keyboard.Loop()`

### Key source files
| File | Purpose |
|------|---------|
| `src/eye_term.cpp` | Entry point (setup/loop) |
| `src/typist.cpp` | Typist FreeRTOS task, ShadowEditor global, HandleUnicode/HandleKey routing |
| `src/typist.hpp` | Declares InitTypist, HandleUnicode, HandleKey, SendKeystroke, WakeTypist |
| `src/shadow_editor.hpp` | ShadowEditor, EditorState, Keystroke, ApplyKeystroke, AlignLines |
| `src/shadow_editor.cpp` | ShadowEditor implementation (desktop-testable, no ESP32 deps) |
| `src/forth.cpp` | Forth VM integration, SSH opcode, ForthEvalInPlace |
| `src/forth.hpp` | Declares ForthInit |
| `src/forth_boot.fs` | Forth boot source — edit this, regenerate forth_boot.h |
| `src/forth_boot.h` | **Generated** from forth_boot.fs — never edit directly |
| `src/keyer.cpp` | Chord detection, layout, RegisterChord API |
| `src/keyboard.cpp` | Keyboard utilities: terminal sequences, ApplyShift, GetOgonekBase |
| `src/app_keyboard.hpp` | BLE HID keyboard — global `ble_keyboard` instance |
| `src/app_keyboard.cpp` | BLE HID keyboard implementation |
| `src/ssh.cpp` | SSH client, VTerm terminal emulation, persistent background task |
| `src/ssh.hpp` | SSH interface: ssh_chan, StartSSHSession() |
| `src/common_esp32.cpp` | Platform init, battery monitoring, Debugf() |
| `platformio.ini` | Build config — note `lib_ignore = ueforth` (we include headers directly) |

### ShadowEditor & Typist architecture
- `ShadowEditor` tracks two `EditorState`s: `current` (what's on host screen) and `target` (desired state)
- User keystrokes go through `HandleUnicode`/`HandleKey` → `SendKeystroke` → `editor.HandleKeypress`
- If idle (current == target): keystroke sent to BLE immediately, both states updated
- If busy (typist catching up): keystroke only updates target, typist task wakes
- Typist FreeRTOS task (4KB stack, core 1, priority 3) calls `editor.NextKeystroke()` to get one keystroke at a time, sends via BLE with 20ms rate limiting
- `NextKeystroke` uses LCS-based line alignment + character-level prefix/suffix diffing
- Forth eval and SSH update `editor.target` directly, then call `WakeTypist()`
- Shift resolution: `HandleUnicode` calls `ApplyShift()` (in keyboard.cpp) to resolve shifted characters before creating Keystrokes, so the editor tracks what the host displays

### Keyboard event routing (typist.cpp)
- `HandleUnicode(codepoint, mods)` and `HandleKey(key, mods)` are the central routing functions
- Called by keyer.cpp chord actions
- If `ssh_chan` is set: sends terminal sequences to SSH channel via `ssh_channel_write`
- Otherwise: resolves shift, sends through ShadowEditor → BLE

### SSH architecture
- `ssh` is a Forth opcode that calls `StartSSHSession()`
- Persistent FreeRTOS task (24KB stack, core 1) waits on a semaphore
- `StartSSHSession()` gives the semaphore, task wakes up and runs one SSH session
- SSH output → VTerm (40×16) → `UpdateEditorFromVTerm()` copies screen into `editor.target` → typist incrementally updates host
- Keyboard input during SSH → `HandleUnicode/HandleKey` → `ssh_channel_write` on main thread
- Status messages use `SetStatusMessage()` which sets `editor.target` to a single line

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
- `SSH` — calls `StartSSHSession()` to launch SSH on background task
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
- Chord 2102/mod0 → ForthEvalInPlace: manipulates `editor.target` directly — truncates old output after cursor, evaluates code before cursor via `ForthEval`, appends output to target row, calls `WakeTypist()`. The typist incrementally updates the host display.
- Chord 1102/mod0 → DebugDumpEditor: dumps ShadowEditor state (current + target) via serial for diagnostics.

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

### FreeRTOS task lifecycle
- **Never create/delete tasks repeatedly** — the idle task frees deleted task memory asynchronously. If you create a new 32KB task before the idle task runs, `xTaskCreatePinnedToCore` fails with `-1` (no memory). Use a persistent task with a semaphore instead.
- Pattern: create task once at boot, have it `xSemaphoreTake(sem, portMAX_DELAY)` in a loop. Signal with `xSemaphoreGive(sem)` to wake it.

### FreeRTOS StackType_t and stack scanning pitfalls
- **`StackType_t` is `uint8_t` on ESP32 Xtensa port** (`portmacro.h` line 80: `#define portSTACK_TYPE uint8_t`). Pointers like `pxTopOfStack` are `uint8_t*` — must cast to `uint32_t*` to read actual 32-bit stack words.
- Use `pxTCBGetTopOfStack(h)` and `pxTCBGetEndOfStack(h)` from `freertos/task_snapshot.h` to get stack bounds. `TaskHandle_t` can be passed directly (it's the TCB pointer).
- `uxTaskGetSystemState()` is **NOT linked** in ESP-IDF Arduino builds. Use `eTaskGetState()` + snapshot accessors instead.
- **Xtensa windowed ABI return addresses**: top 2 bits of A0 encode CALL type (CALL4=01→`0x4xxx`, CALL8=10→`0x8xxx`, CALL12=11→`0xCxxx`). Decode: `pc = (w & 0x3FFFFFFF) | 0x40000000`. Filter: `(w >> 30) != 0` to skip non-return-address values.

### ESP32 power management and SSH
- Default PM config: `max_freq_mhz=80, min_freq_mhz=40, light_sleep_enable=true`
- `ESP_PM_CPU_FREQ_MAX` lock only raises to the configured max (80MHz)
- SSH key exchange at 80MHz is too slow — server resets connection ("Connection reset by peer" at session_state=9)
- **Fix**: temporarily `esp_pm_configure` to 240MHz/no-light-sleep before `ssh_connect`, restore after auth
- **Cannot** `esp_wifi_set_ps(WIFI_PS_NONE)` when BLE is active — causes abort! WiFi+BLE coexistence requires modem sleep.

### VTerm on ESP32 — heap exhaustion and sizing
- Internal SRAM is extremely tight. Measured heap at key points (after WiFi/BLE buffer optimization):
  - SSH task start: ~113KB free, ~102KB largest block
  - After VTerm alloc: ~79KB free, ~70KB largest block
  - After WiFi connects: ~28KB free, ~17KB largest block
  - SSH crypto needs: ~15KB+ largest contiguous block for key exchange
- VTerm's internal `ScreenCell` is ~40 bytes (chars[6]=24B + ScreenPen with VTermColor×2 + bitfields ≈16B).
- **60×20 was too large** (1200 cells × 40 = 48KB). **Current size: 40×16** (~34KB total with overhead).
- VTerm is allocated on the SSH task **before WiFi connects** (when ~113KB is free). Kept alive across sessions, reset with `vterm_screen_reset()` each session.
- **Do NOT allocate VTerm after WiFi** — only ~28KB free with 17KB largest block, not enough for both VTerm (~34KB) and SSH crypto.
- **Do NOT allocate VTerm on the main thread before WiFi** — it reduces available RAM and causes WiFi init to fail with `esp_wifi_init 257` (ESP_ERR_NO_MEM).
- **WiFi/BLE buffer optimization** (sdkconfig) was critical to make SSH work:
  - WiFi: static RX 8→4, dynamic RX 32→8, static TX 8→4, CSI/FTM disabled, mgmt bufs reduced
  - BLE: max connections 9→1, ACL bufs 20→10, HCI event hi bufs 30→8
  - SSH task stack: 24KB→17KB (16KB triggers stack canary during ssh_connect)
- `vterm_new()` uses default allocator (malloc from internal SRAM). PSRAM allocator doesn't work because PSRAM isn't initialized (see PSRAM section below).

### libvterm vterm_build() bug
- `vterm_build()` in `lib/libvterm/src/vterm.c` does NOT initialize `vt->screen` or `vt->state` to NULL after allocating the VTerm struct.
- If the allocator returns uninitialized memory, `vterm_obtain_screen()` sees garbage in `vt->screen`, thinks a screen already exists, and returns a garbage pointer → crash.
- **Fix applied**: added `memset(vt, 0, sizeof(*vt))` after malloc in `vterm_build()`. This fix is correct regardless of allocator.

### PSRAM — board has it but we can't use it yet
**Hardware**: Board is T-Energy-S3 (LilyGO) with ESP32-S3-WROOM-1 module. esptool confirms: "Embedded PSRAM 8MB (AP_3v3)". It's OPI (octal) PSRAM.

**What was tried and what happened:**
1. `CONFIG_SPIRAM_MODE_QUAD` + `BOOT_INIT=y`: "PSRAM ID read error: 0x00ffffff" — wrong mode, chip is OPI not quad.
2. `CONFIG_SPIRAM_MODE_OCT` + `BOOT_INIT=y`: Bootloop. IPC task stack overflow ("Stack canary watchpoint triggered (ipc1)"). After fixing IPC stack (1024→2048), Forth boot triggered INT_WDT (interrupt watchdog, 300ms timeout). After increasing INT_WDT to 2000ms AND keeping Forth heap in internal SRAM, Forth booted — but **any key press caused INT_WDT reboot**, even with `CAPS_ALLOC` (PSRAM not in malloc path). Root cause unknown.
3. `CONFIG_SPIRAM_MODE_OCT` in sdkconfig **even with BOOT_INIT off** corrupted NVS — NimBLE couldn't persist bonding data. Required full flash erase + revert to QUAD mode.
4. (2026-03-06) `SPIRAM_MODE_OCT` + `CAPS_ALLOC` (no malloc routing) + `BOOT_INIT=y`: PSRAM detected (8MB, self-test passed), but **TASK_WDT crash during SPIFFS `stat()` in ForthRestore**. Tried:
   - 80/40MHz with light sleep → INT_WDT during Forth boot
   - 240/240MHz fixed, no sleep, INT_WDT 5000ms → first boot looked OK but was actually boot-looping (TASK_WDT after "Forth: ready")
   - 240/80MHz (freq scaling) → immediate INT_WDT
   - SPIRAM_SPEED_40M instead of 80M → same TASK_WDT crash
   - The crash happens during SPIFFS access (`stat()` call), suggesting SPI bus contention between OPI PSRAM and QIO flash

**Root cause**: **GPIO pin conflict.** OPI PSRAM on ESP32-S3 uses GPIO 33-37 for its data bus (SPID4-SPID7 + SPIDQS). Our button pins in `keyer.cpp` use **GPIO 35** (RING_5) and **GPIO 37** (LITTLE_6). When `pinMode(35, INPUT_PULLUP)` runs during `InitKeyer()`, it reconfigures pins the PSRAM controller needs, corrupting PSRAM data transfers. This causes watchdog timeouts on any subsequent SPI flash access (SPIFFS, NVS) because the SPI bus is shared. See: https://esp32.com/viewtopic.php?t=42416

**Current state**: `SPIRAM_MODE_QUAD`, `BOOT_INIT` off. PSRAM not used. A `TestPSRAM()` function exists in common_esp32.cpp for future attempts.

**To enable PSRAM**: Must rewire buttons off GPIO 33-37. No software-only fix is possible.

### Forth heap allocation
- Uses `heap_caps_malloc(kHeapSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)` to keep Forth VM in fast internal SRAM.
- Originally tried `MALLOC_CAP_SPIRAM` with malloc fallback — when PSRAM was briefly working, running Forth from PSRAM was too slow and triggered INT_WDT during boot.

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

### ShadowEditor desktop test
```bash
# Build and run
g++ -std=c++17 -g -O0 -I src -o test_forth/test_shadow_editor test_forth/test_shadow_editor.cpp
./test_forth/test_shadow_editor
```
- Test file includes `shadow_editor.cpp` and `keyboard.cpp` directly — do NOT pass them as separate TUs
- `shadow_editor.cpp` has no ESP32 dependencies — all desktop-testable
- Tests cover: ApplyKeystroke, AlignLines, NextKeystroke convergence, HandleKeypress event mode

### ShadowEditor pitfalls
- **Empty-line LCS ambiguity**: Empty rows are excluded from LCS matching in `AlignLines`. Without this, inserting a blank row via ENTER gets matched with a distant empty target row by LCS, the pre-processing silently deletes it, creating an infinite loop. Empty rows are paired positionally via gap post-processing instead.
- **Out-of-bounds cursor**: `EditorState::operator==` compares clamped cursor positions. The final cursor navigation in `NextKeystroke` also clamps target cursor to valid range. Without this, `target.cursor_col` past row length causes infinite RIGHT_ARROW.
- **UP/DOWN column desync**: Host editors may clamp `cursor_col` to line length on UP/DOWN (clamping editors) or preserve a "sticky column" (VS Code, Vim). NavigateToward emits HOME before any UP/DOWN when `cursor_col != 0`, so row changes always start from column 0 — predictable in all editors.
- **ENTER auto-indent**: Smart editors auto-indent after ENTER based on surrounding context. INSERT_LINE uses ENTER at column 0 of the next line (not end of the previous line), giving the host editor no indentation context. Exception: the append case (ENTER at end of last line) has no next line to navigate to.

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

### WiFi lifecycle — cleanup is critical
- `WiFi.begin()` fails with "esp_wifi_init 257" (ESP_ERR_NO_MEM) or "Failed to deinit Wi-Fi driver (0x3001)" if WiFi wasn't properly cleaned up from a previous session.
- **Every error path in SSH must clean up WiFi.** Use `goto cleanup` pattern — multiple early returns with duplicated cleanup will inevitably miss a path and leak ~40KB.
- **Do NOT use `WiFi.mode(WIFI_OFF)`** after disconnect — `WiFi.mode(WIFI_OFF)` calls `esp_wifi_deinit()` which can timeout ("timeout when WiFi un-init, type=4") and leak memory. Just use `WiFi.disconnect()` to keep the driver alive but disconnected.
- `WiFi.mode(WIFI_STA)` before `WiFi.begin()` is required to properly initialize station mode.

### SSH task stack sizing
- libssh's `ssh_connect` does heavy crypto (key exchange) with a very deep call stack (~20+ frames).
- **16KB stack is NOT enough** — causes "Stack canary watchpoint triggered (SSH)" crash during `ssh_connect`.
- **24KB is the minimum** for the SSH task. 32KB gives comfortable margin.
- `DebugHeap()` in `common_esp32.hpp` reports stack high water mark — use it to monitor.

### SSH resource cleanup pattern
- `RunOneSSHSession()` uses `goto cleanup` with state-tracking bools (`ssh_connected`, `pm_modified`) to ensure all resources are freed on every exit path.
- Without this, each failed SSH attempt leaks: WiFi (~40KB heap), PM locks, libssh session state, and ssh_key objects.
- The heap leak is cumulative — after 2-3 failed attempts, there's not enough memory for anything.

### Bluetooth re-pairing after flash erase
After full flash erase, both ESP32 and PC lose bonding data. **Always re-pair immediately after flash erase — don't wait for the user to ask.**
```bash
bluetoothctl remove 30:ED:A0:A5:30:25   # keyboard MAC
# wait for scan to find it
bluetoothctl pair 30:ED:A0:A5:30:25
bluetoothctl trust 30:ED:A0:A5:30:25
```
- Keyboard MAC: `30:ED:A0:A5:30:25`, PC adapter: `e8:9c:25:5e:09:ac`
- BLE name: `maf.klaw`

### Full flash erase + reflash
When partition table or NVS is corrupted, a normal upload isn't enough:
```bash
python3 -m esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 erase_flash
# put board in bootloader mode (hold BOOT → press RESET → release BOOT)
python3 -m esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash \
  0x0000 .pio/build/EyeTerm/bootloader.bin \
  0x8000 .pio/build/EyeTerm/partitions.bin \
  0x10000 .pio/build/EyeTerm/firmware.bin
```

### Changing sdkconfig SPIRAM mode corrupts NVS
- Switching between `CONFIG_SPIRAM_MODE_QUAD` and `CONFIG_SPIRAM_MODE_OCT` (or changing `board_build.memory_type` between `qio_opi` and `qio_qspi`) can corrupt NVS and coredump partitions.
- Symptoms: "NVS open operation failed", "Failed to initialize NVS! Error: 261", NimBLE ESP_ERROR_CHECK abort.
- Fix: full flash erase + reflash all images (bootloader + partitions + firmware). Then re-pair BLE.

### lib/libvterm
The `lib/libvterm/library.json` may need a fix for `-Werror` builds. If you see libvterm compile errors, check that file.
