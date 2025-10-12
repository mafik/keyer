#include <Python.h>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

constexpr int NUM_FINGERS = 4; // Let's assume that pinky is used for shift and
                               // focus on the other fingers

// We're using uint8_t to represent finger bitmask.
// It's ok to increase it but it would require uint16_t.
// (or uint32_t, if you intend to also type with your feet)
static_assert(NUM_FINGERS <= 8);

using Bitmask = uint8_t;

// The maximum number of buttons that a finger can press.
// It's ok if some fingers have fewer buttons.
// This is used for with optimization.
constexpr int MAX_BUTTONS = 3;

// Global cost constants (in milliseconds)
constexpr uint32_t FINGER_TRAVEL_COST_MS[5] = {
    80,  // Thumb
    100, // Index
    110, // Middle
    150, // Ring
    130  // Pinky
};

// Press costs indexed by [finger][button_position]
// Thumb has 3 buttons (1,2,3), others have 2 buttons (1,2) or 1 button (1)
constexpr uint32_t FINGER_PRESS_COST_MS[5][MAX_BUTTONS] = {
    {60, 40, 60}, // Thumb
    {50, 130, 0}, // Index
    {55, 140, 0}, // Middle
    {60, 150, 0}, // Ring
    {70, 0, 0}    // Pinky
};

constexpr Bitmask MASK_ALL = (1 << NUM_FINGERS) - 1;
constexpr Bitmask MASK_THUMB = 1 << 0;
constexpr Bitmask MASK_NON_THUMB = MASK_ALL & ~MASK_THUMB;

struct Fingers {
  // A bitmask that says whether finger i is pressed down.
  Bitmask pressed = 0;
  // A bitmask that says which fingers are over a button row i.
  Bitmask rows[MAX_BUTTONS] = {0};
  uint8_t finger_to_row[NUM_FINGERS] = {0};

  // Constructor from string representation (e.g., "01010")
  static Fingers FromChord(const char *str) {
    Fingers state = {};
    for (int i = 0; i < NUM_FINGERS; ++i) {
      int row = str[i] - '0' - 1;
      if (row >= 0) {
        state.pressed |= (1 << i);
        state.rows[row] |= (1 << i);
        state.finger_to_row[i] = row;
      }
    }
    return state;
  }

  static Fingers FromDefaultPosition() {
    return Fingers{
        .pressed = 0,
        .rows = {MASK_NON_THUMB, // index, middle & ring over first row
                 MASK_THUMB,     // thumb over second row
                 0},
        .finger_to_row = {1}, // thumb over second row
    };
  }

  int get(int finger_idx) const { return finger_to_row[finger_idx]; }

  void set(uint8_t finger_idx, uint8_t new_row) {
    uint8_t finger_bitmask = 1 << finger_idx;
    rows[finger_to_row[finger_idx]] &= ~finger_bitmask;
    rows[new_row] |= finger_bitmask;
    finger_to_row[finger_idx] = new_row;
  }

  void release_mask(Bitmask mask) { pressed &= ~mask; }

  void release_idx(int finger_idx) { release_mask(1 << finger_idx); }

  void press_mask(Bitmask mask) { pressed |= mask; }

  void press_idx(int finger_idx) { press_mask(1 << finger_idx); }

  bool is_pressed(int finger_idx) const {
    return (pressed & (1 << finger_idx)) != 0;
  }

  bool is_all_released() const { return pressed == 0; }

  // Move the fingers to the target positions in a lazy way.
  // If a finger is not being pressed, it will not be moved.
  // Returns the cost of the transition.
  // The returned cost includes a potential cost associated with re-pressing
  // some finger to trigger the target chord.
  uint32_t transition_to(const Fingers &target) {
    uint32_t cost = 0;

    // Step 1 - release at least one finger (to finish the previous chord).
    if (pressed) {
      // Plan A - let's see if there is a natural release from the current
      // `fingers` to `target_chord`
      Bitmask simple_release = pressed & ~target.pressed;

      if (simple_release) {
        // Sweet, let's release those fingers. It's a super fast motion.
        pressed &= ~simple_release;
      } else {
        // Plan B - let's see if a currently pressed finger is being moved to a
        // different position.
        Bitmask moved_fingers = pressed & target.pressed;
        for (int i = 0; i < MAX_BUTTONS; ++i) {
          moved_fingers &= ~(rows[i] & target.rows[i]);
        }
        if (moved_fingers) {
          // Cool, this release is pretty much included in finger movement cost.
          // Let's mark these fingers as released.
          pressed &= ~moved_fingers;
        } else {
          // Plan C - we have to release some of the currently held fingers and
          // re-press them. This is annoying as hell.
          Bitmask re_press_candidates = pressed & target.pressed;
          int best_re_press_finger = std::countr_zero(re_press_candidates);
          uint32_t best_re_press_cost =
              FINGER_PRESS_COST_MS[best_re_press_finger]
                                  [get(best_re_press_finger)];
          re_press_candidates &= ~(1 << best_re_press_finger);
          while (re_press_candidates) {
            int re_press_finger = std::countr_zero(re_press_candidates);
            uint32_t re_press_cost =
                FINGER_PRESS_COST_MS[re_press_finger][get(re_press_finger)];
            if (re_press_cost < best_re_press_cost) {
              best_re_press_finger = re_press_finger;
              best_re_press_cost = re_press_cost;
            }
            re_press_candidates &= ~(1 << re_press_finger);
          }

          // Replicate the current behavior - just release the cheapest finger
          pressed &= ~(1 << best_re_press_finger);

          // TODO: add a re-press penalty
          // TODO: release all fingers
        }
      }
    }

    // Step 2: Move the fingers to their target positions
    Bitmask fingers_to_move = target.pressed;
    while (fingers_to_move) {
      int finger_to_move = std::countr_zero(fingers_to_move);
      fingers_to_move &= ~(1 << finger_to_move);
      int current_position = get(finger_to_move);
      int target_position = target.get(finger_to_move);
      int distance = current_position - target_position;
      if (distance) {
        release_idx(finger_to_move);
        set(finger_to_move, target_position);
        cost += FINGER_TRAVEL_COST_MS[finger_to_move] * abs(distance);
      }
    }

    // Step 3: Press the fingers
    Bitmask fingers_to_press = target.pressed & ~pressed;
    while (fingers_to_press) {
      int finger_to_press = std::countr_zero(fingers_to_press);
      fingers_to_press &= ~(1 << finger_to_press);
      press_idx(finger_to_press);
      cost +=
          FINGER_PRESS_COST_MS[finger_to_press][target.get(finger_to_press)];
    }

    return cost;
  }
};

