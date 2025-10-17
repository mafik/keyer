#include "BleKeyboard.h"
#include "esp_gap_ble_api.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/portmacro.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLESecurity.h>

#include <cstdint>
#include <vector>

// Change to true to enable serial debug output
//
// The default is false because when device is not connected to a computer but
// is printing to the serial port, it causes the device to become laggy (weird).
constexpr bool kDebug = false;

// Set this to something like 350 to enable chord autostart when a chord is held
// down for this duration. Chords started this way cause the keys to be pressed
// and they will be released only when the chord is also released. This allows
// chords to function more like keyboard keys.
//
// This is disabled by default because it makes learning very hard. Re-enable
// this once your WPM is above 20.
constexpr unsigned long kChordAutostartMillis = 350 * 1000 * 1000;

// The two arpeggio keys must be spread apart by at least this many
// milliseconds.
constexpr unsigned long kArpeggioMinSpacingMillis = 80;

// Arpeggios must be released quickly after the last button is pressed. This
// constant conrols how long the last button can be held down for an action to
// be registered as an arpeggio.
constexpr unsigned long kArpeggioMaxHoldMillis = 240;

// Character sent by the keyboard to the computer
using IBM_Key = uint8_t;

using GPIO_Pin = uint8_t;

// A mechanical switch numbered 0-9
using Button = uint8_t;

// 0 = not pressing, 1 = pressing first button, 2 = pressing second button, etc.
using FingerPosition = uint8_t;

BleKeyboard ble_keyboard("temp", "𝖒𝖆𝖋", 100);

const GPIO_Pin BATTERY_PIN = 3;

enum ButtonEnum {
  THUMB_0,
  THUMB_1,
  THUMB_2,
  INDEX_3,
  MIDDLE_4,
  RING_5,
  LITTLE_6,
  INDEX_7,
  MIDDLE_8,
  RING_9,
  NUM_BUTTONS
};

const char *ButtonToStr(int btn) {
  switch (btn) {
  case THUMB_0:
    return "THUMB_0";
  case THUMB_1:
    return "THUMB_1";
  case THUMB_2:
    return "THUMB_2";
  case INDEX_3:
    return "INDEX_3";
  case MIDDLE_4:
    return "MIDDLE_4";
  case RING_5:
    return "RING_5";
  case LITTLE_6:
    return "LITTLE_6";
  case INDEX_7:
    return "INDEX_7";
  case MIDDLE_8:
    return "MIDDLE_8";
  case RING_9:
    return "RING_9";
  default:
    return "UNKNOWN";
  }
}

constexpr GPIO_Pin kButtonPin[NUM_BUTTONS] = {
    [THUMB_0] = 2,   [THUMB_1] = 5, [THUMB_2] = 0,   [INDEX_3] = 46,
    [MIDDLE_4] = 13, [RING_5] = 35, [LITTLE_6] = 37, [INDEX_7] = 38,
    [MIDDLE_8] = 8,  [RING_9] = 42,
};

struct Action {
  virtual void OnStart() = 0;
  virtual void OnStop() = 0;
  Action(Action *next = nullptr) : next(next) {}
  void Execute() {
    Start();
    Stop();
  }
  void Start() {
    OnStart();
    if (next) {
      next->Start();
    }
  }
  void Stop() {
    if (next) {
      next->Stop();
    }
    OnStop();
  }
  Action *next;
};

struct Layer {
  Action *chords[4][3][3][3][2];
};

Layer base_layer = {};
Layer *current_layer = &base_layer;

#define CHORDS current_layer->chords

Action *arpeggios[NUM_BUTTONS][NUM_BUTTONS];

bool buttons_down[NUM_BUTTONS];
Action *active_button_actions[NUM_BUTTONS];
Action *chord_action;
esp_timer_handle_t chord_autostart_timer;

enum ArpeggioState {
  STATE_READY,
  STATE_BUTTON1_DOWN,
  STATE_BUTTON2_DOWN,
  STATE_INACTIVE,
} arpeggio_state = STATE_READY;
unsigned long arpeggio_start_millis = 0;
Button arpeggio_button1 = 0;
Button arpeggio_button2 = 0;

