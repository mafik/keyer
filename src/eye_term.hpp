#pragma once

#include "common_esp32.hpp"
#include "main_loop.hpp"

namespace atmt {

// Used internally
void LogAndDeleteOnMain(uint64_t str_ptr);

// Log the given message to the serial console
//
// Make sure to end the message with \n, otherwise it's going to mess up the
// output.
//
// Can be called from any core
//
// Repeated log lines will be automatically deduplicated
template <typename... Args> void Log(Args... args) {
  int required_bytes = snprintf(nullptr, 0, args...);
  auto buffer = new string(required_bytes, '\0');
  snprintf(buffer->data(), required_bytes + 1, args...);
  RunOnMain(LogAndDeleteOnMain, (uint64_t)buffer);
}
} // namespace atmt
