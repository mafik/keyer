#include "shadow_editor.hpp"

#include <algorithm>
#include <cstring>

namespace atmt {

// --- UTF-8 helpers (inline for desktop testability) ---

// Encode Unicode codepoint to UTF-8. Returns number of bytes written (1-4).
static int EncodeUtf8(char *buf, uint32_t cp) {
  if (cp < 0x80) {
    buf[0] = cp;
    return 1;
  }
  if (cp < 0x800) {
    buf[0] = 0xC0 | (cp >> 6);
    buf[1] = 0x80 | (cp & 0x3F);
    return 2;
  }
  if (cp < 0x10000) {
    buf[0] = 0xE0 | (cp >> 12);
    buf[1] = 0x80 | ((cp >> 6) & 0x3F);
    buf[2] = 0x80 | (cp & 0x3F);
    return 3;
  }
  buf[0] = 0xF0 | (cp >> 18);
  buf[1] = 0x80 | ((cp >> 12) & 0x3F);
  buf[2] = 0x80 | ((cp >> 6) & 0x3F);
  buf[3] = 0x80 | (cp & 0x3F);
  return 4;
}

// Decode one UTF-8 character. Returns number of bytes consumed (1-4).
static int DecodeUtf8(const char *s, uint32_t &cp) {
  uint8_t c = (uint8_t)s[0];
  if (c < 0x80) {
    cp = c;
    return 1;
  }
  if ((c & 0xE0) == 0xC0) {
    cp = ((c & 0x1F) << 6) | (s[1] & 0x3F);
    return 2;
  }
  if ((c & 0xF0) == 0xE0) {
    cp = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    return 3;
  }
  cp = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) |
       (s[3] & 0x3F);
  return 4;
}

// Count UTF-8 characters in a string.
static int Utf8Len(const std::string &s) {
  int count = 0;
  const char *p = s.c_str();
  const char *end = p + s.size();
  while (p < end) {
    uint8_t c = (uint8_t)*p;
    if (c < 0x80)
      p += 1;
    else if ((c & 0xE0) == 0xC0)
      p += 2;
    else if ((c & 0xF0) == 0xE0)
      p += 3;
    else
      p += 4;
    count++;
  }
  return count;
}

