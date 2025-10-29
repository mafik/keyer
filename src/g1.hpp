#pragma once

#include "common.hpp"
#include <cstdint>
#include <deque>
#include <list>

namespace atmt {

struct G1 {

  struct Callbacks {
    // Called during routine communication issues.
    //
    // Generally the library should recover from these but it could be useful to
    // log them for resolving tricky situations.
    virtual void OnError(string_view message) {}

    // Called when the library detects API misuse or an internal error.
    //
    // If you see one of those, then you have a bug in your code that needs to
    // be fixed.
    virtual void OnAssertionFailure(string_view message) { OnError(message); }
    virtual void OnExit(bool right_eye) {}
    virtual void OnHeadUp(bool right_eye) {}
    virtual void OnHeadStraight(bool right_eye) {}
    virtual void OnGlassesOn(bool right_eye) {}
    virtual void OnGlassesOff(bool right_eye) {}
    virtual void OnCradleOpen(bool right_eye) {}
    virtual void OnCradleClosed(bool right_eye) {}
    virtual void OnCradleState(bool right_eye, bool in_cradle) {}
    virtual void OnBatteryLevel(bool right_eye, uint8_t battery_pct) {}
    virtual void OnGlassesCharging(bool right_eye) {}
    virtual void OnCradleBatteryLevel(bool right_eye, uint8_t battery_pct) {
    } // rounded to 10%

    virtual void OnMicrophoneData(string_view data_lc3) {}

    // Called once, after connection is established.
    virtual void OnConnected(bool right_eye) {}

    // Indicates that both eyes have connected and are ready to receive
    // commands.
    virtual void OnReady() {}

    // (TODO: verify) This seems to only be called if auto brightness is on.
    virtual void OnBrightness(bool right_eye, uint8_t brightness_0_to_42) {}

    // This seems to be blocked while text is being displayed.
    virtual void OnLeftPress(bool right_eye) {}
    virtual void OnLeftRelease(bool right_eye) {}

    virtual void OnDashboardOpen(bool right_eye) {}
    virtual void OnDashboardClosed(bool right_eye) {}
  };

  Callbacks &callbacks;

  G1(Callbacks &callbacks);
  ~G1();

  // Note that the display can show up to 5 lines (extra lines will be
  // truncated) and wraps around if a line is long (so a line can take more than
  // one row).
  static constexpr int DISPLAY_WIDTH = 288; // for text display
  static constexpr int DISPLAY_LINES = 5;

  static int MeasureText(std::string_view text);

  //////////////////////////
  // Bluetooth Interface
  //////////////////////////

  enum BleCommand {
    WAIT, // You can stop Poll()-ing now
    SCAN_START,
    SCAN_STOP,
    CONNECT,          // Fills Args::address
    PAIR,             // Fills Args::address
    BOND,             // Fills Args::address
    RESOLVE_SERVICES, // Fills Args::address

    // Writes don't need to be acknowledged
    // Fills Args::{address,service_uuid,characteristic_uuid,value}
    WRITE_CHARACTERISTIC,

    // Fills Args::{address,service_uuid,characteristic_uuid}
    SUBSCRIBE_CHARACTERISTIC_NOTIFY,
  };

  struct Args {
    time_point wait_deadline;
    std::string_view address;
    std::string_view service_uuid;
    std::string_view characteristic_uuid;
    std::string value;
  };

  // Call this function & execute the commands.
  BleCommand Poll(Args &out_args);

  // Call this when scanning has started. This makes G1 think that scanning is
  // active so instead of Command::SCAN_START, it will allow sleeping with
  // Command::WAIT.
  //
  // If you're scanning in a blocking fashion, you can actually skip the
  // OnScanStarted/Stopped and just call OnFoundPeripheral for each found
  // peripheral.
  //
  // Returns empty string on success, or a human-readable error description
  // indicating bad usage of this method.
  void OnScanStarted();

  // Call this when scanning has stopped
  //
  // Returns empty string on success, or a human-readable error description.
  void OnScanStopped();

  // Call this when you find a BLE device
  //
  // Returns empty string on success, or a human-readable error description.
  void OnFoundPeripheral(std::string_view address, std::string_view identifier);

  // Returns empty string on success, or a human-readable error description.
  void OnConnected(std::string_view address);

