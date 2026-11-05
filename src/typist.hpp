#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "shadow_editor.hpp"

namespace atmt {

extern SemaphoreHandle_t editor_sem;
extern ShadowEditor editor;

// Call once from setup(), after ble_keyboard.Setup()
void InitTypist();

// Wake the typist task (call after mutating editor.target)
void WakeTypist();

// Called by keyer when a chord produces a character or key.
// Routes to BLE keyboard or SSH channel depending on mode.
void HandleUnicode(uint32_t codepoint, Modifier mods);
void HandleKey(HID_Key key, Modifier mods);

// Update the sticky modifier state. Sends a modifier-only HID report
// if the state changes. Called by keyer on held modifier start/stop
// and all-buttons-up.
void HandleModifiers(Modifier mods);

// Dump editor state (current + target) via Debugf for diagnostics.
void DebugDumpEditor();

} // namespace atmt
