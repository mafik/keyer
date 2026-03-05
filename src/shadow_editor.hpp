#pragma once

#include "keyboard.hpp"

#include <string>

namespace atmt {

struct EditorState {
  constexpr static int kRows = 7;

  // Row 0 is at the top of the screen.
  std::string rows[kRows];

  // Number of rows currently in use. May be less than kRows.
  int num_rows = 1;

  // Intended row of the text cursor. May actually be < 0 or >= num_rows when a
  // cursor temporarily leaves the tracked area.
  int cursor_row = 0;

  // Intended column of the text cursor. May actually be longer than the given
  // row (e.g. if the cursor was moved vertically from a longer row).
  int cursor_col = 0;

  bool operator==(const EditorState &o) const;
  bool operator!=(const EditorState &o) const { return !(*this == o); }

  // Clamp cursor to valid range.
  void ClampCursor();
  int RowLen(int r) const;
};

// A single keystroke that can be sent via BLE.
// Either a raw IBM key or a unicode character, with optional modifiers.
struct Keystroke {
  IBM_Key key = IBM_Key::NONE; // Non-NONE for raw key presses
  uint32_t codepoint = 0;      // Non-zero for unicode character input
  Modifier mods = 0;

  bool IsNone() const { return key == IBM_Key::NONE && codepoint == 0; }
  bool IsKey() const { return key != IBM_Key::NONE; }
  bool IsChar() const { return codepoint != 0; }

  bool operator==(const Keystroke &o) const {
    return key == o.key && codepoint == o.codepoint && mods == o.mods;
  }
  bool operator!=(const Keystroke &o) const { return !(*this == o); }

  static Keystroke Key(IBM_Key k, Modifier m = 0) { return {k, 0, m}; }
  static Keystroke Char(uint32_t cp, Modifier m = 0) {
    return {IBM_Key::NONE, cp, m};
  }
  static Keystroke None() { return {}; }
};

// Apply a keystroke to an EditorState, mutating it as a simple text editor
// would. This is a pure function — no side effects.
void ApplyKeystroke(EditorState &state, Keystroke ks);

// Line-level edit operation for the alignment step.
struct LineOp {
  enum Type { KEEP, DELETE_LINE, INSERT_LINE };
  Type type;
  int cur_row; // source row in current (for KEEP/DELETE)
  int tgt_row; // source row in target (for KEEP/INSERT)
};

// Compute line-level alignment between current and target using LCS.
// Returns an edit script (sequence of LineOps) to transform current into
// target.
// max_ops: output array, max_len: its capacity.
// Returns the number of ops written.
int AlignLines(const EditorState &current, const EditorState &target,
               LineOp *ops, int max_len);

// ShadowEditor attempts to perfectly reproduce the on-screen text & cursor
// position.
//
// ShadowEditor receives key events as the user presses keyboard keys and
// emulates a text editing area.
//
// The keyboard has no access to actual text content from the OS so it's only a
// best-effort approximation.
//
// # Incremental updates
//
// Rather than overloading the bluetooth connection with tons of text updates,
// ShadowEditor limits its output speed. It keeps track of the "target" display
// state & the "current" display state. The animation runs in a separate task
// that ensures sleep between key presses.
//
// The editor supports two modes of operation:
// - Event-based: for keys pressed by the user. These should be sent to the
//   typer task immediately. If target & current state are identical, they
//   should be sent to the host immediately, updating both states at once.
//   Otherwise (while typing animation is active) they should update the target
//   state only.
// - State-based: for bulk edits performed by Forth & SSH. These should update
//   the target state & wake the typer task.
//
// The typer task is extremely incremental — it re-calculates the edit plan each
// time and only sends one keystroke (bringing `current` closer to `target`) at
// a time.
//
// # Editing strategy
//
// The editing relies on keys: Home, End, Up, Down, Left, Right, Delete &
// standard character input keys (unicode will be replaced with '?', unless
// alt+combo can enter the character).
//
// Two host-editor compatibility measures:
// - NavigateToward emits HOME before UP/DOWN when cursor_col != 0, so row
//   changes always start from column 0 (avoids sticky-column desync).
// - INSERT_LINE uses ENTER at column 0 of the next line (not end of the
//   previous line), so the host editor has no indentation context to
//   auto-indent from. The append case (ENTER at end of last line) is the
//   exception — there's no next line to go to.
//
// It is perfectly fine to push a line out of the tracked area — it's a common
// pattern for the terminal. Such line may (and even should) be left in the OS
// display, without being deleted. Even though the keyboard will forget it, the
// text area where the BLE keyboard is typing will retain it as "history".
//
// # Edit algorithm (NextKeystroke)
//
// The first step of the algorithm is to align the lines which didn't change
// (LCS-based). Lines that are only in `current` (deleted) are silently
// forgotten. Lines that are only in `target` (inserted) require Enter + typing.
// Lines present in both but with different content get character-level edits.
//
// Character-level diffs use common prefix/suffix matching. The middle section
// is deleted (via Delete key), then the replacement is typed.
//
// Since ESP32 has strictly limited memory, alignment uses at most O(n^2) space
// where n = kRows (16), which is 289 ints — trivial.
//
// # Line length
//
// Even though SSH terminal uses fixed-width lines, ShadowEditor allows for
// arbitrary line lengths. The Forth interpreter may need much longer lines for
// its input and output.
//
// # Testing
//
// ShadowEditor is designed to allow unit testing without the need for ESP32
// hardware. See test_forth/test_shadow_editor.cpp.
struct ShadowEditor {
  EditorState target;
  EditorState current;

  // Returns the next keystroke to bring current closer to target.
  // Returns Keystroke::None() if current == target (nothing to do).
  // Each call sends at most one keystroke and updates `current` accordingly.
  Keystroke NextKeystroke();

  // Handle a user keypress (event-based mode).
  // Returns true if the key should be sent immediately (current == target).
  // Returns false if the typer is still catching up (key only updates target).
  bool HandleKeypress(Keystroke ks);

  // Returns true if current matches target.
  bool IsIdle() const { return current == target; }
};

} // namespace atmt
