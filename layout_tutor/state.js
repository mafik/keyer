// Global state for the tutor - learning progress only (persistent)

let newIndex = 0; // Position of the new key being learned (start at 1 to skip space-only training)
let oldIndex = -1; // Position of an already-known key
let targetWPM = 10; // Starting target WPM, increases by 5 after each completion

// Thresholds for advancement
const targetAccuracy = 0.9;
const statsHistoryWindowSize = 30; // Number of recent characters for WPM calculation

// Debug logs (ephemeral - for mobile debugging)
let debugLogs = [];

// Save state to localStorage
function saveState() {
  const state = {
    newIndex,
    oldIndex,
    targetWPM,
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
      targetWPM = state.targetWPM || 10;
      // phase and recentPerformance are not loaded - they reset on page reload
    } catch (e) {
      addLog("Failed to load progress: " + e);
    }
  }
}

// Reset state to initial values
function resetState() {
  newIndex = 0;
  oldIndex = -1;
  targetWPM = 10;
  saveState();
}

// Add a debug log message
function addLog(message) {
  debugLogs.unshift(message); // Add to the beginning (top)
  if (debugLogs.length > 10) {
    debugLogs = debugLogs.slice(0, 10); // Keep only last 10 messages
  }
}
