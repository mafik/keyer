#include <Python.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdint>

// Global cost constants (in milliseconds)
// Indexed by finger: 0=Thumb, 1=Index, 2=Middle, 3=Ring, 4=Pinky
const uint32_t FINGER_TRAVEL_COST_MS[5] = {
    80,   // Thumb (3 keys)
    100,  // Index (2 keys)
    110,  // Middle (2 keys)
    150,  // Ring (2 keys)
    130   // Pinky (1 key)
};

// Press costs indexed by [finger][key_position]
// Thumb has 3 keys (1,2,3), others have 2 keys (1,2) or 1 key (1)
const uint32_t FINGER_PRESS_COST_MS[5][4] = {
    {0, 60, 40, 60},  // Thumb: keys 1,2,3
    {0, 50, 130, 0},  // Index: keys 1,2
    {0, 55, 140, 0},  // Middle: keys 1,2
    {0, 60, 150, 0},  // Ring: keys 1,2
    {0, 70, 0, 0}     // Pinky: key 1
};

// Struct to represent finger positions (which key each finger is hovering over)
// Fingers always hover over a valid key position (never 0)
struct FingerPositions {
    uint8_t thumb;    // 1-3 (keys 1,2,3)
    uint8_t index;    // 1-2 (keys 1,2)
    uint8_t middle;   // 1-2 (keys 1,2)
    uint8_t ring;     // 1-2 (keys 1,2)
    uint8_t pinky;    // 1 (key 1)

    FingerPositions() : thumb(2), index(1), middle(1), ring(1), pinky(1) {}

    uint8_t get(int finger_idx) const {
        switch(finger_idx) {
            case 0: return thumb;
            case 1: return index;
            case 2: return middle;
            case 3: return ring;
            case 4: return pinky;
            default: return 1;
        }
    }

    void set(int finger_idx, uint8_t value) {
        switch(finger_idx) {
            case 0: thumb = value; break;
            case 1: index = value; break;
            case 2: middle = value; break;
            case 3: ring = value; break;
            case 4: pinky = value; break;
        }
    }
};

// Struct to represent a chord (which key each finger presses)
struct Chord {
    uint8_t thumb;    // 0 = not pressed, 1-3 = key position
    uint8_t index;    // 0 = not pressed, 1-2 = key position
    uint8_t middle;   // 0 = not pressed, 1-2 = key position
    uint8_t ring;     // 0 = not pressed, 1-2 = key position
    uint8_t pinky;    // 0 = not pressed, 1 = key position

    Chord() : thumb(0), index(0), middle(0), ring(0), pinky(0) {}

    // Constructor from string representation (e.g., "01010")
    explicit Chord(const std::string& str) {
        thumb = str.size() > 0 ? str[0] - '0' : 0;
        index = str.size() > 1 ? str[1] - '0' : 0;
        middle = str.size() > 2 ? str[2] - '0' : 0;
        ring = str.size() > 3 ? str[3] - '0' : 0;
        pinky = str.size() > 4 ? str[4] - '0' : 0;
    }

    uint8_t get(int finger_idx) const {
        switch(finger_idx) {
            case 0: return thumb;
            case 1: return index;
            case 2: return middle;
            case 3: return ring;
            case 4: return pinky;
            default: return 0;
        }
    }

    void set(int finger_idx, uint8_t key_pos) {
        switch(finger_idx) {
            case 0: thumb = key_pos; break;
            case 1: index = key_pos; break;
            case 2: middle = key_pos; break;
            case 3: ring = key_pos; break;
            case 4: pinky = key_pos; break;
        }
    }

    bool is_pressed(int finger_idx) const {
        return get(finger_idx) != 0;
    }

    bool is_all_released() const {
        return thumb == 0 && index == 0 && middle == 0 && ring == 0 && pinky == 0;
    }
};

// Struct to represent which fingers are currently pressed (bitfield)
// Uses 5 bits packed into a single uint8_t
struct FingerPressedState {
    uint8_t bits;  // bit 0=thumb, bit 1=index, bit 2=middle, bit 3=ring, bit 4=pinky

    FingerPressedState() : bits(0) {}

