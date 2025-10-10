Goal - a web-based typing tutor for a keyer (one-handed chording keyboard)

1. Pass the layout to the Tutor as a JSON file (layout.json)
2. Identify the most important patterns
   - single-key chords
     - require all fingers to be lifted BEFORE they're pressed
     - require all fingers to be lifted AFTER they're pressed
     - these are 'e', 'r', '-', '=', ' ', 'x', 'z'
     - these don't require much muscle memory - you just press them in isolation
   - patterns where a single finger switches position to a different key
   - patterns where some fingers are lifted and some are pressed
   - patterns where an extra finger has to press a key
     - some finger must be released
     - then the extra finger + an additional finger must be pressed
   - patterns where some finger has to be released
     - multiple fingers (the diff + an extra one) must be released
     - the extra finger must be pressed
3. How the tutor could work
   - canvas-based rendering
   - get a sequence of keys to learn
   - while learning it would maintain two positions within this sequence:
     - `new_i` the position of the new key that is being learned now
     - `old_i` a position of some other key that is already known
   - the sequence of keys to learn can be shown in the corner of the screen
   - below each key there will be a legend showing its chord
   - the `new_i` and `old_i` will be marked on this canvas
   - training loop will have three phases:
     - train <NEW><NEW><NEW>...
     - train <NEW><OLD><NEW><OLD>
     - train <random word that only includes known bigrams and some NEW/OLD or OLD/NEW transition>
   - once the user maintains a target WPM & accuracy for some time, the tutor will advance the training
     - advance the phase to the next once
     - when at the last phase, advance the old_i to the next one
     - when old_i reaches new_i, advance new_i to the next one and set old_i to 0
   - the tutor will eventually also show the visualization of the optimal finger motion for a given word
4. Code organization:
   index.html - main file
   layout.js - layout, defined by the user
   render.js - takes in an object that represents the current training state & draws the interface on the canvas
   word_picker.js - logic for picking words that only includes known bigrams and at least one NEW/OLD or OLD/NEW transition (takes in learning_sequence, old_i, new_i)
   tutor.js - logic for getting the key events, picking a sequence of words (from word_picker.js), calling render.js to draw it on screen, keeping track of the current WPM & accuracy & saving the progress to local storage