FingerPosition Thumb() {
  if (buttons_down[THUMB_0])
    return 1;
  if (buttons_down[THUMB_1])
    return 2;
  if (buttons_down[THUMB_2])
    return 3;
  return 0;
}

FingerPosition Index() {
  if (buttons_down[INDEX_3])
    return 1;
  if (buttons_down[INDEX_7])
    return 2;
  return 0;
}

FingerPosition Middle() {
  if (buttons_down[MIDDLE_4])
    return 1;
  if (buttons_down[MIDDLE_8])
    return 2;
  return 0;
}

FingerPosition Ring() {
  if (buttons_down[RING_5])
    return 1;
  if (buttons_down[RING_9])
    return 2;
  return 0;
}

FingerPosition Little() {
  if (buttons_down[LITTLE_6])
    return 1;
  return 0;
}

// The returned string is going to be invalidated after the next call to
// ByteToStr()
const char *IBM_KeyToStr(IBM_Key key) {
  switch (key) {
  case KEY_LEFT_CTRL:
    return "CtrlL";
  case KEY_RIGHT_CTRL:
    return "CtrlR";
  case KEY_LEFT_SHIFT:
    return "ShiftL";
  case KEY_RIGHT_SHIFT:
    return "ShiftR";
  case KEY_LEFT_ALT:
    return "AltL";
  case KEY_RIGHT_ALT:
    return "AltR";
  case KEY_LEFT_GUI:
    return "GuiL";
  case KEY_RIGHT_GUI:
    return "GuiR";
  case KEY_ESC:
    return "Esc";
  case KEY_RETURN:
    return "Enter";
  case ' ':
    return "Space";
  case KEY_TAB:
    return "Tab";
  case KEY_BACKSPACE:
    return "Backspace";
  case KEY_DELETE:
    return "Delete";
  default:
    break;
  }
  static char buf[10];
  if (isprint(key)) {
    snprintf(buf, sizeof(buf), "%c", key);
  } else {
    snprintf(buf, sizeof(buf), "0x%02x", key);
  }
  return buf;
}

std::vector<IBM_Key> temp_modifiers;

template <typename... Args> void DebugPrintf(Args... args) {
  if constexpr (kDebug) {
    Serial.printf(args...);
  }
}

void ReleaseTempModifiers() {
  for (auto mod : temp_modifiers) {
    DebugPrintf("  Releasing modifier: %s (ReleaseTempModifiers)\n",
                IBM_KeyToStr(mod));
    ble_keyboard.release(mod);
  }
  temp_modifiers.clear();
}

struct WriteKeyAction : Action {
  IBM_Key key;
  WriteKeyAction(IBM_Key key, Action *next = nullptr)
      : Action(next), key(key) {}
  void OnStart() override {
    DebugPrintf("  Pressing key: %s (WriteKeyAction)\n", IBM_KeyToStr(key));
    ble_keyboard.press(key);
  }
  void OnStop() override {
    DebugPrintf("  Releasing key: %s (WriteKeyAction)\n", IBM_KeyToStr(key));
    ble_keyboard.release(key);
    ReleaseTempModifiers();
  }
};
// A modifier that affects the next key press.
// It's released along with the next key.
struct TemporaryModifierAction : Action {
  IBM_Key modifier;
  TemporaryModifierAction(IBM_Key modifier, Action *next = nullptr)
      : Action(next), modifier(modifier) {}
  void OnStart() override {
    auto existing_modifier_it = temp_modifiers.end();
    for (auto it = temp_modifiers.begin(); it != temp_modifiers.end(); ++it) {
      if (*it == modifier) {
        existing_modifier_it = it;
        break;
      }
    }
    if (existing_modifier_it != temp_modifiers.end()) {
      DebugPrintf("  Releasing modifier [%s] (TemporaryModifierAction)\n",
                  IBM_KeyToStr(modifier));
      ble_keyboard.release(modifier);
      temp_modifiers.erase(existing_modifier_it);
    } else {
      DebugPrintf("  Pressing modifier [%s] (TemporaryModifierAction)\n",
                  IBM_KeyToStr(modifier));
      ble_keyboard.press(modifier);
      temp_modifiers.push_back(modifier);
    }
  }
  void OnStop() override {}
};
struct HoldModifierAction : Action {
  Button hold_button;
  IBM_Key modifier;
  struct ReleaseHeldModifierAction : Action {
    HoldModifierAction &hold_action;
    ReleaseHeldModifierAction(HoldModifierAction &hold_action)
        : Action(nullptr), hold_action(hold_action) {}
    void OnStart() override {}
    void OnStop() override {
      DebugPrintf("  Releasing modifier [%s] (ReleaseHeldModifierAction)\n",
                  IBM_KeyToStr(hold_action.modifier));
      ble_keyboard.release(hold_action.modifier);
    }
  };
  ReleaseHeldModifierAction release_action;
  HoldModifierAction(Button held_key, IBM_Key modifier, Action *next = nullptr)
      : Action(next), hold_button(held_key), modifier(modifier),
        release_action(*this) {}
  void OnStart() override {
    if (active_button_actions[hold_button]) {
      DebugPrintf("  Keeping modifier [%s] (HoldModifierAction)\n",
                  IBM_KeyToStr(modifier));
      return;
    }
    DebugPrintf("  Pressing modifier [%s] (HoldModifierAction)\n",
                IBM_KeyToStr(modifier));
    ble_keyboard.press(modifier);
    active_button_actions[hold_button] = &release_action;
  }
  void OnStop() override {}
};