    bool get(int finger_idx) const {
        return (bits & (1 << finger_idx)) != 0;
    }

    void set(int finger_idx, bool pressed) {
        if (pressed) {
            bits |= (1 << finger_idx);
        } else {
            bits &= ~(1 << finger_idx);
        }
    }

    bool is_all_released() const {
        return bits == 0;
    }
};

class KeyerSimulator {
private:
    FingerPositions finger_position;
    FingerPressedState finger_pressed;
    int num_fingers;

    uint32_t get_travel_cost(int finger_idx) const {
        if (finger_idx >= 0 && finger_idx < 5) {
            return FINGER_TRAVEL_COST_MS[finger_idx];
        }
        return 100;
    }

    uint32_t get_press_cost(int finger_idx, int key_pos) const {
        if (finger_idx >= 0 && finger_idx < 5 && key_pos >= 0 && key_pos < 4) {
            uint32_t cost = FINGER_PRESS_COST_MS[finger_idx][key_pos];
            return cost != 0 ? cost : 50;
        }
        return 50;
    }

    uint32_t calculate_transition_cost(const Chord& target_chord) {
        uint32_t travel_cost = 0;
        uint32_t press_cost = 0;

        // Determine which fingers can stay pressed (using bitmask)
        // A finger can stay pressed if it's currently pressed AND at the same position as target
        uint8_t fingers_can_stay_pressed = 0;
        for (int i = 0; i < num_fingers; i++) {
            if (finger_pressed.get(i) &&
                finger_position.get(i) == target_chord.get(i) &&
                target_chord.get(i) != 0) {
                fingers_can_stay_pressed |= (1 << i);
            }
        }

        // Check if any finger is naturally being released
        bool any_finger_released = false;
        for (int i = 0; i < num_fingers; i++) {
            if (finger_pressed.get(i) && target_chord.get(i) == 0) {
                any_finger_released = true;
                break;
            }
        }

        // Check if any finger will be newly pressed
        bool any_finger_pressed = false;
        for (int i = 0; i < num_fingers; i++) {
            if (!finger_pressed.get(i) && target_chord.get(i) != 0) {
                any_finger_pressed = true;
                break;
            }
        }

        // If no finger is naturally released and no finger will be newly pressed,
        // we must force a release+repress
        if (fingers_can_stay_pressed != 0 && !finger_pressed.is_all_released() &&
            (!any_finger_released || !any_finger_pressed)) {
            // Find the cheapest finger to release (cheapest to re-press)
            int cheapest_finger = -1;
            uint32_t cheapest_cost = UINT32_MAX;

            for (int i = 0; i < num_fingers; i++) {
                if (fingers_can_stay_pressed & (1 << i)) {
                    uint32_t cost = get_press_cost(i, target_chord.get(i));
                    if (cost < cheapest_cost) {
                        cheapest_cost = cost;
                        cheapest_finger = i;
                    }
                }
            }

            // Remove cheapest finger from stay-pressed bitmask
            if (cheapest_finger >= 0) {
                fingers_can_stay_pressed &= ~(1 << cheapest_finger);
            }
        }

        // Calculate new finger positions and costs
        FingerPositions new_finger_position = finger_position;
        FingerPressedState new_finger_pressed;

        for (int finger_idx = 0; finger_idx < num_fingers; finger_idx++) {
            uint8_t current_pos = finger_position.get(finger_idx);
            uint8_t target_key = target_chord.get(finger_idx);

            // Check if this finger can stay pressed
            bool can_stay = (fingers_can_stay_pressed & (1 << finger_idx)) != 0;

            if (can_stay) {
                new_finger_pressed.set(finger_idx, true);
                continue;
            }

            // Determine where the finger needs to go
            uint8_t target_pos;
            if (target_key == 0) {
                // Finger is released but stays at current position
                target_pos = current_pos;
                new_finger_pressed.set(finger_idx, false);
            } else {
                // Finger needs to move to the target key position
                target_pos = target_key;
                new_finger_pressed.set(finger_idx, true);
            }

            // Calculate travel cost if finger position changes
            if (current_pos != target_pos) {
                uint32_t base_travel_cost = get_travel_cost(finger_idx);

                // For thumb (finger 0), increase cost for long-distance travel
                if (finger_idx == 0) {
                    int distance = std::abs(static_cast<int>(target_pos) - static_cast<int>(current_pos));
                    if (distance > 1) {
                        base_travel_cost *= 2;
                    }
                }

                travel_cost += base_travel_cost;
                new_finger_position.set(finger_idx, target_pos);
            }

            // Calculate press cost if finger is being pressed down
            if (target_key != 0) {
                press_cost += get_press_cost(finger_idx, target_key);
            }
        }

        // Update state
        finger_position = new_finger_position;
        finger_pressed = new_finger_pressed;

        return travel_cost + press_cost;
    }

public:
    KeyerSimulator(int n_fingers = 5) : num_fingers(n_fingers) {
        reset();
    }

