#include "keyer.hpp"

#include "app.hpp"
#include "eye_term.hpp"
#include "keyboard.hpp"
#include "main_loop.hpp"

namespace atmt {

// A mechanical switch numbered 0-9
using Button = uint8_t;

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

const char *ButtonToStr(int btn);

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

Modifier temp_modifiers, held_modifiers;

bool ogonek = false;
bool modifiers_consumed = false;

uint32_t GetOgonek(uint32_t codepoint) {
  switch (codepoint) {
  case 'c':
    return U'ć';
  case 'l':
    return U'ł';
  case 'n':
    return U'ń';
  case 'o':
    return U'ó';
  case 's':
    return U'ś';
  case 'z':
    return U'ż';
  case 'x':
    return U'ź';
  case 'a':
    return U'ą';
  case 'e':
    return U'ę';
  }
  return codepoint;
}

struct FunctionAction : Action {
  std::function<void()> func;
  FunctionAction(std::function<void()> func, Action *next = nullptr)
      : Action(next), func(func) {}
  void OnStart() override { func(); }
  void OnStop() override {}
};

struct WriteUnicodeAction : Action {
  uint32_t codepoint;
  WriteUnicodeAction(uint32_t unichar, Action *next = nullptr)
      : Action(next), codepoint(unichar) {}
  void OnStart() override {
    Modifier modifiers = temp_modifiers | held_modifiers;
    modifiers_consumed = true;
    if (ogonek) {
      IBM_Key fkey = IBM_Key::NONE;
      if (codepoint >= '1' && codepoint <= '9') {
        fkey = static_cast<IBM_Key>(static_cast<int>(IBM_Key::F1) +
                                    (codepoint - '1'));
      } else if (codepoint == '0') {
        fkey = IBM_Key::F10;
      } else if (codepoint == '-') {
        fkey = IBM_Key::F11;
      } else if (codepoint == '=') {
        fkey = IBM_Key::F12;
      }
      if (fkey != IBM_Key::NONE) {
        Debugf("key %s", ToStr(fkey));
        current_app->OnKey(fkey, modifiers);
      } else {
        Debugf("unicode %d (ogonek)", codepoint);
        current_app->OnUnicode(GetOgonek(codepoint), modifiers);
      }
    } else {
      Debugf("unicode %d", codepoint);
      current_app->OnUnicode(codepoint, modifiers);
    }
  }
  void OnStop() override {}
};

struct WriteIBM_KeyAction : Action {
  IBM_Key key;
  WriteIBM_KeyAction(IBM_Key key, Action *next = nullptr)
      : Action(next), key(key) {}
  void OnStart() override {
    Debugf("ibm_key %s", ToStr(key));
    Modifier modifiers = temp_modifiers | held_modifiers;
    modifiers_consumed = true;
    current_app->OnKey(key, modifiers);
  }
  void OnStop() override {}
};

// A modifier that affects the next key press.
// It's released along with the next key.
struct TemporaryModifierAction : Action {
  Modifier modifier;
  TemporaryModifierAction(Modifier modifier, Action *next = nullptr)
      : Action(next), modifier(modifier) {}

