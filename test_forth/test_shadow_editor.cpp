#include "../src/keyboard.cpp"      // For ToStr(HID_Key) etc.
#include "../src/shadow_editor.cpp" // Include implementation directly for simplicity
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace atmt;

// Helper: make an EditorState from lines
static EditorState MakeState(std::initializer_list<const char *> lines,
                             int cursor_row = 0, int cursor_col = 0) {
  EditorState s;
  s.num_rows = 0;
  for (auto *line : lines) {
    assert(s.num_rows < EditorState::kRows);
    s.rows[s.num_rows++] = line;
  }
  if (s.num_rows == 0)
    s.num_rows = 1;
  s.cursor_row = cursor_row;
  s.cursor_col = cursor_col;
  return s;
}

// Helper: run NextKeystroke until idle, collecting keystrokes.
// Returns false if it didn't converge within max_steps.
static bool RunToCompletion(ShadowEditor &ed, std::vector<Keystroke> &out,
                            int max_steps = 500) {
  out.clear();
  for (int i = 0; i < max_steps; ++i) {
    Keystroke ks = ed.NextKeystroke();
    if (ks.IsNone())
      return true;
    out.push_back(ks);
    ApplyKeystroke(ed.current, ks);
  }
  return false;
}

// Helper: print editor state
static void PrintState(const char *label, const EditorState &s) {
  printf("%s (%d rows, cursor=%d,%d):\n", label, s.num_rows, s.cursor_row,
         s.cursor_col);
  for (int i = 0; i < s.num_rows; ++i) {
    printf("  %d: \"%s\"", i, s.rows[i].c_str());
    if (i == s.cursor_row)
      printf(" <-- cursor at col %d", s.cursor_col);
    printf("\n");
  }
}

static void PrintKeystrokes(const std::vector<Keystroke> &ks) {
  printf("  Keystrokes (%zu):", ks.size());
  for (auto &k : ks) {
    if (k.IsChar())
      printf(" '%c'", (char)k.codepoint);
    else
      printf(" %s", ToStr(k.key));
  }
  printf("\n");
}

// Verify that after running to completion, current content matches target
// content (ignoring cursor position).
static void AssertContentMatch(const EditorState &a, const EditorState &b,
                               const char *test_name) {
  if (a.num_rows != b.num_rows) {
    printf("FAIL %s: num_rows %d vs %d\n", test_name, a.num_rows, b.num_rows);
    PrintState("  got", a);
    PrintState("  expected", b);
    assert(false);
  }
  for (int i = 0; i < a.num_rows; ++i) {
    if (a.rows[i] != b.rows[i]) {
      printf("FAIL %s: row %d \"%s\" vs \"%s\"\n", test_name, i,
             a.rows[i].c_str(), b.rows[i].c_str());
      PrintState("  got", a);
      PrintState("  expected", b);
      assert(false);
    }
  }
}

// ========== ApplyKeystroke tests ==========

static void test_apply_char() {
  printf("test_apply_char...");
  EditorState s = MakeState({""});
  ApplyKeystroke(s, Keystroke::Char('h'));
  ApplyKeystroke(s, Keystroke::Char('i'));
  assert(s.rows[0] == "hi");
  assert(s.cursor_col == 2);
  printf(" OK\n");
}

static void test_apply_backspace() {
  printf("test_apply_backspace...");
  EditorState s = MakeState({"hello"}, 0, 3);
  ApplyKeystroke(s, Keystroke::Key(HID_Key::BACKSPACE));
  assert(s.rows[0] == "helo");
  assert(s.cursor_col == 2);
  printf(" OK\n");
}

static void test_apply_backspace_merge_lines() {
  printf("test_apply_backspace_merge_lines...");
  EditorState s = MakeState({"hello", "world"}, 1, 0);
  ApplyKeystroke(s, Keystroke::Key(HID_Key::BACKSPACE));
  assert(s.num_rows == 1);
  assert(s.rows[0] == "helloworld");
  assert(s.cursor_row == 0);
  assert(s.cursor_col == 5);
  printf(" OK\n");
}

