#include "esp_sleep.h"
#include "esp_pm.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include "BleKeyboard.h"

#include <cstdint>
#include <vector>

BleKeyboard bleKeyboard("temp", "𝖒𝖆𝖋", 100);

const int BATTERY_PIN = 3;

enum KeyName {
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
  TOTAL_KEYS
};

const char *KeyToStr(int key) {
  switch (key) {
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

constexpr int keyPins[TOTAL_KEYS] = {
    [THUMB_0] = 2,   [THUMB_1] = 5, [THUMB_2] = 0,   [INDEX_3] = 46,
    [MIDDLE_4] = 13, [RING_5] = 35, [LITTLE_6] = 37, [INDEX_7] = 38,
    [MIDDLE_8] = 8,  [RING_9] = 42,
};

bool lastKeyState[TOTAL_KEYS];
bool currentKeyState[TOTAL_KEYS];

unsigned long lastBatteryCheck = 0;
const unsigned long BATTERY_CHECK_INTERVAL = 5000;

// PIN collection state
bool collectingPIN = false;
String pinBuffer = "";
const int PIN_LENGTH = 6;

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

Action *arpeggios[TOTAL_KEYS][TOTAL_KEYS];

bool keys_down[TOTAL_KEYS];
Action *active_key_actions[TOTAL_KEYS];
Action *chord_action;
unsigned long chord_start_millis; // 0 means that no chord is being composed

constexpr unsigned long ARPEGGIO_MIN_DELAY_MS = 80;
constexpr unsigned long ARPEGGIO_MAX_DOWN_MS = 240;
constexpr unsigned long CHORD_AUTOSTART_MILLIS = 350;

enum ArpeggioState {
  STATE_READY,
  STATE_KEY1_DOWN,
  STATE_KEY2_DOWN,
  STATE_INACTIVE,
} arpeggio_state = STATE_READY;
unsigned long arpeggio_start_millis = 0;
uint8_t arpeggio_key1 = 0;
uint8_t arpeggio_key2 = 0;

uint8_t Thumb() {
  if (keys_down[THUMB_0]) {
    return 1;
  } else if (keys_down[THUMB_1]) {
    return 2;
  } else if (keys_down[THUMB_2]) {
    return 3;
  } else {
    return 0;
  }
}

uint8_t Index() {
  if (keys_down[INDEX_3]) {
    return 1;
  } else if (keys_down[INDEX_7]) {
    return 2;
  } else {
    return 0;
  }
}

uint8_t Middle() {
  if (keys_down[MIDDLE_4]) {
    return 1;
  } else if (keys_down[MIDDLE_8]) {
    return 2;
  } else {
    return 0;
  }
}

uint8_t Ring() {
  if (keys_down[RING_5]) {
    return 1;
  } else if (keys_down[RING_9]) {
    return 2;
  } else {
    return 0;
  }
}

uint8_t Little() {
  if (keys_down[LITTLE_6]) {
    return 1;
  } else {
    return 0;
  }
}

// The returned string is going to be invalidated after the next call to
// ByteToStr()
const char *ByteToStr(uint8_t key) {
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

std::vector<uint8_t> temp_modifiers;

void ReleaseTempModifiers() {
  for (auto mod : temp_modifiers) {
    Serial.printf("  Releasing modifier: %s (ReleaseTempModifiers)\n",
                  ByteToStr(mod));
    bleKeyboard.release(mod);
  }
  temp_modifiers.clear();
}

struct WriteKeyAction : Action {
  uint8_t key;
  WriteKeyAction(uint8_t key, Action *next = nullptr)
      : Action(next), key(key) {}
  void OnStart() override {
    Serial.printf("  Pressing key: %s (WriteKeyAction)\n", ByteToStr(key));
    bleKeyboard.press(key);
  }
  void OnStop() override {
    Serial.printf("  Releasing key: %s (WriteKeyAction)\n", ByteToStr(key));
    bleKeyboard.release(key);
    ReleaseTempModifiers();
  }
};
// A modifier that affects the next key press.
// It's released along with the next key.
struct TemporaryModifierAction : Action {
  uint8_t modifier;
  TemporaryModifierAction(uint8_t modifier, Action *next = nullptr)
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
      Serial.printf("  Releasing modifier [%s] (TemporaryModifierAction)\n",
                    ByteToStr(modifier));
      bleKeyboard.release(modifier);
      temp_modifiers.erase(existing_modifier_it);
    } else {
      Serial.printf("  Pressing modifier [%s] (TemporaryModifierAction)\n",
                    ByteToStr(modifier));
      bleKeyboard.press(modifier);
      temp_modifiers.push_back(modifier);
    }
  }
  void OnStop() override {}
};
struct HoldModifierAction : Action {
    int held_key;
    uint8_t modifier;
    struct ReleaseHeldModifierAction : Action {
        HoldModifierAction& hold_action;
        ReleaseHeldModifierAction(HoldModifierAction& hold_action) : Action(nullptr), hold_action(hold_action) {}
        void OnStart() override {}
        void OnStop() override {
            Serial.printf("  Releasing modifier [%s] (ReleaseHeldModifierAction)\n",
                          ByteToStr(hold_action.modifier));
            bleKeyboard.release(hold_action.modifier);
        }
    };
    ReleaseHeldModifierAction release_action;
    HoldModifierAction(int held_key, uint8_t modifier, Action *next = nullptr)
        : Action(next), held_key(held_key), modifier(modifier), release_action(*this) {}
    void OnStart() override {
        if (active_key_actions[held_key]) {
            Serial.printf("  Keeping modifier [%s] (HoldModifierAction)\n",
                          ByteToStr(modifier));
            return;
        }
        Serial.printf("  Pressing modifier [%s] (HoldModifierAction)\n",
                        ByteToStr(modifier));
        bleKeyboard.press(modifier);
        active_key_actions[held_key] = &release_action;
    }
    void OnStop() override {}
};

// Shortcuts for faster layout definition
static Action *Key(uint8_t key, Action *next = nullptr) {
  return new WriteKeyAction(key, next);
}
static Action *Mod(uint8_t modifier, Action *next = nullptr) {
  return new TemporaryModifierAction(modifier, next);
}
static Action *Hold(int hold_button, uint8_t modifier, Action *next = nullptr) {
  return new HoldModifierAction(hold_button, modifier, next);
}

Action *FindUniqueAction() {
  Action *found = nullptr;
  int thumb_current = Thumb();
  int index_current = Index();
  int middle_current = Middle();
  int ring_current = Ring();
  int little_current = Little();
  for (int thumb = 0; thumb <= 3; ++thumb) {
    if (thumb_current && thumb_current != thumb)
      continue;
    for (int index = 0; index <= 2; ++index) {
      if (index_current && index_current != index)
        continue;
      for (int middle = 0; middle <= 2; ++middle) {
        if (middle_current && middle_current != middle)
          continue;
        for (int ring = 0; ring <= 2; ++ring) {
          if (ring_current && ring_current != ring)
            continue;
          for (int little = 0; little <= 1; ++little) {
            if (little_current && little_current != little)
              continue;
            if (Action *candidate =
                    CHORDS[thumb][index][middle][ring][little]) {
              if (found) {
                return nullptr;
              } else {
                found = candidate;
              }
            }
          }
        }
      }
    }
  }
  return found;
}

void OnKeyDown(int i) {
  auto now = millis();
  if (arpeggio_state == STATE_READY) {
    arpeggio_start_millis = now;
    arpeggio_key1 = i;
    arpeggio_state = STATE_KEY1_DOWN;
  } else if (arpeggio_state == STATE_KEY1_DOWN) {
    Serial.printf("Arpeggio key 1 down millis: %lu\n",
                  now - arpeggio_start_millis);
    if (now - arpeggio_start_millis >= ARPEGGIO_MIN_DELAY_MS) {
      arpeggio_key2 = i;
      arpeggio_start_millis = now;
      arpeggio_state = STATE_KEY2_DOWN;
    } else {
      arpeggio_state = STATE_INACTIVE;
    }
  } else {
    arpeggio_state = STATE_INACTIVE;
  }

  keys_down[i] = true;
  auto unique_action = FindUniqueAction();
  if (unique_action) {
    // If a unique key action was found, then don't add it to the chord but
    // rather start it immediately This allows multiple actions to be active at
    // the same time (as long as they have been unique at press time)
    keys_down[i] = false;
    // We also don't want to start a new chord
    chord_start_millis = 0;
    Serial.printf(" Unique action!\n");
    active_key_actions[i] = unique_action;
    unique_action->Start();
  } else {
    chord_start_millis = now;
  }
}

void OnKeyUp(int i) {
  auto now = millis();
  if (arpeggio_state == STATE_KEY2_DOWN) {
    Serial.printf("Arpeggio key 2 down millis: %lu\n",
                  now - arpeggio_start_millis);
    if (now - arpeggio_start_millis <= ARPEGGIO_MAX_DOWN_MS) {
      auto action = arpeggios[arpeggio_key1][arpeggio_key2];
      if (action) {
        Serial.printf("Arpeggio action\n");
        action->Execute();
        chord_start_millis = 0;
      }
    }
    arpeggio_state = STATE_INACTIVE;
  }

  if (auto &active_key_action = active_key_actions[i]) {
    Serial.printf("Stopping active key action\n");
    active_key_action->Stop();
    active_key_action = nullptr;
  } else if (chord_action && keys_down[i]) {
    Serial.printf("Stopping chord action\n");
    chord_action->Stop();
    chord_action = nullptr;
  } else if (chord_start_millis) {
    auto action = CHORDS[Thumb()][Index()][Middle()][Ring()][Little()];
    if (action) {
      Serial.printf("Chord action\n");
      action->Execute();

      // It's possible that chord action attaches an "active key" action to the currently released key.
      // If that's the case then it should be immediately stopped.
      if (auto &active_key_action = active_key_actions[i]) {
        Serial.printf("Stopping active key action\n");
        active_key_action->Stop();
        active_key_action = nullptr;
      }
    } else {
      Serial.printf("No chord action\n");
    }
    chord_start_millis = 0;
  }

  keys_down[i] = false;

  bool any_key_down = false;
  for (int i = 0; i < TOTAL_KEYS; i++) {
    any_key_down |= keys_down[i];
  }
  bool all_keys_up = !any_key_down;
  if (all_keys_up) {
    arpeggio_state = STATE_READY;
  }
}

class BleKeyboardSecurityCallbacks : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() {
    Serial.println(
        "DEBUG: onPassKeyRequest called - collecting PIN from keyboard");
    Serial.println("DEBUG: Please type 6 digits on the keyboard");

    collectingPIN = true;
    pinBuffer = "";

    // Wait for 6 digits to be entered
    unsigned long startTime = millis();
    const unsigned long timeout = 30000; // 30 second timeout

    while (pinBuffer.length() < PIN_LENGTH &&
           (millis() - startTime) < timeout) {
      delay(10); // Small delay to prevent busy waiting
      // PIN collection happens in main loop
    }

    collectingPIN = false;

    if (pinBuffer.length() == PIN_LENGTH) {
      uint32_t pin = pinBuffer.toInt();
      Serial.printf("DEBUG: Collected PIN: %06d\n", pin);
      return pin;
    } else {
      Serial.println("DEBUG: PIN collection timeout - using default");
      return 123456;
    }
  }

  void onPassKeyNotify(uint32_t pass_key) {
    Serial.printf("DEBUG: onPassKeyNotify - PIN displayed: %06d\n", pass_key);
  }

  bool onConfirmPIN(uint32_t pass_key) {
    Serial.printf("DEBUG: onConfirmPIN - PIN to confirm: %06d\n", pass_key);
    return true;
  }

  bool onSecurityRequest() {
    // New device is connecting.
    Serial.println("DEBUG: onSecurityRequest called");
    return true;
  }

  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) {
    Serial.println("DEBUG: onAuthenticationComplete called");
    if (cmpl.success) {
      Serial.println("DEBUG: Pairing successful!");
    } else {
      Serial.printf("DEBUG: Pairing failed, reason: %d\n", cmpl.fail_reason);
    }
  }
};

