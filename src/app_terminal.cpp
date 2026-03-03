#include "app_terminal.hpp"

#include "common_esp32.hpp"
#include "eye_term.hpp"
#include "img.hpp"
#include "keyer.hpp"
#include "main_loop.hpp"
#include "secrets.hpp"
#include "ssh.hpp"

#include <WiFi.h>

namespace atmt {

#define DEBUG_F(line, ...)                                                     \
  if constexpr (kDebug) {                                                      \
    RunOnMain([=]() { Debugf(line, ##__VA_ARGS__); });                         \
  }

// G1_Callbacks

void AppTerminal::G1_Callbacks::OnError(string_view message) {
  auto msg_copy = string(message);
  DEBUG_F("OnError(%s)\n", msg_copy.c_str());
}
void AppTerminal::G1_Callbacks::OnAssertionFailure(string_view message) {
  auto msg_copy = string(message);
  DEBUG_F("OnAssertionFailure(%s)\n", msg_copy.c_str());
}
void AppTerminal::G1_Callbacks::OnExit(bool right_eye) {
  DEBUG_F("OnExit(%c)\n", right_eye ? 'R' : 'L');
}
void AppTerminal::G1_Callbacks::OnHeadUp(bool right_eye) {
  DEBUG_F("OnHeadUp(%c)\n", right_eye ? 'R' : 'L');
}
void AppTerminal::G1_Callbacks::OnHeadStraight(bool right_eye) {
  DEBUG_F("OnHeadStraight(%c)\n", right_eye ? 'R' : 'L');
}
void AppTerminal::G1_Callbacks::OnGlassesOn(bool right_eye) {
  DEBUG_F("OnGlassesOn(%c)\n", right_eye ? 'R' : 'L');
}
void AppTerminal::G1_Callbacks::OnGlassesOff(bool right_eye) {
  DEBUG_F("OnGlassesOff(%c)\n", right_eye ? 'R' : 'L');
}
void AppTerminal::G1_Callbacks::OnCradleOpen(bool right_eye) {
  DEBUG_F("OnCradleOpen(%c)\n", right_eye ? 'R' : 'L');
}
void AppTerminal::G1_Callbacks::OnCradleClosed(bool right_eye) {
  DEBUG_F("OnCradleClosed(%c)\n", right_eye ? 'R' : 'L');
}
void AppTerminal::G1_Callbacks::OnCradleState(bool right_eye, bool in_cradle) {
  DEBUG_F("OnCradleState(%c)\n", right_eye ? 'R' : 'L');
}
void AppTerminal::G1_Callbacks::OnBatteryLevel(bool right_eye,
                                               uint8_t battery_pct) {
  DEBUG_F("OnBatteryLevel(%c, %d)\n", right_eye ? 'R' : 'L', battery_pct);
}
void AppTerminal::G1_Callbacks::OnGlassesCharging(bool right_eye) {
  DEBUG_F("OnGlassesCharging(%c)\n", right_eye ? 'R' : 'L');
}
void AppTerminal::G1_Callbacks::OnCradleBatteryLevel(bool right_eye,
                                                     uint8_t battery_pct) {
  DEBUG_F("OnCradleBatteryLevel(%c, %d)\n", right_eye ? 'R' : 'L',
          battery_pct);
}
void AppTerminal::G1_Callbacks::OnMicrophoneData(string_view data_lc3) {
  DEBUG_F("OnMicrophoneData(%d)\n", data_lc3.size());
  g1->Microphone(false);
}
void AppTerminal::G1_Callbacks::OnConnected(bool right_eye) {
  DEBUG_F("OnConnected(%c)\n", right_eye ? 'R' : 'L');
}
void AppTerminal::G1_Callbacks::OnReady() { DEBUG_F("OnReady()\n"); }
void AppTerminal::G1_Callbacks::OnBrightness(bool right_eye,
                                             uint8_t brightness_0_to_42) {
  DEBUG_F("OnBrightness(%c)\n", right_eye ? 'R' : 'L');
}
void AppTerminal::G1_Callbacks::OnLeftPress(bool right_eye) {
  DEBUG_F("OnLeftPress(%c)\n", right_eye ? 'R' : 'L');
}
void AppTerminal::G1_Callbacks::OnLeftRelease(bool right_eye) {
  DEBUG_F("OnLeftRelease(%c)\n", right_eye ? 'R' : 'L');
}
void AppTerminal::G1_Callbacks::OnDashboardOpen(bool right_eye) {
  DEBUG_F("OnDashboardOpen(%c)\n", right_eye ? 'R' : 'L');
}
void AppTerminal::G1_Callbacks::OnDashboardClosed(bool right_eye) {
  DEBUG_F("OnDashboardClosed(%c)\n", right_eye ? 'R' : 'L');
}

// BluetoothCallbacks

void AppTerminal::BluetoothCallbacks::OnDeviceFound(
    string_view address, string_view identifier) {
  atmt::RunOnMain([g1 = this->g1, address = string(address),
                   identifier = string(identifier)]() {
    Debugf("Found device: %s (%s)\n", address.c_str(), identifier.c_str());
    g1->OnFoundPeripheral(address, identifier);
  });
}
void AppTerminal::BluetoothCallbacks::OnConnected(string_view address) {
  atmt::RunOnMain([g1 = this->g1, address = string(address)]() {
    Debugf("Connected: %s\n", address.c_str());
    g1->OnConnected(address);
  });
}
void AppTerminal::BluetoothCallbacks::OnDisconnected(string_view address) {
  atmt::RunOnMain([g1 = this->g1, address = string(address)]() {
    Debugf("Disconnected: %s\n", address.c_str());
    g1->OnDisconnected(address);
  });
}
void AppTerminal::BluetoothCallbacks::OnPaired(string_view address) {
  atmt::RunOnMain([g1 = this->g1, address = string(address)]() {
    Debugf("Paired: %s\n", address.c_str());
    g1->OnPaired(address);
  });
}
void AppTerminal::BluetoothCallbacks::OnBonded(string_view address) {
  atmt::RunOnMain([g1 = this->g1, address = string(address)]() {
    Debugf("Bonded: %s\n", address.c_str());
    g1->OnBonded(address);
  });
}
void AppTerminal::BluetoothCallbacks::OnServicesResolved(string_view address) {
  atmt::RunOnMain([g1 = this->g1, address = string(address)]() {
    Debugf("Services resolved: %s\n", address.c_str());
    g1->OnServicesResolved(address);
  });
}
void AppTerminal::BluetoothCallbacks::OnCharacteristicNotified(
    string_view address, string_view characteristic_uuid, string_view value) {
  atmt::RunOnMain([g1 = this->g1, address = string(address),
                   characteristic_uuid = string(characteristic_uuid),
                   value = string(value)]() {
    g1->OnCharacteristicNotified(address, characteristic_uuid, value);
  });
}
void AppTerminal::BluetoothCallbacks::OnSubscribed(
    string_view address, string_view service_uuid,
    string_view characteristic_uuid) {
  atmt::RunOnMain([g1 = this->g1, address = string(address),
                   service_uuid = string(service_uuid),
                   characteristic_uuid = string(characteristic_uuid)]() {
    Debugf("Service subscribed: %s\n", address.c_str());
    g1->OnSubscribedCharacteristic(address, service_uuid, characteristic_uuid);
  });
}

#undef DEBUG_F

// AppTerminal

AppTerminal::AppTerminal() {
  g1_callbacks_.g1 = &g1_;
  bt_callbacks_.g1 = &g1_;
}

void AppTerminal::OnSetup() {
  bluetooth_.emplace(bt_callbacks_);
  g1_.Brightness(42, true);

  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      auto &i = info.wifi_sta_disconnected;
      if (i.reason == WIFI_REASON_ASSOC_FAIL) {
        Log("WiFi Association failed. Retrying...\n");
        WiFi.setAutoReconnect(true);
        WiFi.reconnect();
      } else if (i.reason == WIFI_REASON_NO_AP_FOUND) {
        Log("WiFi Access Point not found: SSID=%*s pass=%s\n", i.ssid_len,
            i.ssid, WIFI_PASSWORD);
      } else {
        Log("WiFi lost %s, SSID=%*s, RSSI=%d\n",
            WiFi.disconnectReasonName((wifi_err_reason_t)i.reason), i.ssid_len,
            i.ssid, i.rssi);
      }
      WiFi.disconnect();
      WiFi.begin();
    } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      RunOnMain([](uint64_t) { OnNetworkConnectedSSH(); }, 0);
    } else {
      RunOnMain(
          [event]() { Debugf("WiFi event: %s\n", WiFi.eventName(event)); });
    }
  });
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  RegisterChord(0, 0, 1, 2, 0, [this]() { g1_.BMP(img, sizeof(img)); });
  RegisterChord(0, 1, 1, 2, 0, [this]() { g1_.Exit(); });
  RegisterChord(3, 2, 2, 2, 0, [this]() { g1_.Restart(); });
}