uint64_t type_text(const char *text, const std::vector<Fingers> key_map[256]) {
  Fingers fingers = Fingers::FromDefaultPosition();
  uint64_t total_cost = 0;

  while (*text) {
    unsigned char idx = static_cast<unsigned char>(*text++);
    const std::vector<Fingers> &available_chords = key_map[idx];

    if (available_chords.empty()) {
      // Unknown key - let's reset the finger position back to default
      // fingers = Fingers::FromDefaultPosition();
    } else if (available_chords.size() == 1) {
      total_cost += fingers.transition_to(available_chords[0]);
    } else {

      // Try all available chords and pick the best one
      uint32_t min_cost = UINT32_MAX;
      Fingers best_fingers;

      for (const Fingers &target : available_chords) {
        // Save state
        Fingers target_fingers = fingers;

        // Try this chord
        uint32_t cost = target_fingers.transition_to(target);

        if (cost < min_cost) {
          min_cost = cost;
          best_fingers = target_fingers;
        }
      }

      // Apply best transition
      fingers = best_fingers;
      total_cost += min_cost;
    }
  }

  return total_cost;
}

// Python wrapper functions
static PyObject *score_layout(PyObject *self, PyObject *args) {
  PyObject *key_map_obj;
  const char *text;

  if (!PyArg_ParseTuple(args, "Os", &key_map_obj, &text)) {
    return NULL;
  }

  // Convert Python dict to C++ array (indexed by character code)
  std::vector<Fingers> key_map[256];
  PyObject *key, *value;
  Py_ssize_t pos = 0;

  while (PyDict_Next(key_map_obj, &pos, &key, &value)) {
    // Get character key
    if (!PyUnicode_Check(key)) {
      PyErr_SetString(PyExc_TypeError, "Key must be a string");
      return NULL;
    }

    Py_ssize_t key_size;
    const char *key_str = PyUnicode_AsUTF8AndSize(key, &key_size);
    if (key_size != 1) {
      PyErr_SetString(PyExc_ValueError, "Key must be a single character");
      return NULL;
    }
    unsigned char ch = static_cast<unsigned char>(key_str[0]);

    // Get all chords from list
    if (!PyList_Check(value)) {
      PyErr_SetString(PyExc_TypeError, "Value must be a list");
      return NULL;
    }

    Py_ssize_t num_chords = PyList_Size(value);

    for (Py_ssize_t i = 0; i < num_chords; i++) {
      PyObject *chord_obj = PyList_GetItem(value, i);
      if (!PyUnicode_Check(chord_obj)) {
        PyErr_SetString(PyExc_TypeError, "Chord must be a string");
        return NULL;
      }

      const char *chord_str = PyUnicode_AsUTF8(chord_obj);
      key_map[ch].push_back(Fingers::FromChord(chord_str));
    }
  }

  // Run simulation
  uint64_t cost = type_text(text, key_map);

  return PyLong_FromUnsignedLongLong(cost);
}

// Module methods
static PyMethodDef KeyerMethods[] = {
    {"score_layout", score_layout, METH_VARARGS,
     "Score a keyboard layout by simulating text input"},
    {NULL, NULL, 0, NULL}};

// Module definition
static struct PyModuleDef keyermodule = {
    PyModuleDef_HEAD_INIT, "keyer_simulator_native",
    "Native C++ keyer simulator for performance", -1, KeyerMethods};

// Module initialization
PyMODINIT_FUNC PyInit_keyer_simulator_native(void) {
  return PyModule_Create(&keyermodule);
}