static void test_apply_delete() {
  printf("test_apply_delete...");
  EditorState s = MakeState({"hello"}, 0, 2);
  ApplyKeystroke(s, Keystroke::Key(HID_Key::DELETE));
  assert(s.rows[0] == "helo");
  assert(s.cursor_col == 2);
  printf(" OK\n");
}

static void test_apply_delete_nl() {
  printf("test_apply_delete...");
  EditorState s = MakeState({"hello", "", "", "", "", "", ""}, 0, 5);
  assert(s.num_rows == 7);
  ApplyKeystroke(s, Keystroke::Key(HID_Key::DELETE));
  assert(s.num_rows == 6);
  assert(s.cursor_col == 5);
  printf(" OK\n");
}

static void test_apply_delete_merge_lines() {
  printf("test_apply_delete_merge_lines...");
  EditorState s = MakeState({"hello", "world"}, 0, 5);
  ApplyKeystroke(s, Keystroke::Key(HID_Key::DELETE));
  assert(s.num_rows == 1);
  assert(s.rows[0] == "helloworld");
  assert(s.cursor_row == 0);
  assert(s.cursor_col == 5);
  printf(" OK\n");
}

static void test_apply_enter() {
  printf("test_apply_enter...");
  EditorState s = MakeState({"hello"}, 0, 2);
  ApplyKeystroke(s, Keystroke::Key(HID_Key::ENTER));
  assert(s.num_rows == 2);
  assert(s.rows[0] == "he");
  assert(s.rows[1] == "llo");
  assert(s.cursor_row == 1);
  assert(s.cursor_col == 0);
  printf(" OK\n");
}

static void test_apply_enter_full() {
  printf("test_apply_enter_full...");
  // Fill all kRows rows.
  EditorState s;
  s.num_rows = EditorState::kRows;
  for (int i = 0; i < EditorState::kRows; i++)
    s.rows[i] = "row" + std::to_string(i);
  // Cursor on last row ("row6"), col 2.
  s.cursor_row = EditorState::kRows - 1;
  s.cursor_col = 2;

  ApplyKeystroke(s, Keystroke::Key(HID_Key::ENTER));

  // Row 0 ("row0") dropped. Last row split at col 2: "ro" + "w6".
  assert(s.num_rows == EditorState::kRows);
  assert(s.rows[0] == "row1");
  assert(s.rows[EditorState::kRows - 2] == "ro");
  assert(s.rows[EditorState::kRows - 1] == "w6");
  assert(s.cursor_row == EditorState::kRows - 1);
  assert(s.cursor_col == 0);
  printf(" OK\n");
}

static void test_apply_enter_full_middle() {
  printf("test_apply_enter_full_middle...");
  EditorState s;
  s.num_rows = EditorState::kRows;
  for (int i = 0; i < EditorState::kRows; i++)
    s.rows[i] = "row" + std::to_string(i);
  // ENTER in the middle (row 3, col 2).
  s.cursor_row = 3;
  s.cursor_col = 2;

  ApplyKeystroke(s, Keystroke::Key(HID_Key::ENTER));

  // Row 0 ("row0") dropped. Row 3 ("row3") split: "ro" + "w3".
  assert(s.num_rows == EditorState::kRows);
  assert(s.rows[0] == "row1");
  assert(s.rows[1] == "row2");
  assert(s.rows[2] == "ro");
  assert(s.rows[3] == "w3");
  assert(s.rows[4] == "row4");
  assert(s.rows[5] == "row5");
  assert(s.rows[6] == "row6");
  assert(s.cursor_row == 3);
  assert(s.cursor_col == 0);
  printf(" OK\n");
}

static void test_apply_navigation() {
  printf("test_apply_navigation...");
  EditorState s = MakeState({"abc", "defgh"}, 0, 1);

  ApplyKeystroke(s, Keystroke::Key(HID_Key::HOME));
  assert(s.cursor_col == 0);

  ApplyKeystroke(s, Keystroke::Key(HID_Key::END));
  assert(s.cursor_col == 3);

  ApplyKeystroke(s, Keystroke::Key(HID_Key::DOWN_ARROW));
  assert(s.cursor_row == 1);

  ApplyKeystroke(s, Keystroke::Key(HID_Key::UP_ARROW));
  assert(s.cursor_row == 0);

  // After UP, cursor_row=0, cursor_col=3 (from END). RIGHT wraps since
  // col 3 == len("abc") -> next line col 0.
  ApplyKeystroke(s, Keystroke::Key(HID_Key::RIGHT_ARROW));
  assert(s.cursor_row == 1);
  assert(s.cursor_col == 0);

  printf(" OK\n");
}

