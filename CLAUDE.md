# Keyer Project

If along the way you execute some command, it doesn't work and then you figure out how to make it work, please record that in CLAUDE.md - this way you'll avoid making the same mistakes over and over.

**Do NOT use the auto memory feature** (files in `.claude/projects/.../memory/`). Store all learnings in this CLAUDE.md file instead.

## CRITICAL: Never revert working directory files

**NEVER run `git stash`, `git checkout .`, `git restore`, `git reset --hard`, or ANY command that reverts working directory files.** This has destroyed user data TWICE already. The repo has tracked files (e.g. `src/secrets.cpp`) where the working copy contains real credentials over placeholder values checked into git. Reverting the working directory replaces these with useless placeholders and the real values are unrecoverable.

If you need to check whether a build error is pre-existing, ask the user or inspect `git log` / `git diff` — NEVER revert files.

**NEVER read or access `src/secrets.cpp`** — it contains real credentials.

## Design philosophy

When fixing a problem or adding a feature, **prefer solutions that simplify the system** over ones that add state and complexity on top. Before implementing, ask: "Can I replace existing complexity instead of adding to it?" A good change removes code or collapses layers; a bad change adds flags and extra methods that callers must remember to invoke. Push complexity down into self-contained components (e.g. a timer-based queue that handles its own timing) rather than spreading coordination logic across callers.

## Project Overview

Chording keyboard (5 fingers: Thumb/Index/Middle/Ring/Pinky) running on ESP32-S3 with BLE HID. Pinky defaults to shift modifier but individual chords can use it explicitly. The firmware includes a chording keyboard layout, an embedded Jim Tcl interpreter, SSH client with VTerm terminal emulation, and BLE HID keyboard output.

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
1. `setup()` in `eye_term.cpp` → `InitESP32()` → `InitMainLoop()` → `InitKeyer()` → `ble_keyboard.Setup()` → `InitTypist()` → `TclInit()`
2. `loop()` runs `MainLoopNonBlocking()` repeatedly, then `ble_keyboard.Loop()`

### Key source files
| File | Purpose |
|------|---------|
| `src/eye_term.cpp` | Entry point (setup/loop) |
| `src/typist.cpp` | Typist FreeRTOS task, ShadowEditor global, HandleUnicode/HandleKey routing |
| `src/typist.hpp` | Declares InitTypist, HandleUnicode, HandleKey, SendKeystroke, WakeTypist |
| `src/shadow_editor.hpp` | ShadowEditor, EditorState, Keystroke, ApplyKeystroke, AlignLines |
| `src/shadow_editor.cpp` | ShadowEditor implementation (desktop-testable, no ESP32 deps) |
| `src/tcl.cpp` | Jim Tcl integration, SSH command, TclEvalInPlace |
| `src/tcl.hpp` | Declares TclInit |
| `src/keyer.cpp` | Chord detection, layout, RegisterChord API |
| `src/keyboard.cpp` | Keyboard utilities: terminal sequences, ApplyShift, GetOgonekBase |
| `src/app_keyboard.hpp` | BLE HID keyboard — global `ble_keyboard` instance |
| `src/app_keyboard.cpp` | BLE HID keyboard implementation |
| `src/ssh.cpp` | SSH client, VTerm terminal emulation, persistent background task |
| `src/ssh.hpp` | SSH interface: ssh_chan, StartSSHSession() |
| `src/common_esp32.cpp` | Platform init, battery monitoring, Debugf() |
| `platformio.ini` | Build config |

