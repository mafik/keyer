#pragma once

#include "app.hpp"
#include "esp32_bluetooth.hpp"
#include "g1.hpp"

#include <optional>

namespace atmt {

struct AppTerminal : App {

  struct G1_Callbacks : G1::Callbacks {
    G1 *g1 = nullptr;

    void OnError(string_view message) override;
    void OnAssertionFailure(string_view message) override;
    void OnExit(bool right_eye) override;
    void OnHeadUp(bool right_eye) override;
    void OnHeadStraight(bool right_eye) override;
    void OnGlassesOn(bool right_eye) override;
    void OnGlassesOff(bool right_eye) override;
    void OnCradleOpen(bool right_eye) override;
    void OnCradleClosed(bool right_eye) override;
    void OnCradleState(bool right_eye, bool in_cradle) override;
    void OnBatteryLevel(bool right_eye, uint8_t battery_pct) override;
    void OnGlassesCharging(bool right_eye) override;
    void OnCradleBatteryLevel(bool right_eye, uint8_t battery_pct) override;
    void OnMicrophoneData(string_view data_lc3) override;
    void OnConnected(bool right_eye) override;
    void OnReady() override;
    void OnBrightness(bool right_eye, uint8_t brightness_0_to_42) override;
    void OnLeftPress(bool right_eye) override;
    void OnLeftRelease(bool right_eye) override;
    void OnDashboardOpen(bool right_eye) override;
    void OnDashboardClosed(bool right_eye) override;
  };

  struct BluetoothCallbacks : Bluetooth::Callbacks {
    G1 *g1 = nullptr;

    void OnDeviceFound(string_view address, string_view identifier) override;
    void OnConnected(string_view address) override;
    void OnDisconnected(string_view address) override;
    void OnPaired(string_view address) override;
    void OnBonded(string_view address) override;
    void OnServicesResolved(string_view address) override;
    void OnCharacteristicNotified(string_view address,
                                  string_view characteristic_uuid,
                                  string_view value) override;
    void OnSubscribed(string_view address, string_view service_uuid,
                      string_view characteristic_uuid) override;
  };

  G1_Callbacks g1_callbacks_;
  G1 g1_{g1_callbacks_};
  BluetoothCallbacks bt_callbacks_;
  std::optional<ESP32Bluetooth> bluetooth_;

  AppTerminal();
  void OnSetup() override;
  void OnLoop() override;
  void OnUnicode(uint32_t codepoint, Modifier mods) override;
  void OnKey(IBM_Key key, Modifier mods) override;
  void ShowText(string_view text) override;
};

} // namespace atmt