static void test_apply_left_wraps() {
  printf("test_apply_left_wraps...");
  EditorState s = MakeState({"abc", "def"}, 1, 0);
  ApplyKeystroke(s, Keystroke::Key(HID_Key::LEFT_ARROW));
  assert(s.cursor_row == 0);
  assert(s.cursor_col == 3);
  printf(" OK\n");
}

static void test_leave_and_return() {
  printf("test_leave_and_return...");
  EditorState s = MakeState({"abc", "defgh"}, 0, 1);

  ApplyKeystroke(s, Keystroke::Key(HID_Key::UP_ARROW));
  assert(s.cursor_row == -1);

  ApplyKeystroke(s, Keystroke::Key(HID_Key::UP_ARROW));
  assert(s.cursor_row == -2);

  ApplyKeystroke(s, Keystroke::Key(HID_Key::DOWN_ARROW));
  assert(s.cursor_row == -1);

  ApplyKeystroke(s, Keystroke::Key(HID_Key::DOWN_ARROW));
  assert(s.cursor_row == 0);

  ApplyKeystroke(s, Keystroke::Key(HID_Key::DOWN_ARROW));
  assert(s.cursor_row == 1);

  ApplyKeystroke(s, Keystroke::Key(HID_Key::DOWN_ARROW));
  assert(s.cursor_row == 2);

  ApplyKeystroke(s, Keystroke::Key(HID_Key::DOWN_ARROW));
  assert(s.cursor_row == 3);

  ApplyKeystroke(s, Keystroke::Key(HID_Key::UP_ARROW));
  assert(s.cursor_row == 2);

  ApplyKeystroke(s, Keystroke::Key(HID_Key::UP_ARROW));
  assert(s.cursor_row == 1);

  ApplyKeystroke(s, Keystroke::Key(HID_Key::UP_ARROW));
  assert(s.cursor_row == 0);
  assert(s.cursor_col == 1);

  printf(" OK\n");
}

static void test_leave_up_and_write() {
  printf("test_leave_up_and_write...");
  EditorState s = MakeState({"abc", "defgh"}, 0, 3);

  ApplyKeystroke(s, Keystroke::Key(HID_Key::UP_ARROW));
  ApplyKeystroke(s, Keystroke::Key(HID_Key::UP_ARROW));
  ApplyKeystroke(s, Keystroke::Char('x'));

  EditorState expected = MakeState({"x", "", "abc", "defgh"}, 0, 1);

  assert(expected == s);
  printf(" OK\n");
}

static void test_leave_down_and_write() {
  printf("test_leave_down_and_write...");
  EditorState s = MakeState({"abc", "defgh"}, 0, 3);

  ApplyKeystroke(s, Keystroke::Key(HID_Key::DOWN_ARROW));
  ApplyKeystroke(s, Keystroke::Key(HID_Key::DOWN_ARROW));
  ApplyKeystroke(s, Keystroke::Key(HID_Key::DOWN_ARROW));
  ApplyKeystroke(s, Keystroke::Char('x'));

  EditorState expected = MakeState({"abc", "defgh", "", "x"}, 3, 1);

  assert(expected == s);
  printf(" OK\n");
}

// ========== NextKeystroke / full integration tests ==========

static void test_nk_identical() {
  printf("test_nk_identical...");
  ShadowEditor ed;
  ed.current = MakeState({"hello"});
  ed.target = MakeState({"hello"});
  assert(ed.NextKeystroke().IsNone());
  printf(" OK\n");
}

