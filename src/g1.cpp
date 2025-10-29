#include "g1.hpp"
#include "common.hpp"
#include "common_esp32.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace atmt {

// Helper functions for C++23 features not available in C++20
template <typename T>
bool starts_with(const std::basic_string_view<T> &sv,
                 const std::basic_string_view<T> &prefix) {
  return sv.size() >= prefix.size() && sv.substr(0, prefix.size()) == prefix;
}

template <typename T>
bool starts_with(const std::basic_string_view<T> &sv, T ch) {
  return !sv.empty() && sv[0] == ch;
}

template <typename T>
bool contains(const std::basic_string_view<T> &sv,
              const std::basic_string_view<T> &substr) {
  return sv.find(substr) != std::basic_string_view<T>::npos;
}

constexpr string_view UART_SERVICE_UUID =
    "6e400001-b5a3-f393-e0a9-e50e24dcca9e"sv;
constexpr string_view UART_TX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"sv;
constexpr string_view UART_RX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"sv;

constexpr duration HEARTBEAT_INTERVAL = 5s;

static std::array<std::pair<int, int>, 296> GetCharWidths() {
  std::array<std::pair<int, int>, 296> char_widths = {};
  int i = 0;
  char_widths[i++] = {' ', 2};
  char_widths[i++] = {'!', 1};
  char_widths[i++] = {'\\', 2};
  char_widths[i++] = {'#', 6};
  char_widths[i++] = {'$', 5};
  char_widths[i++] = {'%', 6};
  char_widths[i++] = {'&', 7};
  char_widths[i++] = {'\'', 1};
  char_widths[i++] = {'(', 2};
  char_widths[i++] = {')', 2};
  char_widths[i++] = {'*', 3};
  char_widths[i++] = {'+', 4};
  char_widths[i++] = {',', 1};
  char_widths[i++] = {'-', 4};
  char_widths[i++] = {'.', 1};
  char_widths[i++] = {'/', 3};
  char_widths[i++] = {'0', 5};
  char_widths[i++] = {'1', 3};
  char_widths[i++] = {'2', 5};
  char_widths[i++] = {'3', 5};
  char_widths[i++] = {'4', 5};
  char_widths[i++] = {'5', 5};
  char_widths[i++] = {'6', 5};
  char_widths[i++] = {'7', 5};
  char_widths[i++] = {'8', 5};
  char_widths[i++] = {'9', 5};
  char_widths[i++] = {':', 1};
  char_widths[i++] = {';', 1};
  char_widths[i++] = {'<', 4};
  char_widths[i++] = {'=', 4};
  char_widths[i++] = {'>', 4};
  char_widths[i++] = {'?', 5};
  char_widths[i++] = {'@', 7};
  char_widths[i++] = {'A', 6};
  char_widths[i++] = {'B', 5};
  char_widths[i++] = {'C', 5};
  char_widths[i++] = {'D', 5};
  char_widths[i++] = {'E', 4};
  char_widths[i++] = {'F', 4};
  char_widths[i++] = {'G', 5};
  char_widths[i++] = {'H', 5};
  char_widths[i++] = {'I', 2};
  char_widths[i++] = {'J', 3};
  char_widths[i++] = {'K', 5};
  char_widths[i++] = {'L', 4};
  char_widths[i++] = {'M', 7};
  char_widths[i++] = {'N', 5};
  char_widths[i++] = {'O', 5};
  char_widths[i++] = {'P', 5};
  char_widths[i++] = {'Q', 5};
  char_widths[i++] = {'R', 5};
  char_widths[i++] = {'S', 5};
  char_widths[i++] = {'T', 5};
  char_widths[i++] = {'U', 5};
  char_widths[i++] = {'V', 6};
  char_widths[i++] = {'W', 7};
  char_widths[i++] = {'X', 6};
  char_widths[i++] = {'Y', 6};
  char_widths[i++] = {'Z', 5};
  char_widths[i++] = {'[', 2};
  char_widths[i++] = {'\\', 3};
  char_widths[i++] = {']', 2};
  char_widths[i++] = {'^', 4};
  char_widths[i++] = {'_', 3};
  char_widths[i++] = {'`', 2};
  char_widths[i++] = {'a', 5};
  char_widths[i++] = {'b', 4};
  char_widths[i++] = {'c', 4};
  char_widths[i++] = {'d', 4};
  char_widths[i++] = {'e', 4};
  char_widths[i++] = {'f', 4};
  char_widths[i++] = {'g', 4};
  char_widths[i++] = {'h', 4};
  char_widths[i++] = {'i', 1};
  char_widths[i++] = {'j', 2};
  char_widths[i++] = {'k', 4};
  char_widths[i++] = {'l', 1};
  char_widths[i++] = {'m', 7};
  char_widths[i++] = {'n', 4};
  char_widths[i++] = {'o', 4};
  char_widths[i++] = {'p', 4};
  char_widths[i++] = {'q', 4};
  char_widths[i++] = {'r', 3};
  char_widths[i++] = {'s', 4};
  char_widths[i++] = {'t', 3};
  char_widths[i++] = {'u', 5};
  char_widths[i++] = {'v', 5};
  char_widths[i++] = {'w', 7};
  char_widths[i++] = {'x', 5};
  char_widths[i++] = {'y', 5};
  char_widths[i++] = {'z', 4};
  char_widths[i++] = {'{', 3};
  char_widths[i++] = {'|', 1};
  char_widths[i++] = {'}', 3};
  char_widths[i++] = {'~', 7};
  char_widths[i++] = {192, 6};
  char_widths[i++] = {194, 6};
  char_widths[i++] = {199, 5};
  char_widths[i++] = {200, 4};
  char_widths[i++] = {201, 4};
  char_widths[i++] = {202, 4};
  char_widths[i++] = {203, 4};
  char_widths[i++] = {206, 3};
  char_widths[i++] = {207, 3};
  char_widths[i++] = {212, 5};
  char_widths[i++] = {217, 5};
  char_widths[i++] = {219, 5};
  char_widths[i++] = {220, 5};
  char_widths[i++] = {224, 5};
  char_widths[i++] = {231, 4};
  char_widths[i++] = {232, 4};
  char_widths[i++] = {233, 4};
  char_widths[i++] = {234, 4};
  char_widths[i++] = {235, 4};
  char_widths[i++] = {238, 3};
  char_widths[i++] = {239, 3};
  char_widths[i++] = {244, 4};
  char_widths[i++] = {249, 5};
  char_widths[i++] = {251, 5};
  char_widths[i++] = {252, 5};
  char_widths[i++] = {255, 5};
  char_widths[i++] = {376, 6};
  char_widths[i++] = {196, 6};
  char_widths[i++] = {228, 5};
  char_widths[i++] = {214, 5};
  char_widths[i++] = {246, 4};
  char_widths[i++] = {223, 4};
  char_widths[i++] = {7838, 5};
  char_widths[i++] = {226, 5};
  char_widths[i++] = {193, 6};
  char_widths[i++] = {225, 5};
  char_widths[i++] = {205, 2};
  char_widths[i++] = {237, 2};
  char_widths[i++] = {209, 5};
  char_widths[i++] = {241, 4};
  char_widths[i++] = {250, 5};
  char_widths[i++] = {211, 5};
  char_widths[i++] = {243, 4};
  char_widths[i++] = {218, 5};
  char_widths[i++] = {'.', 1};
  char_widths[i++] = {',', 1};
  char_widths[i++] = {':', 1};
  char_widths[i++] = {';', 1};
  char_widths[i++] = {8230, 4};
  char_widths[i++] = {'!', 1};
  char_widths[i++] = {'?', 5};
  char_widths[i++] = {183, 1};
  char_widths[i++] = {8226, 3};
  char_widths[i++] = {'*', 3};
  char_widths[i++] = {'#', 6};
  char_widths[i++] = {'/', 3};
  char_widths[i++] = {'\\', 3};
  char_widths[i++] = {'-', 4};
  char_widths[i++] = {8211, 6};
  char_widths[i++] = {8212, 6};
  char_widths[i++] = {'_', 3};
  char_widths[i++] = {'(', 2};
  char_widths[i++] = {')', 2};
  char_widths[i++] = {'{', 3};
  char_widths[i++] = {'}', 3};
  char_widths[i++] = {'[', 2};
  char_widths[i++] = {']', 2};
  char_widths[i++] = {8220, 3};
  char_widths[i++] = {8221, 3};
  char_widths[i++] = {8216, 1};
  char_widths[i++] = {8217, 1};
  char_widths[i++] = {8249, 3};
  char_widths[i++] = {8250, 3};
  char_widths[i++] = {'"', 2};
  char_widths[i++] = {'\'', 1};
  char_widths[i++] = {'@', 7};
  char_widths[i++] = {'&', 7};
  char_widths[i++] = {'|', 1};
  char_widths[i++] = {'+', 4};
  char_widths[i++] = {'=', 4};
  char_widths[i++] = {'>', 4};
  char_widths[i++] = {'<', 4};
  char_widths[i++] = {'~', 7};
  char_widths[i++] = {'^', 4};
  char_widths[i++] = {'%', 6};
  char_widths[i++] = {8260, 4};
  char_widths[i++] = {189, 6};
  char_widths[i++] = {188, 6};
  char_widths[i++] = {190, 7};
  char_widths[i++] = {8539, 6};
  char_widths[i++] = {8540, 7};
  char_widths[i++] = {8541, 7};
  char_widths[i++] = {8542, 6};
  char_widths[i++] = {8320, 3};
  char_widths[i++] = {8321, 2};
  char_widths[i++] = {8322, 3};
  char_widths[i++] = {8323, 3};
  char_widths[i++] = {8324, 3};
  char_widths[i++] = {8325, 3};
  char_widths[i++] = {8326, 3};
  char_widths[i++] = {8327, 3};
  char_widths[i++] = {8328, 3};
  char_widths[i++] = {8329, 3};
  char_widths[i++] = {8304, 3};
  char_widths[i++] = {8305, 6};
  char_widths[i++] = {8306, 6};
  char_widths[i++] = {8307, 6};
  char_widths[i++] = {8308, 3};
  char_widths[i++] = {8309, 3};
  char_widths[i++] = {8310, 3};
  char_widths[i++] = {8311, 3};
  char_widths[i++] = {8312, 3};
  char_widths[i++] = {8313, 3};
  char_widths[i++] = {191, 5};
  char_widths[i++] = {8218, 1};
  char_widths[i++] = {8222, 3};
  char_widths[i++] = {171, 5};
  char_widths[i++] = {187, 5};
  char_widths[i++] = {3647, 5};
  char_widths[i++] = {182, 7};
  char_widths[i++] = {167, 5};
  char_widths[i++] = {169, 8};
  char_widths[i++] = {174, 5};
  char_widths[i++] = {8482, 6};
  char_widths[i++] = {176, 2};
  char_widths[i++] = {166, 1};
  char_widths[i++] = {8224, 5};
  char_widths[i++] = {8225, 5};
  char_widths[i++] = {8364, 7};
  char_widths[i++] = {8383, 5};
  char_widths[i++] = {162, 5};
  char_widths[i++] = {'$', 5};
  char_widths[i++] = {163, 6};
  char_widths[i++] = {165, 5};
  char_widths[i++] = {8722, 4};
  char_widths[i++] = {215, 4};
  char_widths[i++] = {247, 4};
  char_widths[i++] = {8800, 4};
  char_widths[i++] = {8805, 4};
  char_widths[i++] = {8804, 4};
  char_widths[i++] = {177, 4};
  char_widths[i++] = {8776, 5};
  char_widths[i++] = {172, 5};
  char_widths[i++] = {8734, 8};
  char_widths[i++] = {8747, 5};
  char_widths[i++] = {8719, 5};
  char_widths[i++] = {8721, 5};
  char_widths[i++] = {8730, 5};
  char_widths[i++] = {8706, 5};
  char_widths[i++] = {8240, 7};
  char_widths[i++] = {8593, 6};
  char_widths[i++] = {8599, 6};
  char_widths[i++] = {8594, 7};
  char_widths[i++] = {8600, 6};
  char_widths[i++] = {8595, 6};
  char_widths[i++] = {8601, 6};
  char_widths[i++] = {8592, 7};
  char_widths[i++] = {8598, 6};
  char_widths[i++] = {8596, 8};
  char_widths[i++] = {8597, 6};
  char_widths[i++] = {9676, 9};
  char_widths[i++] = {9674, 5};
  char_widths[i++] = {168, 3};
  char_widths[i++] = {729, 1};
  char_widths[i++] = {'`', 2};
  char_widths[i++] = {180, 2};
  char_widths[i++] = {733, 4};
  char_widths[i++] = {710, 3};
  char_widths[i++] = {711, 3};
  char_widths[i++] = {728, 4};
  char_widths[i++] = {730, 2};
  char_widths[i++] = {732, 3};
  char_widths[i++] = {175, 3};
  char_widths[i++] = {184, 2};
  char_widths[i++] = {731, 2};
  char_widths[i++] = {306, 4};
  char_widths[i++] = {307, 2};
  char_widths[i++] = {352, 5};
  char_widths[i++] = {353, 4};
  char_widths[i++] = {381, 5};
  char_widths[i++] = {382, 4};
  char_widths[i++] = {195, 6};
  char_widths[i++] = {197, 6};
  char_widths[i++] = {198, 8};
  char_widths[i++] = {204, 2};
  char_widths[i++] = {208, 5};
  char_widths[i++] = {210, 5};
  char_widths[i++] = {213, 5};
  char_widths[i++] = {216, 6};
  char_widths[i++] = {221, 6};
  char_widths[i++] = {222, 5};
  char_widths[i++] = {227, 5};
  char_widths[i++] = {229, 5};
  char_widths[i++] = {230, 8};
  char_widths[i++] = {236, 2};
  char_widths[i++] = {240, 5};
  char_widths[i++] = {242, 4};
  char_widths[i++] = {245, 4};
  char_widths[i++] = {248, 5};
  char_widths[i++] = {253, 5};
  char_widths[i++] = {254, 4};
  std::sort(char_widths.begin(), char_widths.end());
  return char_widths;
}

