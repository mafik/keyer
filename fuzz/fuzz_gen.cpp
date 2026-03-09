// Generates random ShadowEditor test cases for fuzz_browser.py.
//
// For each test case, generates random current & target states, runs
// NextKeystroke() in a loop, and outputs each keystroke + expected state.
//
// Build: g++ -std=c++17 -O2 -I ../src -o fuzz_gen fuzz_gen.cpp
// Usage: ./fuzz_gen [num_cases] [seed]

#include "../src/keyboard.cpp"
#include "../src/shadow_editor.cpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <random>

using namespace atmt;

static std::mt19937 rng;

constexpr char random_chars[] = "aA1.-_ ";

static std::string RandomString(int max_len) {
  int len = rng() % (max_len + 1);
  std::string s;
  s.reserve(len);
  for (int i = 0; i < len; ++i)
    s += random_chars[rng() % (sizeof(random_chars) - 1)];
  return s;
}

static EditorState RandomState() {
  EditorState s;
  s.num_rows = 1 + rng() % EditorState::kRows;
  for (int i = 0; i < s.num_rows; ++i)
    s.rows[i] = RandomString(5);
  s.cursor_row = rng() % s.num_rows;
  s.cursor_col = rng() % (s.RowLen(s.cursor_row) + 1);
  return s;
}

static std::string JsonEscape(const std::string &s) {
  std::string out;
  out += '"';
  for (char c : s) {
    if (c == '"')
      out += "\\\"";
    else if (c == '\\')
      out += "\\\\";
    else if (c == '\n')
      out += "\\n";
    else
      out += c;
  }
  out += '"';
  return out;
}

static void PrintState(const EditorState &s) {
  printf("{\"rows\":[");
  for (int i = 0; i < s.num_rows; ++i) {
    if (i > 0)
      printf(",");
    printf("%s", JsonEscape(s.rows[i]).c_str());
  }
  printf("],\"cr\":%d,\"cc\":%d}", s.cursor_row, s.cursor_col);
}

static void PrintKeystroke(const Keystroke &ks) {
  if (ks.IsChar())
    printf("{\"type\":\"char\",\"cp\":%u}", ks.codepoint);
  else
    printf("{\"type\":\"key\",\"key\":\"%s\"}", ToStr(ks.key));
}

int main(int argc, char **argv) {
  int num_cases = 100;
  unsigned seed = (unsigned)time(nullptr);

  if (argc > 1)
    num_cases = atoi(argv[1]);
  if (argc > 2)
    seed = (unsigned)atoi(argv[2]);

  rng.seed(seed);
  fprintf(stderr, "Seed: %u, cases: %d\n", seed, num_cases);

  int max_steps = 500;

  printf("[");
  for (int c = 0; c < num_cases; ++c) {
    if (c > 0)
      printf(",\n");

    ShadowEditor ed;
    ed.current = RandomState();
    ed.target = RandomState();

    printf("{\"id\":%d,\"initial\":", c);
    PrintState(ed.current);
    printf(",\"target\":");
    PrintState(ed.target);
    printf(",\"steps\":[");

    bool converged = false;
    for (int step = 0; step < max_steps; ++step) {
      Keystroke ks = ed.NextKeystroke();
      if (ks.IsNone()) {
        converged = true;
        break;
      }
      if (step > 0)
        printf(",");
      printf("{\"k\":");
      PrintKeystroke(ks);
      ApplyKeystroke(ed.current, ks);
      printf(",\"e\":");
      PrintState(ed.current);
      printf("}");
    }

    printf("],\"ok\":%s}", converged ? "true" : "false");
  }
  printf("]\n");

  return 0;
}