void setup() {

  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting Chord Keyboard...");
  delay(1000);
  // Serial.end();

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

  // Generation 7004, 125.88ms
  // Thumb layer 0 (no thumb key pressed)
  CHORDS[0][0][0][2][0] = Key('z');
  CHORDS[0][0][0][1][0] = Key('-');
  CHORDS[0][0][1][0][0] = Key('r');
  CHORDS[0][0][1][1][0] = Key('a');
  CHORDS[0][0][2][0][0] = Key('x');
  CHORDS[0][0][2][1][0] = Key('0');
  CHORDS[0][1][0][0][0] = Key('e');
  CHORDS[0][1][0][1][0] = Key('t');
  CHORDS[0][1][0][2][0] = Key('`');
  CHORDS[0][1][1][0][0] = Key('o');
  CHORDS[0][1][1][1][0] = Key('c');
  CHORDS[0][1][2][0][0] = Key('9');
  CHORDS[0][1][2][1][0] = Key('8');
  CHORDS[0][2][0][0][0] = Key('=');
  CHORDS[0][2][0][1][0] = Key(',');
  CHORDS[0][2][1][0][0] = Key('y');
  CHORDS[0][2][1][1][0] = Mod(KEY_RIGHT_ALT);
  CHORDS[0][2][2][1][0] = Key('5');

  // Thumb layer 1 (THUMB_0 pressed)
  CHORDS[1][0][0][0][0] = Key(KEY_BACKSPACE);
  CHORDS[1][0][0][0][1] = Key(KEY_DELETE);
  CHORDS[1][0][0][1][0] = Key('.');
  CHORDS[1][0][1][0][0] = Key('m');
  CHORDS[1][0][1][1][0] = Key('b');
  CHORDS[1][0][2][0][0] = Key('1');
  CHORDS[1][0][2][1][0] = Key('2');
  CHORDS[1][1][0][0][0] = Key('p');
  CHORDS[1][1][0][1][0] = Key('u');
  CHORDS[1][1][1][0][0] = Key('/');
  CHORDS[1][1][1][1][0] = Key('h');
  CHORDS[1][1][2][0][0] = Key('6');
  CHORDS[1][1][2][1][0] = Key('4');
  CHORDS[1][2][0][0][0] = Key('7');
  CHORDS[1][2][0][1][0] = Key('j');
  CHORDS[1][2][1][0][0] = Key('q');

  // Thumb layer 2 (THUMB_1 pressed)
  CHORDS[2][0][0][0][0] = Key(' ');
  CHORDS[2][0][0][1][0] = Key('s');
  CHORDS[2][0][1][0][0] = Key('l');
  CHORDS[2][0][1][1][0] = Key('n');
  CHORDS[2][0][2][0][0] = Key(';');
  CHORDS[2][1][2][0][0] = Key('[');
  CHORDS[2][1][0][0][0] = Key('\n');
  CHORDS[2][1][0][0][1] = Key(KEY_ESC);
  CHORDS[2][1][0][1][0] = Key('i');
  CHORDS[2][1][1][0][0] = Key('d');
  CHORDS[2][1][1][1][0] = Key('g');
  CHORDS[2][0][2][1][0] = Key(']');
  CHORDS[2][1][2][1][0] = Key('3');
  CHORDS[2][2][0][0][0] = Key('\t');
  CHORDS[2][2][0][1][0] = Key('\'');
  CHORDS[2][2][1][0][0] = Key('v');
  CHORDS[2][2][1][1][0] = Key('\\');

  // Thumb layer 3 (THUMB_2 pressed) - special keys and navigation
  CHORDS[3][0][0][0][0] = Mod(KEY_LEFT_CTRL);
  CHORDS[3][0][0][1][0] = Key('f');
  CHORDS[3][0][0][2][0] = Mod(KEY_LEFT_CTRL, Key('z'));
  CHORDS[3][0][1][0][0] = Key('w');
  CHORDS[3][0][1][1][0] = Key(KEY_RIGHT_ARROW);
  CHORDS[3][0][1][2][0] = Key(KEY_DOWN_ARROW);
  CHORDS[3][0][2][0][0] = Mod(KEY_LEFT_CTRL, Key('x'));
  CHORDS[3][0][2][1][0] = Mod(KEY_LEFT_CTRL, Key(KEY_RIGHT_ARROW));
  CHORDS[3][0][2][2][0] = Key(KEY_PAGE_DOWN);
  CHORDS[3][1][0][0][0] = Mod(KEY_RIGHT_GUI, Key(KEY_RETURN));
  CHORDS[3][1][0][1][0] = Key(KEY_LEFT_ARROW);
  CHORDS[3][1][0][2][0] = Key(KEY_UP_ARROW);
  CHORDS[3][1][1][0][0] = Key('k');
  CHORDS[3][1][1][1][0] = Mod(KEY_LEFT_CTRL, Key('c'));
  CHORDS[3][1][2][0][0] = Key('2');
  CHORDS[3][1][2][1][0] = Key(KEY_HOME);
  CHORDS[3][2][0][0][0] = Hold(THUMB_2, KEY_LEFT_ALT, Key(KEY_TAB));
  CHORDS[3][2][0][1][0] = Mod(KEY_LEFT_CTRL, Key(KEY_LEFT_ARROW));
  CHORDS[3][2][0][2][0] = Key(KEY_PAGE_UP);
  CHORDS[3][2][1][0][0] = Mod(KEY_LEFT_CTRL, Key('v'));
  CHORDS[3][2][1][1][0] = Key(KEY_END);

  // Add Shifts
  for (int thumb = 0; thumb <= 3; ++thumb) {
    for (int index = 0; index <= 2; ++index) {
      for (int middle = 0; middle <= 2; ++middle) {
        for (int ring = 0; ring <= 2; ++ring) {
            auto*& base = CHORDS[thumb][index][middle][ring][0];
            auto*& shift = CHORDS[thumb][index][middle][ring][1];
            if (base == nullptr) continue;
            if (shift) continue;
            shift = Hold(LITTLE_6, KEY_LEFT_SHIFT, base);
        }
      }
    }
  }


  for (int i = 0; i < TOTAL_KEYS; i++) {
    pinMode(keyPins[i], INPUT_PULLUP);
    lastKeyState[i] = HIGH;
    currentKeyState[i] = HIGH;
  }

  pinMode(BATTERY_PIN, INPUT);

  bleKeyboard.setName("𝖒𝖆𝖋.🎹");
  bleKeyboard.begin();

  BLESecurity *pSecurity = new BLESecurity();
  pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  pSecurity->setCapability(ESP_IO_CAP_IN);
  pSecurity->setKeySize(16);
  pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  BLEDevice::setSecurityCallbacks(new BleKeyboardSecurityCallbacks());
  Serial.println("BLE Keyboard initialized");

  // Enable automatic light-sleep (modem-sleep)
  // Read and dump initial PM configuration
  esp_pm_config_esp32s3_t initial_pm_config;
  esp_err_t get_err = esp_pm_get_configuration(&initial_pm_config);
  if (get_err == ESP_OK) {
    Serial.println("Initial PM configuration:");
    Serial.printf("  max_freq_mhz: %d\n", initial_pm_config.max_freq_mhz);
    Serial.printf("  min_freq_mhz: %d\n", initial_pm_config.min_freq_mhz);
    Serial.printf("  light_sleep_enable: %d\n", initial_pm_config.light_sleep_enable);
  } else {
    Serial.printf("Failed to get initial PM config: %d\n", get_err);
  }

  esp_sleep_enable_gpio_wakeup();
  esp_pm_config_esp32s3_t pm_config = {
      .max_freq_mhz = 40,
      .min_freq_mhz = 40,
      .light_sleep_enable = true
  };
  esp_err_t err = esp_pm_configure(&pm_config);
  if (err == ESP_OK) {
    Serial.println("Automatic light-sleep enabled (modem-sleep)");
  } else {
    Serial.printf("Failed to enable light-sleep: %d\n", err);
  }
}