static std::array<std::pair<int, int>, 296> CHAR_WIDTHS = GetCharWidths();

int GetCharWidth(int codepoint) {
  auto it = std::lower_bound(
      CHAR_WIDTHS.begin(), CHAR_WIDTHS.end(), codepoint,
      [](const auto &pair, int cp) { return pair.first < cp; });

  if (it != CHAR_WIDTHS.end() && it->first == codepoint) {
    return it->second + 1;
  }

  return 0;
}

int G1::MeasureText(string_view text) {
  int total_width = 0;
  size_t i = 0;

  while (i < text.size()) {
    uint8_t byte = static_cast<uint8_t>(text[i]);
    int codepoint = 0;
    size_t bytes_to_read = 0;

    if ((byte & 0x80) == 0) {
      codepoint = byte;
      bytes_to_read = 1;
    } else if ((byte & 0xE0) == 0xC0) {
      codepoint = byte & 0x1F;
      bytes_to_read = 2;
    } else if ((byte & 0xF0) == 0xE0) {
      codepoint = byte & 0x0F;
      bytes_to_read = 3;
    } else if ((byte & 0xF8) == 0xF0) {
      codepoint = byte & 0x07;
      bytes_to_read = 4;
    } else {
      i++;
      continue;
    }

    for (size_t j = 1; j < bytes_to_read && (i + j) < text.size(); j++) {
      uint8_t continuation = static_cast<uint8_t>(text[i + j]);
      if ((continuation & 0xC0) != 0x80) {
        break;
      }
      codepoint = (codepoint << 6) | (continuation & 0x3F);
    }

    total_width += GetCharWidth(codepoint);
    i += bytes_to_read;
  }

  return total_width;
}

