// Unit tests for EditorExtractTerm and EditorWrite from tcl_extract.cpp.
//
// These functions only depend on EditorState (from shadow_editor.hpp),
// so we can test them on desktop without ESP32.
//
// Build & run:
//   g++ -std=c++17 -g -O0 -I src -o test_forth/test_tcl_extract \
//       test_forth/test_tcl_extract.cpp && ./test_forth/test_tcl_extract

#include "../src/keyboard.cpp"
#include "../src/shadow_editor.cpp"
#include "../src/tcl_extract.cpp"

using namespace atmt;

#include <cassert>
#include <cstdio>
#include <cstring>

static EditorState MakeState(const char *text, int cursor_col) {
  EditorState s;
  s.num_rows = 1;
  s.rows[0] = text;
  s.cursor_row = 0;
  s.cursor_col = cursor_col;
  return s;
}

static EditorState MakeMultiline(std::initializer_list<const char *> lines,
                                 int cursor_row, int cursor_col) {
  EditorState s;
  s.num_rows = 0;
  for (auto *line : lines) {
    assert(s.num_rows < EditorState::kRows);
    s.rows[s.num_rows++] = line;
  }
  s.cursor_row = cursor_row;
  s.cursor_col = cursor_col;
  return s;
}

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                             \
  do {                                                                         \
    tests_run++;                                                               \
    printf("  %-50s", name);                                                   \
  } while (0)

#define PASS()                                                                 \
  do {                                                                         \
    tests_passed++;                                                            \
    printf("OK\n");                                                            \
  } while (0)

#define ASSERT_EQ(a, b)                                                        \
  do {                                                                         \
    if ((a) != (b)) {                                                          \
      printf("FAIL\n    expected: \"%s\"\n    got:      \"%s\"\n",             \
             std::string(b).c_str(), std::string(a).c_str());                  \
      assert(false);                                                           \
    }                                                                          \
  } while (0)

#define ASSERT_INT_EQ(a, b)                                                    \
  do {                                                                         \
    if ((a) != (b)) {                                                          \
      printf("FAIL\n    expected: %d\n    got:      %d\n", (b), (a));          \
      assert(false);                                                           \
    }                                                                          \
  } while (0)

// ========== EditorExtractTerm tests ==========

void test_extract_bare_word() {
  TEST("extract bare word");
  auto s = MakeState("hello world", 11);
  auto term = EditorExtractTerm(s);
  ASSERT_EQ(term, "world");
  ASSERT_EQ(s.rows[0], "hello ");
  ASSERT_INT_EQ(s.cursor_col, 6);
  PASS();
}

void test_extract_bare_word_only() {
  TEST("extract bare word (only word)");
  auto s = MakeState("foobar", 6);
  auto term = EditorExtractTerm(s);
  ASSERT_EQ(term, "foobar");
  ASSERT_EQ(s.rows[0], "");
  ASSERT_INT_EQ(s.cursor_col, 0);
  PASS();
}

void test_extract_bare_word_mid_row() {
  TEST("extract bare word (mid row)");
  auto s = MakeState("aaa bbb ccc", 7); // cursor after "bbb"
  auto term = EditorExtractTerm(s);
  ASSERT_EQ(term, "bbb");
  ASSERT_EQ(s.rows[0], "aaa  ccc");
  ASSERT_INT_EQ(s.cursor_col, 4);
  PASS();
}

void test_extract_bracket_command() {
  TEST("extract [command]");
  auto s = MakeState("prefix [expr 1+2]", 17);
  auto term = EditorExtractTerm(s);
  ASSERT_EQ(term, "[expr 1+2]");
  ASSERT_EQ(s.rows[0], "prefix ");
  ASSERT_INT_EQ(s.cursor_col, 7);
  PASS();
}

void test_extract_nested_brackets() {
  TEST("extract nested [command [sub]]");
  auto s = MakeState("[string length [info hostname]]", 31);
  auto term = EditorExtractTerm(s);
  ASSERT_EQ(term, "[string length [info hostname]]");
  ASSERT_EQ(s.rows[0], "");
  ASSERT_INT_EQ(s.cursor_col, 0);
  PASS();
}

void test_extract_braces() {
  TEST("extract {braced}");
  auto s = MakeState("x {hello world}", 15);
  auto term = EditorExtractTerm(s);
  ASSERT_EQ(term, "{hello world}");
  ASSERT_EQ(s.rows[0], "x ");
  ASSERT_INT_EQ(s.cursor_col, 2);
  PASS();
}

void test_extract_nested_braces() {
  TEST("extract {nested {braces}}");
  auto s = MakeState("{a {b c} d}", 11);
  auto term = EditorExtractTerm(s);
  ASSERT_EQ(term, "{a {b c} d}");
  ASSERT_EQ(s.rows[0], "");
  ASSERT_INT_EQ(s.cursor_col, 0);
  PASS();
}