  void OnStart() override {
    if (temp_modifiers & modifier) {
      Debugf("  Releasing modifier [%d] (TemporaryModifierAction)\n", modifier);
      temp_modifiers &= ~modifier;
    } else {
      Debugf("  Pressing modifier [%d] (TemporaryModifierAction)\n", modifier);
      temp_modifiers |= modifier;
    }
  }
  void OnStop() override {}
};

struct TemporaryOgonekAction : Action {
  TemporaryOgonekAction(Action *next = nullptr) : Action(next) {}
  void OnStart() override { ogonek = !ogonek; }
  void OnStop() override {}
};

struct HoldModifierAction : Action {
  Button hold_button;
  Modifier modifier;
  struct ReleaseHeldModifierAction : Action {
    HoldModifierAction &hold_action;
    ReleaseHeldModifierAction(HoldModifierAction &hold_action)
        : Action(nullptr), hold_action(hold_action) {}
    void OnStart() override {}
    void OnStop() override {
      Debugf("  Releasing modifier [%d] (ReleaseHeldModifierAction)\n",
             hold_action.modifier);
      held_modifiers &= ~hold_action.modifier;
    }
  };
  ReleaseHeldModifierAction release_action;
  HoldModifierAction(Button held_key, Modifier modifier, Action *next = nullptr)
      : Action(next), hold_button(held_key), modifier(modifier),
        release_action(*this) {}
  void OnStart() override {
    if (active_button_actions[hold_button]) {
      Debugf("  Keeping modifier [%d] (HoldModifierAction)\n", modifier);
      return;
    }
    Debugf("  Pressing modifier [%d] (HoldModifierAction)\n", modifier);
    held_modifiers |= modifier;
    active_button_actions[hold_button] = &release_action;
  }
  void OnStop() override {}
};

// Shortcuts for faster layout definition
static Action *Fn(std::function<void()> func, Action *next = nullptr) {
  return new FunctionAction(func, next);
}
static Action *Key(uint32_t unichar, Action *next = nullptr) {
  return new WriteUnicodeAction(unichar, next);
}
static Action *Key(IBM_Key key, Action *next = nullptr) {
  return new WriteIBM_KeyAction(key, next);
}
static Action *Mod(Modifier modifier, Action *next = nullptr) {
  return new TemporaryModifierAction(modifier, next);
}
static Action *Hold(Button hold_button, Modifier modifier,
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
    // Debugf("Arpeggio key 1 down millis: %lu\n", now - arpeggio_start_millis);
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
    // If a unique key action was found, then don't add it to the
    // chord but rather start it immediately This allows multiple
    // actions to be active at the same time (as long as they have
    // been unique at press time)
    buttons_down[i] = false;
    // We also don't want to start a new chord
    if (esp_timer_is_active(chord_autostart_timer)) {
      esp_timer_stop(chord_autostart_timer);
    }
    Debugln("Unique action!");
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
    // Debugf("Arpeggio button 2 down millis: %lu\n", now -
    // arpeggio_start_millis);
    if (now - arpeggio_start_millis <= kArpeggioMaxHoldMillis) {
      auto action = arpeggios[arpeggio_button1][arpeggio_button2];
      if (action) {
        Debugf("Arpeggio action: ");
        action->Execute();
        if (esp_timer_is_active(chord_autostart_timer)) {
          esp_timer_stop(chord_autostart_timer);
        }
      }
    }
    arpeggio_state = STATE_INACTIVE;
  }

  if (auto &active_button_action = active_button_actions[i]) {
    Debugf("Stopping active button action\n");
    active_button_action->Stop();
    active_button_action = nullptr;
  } else if (chord_action && buttons_down[i]) {
    Debugf("Stopping chord action\n");
    chord_action->Stop();
    chord_action = nullptr;
  } else if (esp_timer_is_active(chord_autostart_timer)) {
    esp_timer_stop(chord_autostart_timer);
    auto action = CHORDS[Thumb()][Index()][Middle()][Ring()][Little()];
    Debugf("Chord %d%d%d%d%d => ", Thumb(), Index(), Middle(), Ring(),
           Little());
    if (action) {
      action->Execute();

      // It's possible that chord action attaches an "active key"
      // action to the currently released key. If that's the case then
      // it should be immediately stopped.
      if (auto &active_button_action = active_button_actions[i]) {
        Debugf("Stopping active button action\n");
        active_button_action->Stop();
        active_button_action = nullptr;
      }
    } else {
      Debug("undefined");
    }
    Debug('\n');
  }

  buttons_down[i] = false;

  bool any_button_down = false;
  for (int i = 0; i < NUM_BUTTONS; i++) {
    any_button_down |= buttons_down[i];
  }
  bool all_buttons_up = !any_button_down;
  if (all_buttons_up) {
    arpeggio_state = STATE_READY;
    if (modifiers_consumed) {
      temp_modifiers = 0;
      ogonek = false;
      modifiers_consumed = false;
    }
  }
}

void OnChordAutostart() {
  if (chord_action) {
    Debugf("ERROR: Chord action already active\n");
    return;
  }
  auto action = CHORDS[Thumb()][Index()][Middle()][Ring()][Little()];
  if (action) {
    Debugf("Starting chord hold\n");
    action->Start();
    chord_action = action;
  }
}

// Zero-latency button debouncer.
//
// Initial state change is immediately registered as button press or
// release. Subsequent state changes are ignored for a short time
// window (a couple of milliseconds). After a period of no activity,
// the GPIO state is read directly to verify the current button state.
//
// The approach used by this debouncer results in zero latency but a
// minimal press duration equal to the debounce window.
struct ButtonDebouncer {
  Button i;
  bool pressed_state;
  esp_timer_handle_t timer;
  int64_t last_change;

  // Experimentally, the shortest physically possible key press was a
  // tad over 15ms
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
      Debugf("Failed to create timer for button %s\n", ButtonToStr(i));
    }
  }

  void OnChange(int64_t time) {
    auto delta = time - last_change;
    last_change = time;
    if (delta <= kDebounceMicroseconds) {
      // Ignore state changes that happen within the debounce window.
      // If it leads to any issues, then the ground-truth timer will
      // fix them.
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
      OnButtonDown(i);
    } else {
      OnButtonUp(i);
    }
  }
} button_debouncers[NUM_BUTTONS];