//////////////////
// Utility functions for common communication tasks
//////////////////

static void MuteAck(G1 &g1, std::function<bool(std::string_view)> predicate) {
  for (auto *eye : {&g1.left, &g1.right}) {
    eye->watchers.emplace_back(
        [predicate, deadline = clock::now() + 5s](
            string_view message, bool &keep_watching, bool &keep_processing) {
          if (predicate(message)) {
            keep_watching = false;
            keep_processing = false;
          } else if (clock::now() > deadline) {
            keep_watching = false;
          }
        });
  }
}

static void MuteAck(G1 &g1, uint8_t command) {
  MuteAck(g1, [command](string_view msg) {
    return starts_with(msg, (char)command);
  });
}

constexpr uint8_t CMD_BRIGHTNESS = 0x01;

static string_view BrightnessCmd(int level_0_to_42, bool auto_adjust) {
  static char cmd[] = {CMD_BRIGHTNESS, static_cast<char>(level_0_to_42 & 0xFF),
                       static_cast<char>(auto_adjust ? 0x01 : 0x00)};
  return {cmd, sizeof(cmd)};
}

G1::G1(Callbacks &callbacks) : callbacks(callbacks) {}

G1::~G1() {}

G1::BleCommand G1::Poll(Args &args) {
  bool want_scan = left.address.empty() || right.address.empty();
  if (want_scan && !scanning) {
    return BleCommand::SCAN_START;
  }

  if (!want_scan && scanning) {
    return BleCommand::SCAN_STOP;
  }

  // We need to find both eyes before proceeding further
  if (want_scan) {
    args.wait_deadline = time_point::max();
    return BleCommand::WAIT;
  }

  auto now = std::chrono::steady_clock::now();

  for (auto *eye : {&left, &right}) {
    // heh, it's basically a coroutine
    switch (eye->connection) {
    case Eye::NOT_CONNECTED:
      eye->connection = Eye::CONNECTING;
      args.address = eye->address;
      return BleCommand::CONNECT;
    case Eye::CONNECTING:
      // Only one connection attempt can happen at a time
      args.wait_deadline = time_point::max();
      return BleCommand::WAIT;
    case Eye::SERVICES_NOT_RESOLVED: // a.k.a. CONNECTED
      switch (eye->pairing) {
      case Eye::NOT_PAIRED:
        eye->pairing = Eye::PAIRING;
        args.address = eye->address;
        return BleCommand::PAIR;
      case Eye::PAIRING:
        // Only one pairing can happen at a time
        args.wait_deadline = time_point::max();
        return BleCommand::WAIT;
      case Eye::NOT_BONDED:
        eye->pairing = Eye::BONDING;
        args.address = eye->address;
        return BleCommand::BOND;
      case Eye::BONDING:
        break;
      case Eye::BONDED:
        eye->connection = Eye::SERVICES_RESOLVING;
        args.address = eye->address;
        return BleCommand::RESOLVE_SERVICES;
      }
      break;
    case Eye::SERVICES_RESOLVING:
      break;
    case Eye::UART_NOT_SUBSCRIBED: // a.k.a. SERVICES_RESOLVED
      eye->connection = Eye::UART_SUBSCRIBING;
      args.address = eye->address;
      args.service_uuid = UART_SERVICE_UUID;
      args.characteristic_uuid = UART_RX_UUID;
      return BleCommand::SUBSCRIBE_CHARACTERISTIC_NOTIFY;
    case Eye::UART_SUBSCRIBING:
      break;
    case Eye::IDLE:
      break;
    }
  }
  bool both_eyes_present =
      left.connection == Eye::IDLE && right.connection == Eye::IDLE;

  if (both_eyes_present && !ready) {
    ready = true;

    if (target_brightness) {
      Brightness(target_brightness->value_0_to_42,
                 target_brightness->auto_adjust);
    }
    if (!target_text.empty()) {
      Text(target_text);
    }
    callbacks.OnReady();
  }

  if (both_eyes_present) {

    if (next_heartbeat <= now) { // Check if heartbeat is due
      next_heartbeat = now + HEARTBEAT_INTERVAL;

      args.wait_deadline = next_heartbeat + 1ms;
      std::array<char, 6> heartbeat_buffer;
      heartbeat_buffer[0] = 0x25;
      heartbeat_buffer[1] = 0x06;
      heartbeat_buffer[2] = 0x00;
      heartbeat_buffer[3] = heartbeat_seq;
      heartbeat_buffer[4] = 0x04;
      heartbeat_buffer[5] = heartbeat_seq;
      heartbeat_seq++;
      outbox.erase(std::remove_if(outbox.begin(), outbox.end(),
                                  [](const auto &write) {
                                    return starts_with(string_view(write.value),
                                                       (char)0x25);
                                  }),
                   outbox.end());
      Send(string_view(heartbeat_buffer.data(), heartbeat_buffer.size()));
    }

    if (!outbox.empty()) { // Check outbox for messages to send
      auto it = outbox.begin();
      args.address = it->eye->address;
      args.service_uuid = UART_SERVICE_UUID;
      args.characteristic_uuid = UART_TX_UUID;
      args.value = std::move(it->value);
      outbox.erase(it);
      next_heartbeat = now + HEARTBEAT_INTERVAL;
      return BleCommand::WRITE_CHARACTERISTIC;
    }

    args.wait_deadline = next_heartbeat + 1ms;
  } else {
    args.wait_deadline = time_point::max();
  }

  return BleCommand::WAIT;
}