  // Returns empty string on success, or a human-readable error description.
  void OnPaired(std::string_view address);

  // Returns empty string on success, or a human-readable error description.
  void OnBonded(std::string_view address);

  // Returns empty string on success, or a human-readable error description.
  void OnServicesResolved(std::string_view address);

  // Returns empty string on success, or a human-readable error description.
  void OnDisconnected(std::string_view address);

  // Returns empty string on success, or a human-readable error description.
  void OnSubscribedCharacteristic(std::string_view address,
                                  std::string_view service_uuid,
                                  std::string_view characteristic_uuid);

  // Returns empty string on success, or a human-readable error description.
  void OnCharacteristicNotified(std::string_view address,
                                std::string_view characteristic_uuid,
                                std::string_view value);

  /////////////////////////////////
  // Control intefrace
  /////////////////////////////////

  struct Eye;

  void Send(std::string_view bytes, Eye *eye = nullptr);
  void Brightness(int level_0_to_42, bool auto_adjust);
  void Silent(bool silent);
  void HeadUpAngle(int angle_0_to_60);
  void Microphone(bool on);
  void NoteAdd(int slot_1_to_4, std::string_view title, std::string_view text);
  void NoteDelete(int slot_1_to_4);
  void Dashboard(bool show, char position_0_to_8, char depth_0_to_9);
  void BMP(const uint8_t *bmp_bytes, size_t len);
  void Restart();
  void Exit();

  void Text(std::string_view text);

  enum AIStatus {
    WRITE, // animates each line shown, animated ticker on the left, persists
    DONE,  // animates each line shown, closes after a delay
    SHOW,  // no animation, persists
  };

  // The AI display follows an unspecified protocol and often fails to work.
  void WriteAI(std::string_view text, AIStatus ai_status = WRITE,
               std::optional<int> scroll_0_to_100 = std::nullopt);

  // The implementation is based on some MentraOS code and seems to be broken.
  void Whitelist(bool calendar, bool calls, bool messages, bool mail,
                 std::string_view id, std::string_view name);

  // TODO: Battery query (0x2C)
  // TODO: Notifications (0x4B)
  // TODO: Wear detection (0x27)

  /////////////////////////////////////////
  // Private stuff, exposed for hackers
  /////////////////////////////////////////

  bool scanning = false;
  bool ready = false;
  uint8_t heartbeat_seq = 0;
  uint8_t dashboard_counter = 0;
  uint8_t text_seq = 0;
  enum Activity {
    NOTHING,
    DASHBOARD,
    WRITE_AI,
    ACTIVITY_TEXT,
    ACTIVITY_BMP,
    ACTIVITY_TEXT_TRANSMISSION,
    ACTIVITY_BMP_TRANSMISSION,
  } activity = NOTHING;
  string target_text;
  string target_bmp;

  struct BrightnessConfig {
    int value_0_to_42;
    bool auto_adjust;
  };
  std::optional<BrightnessConfig> target_brightness;

  time_point next_heartbeat = time_point::min();

  struct Eye {
    string address = "";
    enum ConnectionState : uint8_t {
      NOT_CONNECTED,
      CONNECTING,
      CONNECTED,
      SERVICES_NOT_RESOLVED = CONNECTED,
      SERVICES_RESOLVING,
      SERVICES_RESOLVED,
      UART_NOT_SUBSCRIBED = SERVICES_RESOLVED,
      UART_SUBSCRIBING,
      UART_SUBSCRIBED,
      IDLE = UART_SUBSCRIBED,
    } connection = NOT_CONNECTED;
    enum Pairing : uint8_t {
      NOT_PAIRED,
      PAIRING,
      PAIRED,
      NOT_BONDED = PAIRED,
      BONDING,
      BONDED
    } pairing = NOT_PAIRED;
    time_point last_activity = time_point::min();

    using Watcher = function<void(std::string_view message, bool &keep_watching,
                                  bool &keep_processing)>;
    std::list<Watcher> watchers;
  };

  struct BleWrite {
    BleWrite(string value, Eye &eye) : value(value), eye(&eye) {}
    string value;
    Eye *eye;
  };

  std::deque<BleWrite> outbox;

  Eye left, right;
};

} // namespace atmt