void AppTerminal::OnLoop() {
  G1::Args args;
  auto cmd = g1_.Poll(args);

  switch (cmd) {
  case G1::BleCommand::WAIT: {
    atmt::MainLoopBlocking(
        args.wait_deadline == atmt::time_point::max()
            ? atmt::optional<atmt::time_point>(atmt::nullopt)
            : atmt::optional<atmt::time_point>(args.wait_deadline));
    break;
  }

  case G1::BleCommand::SCAN_START:
    Debugln("SCAN_START");
    bluetooth_->StartScan();
    g1_.OnScanStarted();
    break;

  case G1::BleCommand::SCAN_STOP:
    Debugln("SCAN_STOP");
    bluetooth_->StopScan();
    g1_.OnScanStopped();
    break;

  case G1::BleCommand::CONNECT:
    Debugf("CONNECT %.*s\n", (int)args.address.size(), args.address.data());
    bluetooth_->Connect(args.address);
    break;

  case G1::BleCommand::PAIR:
    Debugf("PAIR %.*s\n", (int)args.address.size(), args.address.data());
    bluetooth_->Pair(args.address);
    break;

  case G1::BleCommand::BOND:
    Debugf("BOND %.*s\n", (int)args.address.size(), args.address.data());
    bluetooth_->Bond(args.address);
    break;

  case G1::BleCommand::RESOLVE_SERVICES:
    Debugf("RESOLVE_SERVICES %.*s\n", (int)args.address.size(),
           args.address.data());
    bluetooth_->ResolveServices(args.address);
    break;

  case G1::BleCommand::SUBSCRIBE_CHARACTERISTIC_NOTIFY:
    Debugf("SUBSCRIBE_CHARACTERISTIC_NOTIFY addr=%.*s\n",
           (int)args.address.size(), args.address.data());
    bluetooth_->SubscribeCharacteristic(args.address, args.service_uuid,
                                        args.characteristic_uuid);
    break;

  case G1::BleCommand::WRITE_CHARACTERISTIC:
    if (!bluetooth_->WriteCharacteristic(args.address, args.service_uuid,
                                         args.characteristic_uuid,
                                         args.value)) {
      Debugf("Failed to ESP32Bluetooth::WriteCharacteristic!\n");
    }
    break;

  default:
    break;
  }

  vTaskDelay(pdMS_TO_TICKS(1));
}

void AppTerminal::OnUnicode(uint32_t codepoint, Modifier mods) {
  if (ssh_connected && ssh_chan) {
    auto seq = TerminalSequenceFromUnicode(codepoint, mods);
    if (seq.size()) {
      ssh_channel_write(ssh_chan, seq.data(), seq.size());
    }
  }
}

void AppTerminal::OnKey(IBM_Key key, Modifier mods) {
  if (ssh_connected && ssh_chan) {
    auto seq = TerminalSequenceFromIBM_Key(key, mods);
    if (seq.size()) {
      ssh_channel_write(ssh_chan, seq.data(), seq.size());
    }
  }
}

void AppTerminal::ShowText(string_view text) { g1_.Text(text); }

} // namespace atmt