void G1::OnScanStarted() {
  if (scanning) {
    callbacks.OnAssertionFailure(
        "OnScanStarted called but scanning is already active");
  }
  scanning = true;
}

void G1::OnScanStopped() {
  if (!scanning) {
    callbacks.OnAssertionFailure(
        "OnScanStopped called but scanning is not active");
  }
  scanning = false;
}

static void OnFoundEye(G1 *g1, const char *eye_name, G1::Eye &eye,
                       string_view address, string_view identifier) {
  if (eye.address.empty()) {
    eye.address = address;
  } else if (eye.address == address) {
  } else {
    g1->callbacks.OnAssertionFailure(
        "Found EXTRA "s + eye_name + " " + string(identifier) + " at " +
        string(address) + " (existing MAC address: " + eye.address + ")");
  }
}

void G1::OnFoundPeripheral(string_view address, string_view identifier) {
  if (starts_with(identifier, string_view("Even G1"))) {
    if (contains(identifier, string_view("_L_"))) {
      OnFoundEye(this, "LEFT", left, address, identifier);
    }
    if (contains(identifier, string_view("_R_"))) {
      OnFoundEye(this, "RIGHT", right, address, identifier);
    }
  }
}

static G1::Eye *GetEye(G1 &g1, string_view address) {
  if (g1.left.address == address) {
    return &g1.left;
  }
  if (g1.right.address == address) {
    return &g1.right;
  }
  return nullptr;
}

void G1::OnConnected(string_view address) {
  if (auto eye = GetEye(*this, address)) {
    eye->connection = Eye::CONNECTED;
  } else {
    callbacks.OnAssertionFailure(
        "Connected to a device that wasn't passed to OnFoundPeripheral");
  }
}

void G1::OnPaired(string_view address) {
  if (auto eye = GetEye(*this, address)) {
    if (eye->pairing < Eye::PAIRED) {
      eye->pairing = Eye::PAIRED;
    }
  }
}

void G1::OnBonded(string_view address) {
  if (auto eye = GetEye(*this, address)) {
    eye->pairing = Eye::BONDED;
  }
}

void G1::OnServicesResolved(string_view address) {
  if (auto eye = GetEye(*this, address)) {
    eye->connection = Eye::SERVICES_RESOLVED;
  } else {
    callbacks.OnAssertionFailure("Services resolved with a device that wasn't "
                                 "passed to OnFoundPeripheral");
  }
}

void G1::OnDisconnected(string_view address) {
  if (auto eye = GetEye(*this, address)) {
    eye->connection = Eye::NOT_CONNECTED;
    for (auto it = outbox.begin(); it != outbox.end();) {
      if (it->eye == eye) {
        it = outbox.erase(it);
      } else {
        ++it;
      }
    }
  } else {
    callbacks.OnAssertionFailure("Disconnected with a device that wasn't "
                                 "passed to OnFoundPeripheral");
  }
}

void G1::OnSubscribedCharacteristic(string_view address,
                                    string_view service_uuid,
                                    string_view characteristic_uuid) {
  if (auto eye = GetEye(*this, address)) {
    if (service_uuid == UART_SERVICE_UUID &&
        characteristic_uuid == UART_RX_UUID) {
      eye->connection = Eye::IDLE;
    } else {
      callbacks.OnAssertionFailure("Subscribed unknown characteristic");
    }
  } else {
    callbacks.OnAssertionFailure(
        "Subscribed characteristic on unknown device!");
  }
}