void IRAM_ATTR button_isr(void *button) {
  uint64_t arg = (uint64_t)button;
  arg <<= 52;
  arg |= esp_timer_get_time(); // esp_timer_get_time returns up to 52
                               // bits
  RunOnMainISR(
      [](uint64_t arg) {
        int64_t time = arg & ((1ULL << 52) - 1);
        int button = arg >> 52;
        button_debouncers[button].OnChange(time);
      },
      arg);
}

void InitKeyer() {

  // Arpeggios are global
  arpeggios[THUMB_1][INDEX_3] = Mod(MOD_CTRL);
  arpeggios[THUMB_1][MIDDLE_4] = Mod(MOD_ALT);
  arpeggios[THUMB_1][RING_5] = Mod(MOD_SUPER);

  // Fingerwalker layout, Generation 21303, 149.21ms
  // Thumb layer 0 (no thumb key pressed)
  CHORDS[2][2][1][0][0] = new TemporaryOgonekAction();

  // Thumb layer 1 (THUMB_0 pressed)
  CHORDS[1][0][0][0][0] = Key(IBM_Key::BACKSPACE);
  CHORDS[1][0][0][0][1] = Key(IBM_Key::DELETE);

  // Thumb layer 2 (THUMB_1 pressed)
  CHORDS[2][0][0][0][0] = Key(' ');
  CHORDS[2][1][0][0][0] = Key(IBM_Key::ENTER);
  CHORDS[2][2][0][0][0] = Key(IBM_Key::TAB);
  CHORDS[2][1][0][0][1] = Key(IBM_Key::ESC);

  // Thumb layer 3 (THUMB_2 pressed) - special keys and navigation
  CHORDS[3][0][0][0][0] = Mod(MOD_CTRL);
  CHORDS[3][0][1][1][0] = Key(IBM_Key::RIGHT_ARROW);
  CHORDS[3][0][1][2][0] = Key(IBM_Key::DOWN_ARROW);
  CHORDS[3][0][2][1][0] = Mod(MOD_CTRL, Key(IBM_Key::RIGHT_ARROW)); // C+right
  CHORDS[3][0][2][2][0] = Key(IBM_Key::PAGE_DOWN);                  // page down
  CHORDS[3][1][0][0][0] = Mod(MOD_SUPER, Key('\n'));
  CHORDS[3][1][0][1][0] = Key(IBM_Key::LEFT_ARROW); // left
  CHORDS[3][1][0][2][0] = Key(IBM_Key::UP_ARROW);   // up
  CHORDS[3][1][2][1][0] = Key(IBM_Key::HOME);       // home
  CHORDS[3][2][0][0][0] = Hold(THUMB_2, MOD_ALT, Key('\t'));
  CHORDS[3][2][0][1][0] = Mod(MOD_CTRL, Key(IBM_Key::LEFT_ARROW)); // C+left
  CHORDS[3][2][0][2][0] = Key(IBM_Key::PAGE_UP);                   // page up
  CHORDS[3][2][1][1][0] = Key(IBM_Key::END);                       // end

  // abcdefghijklmnopqrstuvwxyz 1234567890 `-= []\;',./
  // ABCDEFGHIJKLMNOPQRSTUVWXYZ !@#$%^&*() ~_+ {}|:"<>?
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
  arpeggios[INDEX_7][RING_9] =
      Fn([]() { App::SaveAndRestart(App::Kind::kKeyboard); });
  arpeggios[RING_9][INDEX_7] =
      Fn([]() { App::SaveAndRestart(App::Kind::kTerminal); });

  // Add Shifts
  for (FingerPosition thumb = 0; thumb <= 3; ++thumb) {
    for (FingerPosition index = 0; index <= 2; ++index) {
      for (FingerPosition middle = 0; middle <= 2; ++middle) {
        for (FingerPosition ring = 0; ring <= 2; ++ring) {
          auto *&base = CHORDS[thumb][index][middle][ring][0];
          auto *&shift = CHORDS[thumb][index][middle][ring][1];
          if (base == nullptr) {
            Debugf("Free chord: CHORDS[%d][%d][%d][%d][0]\n", thumb, index,
                   middle, ring);
            continue;
          }
          if (shift)
            continue;
          shift = Hold(LITTLE_6, MOD_SHIFT, base);
        }
      }
    }
  }

  for (Button i = 0; i < NUM_BUTTONS; i++) {
    button_debouncers[i].OnSetup(i);
    attachInterruptArg(kButtonPin[i], button_isr, (void *)(uint64_t)i, CHANGE);
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
      Debugf("Failed to create timer for chord autostart: %d\n", timer_result);
    }
  }
}

void RegisterChord(FingerPosition thumb, FingerPosition index,
                   FingerPosition middle, FingerPosition ring,
                   FingerPosition little, std::function<void()> fn) {
  CHORDS[thumb][index][middle][ring][little] = new FunctionAction(fn);
}

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
} // namespace atmt