void ReadBattery() {
  int rawValue = analogRead(BATTERY_PIN);
  float voltage = (rawValue * 4.187) / 2441.0; // measured with a multimeter
  int batteryPercent =
      map(constrain(voltage * 1000, 3000, 4185), 3000, 4185, 0, 100);

  bleKeyboard.setBatteryLevel(batteryPercent);

  // Serial.print("CPU Frequency: ");
  // Serial.print(getCpuFrequencyMhz());
  // Serial.println(" MHz");

  // Serial.println("PM Locks:");
  // esp_pm_dump_locks(stdout);
}

void loop() {

  auto now = millis();

  for (int i = 0; i < TOTAL_KEYS; i++) {
    currentKeyState[i] = digitalRead(keyPins[i]);

    if (lastKeyState[i] == HIGH && currentKeyState[i] == LOW) {

      Serial.printf("%s down (GPIO %d)\n", KeyToStr(i), keyPins[i]);

      if (collectingPIN) {
        // During PIN collection, add digit to PIN buffer
        pinBuffer += (char)(i + '0');
        Serial.printf("DEBUG: PIN buffer: '%s' (%d/%d)\n", pinBuffer.c_str(),
                      pinBuffer.length(), PIN_LENGTH);
      } else if (bleKeyboard.isConnected()) {
        // Normal operation - send via BLE
        OnKeyDown(i);
      } else {
        Serial.println("BLE not connected");
      }
    } else if (lastKeyState[i] == LOW && currentKeyState[i] == HIGH) {
      Serial.printf("%s up (GPIO %d)\n", KeyToStr(i), keyPins[i]);
      if (collectingPIN) {
        // ignore
      } else if (bleKeyboard.isConnected()) {
        OnKeyUp(i);
      }
    }

    lastKeyState[i] = currentKeyState[i];
  }

  if (chord_start_millis && now - chord_start_millis > CHORD_AUTOSTART_MILLIS) {
    auto action = CHORDS[Thumb()][Index()][Middle()][Ring()][Little()];
    if (action) {
      Serial.printf("Timeout action\n");
      action->Start();
      chord_action = action;
      chord_start_millis = 0;
    }
  }

  if (now - lastBatteryCheck > BATTERY_CHECK_INTERVAL) {
    ReadBattery();
    lastBatteryCheck = now;
  }

  delay(10);
}