void G1::OnCharacteristicNotified(string_view address,
                                  string_view characteristic_uuid,
                                  string_view value) {
  if (value.empty()) {
    callbacks.OnError("Got an empty notification");
  }

  if (characteristic_uuid != UART_RX_UUID) {
    callbacks.OnAssertionFailure(
        "Got a notification from an unknown GATT characteristic");
  }

  Eye *eye = GetEye(*this, address);
  if (!eye) {
    callbacks.OnAssertionFailure("Got a notification from an unknown device!");
  }
  eye->last_activity = clock::now();

  bool keep_processing = true;
  for (auto it = eye->watchers.begin(); it != eye->watchers.end();) {
    bool keep_watching = true;
    (*it)(value, keep_watching, keep_processing);
    if (keep_watching) {
      ++it;
    } else {
      it = eye->watchers.erase(it);
    }
    if (!keep_processing) {
      return;
    }
  }

  unsigned char cmd = static_cast<unsigned char>(value[0]);

  switch (cmd) {
  case 0x25:
    // Heartbeat response - handled internally by G1, don't print
    return;

  case 0xF1:
    callbacks.OnMicrophoneData(value.substr(1));
    return;

  case 0x22:
    // Dashboard status
    activity = DASHBOARD;
    return;

  case 0xF5:
    // Event notifications
    if (value.size() < 2) {
      callbacks.OnError(
          "Invalid 0xF5 packet (event notification): needs at least two bytes");
      return;
    }

    {
      unsigned char subcmd = static_cast<unsigned char>(value[1]);
      switch (subcmd) {
      case 0x00:
        callbacks.OnExit(eye == &right);
        return;

      case 0x02:
        callbacks.OnHeadUp(eye == &right);
        return;

      case 0x03:
        callbacks.OnHeadStraight(eye == &right);
        return;

      case 0x06:
        callbacks.OnGlassesOn(eye == &right);
        return;

      case 0x07:
        callbacks.OnGlassesOff(eye == &right);
        return;

      case 0x08:
        callbacks.OnCradleOpen(eye == &right);
        return;

      case 0x09:
        if (value.size() >= 3) {
          unsigned char in_cradle = static_cast<unsigned char>(value[2]);
          callbacks.OnCradleState(eye == &right, in_cradle);
        } else {
          callbacks.OnError("Invalid 0xF5 0x09 packet: missing cradle status");
        }
        return;

      case 0x0A:
        if (value.size() >= 3) {
          unsigned char battery_level = static_cast<unsigned char>(value[2]);
          callbacks.OnBatteryLevel(eye == &right, battery_level);
        } else {
          callbacks.OnError("Invalid 0xF5 0x0A packet: missing battery level");
        }
        return;

      case 0x0B:
        callbacks.OnCradleClosed(eye == &right);
        return;

      case 0x0E:
        callbacks.OnGlassesCharging(eye == &right);
        return;

      case 0x0F:
        if (value.size() >= 3) {
          unsigned char cradle_battery = static_cast<unsigned char>(value[2]);
          callbacks.OnCradleBatteryLevel(eye == &right, cradle_battery);
        } else {
          callbacks.OnError(
              "Invalid 0xF5 0x0F packet: missing cradle battery level");
        }
        return;

      case 0x11:
        callbacks.OnConnected(eye == &right);
        return;

      case 0x12:
        if (value.size() >= 3) {
          unsigned char brightness = static_cast<unsigned char>(value[2]);
          callbacks.OnBrightness(eye == &right, brightness);
        } else {
          callbacks.OnError(
              "Invalid 0xF5 0x12 packet: missing brightness level");
        }
        return;

      case 0x17:
        callbacks.OnLeftPress(eye == &right);
        return;

      case 0x18:
        callbacks.OnLeftRelease(eye == &right);
        return;

      case 0x1E:
        callbacks.OnDashboardOpen(eye == &right);
        return;

      case 0x1F:
        callbacks.OnDashboardClosed(eye == &right);
        return;

      default:
        // Unknown 0xF5 subcommand - print for debugging
        string err = "Event 0xF5 subcommand 0x";
        AppendHex(err, subcmd);
        err += ' ';
        for (size_t i = 2; i < value.size(); i++) {
          AppendHex(err, value[i]);
        }
        callbacks.OnError(err);
        return;
      }
    }

  default: {
    // Unknown or unhandled packet - print for debugging
    string err = "NOTIFY from ";
    err += address;
    err += ": ";
    for (auto byte : value) {
      AppendHex(err, byte);
    }
    callbacks.OnError(err);
    return;
  }
  }
}

//////////////////////
// Smart Glasses API
//////////////////////

void G1::Send(string_view message, Eye *eye) {
  if (eye == nullptr ||
      (eye == &left && left.connection == Eye::UART_SUBSCRIBED)) {
    outbox.emplace_back(std::string(message), left);
  }
  if (eye == nullptr ||
      (eye == &right && right.connection == Eye::UART_SUBSCRIBED)) {
    outbox.emplace_back(std::string(message), right);
  }
}

void G1::Brightness(int level_0_to_42, bool auto_adjust) {
  target_brightness = {level_0_to_42, auto_adjust};
  if (left.connection != Eye::IDLE || right.connection != Eye::IDLE) {
    // target_brightness will be sent when both eyes reconnect
    return;
  }
  Send(BrightnessCmd(level_0_to_42, auto_adjust));
  MuteAck(*this, CMD_BRIGHTNESS);
}

void G1::Silent(bool silent) {
  char cmd[] = {0x03, static_cast<char>(silent ? 0x0C : 0x0A), 0x00};
  Send({cmd, sizeof(cmd)});
  MuteAck(*this, 0x03);
}

void G1::HeadUpAngle(int angle_0_to_60) {
  char cmd[] = {
      0x0B,
      static_cast<char>(angle_0_to_60 & 0xFF),
      0x01,
  };
  Send({cmd, sizeof(cmd)});
  MuteAck(*this, 0x0B);
}

void G1::Microphone(bool on) {
  char cmd[] = {
      0x0E,
      static_cast<char>(on ? 0x01 : 0x00),
  };
  Send({cmd, sizeof(cmd)}, &right);
  MuteAck(*this, 0x0E);
}

void G1::Dashboard(bool show, char position_0_to_8, char depth_0_to_9) {
  char cmd[] = {
      0x26, // DASHBOARD_POSITION command
      0x08,
      0x00, // seq (is this related to heartbeats?)
      (char)++dashboard_counter,
      0x02,
      (char)show,      // state: ON or OFF
      position_0_to_8, // position 0-8
      depth_0_to_9,
  };

  Send({cmd, sizeof(cmd)});
  MuteAck(*this, 0x26);
}

static uint32_t CalculateCRC32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFF;
  constexpr uint32_t polynomial = 0xEDB88320;

  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (-(int)(crc & 1) & polynomial);
    }
  }

  return ~crc;
}

