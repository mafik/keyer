#include "eye_term.hpp"

#include "app.hpp"
#include "app_keyboard.hpp"
#include "app_terminal.hpp"
#include "common_esp32.hpp"
#include "keyer.hpp"
#include "main_loop.hpp"
#include "ssh.hpp"

#include <deque>

namespace atmt {

std::deque<string> logs;

void LogAndDeleteOnMain(uint64_t str_ptr) {
  auto str = std::unique_ptr<string>(reinterpret_cast<string *>(str_ptr));
  Debug(str->c_str());
  if (logs.empty() || logs.front() != *str) {
    logs.emplace_front(std::move(*str));
  }
  while (logs.size() > 5) {
    logs.pop_back();
  }
  if (!ssh_connected) {
    string combined;
    for (auto &line : logs) {
      combined += line;
    }
    if (current_app)
      current_app->ShowText(combined);
  }
  // str is auto-deleted here
}

} // namespace atmt

void setup() {
  InitESP32();
  Log("> maf.klaw booting up...\n");
  InitMainLoop();
  InitKeyer();
  current_app = App::Load();
  current_app->OnSetup();
}

void loop() {
  while (atmt::MainLoopNonBlocking()) {
  }
  current_app->OnLoop();
}