// Shortcuts for faster layout definition
static Action *Key(IBM_Key key, Action *next = nullptr) {
  return new WriteKeyAction(key, next);
}
static Action *Mod(IBM_Key modifier, Action *next = nullptr) {
  return new TemporaryModifierAction(modifier, next);
}
static Action *Hold(Button hold_button, IBM_Key modifier,
                    Action *next = nullptr) {
  return new HoldModifierAction(hold_button, modifier, next);
}

Action *FindUniqueAction() {
  Action *first_found = nullptr;
  FingerPosition thumb_current = Thumb(), index_current = Index(),
                 middle_current = Middle(), ring_current = Ring(),
                 little_current = Little();
  for (FingerPosition thumb = 0; thumb <= 3; ++thumb) {
    if (thumb_current && thumb_current != thumb)
      continue;
    for (FingerPosition index = 0; index <= 2; ++index) {
      if (index_current && index_current != index)
        continue;
      for (FingerPosition middle = 0; middle <= 2; ++middle) {
        if (middle_current && middle_current != middle)
          continue;
        for (FingerPosition ring = 0; ring <= 2; ++ring) {
          if (ring_current && ring_current != ring)
            continue;
          for (FingerPosition little = 0; little <= 1; ++little) {
            if (little_current && little_current != little)
              continue;
            if (Action *found = CHORDS[thumb][index][middle][ring][little]) {
              if (first_found) {
                return nullptr;
              } else {
                first_found = found;
              }
            }
          }
        }
      }
    }
  }
  return first_found;
}

void OnButtonDown(Button i) {
  auto now = millis();
  if (arpeggio_state == STATE_READY) {
    arpeggio_start_millis = now;
    arpeggio_button1 = i;
    arpeggio_state = STATE_BUTTON1_DOWN;
  } else if (arpeggio_state == STATE_BUTTON1_DOWN) {
    DebugPrintf("Arpeggio key 1 down millis: %lu\n",
                now - arpeggio_start_millis);
    if (now - arpeggio_start_millis >= kArpeggioMinSpacingMillis) {
      arpeggio_button2 = i;
      arpeggio_start_millis = now;
      arpeggio_state = STATE_BUTTON2_DOWN;
    } else {
      arpeggio_state = STATE_INACTIVE;
    }
  } else {
    arpeggio_state = STATE_INACTIVE;
  }

  buttons_down[i] = true;
  auto unique_action = FindUniqueAction();
  if (unique_action) {
    // If a unique key action was found, then don't add it to the chord but
    // rather start it immediately This allows multiple actions to be active at
    // the same time (as long as they have been unique at press time)
    buttons_down[i] = false;
    // We also don't want to start a new chord
    if (esp_timer_is_active(chord_autostart_timer)) {
      esp_timer_stop(chord_autostart_timer);
    }
    DebugPrintf(" Unique action!\n");
    active_button_actions[i] = unique_action;
    unique_action->Start();
  } else {
    if (esp_timer_is_active(chord_autostart_timer)) {
      esp_timer_stop(chord_autostart_timer);
    }
    esp_timer_start_once(chord_autostart_timer, kChordAutostartMillis * 1000);
  }
}