void G1::NoteAdd(int slot_1_to_4, string_view title, string_view text) {
  if (slot_1_to_4 < 1 || slot_1_to_4 > 4) {
    callbacks.OnAssertionFailure("Invalid note slot: "s +
                                 std::to_string(slot_1_to_4) +
                                 " (must be 1-4)");
    return;
  }

  uint8_t version = static_cast<uint8_t>(
      std::chrono::system_clock::now().time_since_epoch().count() % 256);

  string cmd;
  cmd += static_cast<char>(0x1E);
  cmd += static_cast<char>(0);

  cmd += static_cast<char>(0x00);
  cmd += static_cast<char>(version);
  cmd += static_cast<char>(0x03);
  cmd += static_cast<char>(0x01);
  cmd += static_cast<char>(0x00);
  cmd += static_cast<char>(0x01);
  cmd += static_cast<char>(0x00);
  cmd += static_cast<char>(slot_1_to_4);
  cmd += static_cast<char>(0x01);
  cmd += static_cast<char>(title.size());
  cmd += title;
  cmd += static_cast<char>(text.size());
  cmd += static_cast<char>(0x00);
  cmd += text;

  cmd[1] = static_cast<char>(cmd.size());

  Send(cmd);
  // MuteAck(*this, 0x1E);
}

void G1::NoteDelete(int slot_1_to_4) {
  if (slot_1_to_4 < 1 || slot_1_to_4 > 4) {
    callbacks.OnAssertionFailure("Invalid note slot: "s +
                                 std::to_string(slot_1_to_4) +
                                 " (must be 1-4)");
    return;
  }

  char cmd[] = {0x1E, 0x10,
                0x00, static_cast<char>(0xE0),
                0x03, 0x01,
                0x00, 0x01,
                0x00, static_cast<char>(slot_1_to_4),
                0x00, 0x01,
                0x00, 0x01,
                0x00, 0x00};

  Send({cmd, sizeof(cmd)});
  // MuteAck(*this, 0x1E);
}

static void WaitAck(G1 &g1, uint8_t command,
                    function<void(bool timed_out)> callback) {
  struct Continuation {
    G1 *g1;
    int latch;
    function<void(bool timed_out)> work;

    void count_down(bool timed_out) {
      latch--;
      if (latch == 0) {
        do_work(timed_out);
      }
    }

    void do_work(bool timed_out) {
      work(timed_out);
      delete this;
    }
  };

  auto c = new Continuation();
  c->g1 = &g1;
  c->latch = 2;
  c->work = callback;

  for (auto *eye : {&g1.left, &g1.right}) {
    eye->watchers.emplace_back(
        [c, command, deadline = clock::now() + 5s](
            string_view message, bool &keep_watching, bool &keep_processing) {
          if (starts_with(message, string_view((const char *)&command, 1))) {
            c->count_down(false);
            keep_watching = false;
            keep_processing = false;
          } else if (clock::now() > deadline) {
            c->count_down(true);
            keep_watching = false;
          }
        });
  }
}

