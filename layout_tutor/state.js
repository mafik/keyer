// Global state for the tutor - learning progress only (persistent)

let newIndex = 0; // Position of the new key being learned (start at 1 to skip space-only training)
let oldIndex = -1; // Position of an already-known key

// Thresholds for advancement
const targetWPM = 40;
const targetAccuracy = 0.9;
const statsHistoryWindowSize = 30; // Number of recent characters for WPM calculation

// Save state to localStorage
function saveState() {
  const state = {
    newIndex,
    oldIndex,
  };
  localStorage.setItem("keyer_tutor_progress", JSON.stringify(state));
}

// Load state from localStorage
function loadState() {
  const saved = localStorage.getItem("keyer_tutor_progress");
  if (saved) {
    try {
      const state = JSON.parse(saved);
      newIndex = state.newIndex || 0;
      oldIndex = state.oldIndex || -1;
      // phase and recentPerformance are not loaded - they reset on page reload
    } catch (e) {
      console.error("Failed to load progress:", e);
    }
  }
}

// Reset state to initial values
function resetState() {
  newIndex = 0;
  oldIndex = -1;
  saveState();
}