void test_extract_quoted_string() {
  TEST("extract \"quoted string\"");
  auto s = MakeState("x \"hello world\"", 15);
  auto term = EditorExtractTerm(s);
  ASSERT_EQ(term, "\"hello world\"");
  ASSERT_EQ(s.rows[0], "x ");
  ASSERT_INT_EQ(s.cursor_col, 2);
  PASS();
}

void test_extract_quoted_with_escape() {
  TEST("extract \"quoted with \\\"escape\\\"\"");
  auto s = MakeState(R"(x "say \"hi\"")", 14);
  auto term = EditorExtractTerm(s);
  ASSERT_EQ(term, R"("say \"hi\"")");
  ASSERT_EQ(s.rows[0], "x ");
  ASSERT_INT_EQ(s.cursor_col, 2);
  PASS();
}

void test_extract_dollar_paren() {
  TEST("extract $(math expr)");
  auto s = MakeState("result $(2 + 2)", 15);
  auto term = EditorExtractTerm(s);
  ASSERT_EQ(term, "$(2 + 2)");
  ASSERT_EQ(s.rows[0], "result ");
  ASSERT_INT_EQ(s.cursor_col, 7);
  PASS();
}

void test_extract_dollar_nested_parens() {
  TEST("extract $(nested (parens))");
  auto s = MakeState("$(1 + (2*3))", 12);
  auto term = EditorExtractTerm(s);
  ASSERT_EQ(term, "$(1 + (2*3))");
  ASSERT_EQ(s.rows[0], "");
  ASSERT_INT_EQ(s.cursor_col, 0);
  PASS();
}

void test_extract_plain_parens() {
  TEST("extract (parens) without $");
  auto s = MakeState("x (foo bar)", 11);
  auto term = EditorExtractTerm(s);
  ASSERT_EQ(term, "(foo bar)");
  ASSERT_EQ(s.rows[0], "x ");
  ASSERT_INT_EQ(s.cursor_col, 2);
  PASS();
}

void test_extract_empty_cursor() {
  TEST("extract at col 0 returns empty");
  auto s = MakeState("hello", 0);
  auto term = EditorExtractTerm(s);
  ASSERT_EQ(term, "");
  ASSERT_EQ(s.rows[0], "hello");
  ASSERT_INT_EQ(s.cursor_col, 0);
  PASS();
}

void test_extract_empty_row() {
  TEST("extract from empty row");
  auto s = MakeState("", 0);
  auto term = EditorExtractTerm(s);
  ASSERT_EQ(term, "");
  PASS();
}

void test_extract_unmatched_bracket_falls_back() {
  TEST("unmatched ] falls back to bare word");
  auto s = MakeState("abc]", 4);
  auto term = EditorExtractTerm(s);
  ASSERT_EQ(term, "abc]");
  ASSERT_EQ(s.rows[0], "");
  ASSERT_INT_EQ(s.cursor_col, 0);
  PASS();
}

void test_extract_bracket_with_prefix() {
  TEST("[cmd] with preceding text on row");
  auto s = MakeState("foo bar [expr 1+1] rest", 18);
  auto term = EditorExtractTerm(s);
  ASSERT_EQ(term, "[expr 1+1]");
  ASSERT_EQ(s.rows[0], "foo bar  rest");
  ASSERT_INT_EQ(s.cursor_col, 8);
  PASS();
}

void test_extract_preserves_other_rows() {
  TEST("extract preserves other rows");
  auto s = MakeMultiline({"first", "second [cmd]", "third"}, 1, 12);
  auto term = EditorExtractTerm(s);
  ASSERT_EQ(term, "[cmd]");
  ASSERT_EQ(s.rows[0], "first");
  ASSERT_EQ(s.rows[1], "second ");
  ASSERT_EQ(s.rows[2], "third");
  ASSERT_INT_EQ(s.num_rows, 3);
  PASS();
}

// ========== EditorWrite tests ==========

void test_write_simple() {
  TEST("write simple text");
  auto s = MakeState("hello ", 6);
  EditorWrite(s, "world");
  ASSERT_EQ(s.rows[0], "hello world");
  ASSERT_INT_EQ(s.cursor_col, 11);
  PASS();
}

void test_write_at_beginning() {
  TEST("write at beginning");
  auto s = MakeState("world", 0);
  EditorWrite(s, "hello ");
  ASSERT_EQ(s.rows[0], "hello world");
  ASSERT_INT_EQ(s.cursor_col, 6);
  PASS();
}

void test_write_in_middle() {
  TEST("write in middle preserves suffix");
  auto s = MakeState("hd", 1);
  EditorWrite(s, "ello worl");
  ASSERT_EQ(s.rows[0], "hello world");
  ASSERT_INT_EQ(s.cursor_col, 10);
  PASS();
}

void test_write_with_newline() {
  TEST("write with newline splits row");
  auto s = MakeState("", 0);
  EditorWrite(s, "line1\nline2");
  ASSERT_INT_EQ(s.num_rows, 2);
  ASSERT_EQ(s.rows[0], "line1");
  ASSERT_EQ(s.rows[1], "line2");
  ASSERT_INT_EQ(s.cursor_row, 1);
  ASSERT_INT_EQ(s.cursor_col, 5);
  PASS();
}

