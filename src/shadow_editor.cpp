#include "shadow_editor.hpp"

#include <algorithm>
#include <cstring>

namespace atmt {

// --- EditorState ---

bool EditorState::operator==(const EditorState &o) const {
  if (num_rows != o.num_rows)
    return false;
  for (int i = 0; i < num_rows; ++i)
    if (rows[i] != o.rows[i])
      return false;
  // Compare clamped cursor positions (target may have out-of-bounds cursor)
  auto clamp = [](const EditorState &s) -> std::pair<int, int> {
    int r = s.cursor_row;
    if (r < 0)
      r = 0;
    if (r >= s.num_rows)
      r = s.num_rows - 1;
    int c = s.cursor_col;
    int len = (r >= 0 && r < s.num_rows) ? (int)s.rows[r].size() : 0;
    if (c > len)
      c = len;
    return {r, c};
  };
  return clamp(*this) == clamp(o);
}

void EditorState::ClampCursor() {
  if (cursor_row < 0)
    cursor_row = 0;
  if (cursor_row >= num_rows)
    cursor_row = num_rows - 1;
  if (cursor_col < 0)
    cursor_col = 0;
  int len = RowLen(cursor_row);
  if (cursor_col > len)
    cursor_col = len;
}

int EditorState::RowLen(int r) const {
  if (r < 0 || r >= num_rows)
    return 0;
  return (int)rows[r].size();
}

// --- ApplyKeystroke ---

// When cursor is outside the tracked area (cursor_row < 0 or >= num_rows),
// materialize it by inserting empty rows so the cursor lands on a valid row.
static void MaterializeCursor(EditorState &s) {
  if (s.cursor_row < 0) {
    int insert = std::min(-s.cursor_row, EditorState::kRows - s.num_rows);
    // Shift existing rows down
    for (int i = s.num_rows - 1; i >= 0; --i)
      s.rows[i + insert] = std::move(s.rows[i]);
    for (int i = 0; i < insert; ++i)
      s.rows[i].clear();
    s.num_rows += insert;
    s.cursor_row += insert;
  } else if (s.cursor_row >= s.num_rows) {
    int insert = std::min(s.cursor_row - s.num_rows + 1,
                          EditorState::kRows - s.num_rows);
    for (int i = 0; i < insert; ++i)
      s.rows[s.num_rows + i].clear();
    s.num_rows += insert;
  }
  s.ClampCursor();
}

void ApplyKeystroke(EditorState &s, Keystroke ks) {
  // UP/DOWN move freely — cursor can leave the tracked area.
  if (ks.key == HID_Key::UP_ARROW) {
    s.cursor_row--;
    return;
  }
  if (ks.key == HID_Key::DOWN_ARROW) {
    s.cursor_row++;
    return;
  }

  // Navigation keys that reference row content: clamp first.
  if (ks.key == HID_Key::LEFT_ARROW || ks.key == HID_Key::RIGHT_ARROW ||
      ks.key == HID_Key::HOME || ks.key == HID_Key::END) {
    s.ClampCursor();
    int r = s.cursor_row;
    int c = s.cursor_col;
    switch (ks.key) {
    case HID_Key::LEFT_ARROW:
      if (c > 0) {
        s.cursor_col = c - 1;
      } else if (r > 0) {
        s.cursor_row = r - 1;
        s.cursor_col = s.RowLen(r - 1);
      }
      break;
    case HID_Key::RIGHT_ARROW:
      if (c < s.RowLen(r)) {
        s.cursor_col = c + 1;
      } else if (r < s.num_rows - 1) {
        s.cursor_row = r + 1;
        s.cursor_col = 0;
      }
      break;
    case HID_Key::HOME:
      s.cursor_col = 0;
      break;
    case HID_Key::END:
      s.cursor_col = s.RowLen(r);
      break;
    default:
      break;
    }
    return;
  }

  // Editing operations: materialize cursor into the tracked area first.
  MaterializeCursor(s);
  int r = s.cursor_row;
  int c = s.cursor_col;

  if (ks.IsChar()) {
    char ch = (ks.codepoint <= 127) ? (char)ks.codepoint : '?';
    s.rows[r].insert(s.rows[r].begin() + c, ch);
    s.cursor_col = c + 1;
    return;
  }

  switch (ks.key) {
  case HID_Key::BACKSPACE:
    if (c > 0) {
      s.rows[r].erase(c - 1, 1);
      s.cursor_col = c - 1;
    } else if (r > 0) {
      int prev_len = s.RowLen(r - 1);
      s.rows[r - 1] += s.rows[r];
      for (int i = r; i < s.num_rows - 1; ++i)
        s.rows[i] = std::move(s.rows[i + 1]);
      s.rows[s.num_rows - 1].clear();
      s.num_rows--;
      s.cursor_row = r - 1;
      s.cursor_col = prev_len;
    }
    break;

  case HID_Key::DELETE:
    if (c < s.RowLen(r)) {
      s.rows[r].erase(c, 1);
    } else if (r < s.num_rows - 1) {
      s.rows[r] += s.rows[r + 1];
      for (int i = r + 1; i < s.num_rows - 1; ++i)
        s.rows[i] = std::move(s.rows[i + 1]);
      s.rows[s.num_rows - 1].clear();
      s.num_rows--;
    }
    break;

  case HID_Key::ENTER: {
    std::string right_part = s.rows[r].substr(c);
    s.rows[r] = s.rows[r].substr(0, c);
    if (s.num_rows < EditorState::kRows) {
      // Room available: shift rows down and insert.
      for (int i = s.num_rows; i > r + 1; --i)
        s.rows[i] = std::move(s.rows[i - 1]);
      s.rows[r + 1] = std::move(right_part);
      s.num_rows++;
      s.cursor_row = r + 1;
      s.cursor_col = 0;
    } else {
      // Full: drop row 0 to make room, shift rows 1..r up by one.
      for (int i = 0; i < r; ++i)
        s.rows[i] = std::move(s.rows[i + 1]);
      // rows[r-1] now holds the left part (shifted from rows[r]).
      // Overwrite rows[r] with the right part.
      s.rows[r] = std::move(right_part);
      s.cursor_row = r;
      s.cursor_col = 0;
    }
    break;
  }

  default:
    break;
  }
}

// --- AlignLines ---
//
// Uses LCS to find identical-line anchors, then pairs up remaining lines in
// each gap for in-place editing. Adjacent DELETE/INSERT runs become KEEP ops
// (with character-level diffs handled later).

int AlignLines(const EditorState &current, const EditorState &target,
               LineOp *ops, int max_len) {
  int n = current.num_rows;
  int m = target.num_rows;

  // LCS DP table (17x17 = 289 ints max)
  int dp[EditorState::kRows + 1][EditorState::kRows + 1];
  memset(dp, 0, sizeof(dp));

  for (int i = 1; i <= n; ++i) {
    for (int j = 1; j <= m; ++j) {
      if (!current.rows[i - 1].empty() &&
          current.rows[i - 1] == target.rows[j - 1])
        dp[i][j] = dp[i - 1][j - 1] + 1;
      else
        dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);
    }
  }