static void test_nk_single_char_append() {
  printf("test_nk_single_char_append...");
  ShadowEditor ed;
  ed.current = MakeState({"hell"}, 0, 4);
  ed.target = MakeState({"hello"}, 0, 5);
  std::vector<Keystroke> ks;
  bool ok = RunToCompletion(ed, ks);
  assert(ok);
  PrintKeystrokes(ks);
  AssertContentMatch(ed.current, ed.target, "single_char_append");
  printf(" OK\n");
}

static void test_nk_single_char_change() {
  printf("test_nk_single_char_change...");
  ShadowEditor ed;
  ed.current = MakeState({"abc"});
  ed.target = MakeState({"aXc"});
  std::vector<Keystroke> ks;
  bool ok = RunToCompletion(ed, ks);
  assert(ok);
  PrintKeystrokes(ks);
  AssertContentMatch(ed.current, ed.target, "single_char_change");
  printf(" OK (keystrokes: %zu)\n", ks.size());
}

static void test_nk_line_insert() {
  printf("test_nk_line_insert...");
  ShadowEditor ed;
  ed.current = MakeState({"hello", "world"}, 0, 0);
  ed.target = MakeState({"hello", "new", "world"}, 0, 0);
  std::vector<Keystroke> ks;
  bool ok = RunToCompletion(ed, ks);
  PrintKeystrokes(ks);
  if (!ok) {
    printf(" FAIL: did not converge\n");
    PrintState("  current", ed.current);
    PrintState("  target", ed.target);
    assert(false);
  }
  AssertContentMatch(ed.current, ed.target, "line_insert");
  printf(" OK (keystrokes: %zu)\n", ks.size());
}

static void test_nk_line_delete() {
  printf("test_nk_line_delete...");
  ShadowEditor ed;
  ed.current = MakeState({"hello", "middle", "world"}, 0, 0);
  ed.target = MakeState({"hello", "world"}, 0, 0);
  std::vector<Keystroke> ks;
  bool ok = RunToCompletion(ed, ks);
  PrintKeystrokes(ks);
  if (!ok) {
    printf(" FAIL: did not converge\n");
    PrintState("  current", ed.current);
    PrintState("  target", ed.target);
    assert(false);
  }
  AssertContentMatch(ed.current, ed.target, "line_delete");
  printf(" OK (keystrokes: %zu)\n", ks.size());
}

static void test_nk_complete_replace() {
  printf("test_nk_complete_replace...");
  ShadowEditor ed;
  ed.current = MakeState({"aaa", "bbb"});
  ed.target = MakeState({"xxx", "yyy"});
  std::vector<Keystroke> ks;
  bool ok = RunToCompletion(ed, ks);
  PrintKeystrokes(ks);
  if (!ok) {
    printf(" FAIL: did not converge\n");
    PrintState("  current", ed.current);
    PrintState("  target", ed.target);
    assert(false);
  }
  AssertContentMatch(ed.current, ed.target, "complete_replace");
  printf(" OK (keystrokes: %zu)\n", ks.size());
}

static void test_nk_scroll() {
  printf("test_nk_scroll...");
  ShadowEditor ed;
  ed.current = MakeState({"line1", "line2", "line3"});
  ed.target = MakeState({"line2", "line3", "line4"});
  std::vector<Keystroke> ks;
  bool ok = RunToCompletion(ed, ks);
  PrintKeystrokes(ks);
  if (!ok) {
    printf(" FAIL: did not converge\n");
    PrintState("  current", ed.current);
    PrintState("  target", ed.target);
    assert(false);
  }
  AssertContentMatch(ed.current, ed.target, "scroll");
  printf(" OK (keystrokes: %zu)\n", ks.size());
}

static void test_nk_empty_to_content() {
  printf("test_nk_empty_to_content...");
  ShadowEditor ed;
  ed.current = MakeState({""});
  ed.target = MakeState({"hello", "world"});
  std::vector<Keystroke> ks;
  bool ok = RunToCompletion(ed, ks);
  PrintKeystrokes(ks);
  if (!ok) {
    printf(" FAIL: did not converge\n");
    PrintState("  current", ed.current);
    PrintState("  target", ed.target);
    assert(false);
  }
  AssertContentMatch(ed.current, ed.target, "empty_to_content");
  printf(" OK (keystrokes: %zu)\n", ks.size());
}