void OnButtonUp(Button i) {
  auto now = millis();
  if (arpeggio_state == STATE_BUTTON2_DOWN) {
    DebugPrintf("Arpeggio button 2 down millis: %lu\n",
                now - arpeggio_start_millis);
    if (now - arpeggio_start_millis <= kArpeggioMaxHoldMillis) {
      auto action = arpeggios[arpeggio_button1][arpeggio_button2];
      if (action) {
        DebugPrintf("Arpeggio action\n");
        action->Execute();
        if (esp_timer_is_active(chord_autostart_timer)) {
          esp_timer_stop(chord_autostart_timer);
        }
      }
    }
    arpeggio_state = STATE_INACTIVE;
  }

  if (auto &active_button_action = active_button_actions[i]) {
    DebugPrintf("Stopping active button action\n");
    active_button_action->Stop();
    active_button_action = nullptr;
  } else if (chord_action && buttons_down[i]) {
    DebugPrintf("Stopping chord action\n");
    chord_action->Stop();
    chord_action = nullptr;
  } else if (esp_timer_is_active(chord_autostart_timer)) {
    esp_timer_stop(chord_autostart_timer);
    auto action = CHORDS[Thumb()][Index()][Middle()][Ring()][Little()];
    if (action) {
      DebugPrintf("Chord action\n");
      action->Execute();

      // It's possible that chord action attaches an "active key" action to the
      // currently released key. If that's the case then it should be
      // immediately stopped.
      if (auto &active_button_action = active_button_actions[i]) {
        DebugPrintf("Stopping active button action\n");
        active_button_action->Stop();
        active_button_action = nullptr;
      }
    } else {
      DebugPrintf("No chord action\n");
    }
  }

  buttons_down[i] = false;

  bool any_button_down = false;
  for (int i = 0; i < NUM_BUTTONS; i++) {
    any_button_down |= buttons_down[i];
  }
  bool all_buttons_up = !any_button_down;
  if (all_buttons_up) {
    arpeggio_state = STATE_READY;
  }
}

void OnChordAutostart() {
  if (chord_action) {
    DebugPrintf("ERROR: Chord action already active\n");
    return;
  }
  auto action = CHORDS[Thumb()][Index()][Middle()][Ring()][Little()];
  if (action) {
    DebugPrintf("Starting chord hold\n");
    action->Start();
    chord_action = action;
  }
}

// See
// https://academy.nordicsemi.com/courses/bluetooth-low-energy-fundamentals/lessons/lesson-3-bluetooth-le-connections/topic/connection-parameters/
void SetupConnectionParameters(esp_bd_addr_t addr) {
  esp_ble_conn_update_params_t conn_params = {
      .bda = {addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]},
      .min_int = 30 * 4 / 5, // 30ms in 1.25ms units
      .max_int = 50 * 4 / 5, // 50ms in 1.25ms units
      .latency = 0,
      .timeout = 6000 / 10, // 6s in 10ms units
  };
  esp_err_t ret = esp_ble_gap_update_conn_params(&conn_params);
  if (ret != ESP_OK) {
    DebugPrintf("DEBUG: Failed to update connection parameters: %d\n", ret);
  } else {
    DebugPrintf("DEBUG: connection parameters changed successfully\n");
  }
}

struct BleKeyboardSecurityCallbacks : public BLESecurityCallbacks {
  bool pass_key_collecting = false;
  String pass_key_buffer = "";
  const int PASS_KEY_LENGTH = 6;

  uint32_t onPassKeyRequest() override {
    DebugPrintf(
        "DEBUG: onPassKeyRequest called - collecting PIN from keyboard\n");
    DebugPrintf("DEBUG: Please type 6 digits on the keyboard\n");

    pass_key_collecting = true;
    pass_key_buffer = "";

    // Wait for 6 digits to be entered
    unsigned long startTime = millis();
    const unsigned long timeout = 30000; // 30 second timeout

    while (pass_key_buffer.length() < PASS_KEY_LENGTH &&
           (millis() - startTime) < timeout) {
      delay(10); // Small delay to prevent busy waiting
      // pass key collection happens in main loop
    }

    pass_key_collecting = false;

    if (pass_key_buffer.length() == PASS_KEY_LENGTH) {
      uint32_t bt_pass_key = pass_key_buffer.toInt();
      DebugPrintf("DEBUG: Collected PIN: %06d\n", bt_pass_key);
      return bt_pass_key;
    } else {
      DebugPrintf("DEBUG: PIN collection timeout - using default\n");
      return 123456;
    }
  }

