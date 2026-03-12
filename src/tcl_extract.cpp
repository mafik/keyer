// EditorExtractTerm and EditorWrite — pure functions on EditorState.
// Separated from tcl.cpp so desktop tests can include them directly.

#include "shadow_editor.hpp"
#include <string>

namespace atmt {

// Scans editor from the current cursor position back using Tcl's parsing
// rules to extract a single term:
//
// - single_word
// - "quoted string [including substitutions]"
// - [sub-command with args [possibly sub-commands] $wariables {brace-quoted strings}]
// - {brace-quoted string {possibly sub-strings}}
// - $(2 + 2 + $a + (2*2)) // JimTcl's math expressions
//
// Removes & returns the matched term.
std::string EditorExtractTerm(EditorState &editor) {
  int r = editor.cursor_row;
  if (r < 0 || r >= editor.num_rows)
    return "";

  std::string &row = editor.rows[r];
  int col = editor.cursor_col;
  if (col > (int)row.size())
    col = row.size();
  if (col == 0)
    return "";

  int end = col; // one past the last char of the term

  // Check what's immediately before cursor for delimiter-based terms.
  char last = row[end - 1];

  auto scan_back_matching = [&](char open, char close) -> int {
    // Cursor is right after `close`. Find matching `open`, respecting nesting.
    int depth = 1;
    int i = end - 2; // skip the close char itself
    while (i >= 0 && depth > 0) {
      if (row[i] == close)
        depth++;
      else if (row[i] == open)
        depth--;
      i--;
    }
    if (depth == 0)
      return i + 1; // points to the open char
    return -1;       // unmatched
  };

  int start = -1;

  if (last == ']') {
    start = scan_back_matching('[', ']');
  } else if (last == '}') {
    start = scan_back_matching('{', '}');
  } else if (last == ')') {
    // Could be $(...) math expression or just parens
    int paren_start = scan_back_matching('(', ')');
    if (paren_start > 0 && row[paren_start - 1] == '$') {
      start = paren_start - 1; // include the $
    } else {
      start = paren_start;
    }
  } else if (last == '"') {
    // Scan back for matching unescaped quote
    int i = end - 2;
    while (i >= 0) {
      if (row[i] == '"') {
        // Check it's not escaped
        int backslashes = 0;
        for (int j = i - 1; j >= 0 && row[j] == '\\'; j--)
          backslashes++;
        if (backslashes % 2 == 0) {
          start = i;
          break;
        }
      }
      i--;
    }
  }

  // Fallback: bare word — scan back to whitespace
  if (start < 0) {
    start = end - 1;
    while (start > 0 && row[start - 1] != ' ' && row[start - 1] != '\t')
      start--;
  }

  std::string term = row.substr(start, end - start);
  row.erase(start, end - start);
  editor.cursor_col = start;
  return term;
}

// Write the given text at the current cursor position,
// taking care of proper newline splitting.
void EditorWrite(EditorState &editor, const std::string &text) {
  if (text.empty())
    return;

  int r = editor.cursor_row;
  if (r < 0 || r >= editor.num_rows) {
    r = 0;
    editor.cursor_row = 0;
    if (editor.num_rows == 0) {
      editor.rows[0] = "";
      editor.num_rows = 1;
    }
  }

  int col = editor.cursor_col;
  if (col > (int)editor.rows[r].size())
    col = editor.rows[r].size();

  // Text after cursor on the current row (will be appended to last line)
  std::string suffix = editor.rows[r].substr(col);
  editor.rows[r].erase(col);

  for (char c : text) {
    if (c == '\n') {
      // Start a new row if there's space
      if (editor.num_rows < EditorState::kRows) {
        // Shift rows down
        for (int i = editor.num_rows; i > r + 1; i--)
          editor.rows[i] = std::move(editor.rows[i - 1]);
        editor.num_rows++;
        r++;
        editor.rows[r] = "";
        editor.cursor_row = r;
        editor.cursor_col = 0;
      }
      // If at capacity, ignore the newline (stay on current row)
    } else {
      editor.rows[r] += c;
      editor.cursor_col = editor.rows[r].size();
    }
  }

  // Re-append the suffix
  editor.rows[r] += suffix;
  // cursor_col stays at the insertion point (before suffix)
}

} // namespace atmt