static void test_nk_content_to_empty() {
  printf("test_nk_content_to_empty...");
  ShadowEditor ed;
  ed.current = MakeState({"hello", "world"});
  ed.target = MakeState({""});
  std::vector<Keystroke> ks;
  bool ok = RunToCompletion(ed, ks);
  PrintKeystrokes(ks);
  if (!ok) {
    printf(" FAIL: did not converge\n");
    PrintState("  current", ed.current);
    PrintState("  target", ed.target);
    assert(false);
  }
  AssertContentMatch(ed.current, ed.target, "content_to_empty");
  printf(" OK (keystrokes: %zu)\n", ks.size());
}

static void test_nk_cursor_only() {
  printf("test_nk_cursor_only...");
  ShadowEditor ed;
  ed.current = MakeState({"hello"}, 0, 0);
  ed.target = MakeState({"hello"}, 0, 3);
  std::vector<Keystroke> ks;
  bool ok = RunToCompletion(ed, ks);
  assert(ok);
  PrintKeystrokes(ks);
  assert(ed.current.cursor_col == 3);
  printf(" OK (keystrokes: %zu)\n", ks.size());
}

// ========== Event-based mode tests ==========

static void test_handle_keypress_idle() {
  printf("test_handle_keypress_idle...");
  ShadowEditor ed;
  ed.current = MakeState({"hello"}, 0, 5);
  ed.target = MakeState({"hello"}, 0, 5);
  bool immediate = ed.HandleKeypress(Keystroke::Char('!'));
  assert(immediate);
  assert(ed.current == ed.target);
  assert(ed.current.rows[0] == "hello!");
  printf(" OK\n");
}

static void test_handle_keypress_busy() {
  printf("test_handle_keypress_busy...");
  ShadowEditor ed;
  ed.current = MakeState({"old"});
  ed.target = MakeState({"new"}, 0, 3);
  bool immediate = ed.HandleKeypress(Keystroke::Char('!'));
  assert(!immediate);
  assert(ed.target.rows[0] == "new!");
  assert(ed.current.rows[0] == "old");
  printf(" OK\n");
}

// ========== Mid-update target change test ==========

static void test_nk_mid_update_change() {
  printf("test_nk_mid_update_change...");
  ShadowEditor ed;
  ed.current = MakeState({"aaa"});
  ed.target = MakeState({"bbb"});

  std::vector<Keystroke> ks;
  for (int i = 0; i < 3; ++i) {
    Keystroke k = ed.NextKeystroke();
    if (k.IsNone())
      break;
    ks.push_back(k);
    ApplyKeystroke(ed.current, k);
  }

  ed.target = MakeState({"ccc"});

  bool ok = RunToCompletion(ed, ks);
  if (!ok) {
    printf(" FAIL: did not converge after target change\n");
    PrintState("  current", ed.current);
    PrintState("  target", ed.target);
    assert(false);
  }
  AssertContentMatch(ed.current, ed.target, "mid_update_change");
  printf(" OK\n");
}

// ========== Efficiency tests ==========

static void test_efficiency_single_char() {
  printf("test_efficiency_single_char...");
  ShadowEditor ed;
  ed.current = MakeState({"hello world"}, 0, 0);
  ed.target = MakeState({"hello World"}, 0, 0);
  std::vector<Keystroke> ks;
  bool ok = RunToCompletion(ed, ks);
  assert(ok);
  PrintKeystrokes(ks);
  assert(ks.size() < 15);
  AssertContentMatch(ed.current, ed.target, "efficiency_single_char");
  printf(" OK (keystrokes: %zu)\n", ks.size());
}

// ========== SSH-like scenario tests ==========

static void test_nk_ssh_prompt_update() {
  printf("test_nk_ssh_prompt_update...");
  ShadowEditor ed;
  ed.current = MakeState({"user@host:~$ ", ""}, 0, 13);
  ed.target = MakeState({"user@host:~$ ls", ""}, 0, 15);
  std::vector<Keystroke> ks;
  bool ok = RunToCompletion(ed, ks);
  assert(ok);
  PrintKeystrokes(ks);
  AssertContentMatch(ed.current, ed.target, "ssh_prompt_update");
  assert(ks.size() == 2);
  printf(" OK (keystrokes: %zu)\n", ks.size());
}