  void onPassKeyNotify(uint32_t pass_key) override {
    DebugPrintf("DEBUG: onPassKeyNotify - PIN displayed: %06d\n", pass_key);
  }

  bool onConfirmPIN(uint32_t pass_key) override {
    DebugPrintf("DEBUG: onConfirmPIN - PIN to confirm: %06d\n", pass_key);
    return true;
  }

  bool onSecurityRequest() override {
    // New device is connecting.
    DebugPrintf("DEBUG: onSecurityRequest called\n");
    return true;
  }

  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override {
    DebugPrintf("DEBUG: onAuthenticationComplete called\n");
    if (cmpl.success) {
      DebugPrintf("DEBUG: Pairing successful!\n");
      SetupConnectionParameters(cmpl.bd_addr);
    } else {
      DebugPrintf("DEBUG: Pairing failed, reason: %d\n", cmpl.fail_reason);
    }
  }
} ble_kb_security;

QueueHandle_t button_changes;

struct ButtonChange {
  uint8_t button : 4;
  int64_t time : 52; // esp_timer_get_time returns up to 52 bits
} __attribute__((packed));

#define BUTTON_ISR(button)                                                     \
  void IRAM_ATTR button_isr_##button() {                                       \
    auto event = ButtonChange{button, esp_timer_get_time()};                   \
    xQueueSendToBackFromISR(button_changes, &event, nullptr);                  \
  }

BUTTON_ISR(0)
BUTTON_ISR(1)
BUTTON_ISR(2)
BUTTON_ISR(3)
BUTTON_ISR(4)
BUTTON_ISR(5)
BUTTON_ISR(6)
BUTTON_ISR(7)
BUTTON_ISR(8)
BUTTON_ISR(9)

void ReadBattery(void *) {
  int rawValue = analogRead(BATTERY_PIN);
  float voltage = (rawValue * 4.187) / 2441.0; // measured with a multimeter
  int batteryPercent =
      map(constrain(voltage * 1000, 3000, 4185), 3000, 4185, 0, 100);

  ble_keyboard.setBatteryLevel(batteryPercent);

  // DebugPrintf("CPU Frequency: ");
  // DebugPrintf(getCpuFrequencyMhz());
  // DebugPrintf(" MHz\n");

  // DebugPrintf("PM Locks:\n");
  // esp_pm_dump_locks(stdout);
}

// Zero-latency button debouncer.
//
// Initial state change is immediately registered as button press or release.
// Subsequent state changes are ignored for a short time window (a couple of
// milliseconds). After a period of no activity, the GPIO state is read directly
// to verify the current button state.
//
// The approach used by this debouncer results in zero latency but a minimal
// press duration equal to the debounce window.
struct ButtonDebouncer {
  Button i;
  bool pressed_state;
  esp_timer_handle_t timer;
  int64_t last_change;

  // Experimentally, the shortest physically possible key press was a tad over
  // 15ms
  constexpr static int64_t kDebounceMicroseconds = 15 * 1000;

  bool ReadPressedGpio() { return digitalRead(kButtonPin[i]) == LOW; }

  // Called at setup time
  void OnSetup(Button button) {
    i = button;
    pinMode(kButtonPin[i], INPUT_PULLUP);
    last_change = esp_timer_get_time();
    pressed_state = ReadPressedGpio();

    auto args =
        esp_timer_create_args_t{.callback =
                                    [](void *arg) {
                                      ButtonDebouncer *debouncer =
                                          static_cast<ButtonDebouncer *>(arg);
                                      debouncer->OnTimer();
                                    },
                                .arg = this,
                                .dispatch_method = ESP_TIMER_TASK,
                                .name = ButtonToStr(i),
                                .skip_unhandled_events = false};
    auto timer_result = esp_timer_create(&args, &timer);
    if (timer_result != ESP_OK) {
      DebugPrintf("Failed to create timer for button %s\n", ButtonToStr(i));
    }
  }

