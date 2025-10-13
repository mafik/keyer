// Finger motion planner based on fingers.cpp algorithm
// Converts a text string into a sequence of finger actions

class Fingers {
  constructor(numFingers) {
    this.numFingers = numFingers;
    this.pressed = 0; // Bitmask of pressed fingers
    this.finger_to_row = new Array(numFingers).fill(0); // Position of each finger
  }

  static fromChord(str) {
    const numFingers = str.length;
    const state = new Fingers(numFingers);
    for (let i = 0; i < numFingers; i++) {
      const row = parseInt(str[i]) - 1;
      if (row >= 0) {
        state.pressed |= 1 << i;
        state.finger_to_row[i] = row;
      }
    }
    return state;
  }

  get(finger_idx) {
    return this.finger_to_row[finger_idx];
  }

  set(finger_idx, new_row) {
    this.finger_to_row[finger_idx] = new_row;
  }

  isPressed(finger_idx) {
    return (this.pressed & (1 << finger_idx)) !== 0;
  }

  releaseIdx(finger_idx) {
    this.pressed &= ~(1 << finger_idx);
  }

  pressIdx(finger_idx) {
    this.pressed |= 1 << finger_idx;
  }

  copy() {
    const newState = new Fingers(this.numFingers);
    newState.pressed = this.pressed;
    newState.finger_to_row = [...this.finger_to_row];
    return newState;
  }

  // Returns array of actions: [pressActions, releaseActions]
  transitionTo(target) {
    const pressActions = new Array(this.numFingers).fill("");
    const releaseActions = new Array(this.numFingers).fill("");

    let re_press_needed = this.pressed !== 0;
    let finger_released_by_move = -1;

    // Step 1: Move fingers to target positions
    let fingers_to_move = target.pressed;
    for (let finger_idx = 0; finger_idx < this.numFingers; finger_idx++) {
      if (!(fingers_to_move & (1 << finger_idx))) continue;

      const current_position = this.get(finger_idx);
      const target_position = target.get(finger_idx);
      const distance = current_position - target_position;

      if (distance !== 0) {
        if (this.isPressed(finger_idx)) {
          re_press_needed = false;
          this.releaseIdx(finger_idx);
          finger_released_by_move = finger_idx;
        }
        this.set(finger_idx, target_position);
      }
    }

    const simple_release = this.pressed & ~target.pressed;
    const new_press = target.pressed & ~this.pressed;

    // Step 2: Determine which finger to release (if re-press needed)
    let finger_to_repress = -1;
    if (re_press_needed) {
      if (simple_release && new_press) {
        // Rolling motion - no re-press needed
      } else {
        // Find cheapest finger to re-press
        let re_press_candidates = this.pressed & target.pressed;
        let best_re_press_finger = -1;
        let best_re_press_cost = Infinity;

        for (let i = 0; i < this.numFingers; i++) {
          if (re_press_candidates & (1 << i)) {
            const cost = fingerPressCost[i][this.get(i)];
            if (cost < best_re_press_cost) {
              best_re_press_cost = cost;
              best_re_press_finger = i;
            }
          }
        }

        if (best_re_press_finger >= 0) {
          finger_to_repress = best_re_press_finger;
          this.releaseIdx(best_re_press_finger);
        }
      }
    }

    // Release fingers that aren't in target
    this.pressed &= ~simple_release;

    // Step 3: Set press actions (move to positions)
    for (let i = 0; i < this.numFingers; i++) {
      if (target.isPressed(i)) {
        const row = target.get(i);
        pressActions[i] = String(row + 1); // Convert 0-based to 1-based
      }
    }

    // Step 4: Set release actions (which fingers to release to activate chord)
    // First, mark all currently pressed fingers as "hold"
    for (let i = 0; i < this.numFingers; i++) {
      if (this.isPressed(i)) {
        releaseActions[i] = "hold";
      }
    }

    // Then mark naturally released fingers
    for (let i = 0; i < this.numFingers; i++) {
      if (simple_release & (1 << i)) {
        releaseActions[i] = "release";
      }
    }

    // Mark finger released by movement
    if (finger_released_by_move >= 0) {
      releaseActions[finger_released_by_move] = "release";
    }

    // Mark finger that needs to be re-pressed
    if (finger_to_repress >= 0) {
      releaseActions[finger_to_repress] = "release";
    }

    // Step 5: Press the target fingers
    for (let i = 0; i < this.numFingers; i++) {
      if (target.isPressed(i) && !this.isPressed(i)) {
        this.pressIdx(i);
      }
    }

    return [releaseActions, pressActions];
  }
}

// Plans finger motions for typing the given text using the global layout.
// Returns an array of finger actions, alternating between press and release phases.
//
// Each element is an array of 5 strings (one per finger), where:
// - Even indices (0, 2, 4, ...): PRESS actions - which button to press ("1", "2", "3") or "" if no action
// - Odd indices (1, 3, 5, ...): RELEASE actions - "release" to activate chord, "hold" to keep pressed, or "" if not involved
//
// The array structure: [pressForChar1, releaseForChar1, pressForChar2, releaseForChar2, ..., finalRelease]
// - First element: press actions to prepare the first character
// - Last element: release actions to activate the final character
// - Total length: 2 * text.length
function fingerPlan(text) {
  // Infer number of fingers from the first chord in the global layout
  const firstChord = Object.values(layout)[0];
  const numFingers = firstChord.length;

  const fingers = new Fingers(numFingers);
  const motions = [];

  for (let i = 0; i < text.length; i++) {
    const char = text[i];
    const chord = layout[char];

    if (!chord) {
      console.warn(`Character '${char}' not found in layout`);
      continue;
    }

    const target = Fingers.fromChord(chord);
    const [releaseActions, pressActions] = fingers.transitionTo(target);

    // Skip the first release action (always empty for first chord)
    if (i > 0) {
      motions.push(releaseActions);
    }
    motions.push(pressActions);
  }

  // Add final release to activate the last chord
  const finalRelease = new Array(numFingers).fill("");
  for (let i = 0; i < numFingers; i++) {
    if (fingers.isPressed(i)) {
      finalRelease[i] = "release";
    }
  }
  motions.push(finalRelease);

  return motions;
}