  // Backtrack LCS to get raw edit script (in reverse)
  struct RawOp {
    enum { MATCH, DEL, INS } type;
    int cur_row, tgt_row;
  };
  RawOp raw[EditorState::kRows * 2 + 1];
  int raw_count = 0;

  int i = n, j = m;
  while (i > 0 || j > 0) {
    if (i > 0 && j > 0 && !current.rows[i - 1].empty() &&
        current.rows[i - 1] == target.rows[j - 1]) {
      raw[raw_count++] = {RawOp::MATCH, i - 1, j - 1};
      i--;
      j--;
    } else if (j > 0 && (i == 0 || dp[i][j - 1] > dp[i - 1][j])) {
      raw[raw_count++] = {RawOp::INS, -1, j - 1};
      j--;
    } else {
      raw[raw_count++] = {RawOp::DEL, i - 1, -1};
      i--;
    }
  }

  // Reverse to forward order
  for (int a = 0, b = raw_count - 1; a < b; a++, b--)
    std::swap(raw[a], raw[b]);

  // Post-process: pair up adjacent DEL/INS runs into KEEP (in-place edit).
  int out_count = 0;
  int pos = 0;

  while (pos < raw_count && out_count < max_len) {
    if (raw[pos].type == RawOp::MATCH) {
      ops[out_count++] = {LineOp::KEEP, raw[pos].cur_row, raw[pos].tgt_row};
      pos++;
      continue;
    }

    // Collect a gap of DELs and INSs
    int del_count = 0, ins_count = 0;
    int del_rows[EditorState::kRows], ins_rows[EditorState::kRows];
    while (pos < raw_count && raw[pos].type != RawOp::MATCH) {
      if (raw[pos].type == RawOp::DEL)
        del_rows[del_count++] = raw[pos].cur_row;
      else
        ins_rows[ins_count++] = raw[pos].tgt_row;
      pos++;
    }

    // Pair up: min(del, ins) become KEEP, rest stay as DELETE/INSERT
    int pairs = std::min(del_count, ins_count);
    for (int k = 0; k < pairs && out_count < max_len; ++k)
      ops[out_count++] = {LineOp::KEEP, del_rows[k], ins_rows[k]};
    for (int k = pairs; k < del_count && out_count < max_len; ++k)
      ops[out_count++] = {LineOp::DELETE_LINE, del_rows[k], -1};
    for (int k = pairs; k < ins_count && out_count < max_len; ++k)
      ops[out_count++] = {LineOp::INSERT_LINE, -1, ins_rows[k]};
  }