  void OnChange(int64_t time) {
    auto delta = time - last_change;
    last_change = time;
    if (delta <= kDebounceMicroseconds) {
      // Ignore state changes that happen within the debounce window.
      // If it leads to any issues, then the ground-truth timer will fix them.
    } else {
      pressed_state = !pressed_state;
      ReportPressedState();
    }
    { // Schedule a ground truth read in kDebounceMicroseconds
      if (esp_timer_is_active(timer)) {
        esp_timer_stop(timer);
      }
      esp_timer_start_once(timer, kDebounceMicroseconds);
    }
  }

  void OnTimer() {
    bool pressed_gpio = ReadPressedGpio();
    if (pressed_gpio != pressed_state) {
      pressed_state = pressed_gpio;
      last_change = esp_timer_get_time();
      ReportPressedState();
    }
  }

  void ReportPressedState() {
    if (pressed_state) {
      if (ble_kb_security.pass_key_collecting) {
        // During PIN collection, add digit to PIN buffer
        ble_kb_security.pass_key_buffer += (char)(i + '0');
        DebugPrintf("DEBUG: PIN buffer: '%s' (%d/%d)\n",
                    ble_kb_security.pass_key_buffer.c_str(),
                    ble_kb_security.pass_key_buffer.length(),
                    ble_kb_security.PASS_KEY_LENGTH);
      } else if (ble_keyboard.isConnected()) {
        // Normal operation - send via BLE
        OnButtonDown(i);
      } else {
        DebugPrintf("BLE not connected\n");
      }
    } else {
      if (ble_kb_security.pass_key_collecting) {
        // ignore
      } else if (ble_keyboard.isConnected()) {
        OnButtonUp(i);
      }
    }
  }
} button_debouncers[NUM_BUTTONS];

