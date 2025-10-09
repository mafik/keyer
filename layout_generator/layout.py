#!/usr/bin/env python3
"""
Layout file I/O utilities for chording keyboard layouts.

Provides functions to read and write keyboard layout files in the best_layout.txt format.
"""

from typing import Dict


def load_layout(filepath: str) -> Dict[str, str]:
    """
    Load layout from best_layout.txt format.

    Args:
        filepath: Path to the layout file

    Returns:
        Dictionary mapping characters to their primary chord
    """
    layout = {}

    with open(filepath, "r", encoding="utf-8") as f:
        in_assignments = False
        for line in f:
            line = line.strip()

            if line.startswith("Chord Assignments:"):
                in_assignments = True
                continue

            if not in_assignments or not line:
                continue

            # Skip separator lines (lines with only = or - characters)
            if line.startswith("=") and "=" * 10 in line:
                continue
            if line.startswith("-") and "-" * 10 in line:
                continue

            # Parse lines like: "a     -> 0011"
            if "->" in line:
                parts = line.split("->", 1)  # Split only on first ->
                char_part = parts[0]
                chord_part = parts[1].strip()

                # Handle special characters
                stripped = char_part.strip()
                if (
                    stripped.startswith("'")
                    and stripped.endswith("'")
                    and len(stripped) > 2
                ):
                    # Handle quoted strings like '\t', '\n', etc. (more than just quotes)
                    try:
                        char = eval(stripped)
                    except SyntaxError:
                        # Fallback to first character
                        char = char_part.lstrip()[0] if char_part.lstrip() else None
                else:
                    # For unquoted chars (or single quote), take the first non-space character
                    # (handles control characters like backspace that appear before ->)
                    char_part_stripped = char_part.lstrip()
                    if char_part_stripped:
                        # Take first character before stripping trailing spaces
                        char = char_part_stripped[0]
                    else:
                        # Empty, skip
                        continue

                if char is None or len(char) != 1:
                    continue

                # Take only the first chord (ignore aliases)
                chord = chord_part.split(",")[0].strip()

                layout[char] = chord

    return layout


def save_layout(
    layout: Dict[str, str],
    score: float,
    corpus_length: int,
    generation: int,
    filepath: str = "mutated_layout.txt",
):
    """
    Save layout to file in best_layout.txt format.

    Args:
        layout: Layout to save (char -> chord mapping)
        score: Layout score in milliseconds
        corpus_length: Length of corpus used for scoring
        generation: Generation number
        filepath: Output file path
    """
    with open(filepath, "w") as f:
        f.write("Best Keyboard Layout\n")
        f.write("=" * 60 + "\n")
        f.write(f"Generation: {generation}\n")
        f.write(f"Cost: {score:.1f}ms\n")
        f.write(f"Average cost per character: {score / corpus_length:.2f}ms\n")
        f.write("\nChord Assignments:\n")
        f.write("-" * 60 + "\n")

        # Sort by character
        for char in sorted(layout.keys()):
            chord = layout[char]
            char_repr = repr(char) if char in ["\n", "\t", " "] else char
            f.write(f"{char_repr:5s} -> {chord}\n")

        f.write("\n" + "=" * 60 + "\n")
        f.write(f"Total unique characters: {len(layout)}\n")
        f.write(f"Total chord assignments: {len(layout)}\n")

    print(f"Saved layout to {filepath}")
