// Tutor logic - uses global state from state.js

// Session state (ephemeral - resets on page reload)
let practiceAlternations = false;

// Stats history for rolling WPM calculation
let statsHistory = []; // Array of {correct: boolean, time: number} for last N chars
let lastCharTime = null;

// Current exercise state (ephemeral - resets each exercise)
let targetText = "";
let typedText = "";

// Initialize
function init() {
  // Initialize rendering
  initRender();

  // Load progress from localStorage
  loadState();

  // Generate initial exercise
  generateExercise();

  // Set up event listeners
  setupEventListeners();

  // Initial render
  render();
}

function setupEventListeners() {
  const hiddenInput = document.getElementById("hiddenInput");

  // Focus hidden input on canvas click to trigger mobile keyboard
  document.getElementById("canvas").addEventListener("click", () => {
    hiddenInput.focus();
  });

  // Keep input focused and empty for continuous typing
  hiddenInput.addEventListener("input", (e) => {
    const key = e.data;
    if (key) {
      handleKeyPress(key);
    }
    // Clear the input immediately to prepare for next character
    hiddenInput.value = "";
  });

  // Prevent input from losing focus on mobile
  hiddenInput.addEventListener("blur", () => {
    setTimeout(() => hiddenInput.focus(), 0);
  });

  // Handle keyboard input (for desktop)
  document.addEventListener("keydown", (e) => {
    if (e.ctrlKey || e.metaKey || e.altKey) {
      return; // Allow browser shortcuts
    }

    e.preventDefault();
    handleKeyPress(e.key);
  });

  // Auto-focus on page load
  hiddenInput.focus();
}

function handleKeyPress(key) {
  // Handle Backspace - delete last character
  if (key === "Backspace") {
    if (typedText.length > 0) {
      typedText = typedText.slice(0, -1);
      // Remove last entry from stats history
      if (statsHistory.length > 0) {
        statsHistory.pop();
      }
      render();
    }
    return;
  }

  // Handle Escape - reset current exercise
  if (key === "Escape") {
    typedText = "";
    lastCharTime = null;
    render();
    return;
  }

  // Ignore other special keys (modifier keys, navigation keys, etc.)
  const specialKeys = [
    "Shift",
    "Control",
    "Alt",
    "Meta",
    "Tab",
    "Enter",
    "Delete",
    "ArrowLeft",
    "ArrowRight",
    "ArrowUp",
    "ArrowDown",
    "CapsLock",
    "NumLock",
    "ScrollLock",
    "Pause",
    "Insert",
    "Home",
    "End",
    "PageUp",
    "PageDown",
    "F1",
    "F2",
    "F3",
    "F4",
    "F5",
    "F6",
    "F7",
    "F8",
    "F9",
    "F10",
    "F11",
    "F12",
  ];

  if (specialKeys.includes(key) || key.length > 1) {
    return; // Ignore multi-character key names
  }

  const now = Date.now();

  const expectedChar = targetText[typedText.length];
  const correct = key === expectedChar;

  if (correct) {
    typedText += key;
    playGood();
  } else {
    // Wrong key - still advance but mark as incorrect
    typedText += key;
    playBad();
  }

  // Calculate time since last character (in seconds)
  if (lastCharTime !== null) {
    const timeSinceLastChar = (now - lastCharTime) / 1000;
    // Clamp to maximum of 5 seconds
    const clampedTime = Math.min(timeSinceLastChar, 5);

    // Add to stats history
    statsHistory.push({ correct, time: clampedTime });

    // Keep only last N characters
    if (statsHistory.length > statsHistoryWindowSize) {
      statsHistory.shift();
    }
  }

  lastCharTime = now;

  // Check if exercise is complete
  if (typedText.length >= targetText.length) {
    // Check if we should advance based on performance
    if (statsHistory.length >= statsHistoryWindowSize) {
      const wpm = getCurrentWPM();
      const accuracy = getCurrentAccuracy() / 100; // Convert to decimal

      if (wpm >= targetWPM && accuracy >= targetAccuracy) {
        playLevelUp();
        if (practiceAlternations) {
          // End of alterations practice
          // Switch to dictionary-based practice
          practiceAlternations = false;
        } else {
          // End of dictionary practice
          // Advance to next character
          oldIndex++;
          practiceAlternations = true;
        }
        if (oldIndex >= newIndex) {
          oldIndex = -1; // special case for training key repeat
          newIndex++;
        }
        if (newIndex == learning_sequence.length - 1) {
          newIndex = 0;
          oldIndex = 0;
          alert("Congratulations! You completed the entire layout!");
        }
        saveState();
        // Clear stats history after advancing
        statsHistory = [];
      }
    }

    generateExercise();
  }

  render();
}

function generateExercise() {
  if (oldIndex == -1) {
    const repeatCount = 7;
    targetText = learning_sequence[newIndex].repeat(repeatCount);
  } else {
    const newChar = learning_sequence[newIndex];
    const oldChar = learning_sequence[oldIndex];
    if (practiceAlternations) {
      // NEW-OLD alternating
      const pairs = 5;
      let text = "";
      for (let i = 0; i < pairs; i++) {
        text += newChar + oldChar;
      }
      targetText = text;
    } else {
      // Random words using known characters and NEW/OLD transitions
      const words = pickWordsForPractice(3);
      targetText = words.join(" ");
    }
  }

  typedText = "";
  lastCharTime = null; // Reset timing for new exercise
  // Note: statsHistory is NOT reset - it persists across exercises for rolling WPM calculation
}

function getCurrentWPM() {
  // Need at least 1 entry in history (represents 2 characters)
  if (statsHistory.length < 1) {
    return 0;
  }

  // Sum up total time from stats history
  const totalTime = statsHistory.reduce((sum, stat) => sum + stat.time, 0);

  if (totalTime === 0) {
    return 0;
  }

  const wordsTyped = statsHistory.length / 5;
  const wpm = (wordsTyped / totalTime) * 60;
  return Math.round(wpm);
}

function getCurrentAccuracy() {
  if (statsHistory.length === 0) {
    return 100;
  }

  const correctCount = statsHistory.filter((stat) => stat.correct).length;
  return Math.round((correctCount / statsHistory.length) * 100);
}

// Initialize when page loads
window.addEventListener("load", init);
