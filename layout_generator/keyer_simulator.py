#!/usr/bin/env python3
"""
Provides KeyerLayout class and constants.
"""

from typing import Dict, List

# Number of keys per finger
# 0=Thumb: 3 keys, 1=Index: 2 keys, 2=Middle: 2 keys, 3=Ring: 2 keys, 4=Pinky: 1 key
FINGER_KEY_COUNT = {
    0: 3,  # Thumb
    1: 2,  # Index
    2: 2,  # Middle
    3: 2,  # Ring
    4: 1,  # Pinky
}


class KeyerLayout:
    """Represents a keyboard layout with chord-to-character mappings."""

    def __init__(self, num_fingers: int = 5, key_map: Dict[str, List[str]] = None):
        """
        Initialize a keyboard layout.

        Args:
            num_fingers: Number of fingers to use (default 5)
            key_map: Dictionary mapping characters to lists of chord strings
        """
        self.num_fingers = num_fingers
        self.key_map = key_map if key_map is not None else {}

    def __repr__(self):
        return f"KeyerLayout(num_fingers={self.num_fingers}, chars={len(self.key_map)})"