### ShadowEditor & Typist architecture
- `ShadowEditor` tracks two `EditorState`s: `current` (what's on host screen) and `target` (desired state)
- User keystrokes go through `HandleUnicode`/`HandleKey` → `SendKeystroke` → `editor.HandleKeypress`
- If idle (current == target): keystroke sent to BLE immediately, both states updated
- If busy (typist catching up): keystroke only updates target, typist task wakes
- Typist FreeRTOS task (4KB stack, core 1, priority 3) calls `editor.NextKeystroke()` to get one keystroke at a time, sends via BLE with 20ms rate limiting
- `NextKeystroke` uses LCS-based line alignment + character-level prefix/suffix diffing
- Tcl eval and SSH update `editor.target` directly, then call `WakeTypist()`
- Shift resolution: `HandleUnicode` calls `ApplyShift()` (in keyboard.cpp) to resolve shifted characters before creating Keystrokes, so the editor tracks what the host displays

### Keyboard event routing (typist.cpp)
- `HandleUnicode(codepoint, mods)` and `HandleKey(key, mods)` are the central routing functions
- Called by keyer.cpp chord actions
- If `ssh_chan` is set: sends terminal sequences to SSH channel via `ssh_channel_write`
- Otherwise: resolves shift, sends through ShadowEditor → BLE

### SSH architecture
- `ssh` is a Tcl command that calls `StartSSHSession()`
- Persistent FreeRTOS task (24KB stack, core 1) waits on a semaphore
- `StartSSHSession()` gives the semaphore, task wakes up and runs one SSH session
- SSH output → VTerm (40×16) → `UpdateEditorFromVTerm()` copies screen into `editor.target` → typist incrementally updates host
- Keyboard input during SSH → `HandleUnicode/HandleKey` → `ssh_channel_write` on main thread
- Status messages use `SetStatusMessage()` which sets `editor.target` to a single line

### Jim Tcl integration architecture

Jim Tcl (https://github.com/msteveb/jimtcl) is embedded as a library in `lib/jimtcl/`. The C sources are compiled directly via `src/CMakeLists.txt` SRCS list (PlatformIO's LDF doesn't work with the dual arduino+espidf framework for custom libs).

**Init sequence:**
1. `TclInit()` mounts SPIFFS, redirects stdout to typist (see below), creates interpreter via `Jim_CreateInterp()` + `Jim_RegisterCoreCommands()`
2. Registers custom commands: `ssh`, `heap`
3. Sources `/spiffs/init.tcl` if it exists (for user customizations)
4. Registers chord handlers

**stdout redirect:**
`stdout` is permanently replaced (via `funopen()`) with a custom `FILE*` whose write function appends to `editor.target` and calls `WakeTypist()`. This means `puts`, `printf`, and any C code writing to stdout will output through BLE keyboard. `Debugf`/`Serial` is unaffected — it writes to UART directly. The redirect is set up once during `TclInit()` and stays active for the lifetime of the process.

**Custom Tcl commands:**
- `ssh` — calls `StartSSHSession()` to launch SSH on background task
- `heap` — returns heap debug info string

**TclEval:**
- Calls `Jim_Eval()` with the script text
- Returns `Jim_GetResult()` as std::string
- On error (JIM_ERR), prefixes result with "ERROR: "

**Chord handler:**
- Chord 2102/mod0 → TclEvalInPlace: manipulates `editor.target` directly — truncates old output after cursor, evaluates code before cursor via `TclEval`, appends output to target row, calls `WakeTypist()`. The typist incrementally updates the host display.
- Chord 1102/mod0 → DebugDumpEditor: dumps ShadowEditor state (current + target) via serial for diagnostics.

**Jim Tcl build notes:**
- `lib/jimtcl/src/jimautoconf.h` — ESP32 config (replaces autoconf-generated header)
- `lib/jimtcl/src/jim-config.h` — redirects to `jimautoconf.h`
- `lib/jimtcl/src/_unicode_mapping.c` — generated from UnicodeData.txt (needed for UTF-8 support)
- `lib/jimtcl/library.json` exists but is NOT used by the build (PlatformIO LDF limitation); sources listed in CMakeLists.txt instead

## Build Commands

```bash
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
2. `CONFIG_SPIRAM_MODE_OCT` + `BOOT_INIT=y`: Bootloop. IPC task stack overflow ("Stack canary watchpoint triggered (ipc1)"). After fixing IPC stack (1024→2048), boot triggered INT_WDT (interrupt watchdog, 300ms timeout). After increasing INT_WDT to 2000ms, booted — but **any key press caused INT_WDT reboot**, even with `CAPS_ALLOC` (PSRAM not in malloc path). Root cause unknown.
3. `CONFIG_SPIRAM_MODE_OCT` in sdkconfig **even with BOOT_INIT off** corrupted NVS — NimBLE couldn't persist bonding data. Required full flash erase + revert to QUAD mode.
4. (2026-03-06) `SPIRAM_MODE_OCT` + `CAPS_ALLOC` (no malloc routing) + `BOOT_INIT=y`: PSRAM detected (8MB, self-test passed), but **TASK_WDT crash during SPIFFS `stat()`**. Tried:
   - 80/40MHz with light sleep → INT_WDT during boot
   - 240/240MHz fixed, no sleep, INT_WDT 5000ms → first boot looked OK but was actually boot-looping (TASK_WDT)
   - 240/80MHz (freq scaling) → immediate INT_WDT
   - SPIRAM_SPEED_40M instead of 80M → same TASK_WDT crash
   - The crash happens during SPIFFS access (`stat()` call), suggesting SPI bus contention between OPI PSRAM and QIO flash

**Root cause**: **GPIO pin conflict.** OPI PSRAM on ESP32-S3 uses GPIO 33-37 for its data bus (SPID4-SPID7 + SPIDQS). Our button pins in `keyer.cpp` use **GPIO 35** (RING_5) and **GPIO 37** (LITTLE_6). When `pinMode(35, INPUT_PULLUP)` runs during `InitKeyer()`, it reconfigures pins the PSRAM controller needs, corrupting PSRAM data transfers. This causes watchdog timeouts on any subsequent SPI flash access (SPIFFS, NVS) because the SPI bus is shared. See: https://esp32.com/viewtopic.php?t=42416

**Current state**: `SPIRAM_MODE_QUAD`, `BOOT_INIT` off. PSRAM not used. A `TestPSRAM()` function exists in common_esp32.cpp for future attempts.

**To enable PSRAM**: Must rewire buttons off GPIO 33-37. No software-only fix is possible.

### SPIFFS setup
- Partition table `default_16MB.csv` has a `spiffs` partition at offset 0xc90000, size 0x360000 (3.4MB)
- `SPIFFS.begin(true, "/spiffs", 10)` — `true` = format on first use
- Files are accessed via POSIX paths: `/spiffs/filename`
- Include `"SPIFFS.h"` in tcl.cpp

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

### Jim Tcl on ESP32 — build pitfalls
- Jim Tcl sources are in `lib/jimtcl/src/` but compiled via `src/CMakeLists.txt` SRCS list (not PlatformIO LDF)
- PlatformIO's Library Dependency Finder does NOT work for custom C libs in dual-framework (arduino+espidf) builds — the library.json is recognized but sources are never compiled or linked. Must list `.c` files directly in CMakeLists.txt.
- `jimautoconf.h` replaces the autoconf-generated config. Must define `HAVE_LONG_LONG`, `JIM_WIDE_MIN/MAX`, `JIM_WIDE_MODIFIER`, `USE_UTF8`, `JIM_UTF8`.
- `_unicode_mapping.c` is generated from `UnicodeData.txt` by `parse-unidata.tcl` (from Jim Tcl repo). Required for UTF-8 support. Generate with: `cd /tmp/jimtcl && tclsh parse-unidata.tcl UnicodeData.txt > _unicode_mapping.c`
- `jim-signal.h` must be present (included by `jim-nosignal.c`)
- `jim-win32compat.h` must be present (included by `jim.h`; no-op on non-Windows)

### lib/libvterm
The `lib/libvterm/library.json` may need a fix for `-Werror` builds. If you see libvterm compile errors, check that file.