void G1::BMP(const uint8_t *bmp_bytes, size_t len) {
  target_bmp = string((const char *)bmp_bytes, len);
  if (activity == ACTIVITY_BMP_TRANSMISSION) {
    return;
  }
  if (left.connection != Eye::IDLE || right.connection != Eye::IDLE) {
    return;
  }
  struct BMPTransmitter {
    G1 *g1;
    vector<string> bmp_chunks;
    bool transmission_failed;
    string crc_packet;
    int watchers;

    BMPTransmitter(G1 *g1) : g1(g1) {
      g1->activity = ACTIVITY_BMP_TRANSMISSION;
    }

    ~BMPTransmitter() {
      if (g1->activity == ACTIVITY_BMP_TRANSMISSION) {
        g1->activity = ACTIVITY_BMP;
      }
    }

    void Start() {
      size_t bmp_size = g1->target_bmp.size();
      constexpr size_t packet_data_size = 194;
      // constexpr uint8_t storage_address[] = {0x00, 0x1C, 0x00, 0x00};
      constexpr uint8_t storage_address[] = {0x00, 0x1C, 0x00, 0x00};

      size_t total_packets =
          (bmp_size + packet_data_size - 1) / packet_data_size;

      bmp_chunks.resize(total_packets);

      // Prepare CRC data (storage address + all image data)
      std::vector<uint8_t> crc_data;
      crc_data.reserve(sizeof(storage_address) + bmp_size);

      // Send all data packets
      for (size_t seq = 0; seq < total_packets; seq++) {
        string &packet = bmp_chunks[seq];
        packet.clear();
        packet += 0x15;
        packet += seq & 0xFF;

        if (seq == 0) {
          packet +=
              string_view((char *)storage_address, sizeof(storage_address));
        }

        size_t offset = seq * packet_data_size;
        size_t end = offset + packet_data_size;
        if (end > bmp_size) {
          end = bmp_size;
        }
        int packet_len = end - offset;
        packet += string_view(g1->target_bmp).substr(offset, packet_len);

        g1->Send(packet);

        // This variant produces 'c9' at 5th byte of CRC response.
        // It matches the official demo app:
        // https://github.com/even-realities/EvenDemoApp/blob/main/lib/controllers/bmp_update_manager.dart
        // 'c9' seems to indicate that the checksum is correct.
        crc_data.insert(crc_data.end(), std::begin(packet) + 2,
                        std::end(packet));
      }

      auto crc = CalculateCRC32(crc_data.data(), crc_data.size());
      crc_packet.resize(5);
      crc_packet[0] = 0x16;
      crc_packet[1] = static_cast<uint8_t>((crc >> 24) & 0xFF);
      crc_packet[2] = static_cast<uint8_t>((crc >> 16) & 0xFF);
      crc_packet[3] = static_cast<uint8_t>((crc >> 8) & 0xFF);
      crc_packet[4] = static_cast<uint8_t>(crc & 0xFF);

      // Send end packet
      // static const string end_packet = {0x20, 0x0D, 0x0E};
      static const string end_packet = {0x20, 0x0D, 0x0E};
      bmp_chunks.push_back(end_packet);
      g1->Send(end_packet);

      watchers = 2;

      g1->left.watchers.emplace_back(
          [this, deadline = clock::now() + 10s, eye = &g1->left](
              string_view message, bool &keep_watching, bool &keep_processing) {
            if (message[0] == 0x15 &&
                message[2] == 0xca) { // retransmission request

              Debugf("Removing messages to %s\n", eye->address.c_str());
              auto new_end = std::remove_if(
                  g1->outbox.begin(), g1->outbox.end(),
                  [eye](const BleWrite &write) {
                    return (&write.eye == &eye) &&
                           ((write.value[0] == 0x15) || write.value[0] == 0x20);
                  });
              Debugf("Trimming outbox...\n");
              g1->outbox.erase(new_end, g1->outbox.end());

              Debugf("Filling new_outbox\n");

              std::deque<BleWrite> new_outbox;

              for (int seq = message[1]; seq < bmp_chunks.size(); seq++) {
                new_outbox.emplace_back(bmp_chunks[seq], *eye);
                if (!g1->outbox.empty()) {
                  new_outbox.emplace_back(std::move(g1->outbox.front()));
                  g1->outbox.pop_front();
                }
              }
              Debugf("Moving remaining items... (%d)\n", g1->outbox.size());
              while (!g1->outbox.empty()) {
                new_outbox.emplace_back(std::move(g1->outbox.front()));
                g1->outbox.pop_front();
              }
              Debugf("Swapping new_outbox\n");
              g1->outbox = std::move(new_outbox);
            } else if (message[0] == 0x20) { // end packet delivered
              // Now send it to the right eye
              Debugf("Left eye confirmed end packet!\n");
              // g1->Send(end_packet, &g1->right);
            } else if (message[0] == 0x16) {
              keep_watching = false;
            } else if (clock::now() > deadline) {
              keep_watching = false;
            }
            if (!keep_watching) {
              WatcherStopped();
            }
          });

      g1->right.watchers.emplace_back(
          [this, deadline = clock::now() + 10s, eye = &g1->right](
              string_view message, bool &keep_watching, bool &keep_processing) {
            if (message[0] == 0x15 &&
                message[2] == 0xca) { // retransmission request
              if (message[1] < bmp_chunks.size()) {
                // Debugf("Retransmitting chunk %d/%d to the right eye\n",
                //        message[1], bmp_chunks.size());
                // g1->outbox.emplace_front(bmp_chunks[message[1]], *eye);
              } else {
                // Debugf("Retransmitting end packet to the right eye\n");

                // g1->outbox.emplace_front(end_packet, *eye);
              }

              // auto new_end = std::remove_if(
              //     g1->outbox.begin(), g1->outbox.end(),
              //     [eye](const BleWrite &write) {
              //       return (&write.eye == &eye) &&
              //              ((write.value[0] == 0x15) || write.value[0] ==
              //              0x20);
              //     });
              // g1->outbox.erase(new_end, g1->outbox.end());

              // std::deque<BleWrite> new_outbox;

              // for (int seq = message[1]; seq < bmp_chunks.size(); seq++) {
              //   new_outbox.emplace_back(bmp_chunks[seq], *eye);
              //   if (!g1->outbox.empty()) {
              //     new_outbox.emplace_back(std::move(g1->outbox.front()));
              //     g1->outbox.pop_front();
              //   }
              // }
              // while (!g1->outbox.empty()) {
              //   new_outbox.emplace_back(std::move(g1->outbox.front()));
              //   g1->outbox.pop_front();
              // }
              // g1->outbox = std::move(new_outbox);
            } else if (message[0] == 0x20) { // end packet delivered
              // Now send CRC to both eyes
              Debugf("Right eye confirmed end packet!\n");
              g1->Send(crc_packet);
            } else if (message[0] == 0x16) {
              keep_watching = false;
            } else if (clock::now() > deadline) {
              keep_watching = false;
            }
            if (!keep_watching) {
              WatcherStopped();
            }
          });

      // WaitAck(*this, 0x20, [g1 = this](bool timed_out) {
      //   g1->callbacks.OnError("Sending CRC...");

      //   g1->Send(crc_packet);
      //   g1->activity = ACTIVITY_BMP;
      //   // MuteAck(*g1, 0x16);
      // });
    }

    void WatcherStopped() {
      --watchers;
      Debugf("Watcher stopped. Currently %d watchers.\n", watchers);
      if (watchers == 0) {
        delete this;
      }
    }
  };

  auto t = new BMPTransmitter(this);
  t->Start();
}

void G1::Restart() {
  char cmd[] = {0x23, 0x72};
  Send({cmd, sizeof(cmd)});
}

void G1::Exit() {
  char cmd[] = {0x18};
  Send({cmd, sizeof(cmd)});
  MuteAck(*this, 0x18);
}

void G1::WriteAI(string_view text, AIStatus ai_status,
                 std::optional<int> scroll_0_to_100) {
  auto SendContent = [this, ai_status, scroll_0_to_100,
                      text = std::string(text)](bool timed_out) {
    activity = WRITE_AI;
    string cmd = {
        0x4e,
        (char)(text_seq++), // seq
        (char)0x1,          // total_packages
        0x00,               // current_package
        0x30,               // subcommand = 0x30, 0x40 or 0x50
        0x00,               // pos high byte (unused?)
        0x00,               // pos low byte (unused?)
        0x00,               // scroll
        0x01,               // hide scroll on 1
    };
    switch (ai_status) {
    case WRITE:
      cmd[4] = 0x30;
      break;
    case DONE:
      cmd[4] = 0x40;
      break;
    case SHOW:
      cmd[4] = 0x50;
      break;
    }
    if (scroll_0_to_100.has_value()) {
      cmd[7] = scroll_0_to_100.value();
      cmd[8] = 0x64;
    }
    cmd += text;
    Send(cmd);
  };

  if (activity != WRITE_AI && activity != ACTIVITY_TEXT) {
    string cmd_switch_to_ai = {
        0x4e,
        (char)(text_seq++), // seq
        (char)0x1,          // total_packages
        0x00,               // current_package
        0x31,               // subcommand = 0x31 or 0x41
        0x00,               // pos high byte (unused?)
        0x00,               // pos low byte (unused?)
        0x00,               // scroll (unused for initial screen)
        0x01,               // hide scroll (unused for initial screen)
    };
    switch (ai_status) {
    case WRITE:
      cmd_switch_to_ai[4] = 0x30;
      break;
    case DONE:
    case SHOW: // fallthrough intended
      cmd_switch_to_ai[4] = 0x40;
      break;
    }
    cmd_switch_to_ai += text;
    Send(cmd_switch_to_ai);
    WaitAck(*this, 0x4e, SendContent);
  } else {
    SendContent(false);
  }
}

// Verified by manual fiddling around
static constexpr int MAX_CHUNK_SIZE = 240;