  return out_count;
}

// --- ShadowEditor ---

bool ShadowEditor::HandleKeypress(Keystroke ks) {
  bool was_idle = (current == target);
  ApplyKeystroke(target, ks);
  if (was_idle) {
    ApplyKeystroke(current, ks);
    return true;
  }
  return false;
}

// Navigate cursor one step toward (target_row, target_col).
// Uses Home/End when efficient.
static Keystroke NavigateToward(const EditorState &s, int target_row,
                                int target_col) {
  if (s.cursor_row != target_row) {
    // HOME first so UP/DOWN starts from column 0 — avoids host editor
    // clamping cursor_col to a shorter line, which would desync our model.
    if (s.cursor_col != 0)
      return Keystroke::Key(HID_Key::HOME);
    if (s.cursor_row < target_row)
      return Keystroke::Key(HID_Key::DOWN_ARROW);
    return Keystroke::Key(HID_Key::UP_ARROW);
  }
  if (s.cursor_col == target_col)
    return Keystroke::None();
  if (target_col == 0)
    return Keystroke::Key(HID_Key::HOME);
  if (target_col == s.RowLen(s.cursor_row))
    return Keystroke::Key(HID_Key::END);
  if (s.cursor_col < target_col)
    return Keystroke::Key(HID_Key::RIGHT_ARROW);
  return Keystroke::Key(HID_Key::LEFT_ARROW);
}