void setup() {

  if constexpr (kDebug) {
    Serial.begin(115200);
  }
  DebugPrintf("Starting Chord Keyboard...\n");

  // Arpeggios are global
  arpeggios[THUMB_1][INDEX_3] = Mod(KEY_RIGHT_CTRL);
  arpeggios[INDEX_3][THUMB_1] = Key(KEY_RIGHT_CTRL);
  arpeggios[THUMB_1][INDEX_7] = Mod(KEY_LEFT_CTRL);
  arpeggios[INDEX_7][THUMB_1] = Key(KEY_LEFT_CTRL);

  arpeggios[THUMB_1][MIDDLE_4] = Mod(KEY_RIGHT_ALT);
  arpeggios[MIDDLE_4][THUMB_1] = Key(KEY_RIGHT_ALT);
  arpeggios[THUMB_1][MIDDLE_8] = Mod(KEY_LEFT_ALT);
  arpeggios[MIDDLE_8][THUMB_1] = Key(KEY_LEFT_ALT);

  arpeggios[THUMB_1][RING_5] = Mod(KEY_RIGHT_GUI);
  arpeggios[RING_5][THUMB_1] = Key(KEY_RIGHT_GUI);
  arpeggios[THUMB_1][RING_9] = Mod(KEY_LEFT_GUI);
  arpeggios[RING_9][THUMB_1] = Key(KEY_LEFT_GUI);

  // Fingerwalker layout, Generation 21303, 149.21ms
  // Thumb layer 0 (no thumb key pressed)
  CHORDS[0][2][1][1][0] = Mod(KEY_RIGHT_ALT);

  // Thumb layer 1 (THUMB_0 pressed)
  CHORDS[1][0][0][0][0] = Key(KEY_BACKSPACE);
  CHORDS[1][0][0][0][1] = Key(KEY_DELETE);

  // Thumb layer 2 (THUMB_1 pressed)
  CHORDS[2][0][0][0][0] = Key(' ');
  CHORDS[2][1][0][0][0] = Key('\n');
  CHORDS[2][2][0][0][0] = Key('\t');
  CHORDS[2][1][0][0][1] = Key(KEY_ESC);

  // Thumb layer 3 (THUMB_2 pressed) - special keys and navigation
  CHORDS[3][0][0][0][0] = Mod(KEY_LEFT_CTRL);
  CHORDS[3][0][1][1][0] = Key(KEY_RIGHT_ARROW);
  CHORDS[3][0][1][2][0] = Key(KEY_DOWN_ARROW);
  CHORDS[3][0][2][1][0] = Mod(KEY_LEFT_CTRL, Key(KEY_RIGHT_ARROW));
  CHORDS[3][0][2][2][0] = Key(KEY_PAGE_DOWN);
  CHORDS[3][1][0][0][0] = Mod(KEY_RIGHT_GUI, Key(KEY_RETURN));
  CHORDS[3][1][0][1][0] = Key(KEY_LEFT_ARROW);
  CHORDS[3][1][0][2][0] = Key(KEY_UP_ARROW);
  CHORDS[3][1][2][1][0] = Key(KEY_HOME);
  CHORDS[3][2][0][0][0] = Hold(THUMB_2, KEY_LEFT_ALT, Key(KEY_TAB));
  CHORDS[3][2][0][1][0] = Mod(KEY_LEFT_CTRL, Key(KEY_LEFT_ARROW));
  CHORDS[3][2][0][2][0] = Key(KEY_PAGE_UP);
  CHORDS[3][2][1][1][0] = Key(KEY_END);

  CHORDS[3][1][1][1][0] = Key('\'');
  CHORDS[0][1][2][0][0] = Key(',');
  CHORDS[0][1][0][0][0] = Key('-');
  CHORDS[3][0][0][1][0] = Key('.');
  CHORDS[1][0][1][1][0] = Key('/');
  CHORDS[0][0][2][1][0] = Key('0');
  CHORDS[3][0][2][0][0] = Key('1');
  CHORDS[1][0][2][0][0] = Key('2');
  CHORDS[2][1][2][1][0] = Key('3');
  CHORDS[1][1][2][0][0] = Key('4');
  CHORDS[3][1][2][0][0] = Key('5');
  CHORDS[1][0][2][1][0] = Key('6');
  CHORDS[0][1][0][2][0] = Key('7');
  CHORDS[2][0][2][1][0] = Key('8');
  CHORDS[2][1][1][1][0] = Key('9');
  CHORDS[2][0][2][0][0] = Key(';');
  CHORDS[0][0][0][1][0] = Key('=');
  CHORDS[2][2][1][0][0] = Key('T');
  CHORDS[0][0][2][0][0] = Key('[');
  CHORDS[0][0][0][2][0] = Key('\\');
  CHORDS[0][1][2][1][0] = Key(']');
  CHORDS[1][2][0][0][0] = Key('`');
  CHORDS[0][0][1][1][0] = Key('a');
  CHORDS[1][1][1][0][0] = Key('b');
  CHORDS[1][0][0][1][0] = Key('c');
  CHORDS[2][0][1][1][0] = Key('d');
  CHORDS[0][1][0][1][0] = Key('e');
  CHORDS[1][1][1][1][0] = Key('f');
  CHORDS[3][0][1][0][0] = Key('g');
  CHORDS[0][2][0][1][0] = Key('h');
  CHORDS[2][1][0][1][0] = Key('i');
  CHORDS[1][2][1][0][0] = Key('j');
  CHORDS[0][2][0][0][0] = Key('k');
  CHORDS[2][1][1][0][0] = Key('l');
  CHORDS[1][1][0][0][0] = Key('m');
  CHORDS[2][0][1][0][0] = Key('n');
  CHORDS[0][1][1][1][0] = Key('o');
  CHORDS[1][0][1][0][0] = Key('p');
  CHORDS[1][2][0][1][0] = Key('q');
  CHORDS[0][1][1][0][0] = Key('r');
  CHORDS[0][0][1][0][0] = Key('s');
  CHORDS[2][0][0][1][0] = Key('t');
  CHORDS[1][1][0][1][0] = Key('u');
  CHORDS[3][1][1][0][0] = Key('v');
  CHORDS[0][2][1][0][0] = Key('w');
  CHORDS[2][1][2][0][0] = Key('x');
  CHORDS[0][2][1][1][0] = Key('y');
  CHORDS[2][2][0][1][0] = Key('z');

  // Add Shifts
  for (FingerPosition thumb = 0; thumb <= 3; ++thumb) {
    for (FingerPosition index = 0; index <= 2; ++index) {
      for (FingerPosition middle = 0; middle <= 2; ++middle) {
        for (FingerPosition ring = 0; ring <= 2; ++ring) {
          auto *&base = CHORDS[thumb][index][middle][ring][0];
          auto *&shift = CHORDS[thumb][index][middle][ring][1];
          if (base == nullptr)
            continue;
          if (shift)
            continue;
          shift = Hold(LITTLE_6, KEY_LEFT_SHIFT, base);
        }
      }
    }
  }

  button_changes = xQueueCreate(100, sizeof(ButtonChange));

  for (Button i = 0; i < NUM_BUTTONS; i++) {
    button_debouncers[i].OnSetup(i);
  }

#define ATTACH(button)                                                         \
  attachInterrupt(kButtonPin[button], button_isr_##button, CHANGE);
  ATTACH(0)
  ATTACH(1)
  ATTACH(2)
  ATTACH(3)
  ATTACH(4)
  ATTACH(5)
  ATTACH(6)
  ATTACH(7)
  ATTACH(8)
  ATTACH(9)
#undef ATTACH

  pinMode(BATTERY_PIN, INPUT);

  ble_keyboard.setName("𝖒𝖆𝖋.🎹");
  ble_keyboard.begin();

  BLESecurity *ble_security = new BLESecurity();
  ble_security->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  ble_security->setCapability(ESP_IO_CAP_IN);
  ble_security->setKeySize(16);
  ble_security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK |
                                     ESP_BLE_ID_KEY_MASK);
  ble_security->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK |
                                     ESP_BLE_ID_KEY_MASK);

  BLEDevice::setSecurityCallbacks(&ble_kb_security);
  DebugPrintf("BLE Keyboard initialized\n");

  // Enable automatic light-sleep (modem-sleep)
  // Read and dump initial PM configuration
  esp_pm_config_esp32s3_t initial_pm_config;
  esp_err_t get_err = esp_pm_get_configuration(&initial_pm_config);
  if (get_err == ESP_OK) {
    DebugPrintf("Initial PM configuration:\n");
    DebugPrintf("  max_freq_mhz: %d\n", initial_pm_config.max_freq_mhz);
    DebugPrintf("  min_freq_mhz: %d\n", initial_pm_config.min_freq_mhz);
    DebugPrintf("  light_sleep_enable: %d\n",
                initial_pm_config.light_sleep_enable);
  } else {
    DebugPrintf("Failed to get initial PM config: %d\n", get_err);
  }

  esp_sleep_enable_gpio_wakeup();

  { // Set CPU frequency to 80..40 MHz with automatic light sleep enabled
    esp_pm_config_esp32s3_t pm_config = {
        .max_freq_mhz = 80, .min_freq_mhz = 40, .light_sleep_enable = true};
    esp_err_t err = esp_pm_configure(&pm_config);
    if (err == ESP_OK) {
      DebugPrintf("Automatic light-sleep enabled (modem-sleep)\n");
    } else {
      DebugPrintf("Failed to enable light-sleep: %d\n", err);
    }
  }

  { // Create a battery reading timer
    auto timer_args = esp_timer_create_args_t{
        .callback = ReadBattery,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "ReadBattery",
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t battery_timer; // we don't have to keep it
    esp_err_t err = esp_timer_create(&timer_args, &battery_timer);
    if (err == ESP_OK) {
      DebugPrintf("Battery timer created\n");
      esp_timer_start_periodic(battery_timer, 5000000);
    } else {
      DebugPrintf("Failed to create battery timer: %d\n", err);
    }
  }

  { // Create chord autostart timer
    auto args = esp_timer_create_args_t{
        .callback = [](void *arg) { OnChordAutostart(); },
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "Chord Autostart",
        .skip_unhandled_events = false};
    auto timer_result = esp_timer_create(&args, &chord_autostart_timer);
    if (timer_result != ESP_OK) {
      DebugPrintf("Failed to create timer for chord autostart: %d\n",
                  timer_result);
    }
  }
}

void loop() {
  ButtonChange event;
  auto result = xQueueReceive(button_changes, &event, portMAX_DELAY);
  if (result != pdTRUE)
    return;
  button_debouncers[event.button].OnChange(event.time);
}