void G1::Text(string_view text) {
  target_text = text;
  if (activity == ACTIVITY_TEXT_TRANSMISSION) {
    // existing transmission should pick the new text
    return;
  }
  if (left.connection != Eye::IDLE || right.connection != Eye::IDLE) {
    // target_text will be sent when both eyes reconnect
    return;
  }
  struct TextTransmitter {
    G1 *g1;
    string text;
    uint8_t seq;
    vector<string> commands;
    bool transmission_failed;

    TextTransmitter(G1 *g1) : g1(g1) {
      g1->activity = ACTIVITY_TEXT_TRANSMISSION;
    }

    ~TextTransmitter() {
      if (g1->activity == ACTIVITY_TEXT_TRANSMISSION) {
        g1->activity = ACTIVITY_TEXT;
      }
    }

    void Start() {
      text = g1->target_text;
      seq = g1->text_seq++;
      int total = (text.size() + MAX_CHUNK_SIZE - 1) / MAX_CHUNK_SIZE;
      commands.resize(total);
      for (int current = 0; current < total; ++current) {
        commands[current] = {
            0x4e,
            (char)seq,     // seq
            (char)total,   // total_packages
            (char)current, // current_package
            0x70,          // subcommand = raw text
            0x00,          // pos high byte (unused?)
            0x00,          // pos low byte (unused?)
            0x00,          // scroll (unused for raw text)
            0x01,          // hide scroll (unused for raw text)
        };
        commands[current] +=
            text.substr(current * MAX_CHUNK_SIZE, MAX_CHUNK_SIZE);
      }
      WriteLeft();
    }

    void WriteLeft() {
      transmission_failed = false;
      for (auto &cmd : commands) {
        g1->Send(cmd, &g1->left);
      }

      g1->left.watchers.emplace_back(
          [this, deadline = clock::now() + 2500ms](
              string_view message, bool &keep_watching, bool &keep_processing) {
            if (g1->activity != ACTIVITY_TEXT_TRANSMISSION) {
              // Transmission cancelled
              keep_watching = false;
              Finish();
              return;
            }
            bool for_us =
                message.size() > 5 && message[0] == 0x4e && message[2] == seq;
            bool transmission_over = false;
            if (for_us) {
              // Consume messages which were meant for
              keep_processing = false;

              uint8_t status = message[1];
              uint8_t current = message[4];
              bool last = current == commands.size() - 1;
              if (last) {
                transmission_over = true;
                if (status != 0xc9) {
                  g1->callbacks.OnError(
                      "Bad packet (last) when sending to LEFT eye");
                  transmission_failed = true;
                }
              } else if (status != 0xcb) {
                g1->callbacks.OnError(
                    "Bad packet (middle) when sending to LEFT eye");
                transmission_failed = true;
              }
            } else if (clock::now() > deadline) {
              g1->callbacks.OnError(
                  "Timeout when writing to LEFT eye (seq="s +
                  std::to_string(seq) +
                  ", "
                  "delay=" +
                  std::to_string(MilliS(clock::now() - deadline)) + "ms)");
              transmission_failed = true;
              transmission_over = true;
            } else {
              // Just ignore messages which are not meant for us
            }

            if (transmission_over) {
              if (transmission_failed) {
                if (g1->target_text != text) {
                  // Silver lining is that we can re-transmit the new text
                  // buffer
                  Start();
                } else {
                  WriteLeft();
                }
              } else {
                WriteRight();
              }
              keep_watching = false;
            }
          });
    }

    void WriteRight() {
      transmission_failed = false;
      for (auto &cmd : commands) {
        g1->Send(cmd, &g1->right);
      }

      g1->right.watchers.emplace_back(
          [this, deadline = clock::now() + 2500ms](
              string_view message, bool &keep_watching, bool &keep_processing) {
            if (g1->activity != ACTIVITY_TEXT_TRANSMISSION) {
              // Transmission cancelled
              keep_watching = false;
              Finish();
              return;
            }
            bool for_us =
                message.size() > 5 && message[0] == 0x4e && message[2] == seq;
            bool transmission_over = false;
            if (for_us) {
              // Consume messages which were meant for
              keep_processing = false;

              uint8_t status = message[1];
              uint8_t current = message[4];
              bool last = current == commands.size() - 1;
              if (last) {
                transmission_over = true;
                if (status != 0xc9) {
                  g1->callbacks.OnError(
                      "Bad packet (last) when sending to RIGHT eye");
                  transmission_failed = true;
                }
              } else if (status != 0xcb) {
                g1->callbacks.OnError(
                    "Bad packet (middle) when sending to RIGHT eye");
                transmission_failed = true;
              }
            } else if (clock::now() > deadline) {
              g1->callbacks.OnError("Timeout when writing to RIGHT eye");
              transmission_failed = true;
              transmission_over = true;
            } else {
              // Just ignore messages which are not meant for us
            }

            if (transmission_over) {
              if (transmission_failed) {
                if (g1->target_text != text) {
                  // Silver lining is that we can re-transmit the new text
                  // buffer
                  Start();
                } else {
                  WriteLeft();
                }
              } else {
                Finish();
              }
              keep_watching = false;
            }
          });
    }

    void Finish() {
      if (g1->target_text != text &&
          g1->activity == ACTIVITY_TEXT_TRANSMISSION) {
        Start(); // here we go again
      } else {
        delete this;
      }
    }
  };

  auto t = new TextTransmitter(this);
  t->Start();
}

void G1::Whitelist(bool calendar, bool calls, bool messages, bool ios_mail,
                   string_view id, string_view name) {
  string json = "{";
  json += "\"calendar_enable\":";
  json += calendar ? "true" : "false";
  json += ",\"call_enable\":";
  json += calls ? "true" : "false";
  json += ",\"msg_enable\":";
  json += messages ? "true" : "false";
  json += ",\"ios_mail_enable\":";
  json += ios_mail ? "true" : "false";
  json += ",\"app\":{\"list\":[{\"id\":\"com.augment.os\",\"name\":"
          "\"AugmentOS\"}],\"enable\":true}";
  json += "}";
  // The current message has 139 + len(id + name) bytes so no need to chunk
  // it, but it may be necessary if it's longer.
  string cmd = {
      0x04,
      0x01, // total chunks
      0x00, // current chunk
  };
  cmd += json;
  Send(cmd);
  // MuteAck(*this, 0x4e);
}

} // namespace atmt