Keystroke ShadowEditor::NextKeystroke() {
  if (current == target)
    return Keystroke::None();

  // Pre-process: silently drop lines that exist in current but not in target.
  // These lines leave the tracked area and stay in the host as untracked
  // history. Remove bottom-to-top so indices stay valid.
  {
    LineOp ops[EditorState::kRows * 3];
    int num_ops = AlignLines(current, target, ops, EditorState::kRows * 3);
    for (int i = num_ops - 1; i >= 0; i--) {
      if (ops[i].type != LineOp::DELETE_LINE)
        continue;
      int row = ops[i].cur_row;
      if (row < 0 || row >= current.num_rows)
        continue;
      for (int j = row; j < current.num_rows - 1; ++j)
        current.rows[j] = std::move(current.rows[j + 1]);
      current.rows[current.num_rows - 1].clear();
      if (current.num_rows > 1) {
        current.num_rows--;
        if (current.cursor_row > row)
          current.cursor_row--;
      } else {
        current.rows[0].clear();
      }
    }
    current.ClampCursor();
  }

  if (current == target)
    return Keystroke::None();

  // Compute line alignment (no DELETE ops remain after pre-processing).
  LineOp ops[EditorState::kRows * 3];
  int num_ops = AlignLines(current, target, ops, EditorState::kRows * 3);

  // Walk the edit script. KEEP ops get character-level edits, INSERT ops get
  // ENTER. Each call emits at most one keystroke.
  for (int i = 0; i < num_ops; ++i) {
    auto &op = ops[i];

    if (op.type == LineOp::KEEP) {
      int cr = op.cur_row;
      int tr = op.tgt_row;
      if (cr < 0 || cr >= current.num_rows)
        continue;
      if (tr < 0 || tr >= target.num_rows)
        continue;

      const std::string &cur_line = current.rows[cr];
      const std::string &tgt_line = target.rows[tr];
      if (cur_line == tgt_line)
        continue;

      // Character-level diff: common prefix, common suffix, edit the middle.
      int prefix = 0;
      int min_len = std::min((int)cur_line.size(), (int)tgt_line.size());
      while (prefix < min_len && cur_line[prefix] == tgt_line[prefix])
        prefix++;

      int cur_end = (int)cur_line.size();
      int tgt_end = (int)tgt_line.size();
      while (cur_end > prefix && tgt_end > prefix &&
             cur_line[cur_end - 1] == tgt_line[tgt_end - 1]) {
        cur_end--;
        tgt_end--;
      }

      // Navigate to the first edit position.
      Keystroke nav = NavigateToward(current, cr, prefix);
      if (!nav.IsNone())
        return nav;

      // Left-to-right sweep: DELETE divergent chars, then type replacements.
      if (cur_end > prefix)
        return Keystroke::Key(HID_Key::DELETE);
      if (tgt_end > prefix)
        return Keystroke::Char(tgt_line[prefix]);

      continue;
    }

    if (op.type == LineOp::INSERT_LINE) {
      if (op.tgt_row < 0 || op.tgt_row >= target.num_rows)
        continue;

      // Find which current row this inserts before.
      int insert_at = -1;
      for (int j = i + 1; j < num_ops; ++j) {
        if (ops[j].cur_row >= 0) {
          insert_at = ops[j].cur_row;
          break;
        }
      }

      if (insert_at < 0) {
        // Append after last line.
        int last = current.num_rows - 1;
        Keystroke nav = NavigateToward(current, last, current.RowLen(last));
        if (!nav.IsNone())
          return nav;
      } else {
        // ENTER at (insert_at, 0). Column 0 avoids host auto-indent.
        Keystroke nav = NavigateToward(current, insert_at, 0);
        if (!nav.IsNone())
          return nav;
      }
      return Keystroke::Key(HID_Key::ENTER);
    }
  }

  // All content matches — navigate cursor to target position.
  int goal_row = target.cursor_row;
  int goal_col = target.cursor_col;
  if (goal_row >= current.num_rows)
    goal_row = current.num_rows - 1;
  if (goal_row < 0)
    goal_row = 0;
  if (goal_col > current.RowLen(goal_row))
    goal_col = current.RowLen(goal_row);
  Keystroke nav = NavigateToward(current, goal_row, goal_col);
  if (!nav.IsNone())
    return nav;

  return Keystroke::None();
}

} // namespace atmt