void test_write_newline_preserves_suffix() {
  TEST("write newline preserves suffix");
  auto s = MakeState("helloworld", 5);
  EditorWrite(s, "\n");
  ASSERT_INT_EQ(s.num_rows, 2);
  ASSERT_EQ(s.rows[0], "hello");
  ASSERT_EQ(s.rows[1], "world");
  ASSERT_INT_EQ(s.cursor_row, 1);
  ASSERT_INT_EQ(s.cursor_col, 0);
  PASS();
}

void test_write_multiple_newlines() {
  TEST("write multiple newlines");
  auto s = MakeState("", 0);
  EditorWrite(s, "a\nb\nc");
  ASSERT_INT_EQ(s.num_rows, 3);
  ASSERT_EQ(s.rows[0], "a");
  ASSERT_EQ(s.rows[1], "b");
  ASSERT_EQ(s.rows[2], "c");
  ASSERT_INT_EQ(s.cursor_row, 2);
  ASSERT_INT_EQ(s.cursor_col, 1);
  PASS();
}

void test_write_empty_string() {
  TEST("write empty string is no-op");
  auto s = MakeState("hello", 3);
  EditorWrite(s, "");
  ASSERT_EQ(s.rows[0], "hello");
  ASSERT_INT_EQ(s.cursor_col, 3);
  PASS();
}

void test_write_shifts_rows_down() {
  TEST("write newline shifts existing rows down");
  auto s = MakeMultiline({"first", "second", "third"}, 0, 5);
  EditorWrite(s, "\nnew");
  ASSERT_INT_EQ(s.num_rows, 4);
  ASSERT_EQ(s.rows[0], "first");
  ASSERT_EQ(s.rows[1], "new");
  ASSERT_EQ(s.rows[2], "second");
  ASSERT_EQ(s.rows[3], "third");
  ASSERT_INT_EQ(s.cursor_row, 1);
  ASSERT_INT_EQ(s.cursor_col, 3);
  PASS();
}

void test_write_at_capacity_ignores_newline() {
  TEST("write at kRows capacity ignores newline");
  EditorState s;
  s.num_rows = EditorState::kRows;
  for (int i = 0; i < s.num_rows; i++)
    s.rows[i] = "row";
  s.cursor_row = 0;
  s.cursor_col = 3;
  EditorWrite(s, "\noverflow");
  // Newline ignored (at capacity), text appended to current row
  ASSERT_INT_EQ(s.num_rows, EditorState::kRows);
  ASSERT_EQ(s.rows[0], "rowoverflow");
  PASS();
}

// ========== Combined extract + write (simulating TclEvalInPlace) ==========

void test_extract_then_write() {
  TEST("extract [expr 1+2] then write result");
  auto s = MakeState("prefix [expr 1+2] suffix", 17);
  auto term = EditorExtractTerm(s);
  ASSERT_EQ(term, "[expr 1+2]");
  // Simulate eval result = "3"
  EditorWrite(s, "3");
  ASSERT_EQ(s.rows[0], "prefix 3 suffix");
  ASSERT_INT_EQ(s.cursor_col, 8);
  PASS();
}

void test_extract_then_write_multiline_result() {
  TEST("extract term then write multiline result");
  auto s = MakeState("x [cmd]", 7);
  auto term = EditorExtractTerm(s);
  ASSERT_EQ(term, "[cmd]");
  EditorWrite(s, "line1\nline2");
  ASSERT_INT_EQ(s.num_rows, 2);
  ASSERT_EQ(s.rows[0], "x line1");
  ASSERT_EQ(s.rows[1], "line2");
  PASS();
}

int main() {
  printf("=== EditorExtractTerm tests ===\n");
  test_extract_bare_word();
  test_extract_bare_word_only();
  test_extract_bare_word_mid_row();
  test_extract_bracket_command();
  test_extract_nested_brackets();
  test_extract_braces();
  test_extract_nested_braces();
  test_extract_quoted_string();
  test_extract_quoted_with_escape();
  test_extract_dollar_paren();
  test_extract_dollar_nested_parens();
  test_extract_plain_parens();
  test_extract_empty_cursor();
  test_extract_empty_row();
  test_extract_unmatched_bracket_falls_back();
  test_extract_bracket_with_prefix();
  test_extract_preserves_other_rows();

  printf("\n=== EditorWrite tests ===\n");
  test_write_simple();
  test_write_at_beginning();
  test_write_in_middle();
  test_write_with_newline();
  test_write_newline_preserves_suffix();
  test_write_multiple_newlines();
  test_write_empty_string();
  test_write_shifts_rows_down();
  test_write_at_capacity_ignores_newline();

  printf("\n=== Combined extract + write tests ===\n");
  test_extract_then_write();
  test_extract_then_write_multiline_result();

  printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
  return tests_passed == tests_run ? 0 : 1;
}