// Get byte offset of the i-th UTF-8 character in a string.
static int Utf8ByteOffset(const std::string &s, int char_idx) {
  const char *p = s.c_str();
  const char *end = p + s.size();
  for (int i = 0; i < char_idx && p < end; i++) {
    uint8_t c = (uint8_t)*p;
    if (c < 0x80)
      p += 1;
    else if ((c & 0xE0) == 0xC0)
      p += 2;
    else if ((c & 0xF0) == 0xE0)
      p += 3;
    else
      p += 4;
  }
  return (int)(p - s.c_str());
}

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
    int len = (r >= 0 && r < s.num_rows) ? s.RowLen(r) : 0;
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
  return Utf8Len(rows[r]);
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

  // HOME just sets column to 0 — works even when cursor_row is negative
  // (cursor above the tracked region after forgetting rows).
  if (ks.key == HID_Key::HOME) {
    s.cursor_col = 0;
    return;
  }

  // Other navigation keys reference row content: clamp first.
  if (ks.key == HID_Key::LEFT_ARROW || ks.key == HID_Key::RIGHT_ARROW ||
      ks.key == HID_Key::END) {
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
    char buf[4];
    int len = EncodeUtf8(buf, ks.codepoint);
    int byte_off = Utf8ByteOffset(s.rows[r], c);
    s.rows[r].insert(byte_off, buf, len);
    s.cursor_col = c + 1;
    return;
  }

  switch (ks.key) {
  case HID_Key::BACKSPACE:
    if (c > 0) {
      int byte_start = Utf8ByteOffset(s.rows[r], c - 1);
      int byte_end = Utf8ByteOffset(s.rows[r], c);
      s.rows[r].erase(byte_start, byte_end - byte_start);
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
      int byte_start = Utf8ByteOffset(s.rows[r], c);
      int byte_end = Utf8ByteOffset(s.rows[r], c + 1);
      s.rows[r].erase(byte_start, byte_end - byte_start);
    } else if (r < s.num_rows - 1) {
      s.rows[r] += s.rows[r + 1];
      for (int i = r + 1; i < s.num_rows - 1; ++i)
        s.rows[i] = std::move(s.rows[i + 1]);
      s.rows[s.num_rows - 1].clear();
      s.num_rows--;
    }
    break;

  case HID_Key::ENTER: {
    int byte_c = Utf8ByteOffset(s.rows[r], c);
    std::string right_part = s.rows[r].substr(byte_c);
    s.rows[r] = s.rows[r].substr(0, byte_c);
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

// Flatten EditorState into a u32string (one codepoint per element) with '\n'
// row separators. This allows character-level comparison and edit distance.
static std::u32string FlattenState(const EditorState &s) {
  std::u32string r;
  for (int i = 0; i < s.num_rows; ++i) {
    if (i > 0)
      r += U'\n';
    const char *p = s.rows[i].c_str();
    const char *end = p + s.rows[i].size();
    while (p < end) {
      uint32_t cp;
      p += DecodeUtf8(p, cp);
      r += (char32_t)cp;
    }
  }
  return r;
}

// Convert flat codepoint offset to (row, col) in the editor model.
// A '\n' at the boundary maps to (row_before, end_of_row) so that
// DELETE at that position merges the two rows correctly.
static void FlatOffsetToRowCol(const EditorState &s, int off, int &row,
                               int &col) {
  for (int i = 0; i < s.num_rows; ++i) {
    int char_len = Utf8Len(s.rows[i]);
    if (off <= char_len) {
      row = i;
      col = off;
      return;
    }
    off -= char_len + 1;
  }
  row = s.num_rows - 1;
  col = s.RowLen(row);
}

// Edit distance (insert + delete only, no substitution) between a[as:] and
// b[bs:]. Uses O(m) rolling array. Returns the minimum number of single-char
// insertions and deletions to transform one suffix into the other.
static int EditDist(const std::u32string &a, int as, const std::u32string &b,
                    int bs) {
  int n = std::max(0, (int)a.size() - as);
  int m = std::max(0, (int)b.size() - bs);
  static int dp[512];
  if (m >= 512)
    return n + m; // safety fallback
  for (int j = 0; j <= m; ++j)
    dp[j] = j;
  for (int i = 1; i <= n; ++i) {
    int prev = dp[0];
    dp[0] = i;
    for (int j = 1; j <= m; ++j) {
      int tmp = dp[j];
      if (a[as + i - 1] == b[bs + j - 1])
        dp[j] = prev;
      else
        dp[j] = 1 + std::min(dp[j], dp[j - 1]);
      prev = tmp;
    }
  }
  return dp[m];
}

// Compute suffix_dist[i] = EditDist(a[i:], b) for all i in [0, n].
// Uses reverse DP with rolling array. O(nm) time, O(n + m) space.
static void ComputeSuffixDist(const std::u32string &a, int n,
                              const std::u32string &b, int m,
                              int *suffix_dist) {
  static int dp[512];
  if (m >= 512) {
    for (int i = 0; i <= n; ++i)
      suffix_dist[i] = (n - i) + m;
    return;
  }
  // Base: EditDist("", b) = m
  for (int j = 0; j <= m; ++j)
    dp[j] = j;
  suffix_dist[n] = m;
  // Build up from the end of a: rdp[i][j] = EditDist(a[n-i : n], b[m-j : m])
  // We want rdp[i][m] = EditDist(a[n-i:], b) = suffix_dist[n-i].
  for (int i = 1; i <= n; ++i) {
    int prev = dp[0];
    dp[0] = i;
    for (int j = 1; j <= m; ++j) {
      int tmp = dp[j];
      if (a[n - i] == b[m - j])
        dp[j] = prev;
      else
        dp[j] = 1 + std::min(dp[j], dp[j - 1]);
      prev = tmp;
    }
    suffix_dist[n - i] = dp[m];
  }
}

Keystroke ShadowEditor::NextKeystroke() {
  if (current == target)
    return Keystroke::None();

  // Flatten both states into codepoint arrays with '\n' as row separator.
  std::u32string cf = FlattenState(current);
  std::u32string tf = FlattenState(target);
  int n = (int)cf.size(), m = (int)tf.size();

  // Compute suffix edit distances to detect forgettable leading deletions.
  // suffix_dist[i] = EditDist(cf[i:], tf). The total edit distance is
  // suffix_dist[0]. If i + suffix_dist[i] == suffix_dist[0] at a row
  // boundary i, then deleting cf[0:i] (which includes complete rows) is
  // part of an optimal edit — those rows can be silently "forgotten".
  static int suffix_dist[512];
  if (n >= 512) {
    // Strings too long for stack buffer — skip forgetting.
    suffix_dist[0] = EditDist(cf, 0, tf, 0);
  } else {
    ComputeSuffixDist(cf, n, tf, m, suffix_dist);
  }
  int dp_total = suffix_dist[0];

  // Find the largest row boundary where leading deletions are optimal.
  // These rows have "scrolled off" the top and can be silently forgotten.
  int forget_pos = 0;
  if (n < 512) {
    int pos = 0;
    for (int r = 0; r < current.num_rows - 1; ++r) {
      pos += Utf8Len(current.rows[r]) + 1; // char count + '\n'
      if (pos <= n && pos + suffix_dist[pos] == dp_total)
        forget_pos = pos;
      else
        break;
    }
  }

  // Apply forgetting: silently drop leading rows from current.
  if (forget_pos > 0) {
    int rows_to_forget = 0;
    int pos = 0;
    for (int r = 0; r < current.num_rows; ++r) {
      pos += Utf8Len(current.rows[r]) + 1;
      if (pos <= forget_pos)
        rows_to_forget++;
      else
        break;
    }
    for (int i = 0; i < current.num_rows - rows_to_forget; ++i)
      current.rows[i] = std::move(current.rows[i + rows_to_forget]);
    for (int i = current.num_rows - rows_to_forget; i < current.num_rows; ++i)
      current.rows[i].clear();
    current.num_rows -= rows_to_forget;
    current.cursor_row -= rows_to_forget;
    // Don't clamp cursor_row — it may be negative (cursor is in the
    // forgotten zone above the tracked region). NavigateToward will emit
    // HOME (to reset cursor_col) then DOWN_ARROWs to bring it back.

    if (current == target)
      return Keystroke::None();

    // Re-flatten after forgetting.
    cf = FlattenState(current);
    n = (int)cf.size();
  }

  // Find the first edit in the edit script by scanning for the first
  // position where the flattened codepoint arrays diverge.
  int p = 0;
  int lim = std::min(n, m);
  while (p < lim && cf[p] == tf[p])
    ++p;

  // If content matches, only cursor position differs — navigate there.
  if (p == n && p == m) {
    int gr = target.cursor_row, gc = target.cursor_col;
    if (gr < 0)
      gr = 0;
    if (gr >= current.num_rows)
      gr = current.num_rows - 1;
    if (gc > current.RowLen(gr))
      gc = current.RowLen(gr);
    return NavigateToward(current, gr, gc);
  }

  // Navigate cursor to the first edit position.
  int er, ec;
  FlatOffsetToRowCol(current, std::min(p, n), er, ec);
  Keystroke nav = NavigateToward(current, er, ec);
  if (!nav.IsNone())
    return nav;

  // We're at the edit position. Decide: DELETE current[p] or INSERT target[p].
  if (p >= n) {
    char32_t ch = tf[p];
    if (ch == U'\n')
      return Keystroke::Key(HID_Key::ENTER);
    return Keystroke::Char((uint32_t)ch);
  }
  if (p >= m)
    return Keystroke::Key(HID_Key::DELETE);

  // Both strings have remaining chars. Use edit distance to pick the
  // operation that leads to a smaller remaining distance.
  int d_del = EditDist(cf, p + 1, tf, p);
  int d_ins = EditDist(cf, p, tf, p + 1);

  // Avoid INSERT '\n' at full row capacity — ENTER would drop row 0,
  // destroying content that's part of the matched prefix.
  if (d_ins < d_del && tf[p] == U'\n' &&
      current.num_rows >= EditorState::kRows)
    d_ins = d_del + 1;

  if (d_del <= d_ins)
    return Keystroke::Key(HID_Key::DELETE);

  char32_t ch = tf[p];
  if (ch == U'\n')
    return Keystroke::Key(HID_Key::ENTER);
  return Keystroke::Char((uint32_t)ch);
}

} // namespace atmt
