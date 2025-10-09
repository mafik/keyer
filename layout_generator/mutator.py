#!/usr/bin/env python3
"""
Layout mutator for chording keyboard optimization.

Provides a generator function that produces all valid single-swap mutations
of a given layout while maintaining layout invariants.
"""

from typing import Dict, Set, Optional, Generator
from xxlimited import new


# Thumb alternative mappings (base_key -> thumb_variant)
THUMB_ALTERNATIVES = {
    "\t": "U",  # Alt+Tab
    "c": "C",  # Ctrl+C
    "v": "V",  # Ctrl+V
    "x": "X",  # Ctrl+X
    "z": "Z",  # Ctrl+Z
    "\n": "B",  # Win+Enter
}

THUMB_KEYS = set(THUMB_ALTERNATIVES.keys())

# Keys with fixed positions that cannot be swapped
FIXED_KEYS = {"D", "F", "I", "J", "K", "M", "H", "Q", "G", "P", "R", "\x08"}
FIXED_KEYS.update(THUMB_ALTERNATIVES.values())


def get_all_possible_chords(num_fingers: int = 4) -> Set[str]:
    """
    Generate all possible chords for the keyboard.

    Args:
        num_fingers: Number of fingers (default 4)

    Returns:
        Set of all valid chord strings
    """
    # Finger key counts: thumb=3, index=2, middle=2, ring=2
    finger_keys = [3, 2, 2, 2][:num_fingers]

    chords = []

    def generate_recursive(finger_idx: int, current: str):
        if finger_idx == num_fingers:
            # At least one key must be pressed
            if any(c != "0" for c in current):
                chords.append(current)
            return

        num_keys = finger_keys[finger_idx]
        for pos in range(num_keys + 1):
            generate_recursive(finger_idx + 1, current + str(pos))

    generate_recursive(0, "")
    return set(chords)


all_chords = tuple(get_all_possible_chords())


def mutate_layout(layout: Dict[str, str]) -> Generator[Dict[str, str], None, None]:
    """
    Generate all valid single-swap mutations of a layout.

    This generator produces all possible layouts that differ from the input
    by exactly one swap, while respecting these invariants:
    - Fixed keys cannot be moved
    - Thumb alternatives must have compatible chords (same but with thumb=3)
    - Can swap assigned chords with unassigned chords
    - Can swap two assigned non-fixed characters

    Args:
        layout: Current layout (char -> chord mapping)

    Yields:
        Dict[str, str]: Each valid mutated layout
    """

    layout_reverse = {chord: "" for chord in all_chords}
    for char, chord in layout.items():
        layout_reverse[chord] = char

    for i1 in range(len(all_chords)):
        chord1 = all_chords[i1]
        for i2 in range(i1 + 1, len(all_chords)):
            chord2 = all_chords[i2]
            key1 = layout_reverse[chord1]
            key2 = layout_reverse[chord2]
            if key1 == key2 or key1 in FIXED_KEYS or key2 in FIXED_KEYS:
                continue

            if key1 in THUMB_KEYS and key2 in THUMB_KEYS:
                # ok, just swap them both
                new_layout = layout.copy()
                new_layout[key1] = chord2
                new_layout[key2] = chord1
                new_layout[THUMB_ALTERNATIVES[key1]] = "3" + chord2[1:]
                new_layout[THUMB_ALTERNATIVES[key2]] = "3" + chord1[1:]
                yield new_layout
            elif key1 in THUMB_KEYS or key2 in THUMB_KEYS:
                if key1 in THUMB_KEYS:
                    key_thumb = key1
                    key_other = key2
                    chord_thumb = chord1
                    chord_other = chord2
                else:
                    key_thumb = key2
                    key_other = key1
                    chord_thumb = chord2
                    chord_other = chord1
                # can't move a thumb key onto a thumb layer
                if chord_other.startswith("3"):
                    continue
                # unfortunately, the thumb layer is occupied
                if layout_reverse["3" + chord_other[1:]]:
                    continue
                new_layout = layout.copy()
                new_layout[key_thumb] = chord_other
                new_layout[THUMB_ALTERNATIVES[key_thumb]] = "3" + chord_other[1:]
                if key_other:
                    new_layout[key_other] = chord_thumb
                yield new_layout
            else:
                new_layout = layout.copy()
                if key1:
                    new_layout[key1] = chord2
                if key2:
                    new_layout[key2] = chord1
                yield new_layout
