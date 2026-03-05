#pragma once

#include "shadow_editor.hpp"

namespace atmt {

extern ShadowEditor editor;

// Call once from setup(), after ble_keyboard.Setup()
void InitTypist();

// Wake the typist task (call after mutating editor.target)
void WakeTypist();

// Called by keyer when a chord produces a character or key.
// Routes to BLE keyboard or SSH channel depending on mode.
void HandleUnicode(uint32_t codepoint, Modifier mods);
void HandleKey(IBM_Key key, Modifier mods);

// Send a keystroke through the editor (event-based mode).
// If idle: sends via BLE immediately + updates both states.
// If busy: only updates target, typist catches up.
void SendKeystroke(Keystroke ks);

// Send a keystroke bypassing the editor entirely (for SSH channel routing).
// Goes directly to ble_keyboard.
void SendKeystrokeDirect(Keystroke ks);

// Dump editor state (current + target) via Debugf for diagnostics.
void DebugDumpEditor();

} // namespace atmt
