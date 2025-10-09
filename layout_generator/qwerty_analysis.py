#!/usr/bin/env python3
"""
QWERTY keyboard analysis utilities.

Functions for converting text to QWERTY key sequences and analyzing bigram statistics.
"""

import glob
from collections import Counter
from typing import Dict, Tuple


def QwertyKeys(
    text: str, modifiers: dict[str, str] = {"Shift": "H", "Alt": "T"}
) -> str:
    """
    Convert a string into QWERTY key sequence representation.

    Args:
        text: Input string to convert
        modifiers: a dictionary that maps Shift and Alt to replacement strings

    Returns:
        String representing the physical keys pressed

    Examples:
        >>> QwertyKeys("Test_123%łóąć", True)
        'StestS-123S5AlAoAaAc'
        >>> QwertyKeys("Test_123%łóąć", False)
        'test-1235loac'
    """
    # QWERTY layout mappings
    base_keys = {
        # Letters (lowercase)
        **{chr(i): chr(i) for i in range(ord("a"), ord("z") + 1)},
        # Numbers
        **{chr(i): chr(i) for i in range(ord("0"), ord("9") + 1)},
        # Special characters (unshifted)
        "`": "`",
        "-": "-",
        "=": "=",
        "[": "[",
        "]": "]",
        "\\": "\\",
        ";": ";",
        "'": "'",
        ",": ",",
        ".": ".",
        "/": "/",
        " ": " ",
        "\t": "\t",
        "\n": "\n",
        "\r": "\r",
    }

    shifted_keys = {
        # Shifted letters
        **{chr(i): chr(i).lower() for i in range(ord("A"), ord("Z") + 1)},
        # Shifted numbers
        "!": "1",
        "@": "2",
        "#": "3",
        "$": "4",
        "%": "5",
        "^": "6",
        "&": "7",
        "*": "8",
        "(": "9",
        ")": "0",
        # Shifted special characters
        "~": "`",
        "_": "-",
        "+": "=",
        "{": "[",
        "}": "]",
        "|": "\\",
        ":": ";",
        '"': "'",
        "<": ",",
        ">": ".",
        "?": "/",
    }

    # Polish characters with Alt (US International or Polish layout)
    alt_keys = {
        "ą": "a",
        "ć": "c",
        "ę": "e",
        "ł": "l",
        "ń": "n",
        "ó": "o",
        "ś": "s",
        "ź": "v",
        "ż": "z",
        "Ą": "a",
        "Ć": "c",
        "Ę": "e",
        "Ł": "l",
        "Ń": "n",
        "Ó": "o",
        "Ś": "s",
        "Ź": "v",
        "Ż": "z",
    }

    result = []

    for char in text:
        if char in alt_keys:
            result.append(modifiers["Alt"])
            char = alt_keys[char]
        if char in shifted_keys:
            result.append(modifiers["Shift"])
            char = shifted_keys[char]
        if char in base_keys:
            result.append(char)
        else:
            # Unknown character, skip it
            pass

    return "".join(result)
