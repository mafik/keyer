#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

constexpr bool DEBUG = false;

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
  uint8_t finger_to_row[NUM_FINGERS] = {1}; // thumb over second row

  // Constructor from string representation (e.g., "01010")
  static Fingers FromChord(const char *str) {
    Fingers state = {};
    for (int i = 0; i < NUM_FINGERS; ++i) {
      int row = str[i] - '0' - 1;
      if (row >= 0) {
        state.pressed |= (1 << i);
        state.finger_to_row[i] = row;
      }
    }
    return state;
  }

  int get(int finger_idx) const { return finger_to_row[finger_idx]; }

  void set(uint8_t finger_idx, uint8_t new_row) {
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
    bool re_press_needed = pressed != 0;

    // Move the fingers to their target positions
    Bitmask fingers_to_move = target.pressed;
    while (fingers_to_move) {
      int finger_to_move = std::countr_zero(fingers_to_move);
      fingers_to_move &= ~(1 << finger_to_move);
      int current_position = get(finger_to_move);
      int target_position = target.get(finger_to_move);
      int distance = current_position - target_position;
      if (distance) {
        if (is_pressed(finger_to_move)) {
          if constexpr (DEBUG) {
            printf("  Activating previous position with a move\n");
          }
          re_press_needed = false;
          release_idx(finger_to_move);
        }
        set(finger_to_move, target_position);
        if constexpr (DEBUG) {
          printf("  Finger %d moving from %d to %d\n", finger_to_move,
                 current_position, target_position);
        }
        cost += FINGER_TRAVEL_COST_MS[finger_to_move] * abs(distance);
      }
    }

    Bitmask simple_release = pressed & ~target.pressed;
    // Release at least one finger (to finish the previous chord - if needed).
    if (re_press_needed) {
      // Plan A - let's see if there is a natural release from the current
      // `fingers` to `target_chord`. This is only possible if the target
      // chord also has some new finger press.
      Bitmask new_press = target.pressed & ~pressed;

      if (simple_release && new_press) {
        // Very nice, rolling motion. We don't have to re-press any fingers.
        if constexpr (DEBUG) {
          printf("  Activating previous position with a roll\n");
        }
      } else {
        // The last resort.
        // We have to release some of the currently held fingers and
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

        if constexpr (DEBUG) {
          printf("  Activating previous position with a re-press\n");
        }

        // Replicate the current behavior - just release the cheapest finger
        release_idx(best_re_press_finger);

        // Extra penalty for releasing a finger that's going to be pressed
        // again. This is the part that makes the generated layouts use the
        // "finger-walking" chords.
        cost += best_re_press_cost * 2;
      }
    }

    // We have to release those fingers anyway so let's do it now.
    pressed &= ~simple_release;

    // Step 3: Press the fingers
    Bitmask fingers_to_press = target.pressed & ~pressed;
    while (fingers_to_press) {
      int finger_to_press = std::countr_zero(fingers_to_press);
      fingers_to_press &= ~(1 << finger_to_press);
      press_idx(finger_to_press);
      if constexpr (DEBUG) {
        printf("  Finger %d at %d pressing down\n", finger_to_press,
               target.get(finger_to_press));
      }
      cost +=
          FINGER_PRESS_COST_MS[finger_to_press][target.get(finger_to_press)];
    }

    return cost;
  }
};

uint64_t type_text(const char *text, const std::vector<Fingers> key_map[256]) {
  Fingers fingers = {};
  uint64_t total_cost = 0;

  while (*text) {
    unsigned char idx = static_cast<unsigned char>(*text++);
    const std::vector<Fingers> &available_chords = key_map[idx];

    if (available_chords.empty()) {
      // Unknown key - let's reset the finger position back to default
      fingers = {};
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