    void reset() {
        finger_position = FingerPositions();  // Default: 21111
        finger_pressed = FingerPressedState();  // Default: all released
    }

    uint32_t type_text(const std::string& text,
                       const std::vector<Chord> key_map[256]) {
        reset();
        uint32_t total_cost = 0;

        for (char ch : text) {
            unsigned char idx = static_cast<unsigned char>(ch);
            const std::vector<Chord>& available_chords = key_map[idx];

            if (available_chords.empty()) {
                continue;  // Skip characters not in layout
            }

            // Try all available chords and pick the best one
            uint32_t min_cost = UINT32_MAX;
            FingerPositions best_position;
            FingerPressedState best_pressed;

            for (const Chord& chord : available_chords) {
                // Save state
                FingerPositions saved_position = finger_position;
                FingerPressedState saved_pressed = finger_pressed;

                // Try this chord
                uint32_t cost = calculate_transition_cost(chord);

                if (cost < min_cost) {
                    min_cost = cost;
                    best_position = finger_position;
                    best_pressed = finger_pressed;
                }

                // Restore state to try next chord
                finger_position = saved_position;
                finger_pressed = saved_pressed;
            }

            // Apply best transition
            finger_position = best_position;
            finger_pressed = best_pressed;
            total_cost += min_cost;
        }

        return total_cost;
    }
};

// Python wrapper functions
static PyObject* score_layout(PyObject* self, PyObject* args) {
    PyObject* key_map_obj;
    const char* text;

    if (!PyArg_ParseTuple(args, "Os", &key_map_obj, &text)) {
        return NULL;
    }

    // Convert Python dict to C++ array (indexed by character code)
    std::vector<Chord> key_map[256];
    PyObject *key, *value;
    Py_ssize_t pos = 0;

    while (PyDict_Next(key_map_obj, &pos, &key, &value)) {
        // Get character key
        if (!PyUnicode_Check(key)) {
            PyErr_SetString(PyExc_TypeError, "Key must be a string");
            return NULL;
        }

        Py_ssize_t key_size;
        const char* key_str = PyUnicode_AsUTF8AndSize(key, &key_size);
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
            PyObject* chord_obj = PyList_GetItem(value, i);
            if (!PyUnicode_Check(chord_obj)) {
                PyErr_SetString(PyExc_TypeError, "Chord must be a string");
                return NULL;
            }

            const char* chord_str = PyUnicode_AsUTF8(chord_obj);
            key_map[ch].push_back(Chord(std::string(chord_str)));
        }
    }

    // Run simulation
    KeyerSimulator sim(5);
    uint32_t cost = sim.type_text(std::string(text), key_map);

    return PyLong_FromUnsignedLong(cost);
}

// Module methods
static PyMethodDef KeyerMethods[] = {
    {"score_layout", score_layout, METH_VARARGS,
     "Score a keyboard layout by simulating text input"},
    {NULL, NULL, 0, NULL}
};

// Module definition
static struct PyModuleDef keyermodule = {
    PyModuleDef_HEAD_INIT,
    "keyer_simulator_native",
    "Native C++ keyer simulator for performance",
    -1,
    KeyerMethods
};

// Module initialization
PyMODINIT_FUNC PyInit_keyer_simulator_native(void) {
    return PyModule_Create(&keyermodule);
}
