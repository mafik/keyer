#pragma once

#include <charconv>
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace atmt {

using std::function;
using std::nullopt;
using std::optional;
using std::string;
using std::string_view;
using std::vector;

using namespace std::literals;

using clock = std::chrono::steady_clock;
using time_point = clock::time_point;
using duration = clock::duration;

inline int64_t MilliS(duration d) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

inline int64_t MicroS(duration d) {
  return std::chrono::duration_cast<std::chrono::microseconds>(d).count();
}

inline void AppendHex(string &s, uint8_t hex) {
  auto off = s.size();
  s.resize(off + 2);
  snprintf(&s[off], 3, "%02x", hex);
}

} // namespace atmt

using namespace atmt;