static void test_nk_ssh_output_scroll() {
  printf("test_nk_ssh_output_scroll...");
  ShadowEditor ed;
  ed.current = MakeState({"line1", "line2", "line3", "line4", "$"}, 4, 1);
  ed.target = MakeState({"line2", "line3", "line4", "output", "$"}, 4, 1);
  std::vector<Keystroke> ks;
  bool ok = RunToCompletion(ed, ks);
  assert(ok);
  PrintKeystrokes(ks);
  AssertContentMatch(ed.current, ed.target, "ssh_output_scroll");
  printf(" OK (keystrokes: %zu)\n", ks.size());
}

static void test_nk_multiline_no_change() {
  printf("test_nk_multiline_no_change...");
  ShadowEditor ed;
  ed.current = MakeState({"aaa", "bbb", "ccc", "ddd"}, 2, 1);
  ed.target = MakeState({"aaa", "bbb", "ccc", "ddd"}, 2, 1);
  assert(ed.NextKeystroke().IsNone());
  printf(" OK\n");
}

static void test_bug1() {
  printf("test_bug1...");
  ShadowEditor ed;
  ed.current = MakeState(
      {
          "EEMPT_DYNAMIC Debian 6.1.140-1 (2025-05-",
          "22) x86_64",
          "",
          "",
      },
      2, 0);
  ed.target = MakeState(
      {
          "EEMPT_DYNAMIC Debian 6.1.140-1 (2025-05-",
          "22) x86_64",
          "",
          "The programs included with the Debian GN",
          "",
          "root@protectli ~#",
      },
      5, 18);
  std::vector<Keystroke> ks;
  bool ok = RunToCompletion(ed, ks);
  assert(ok);
  printf(" OK (keystrokes: %zu)\n", ks.size());
}

static void test_bug2() {
  printf("test_bug2...");
  ShadowEditor ed;
  ed.current = MakeState(
      {
          "",
          "The secret source of humor is not joy but sorrow; there is no humor "
          "in Heaven.",
          "                -- Mark Twain",
          "",
          "root@protectli ~#",
          "root@protectli ",
      },
      5, 15);
  ed.target = MakeState(
      {
          "",
          "The secret source of humor is not joy but sorrow; there is no humor "
          "in Heaven.",
          "                -- Mark Twain",
          "",
          "root@protectli ~#",
          "root@protectli ~#  ",
          "",
      },
      5, 18);
  std::vector<Keystroke> ks;
  bool ok = RunToCompletion(ed, ks);
  assert(ok);
  printf(" OK (keystrokes: %zu)\n", ks.size());
}

// ========== UP/DOWN column clamping tests ==========

static void test_nk_navigate_across_short_row() {
  printf("test_nk_navigate_across_short_row...");
  // Bug scenario: current cursor at end of row 0 (long), target content
  // differs on row 2 (long), but row 1 is short. Without HOME-anchor,
  // the host editor clamps cursor_col when passing through row 1,
  // desyncing our model.
  ShadowEditor ed;
  ed.current = MakeState({"long line here", "ab", "old content XX"}, 0, 14);
  ed.target = MakeState({"long line here", "ab", "old content YY"}, 0, 14);
  std::vector<Keystroke> ks;
  bool ok = RunToCompletion(ed, ks);
  assert(ok);
  PrintKeystrokes(ks);
  AssertContentMatch(ed.current, ed.target, "navigate_across_short_row");
  // Should include HOME before the first DOWN
  assert(ks.size() > 0);
  assert(ks[0].key == HID_Key::HOME);
  printf(" OK (keystrokes: %zu)\n", ks.size());
}

// ========== Middle row deletion test ==========

static void test_nk_delete_middle_row() {
  printf("test_nk_delete_middle_row...");
  // Middle rows must be physically removed via keystrokes (DELETE + BACKSPACE),
  // not silently dropped. Only top/bottom rows may be silently forgotten.
  ShadowEditor ed;
  ed.current = MakeState({"abc", "MIDDLE", "def"}, 0, 0);
  ed.target = MakeState({"abc", "def"}, 0, 0);
  std::vector<Keystroke> ks;
  bool ok = RunToCompletion(ed, ks);
  assert(ok);
  PrintKeystrokes(ks);
  AssertContentMatch(ed.current, ed.target, "delete_middle_row");
  // Must use actual keystrokes to remove the middle row (not 0 keystrokes).
  assert(ks.size() > 0);
  // Should contain DELETE or BACKSPACE to remove row content/merge rows.
  bool has_del_or_bs = false;
  for (auto &k : ks)
    if (k.key == HID_Key::BACKSPACE || k.key == HID_Key::DELETE)
      has_del_or_bs = true;
  assert(has_del_or_bs);
  printf(" OK (keystrokes: %zu)\n", ks.size());
}

static void test_scroll() {
  printf("test_scroll...");
  ShadowEditor ed;
  ed.current =
      MakeState({"aaa", "bbb", "ccc", "ddd", "eee", "fff", "ggg"}, 6, 3);
  ed.target =
      MakeState({"bbb", "ccc", "ddd", "eee", "fff", "ggg", "hhh"}, 6, 3);
  std::vector<Keystroke> ks;
  bool ok = RunToCompletion(ed, ks);
  assert(ok);
  PrintKeystrokes(ks);
  AssertContentMatch(ed.current, ed.target, "scroll");
  // Verifies that we "forget" the top row instead of deleting it.
  assert(ks.size() <= 5);
  printf(" OK\n");
}

static void test_newline() {
  printf("test_newline...");
  ShadowEditor ed;
  ed.current = MakeState(
      {
          "File Edit Options Buffers Tools Emacs-Lisp Help",
          "",
          "",
          "",
          "",
          "-UUU:----F1  .emacs         All L1     tih",

      },
      5, 39);
  ed.target = MakeState(
      {
          "File Edit Options Buffers Tools Emacs-Lisp Help",
          "",
          "",
          "",
          "",
          "-UUU:----F1  .emacs         All L1     (ELisp/d ElDoc) "
          "-------------------------",
          ".emacs has auto save data; consider M-x recover-this-file",
      },
      1, 0);
  std::vector<Keystroke> ks;
  bool ok = RunToCompletion(ed, ks);
  assert(ok);
  PrintKeystrokes(ks);
  AssertContentMatch(ed.current, ed.target, "scroll");
  // Verifies that we "forget" the top row instead of deleting it.
  assert(ks.size() < 200);
  printf(" OK\n");
}

// ========== Main ==========

int main() {
  printf("=== ShadowEditor Tests ===\n\n");

  // ApplyKeystroke
  test_apply_char();
  test_apply_backspace();
  test_apply_backspace_merge_lines();
  test_apply_delete();
  test_apply_delete_nl();
  test_apply_delete_merge_lines();
  test_apply_enter();
  test_apply_enter_full();
  test_apply_enter_full_middle();
  test_apply_navigation();
  test_apply_left_wraps();
  test_leave_and_return();
  test_leave_up_and_write();
  test_leave_down_and_write();

  printf("\n");

  // NextKeystroke integration
  test_nk_identical();
  test_nk_single_char_append();
  test_nk_single_char_change();
  test_nk_line_insert();
  test_nk_line_delete();
  test_nk_complete_replace();
  test_nk_scroll();
  test_nk_empty_to_content();
  test_nk_content_to_empty();
  test_nk_cursor_only();

  printf("\n");

  // Event mode
  test_handle_keypress_idle();
  test_handle_keypress_busy();

  printf("\n");

  // Mid-update
  test_nk_mid_update_change();

  printf("\n");

  // Efficiency
  test_efficiency_single_char();

  printf("\n");

  // SSH-like scenarios
  test_nk_ssh_prompt_update();
  test_nk_ssh_output_scroll();
  test_nk_multiline_no_change();

  // UP/DOWN HOME-anchor navigation
  test_nk_navigate_across_short_row();

  // Middle row deletion
  test_nk_delete_middle_row();

  // Bugs
  test_bug1();
  test_bug2();

  test_scroll();
  test_newline();

  printf("\n=== All tests passed! ===\n");
  return 0;
}
