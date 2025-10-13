#!/usr/bin/env python3
"""
Keyboard layout planner and optimizer.

Uses Ant Colony Optimization to generate and optimize chording keyboard layouts
by scoring against real text corpus.
"""

import glob
import random
from typing import List, Dict, Set, Tuple
from collections import Counter
from multiprocessing import Pool, cpu_count

from qwerty_analysis import QwertyKeys
from keyer_simulator import KeyerLayout, FINGER_KEY_COUNT
import keyer_simulator_native


# version with single-chord alt variants for polish letters "best_pl.txt"
# ogonki = "acelnosvz\t\n"

# version with single-key combos for Ctrl+C, Ctrl+V, Ctrl+X, Ctrl+Z
ogonki = ""

ogonki_chords = [
    "000",
    "001",
    "002",
    "010",
    "020",
    "100",
    "110",
    "111",
    "112",
    "120",
    "122",
    "200",
    "210",
    "221",
]


def generate_all_possible_chords(num_fingers: int = 5) -> List[str]:
    """
    Generate all possible valid chords for a 5-finger keyboard.

    Physical layout:
    - Thumb (finger 0): 3 keys (1, 2, 3)
    - Index (finger 1): 2 keys (1, 2)
    - Middle (finger 2): 2 keys (1, 2)
    - Ring (finger 3): 2 keys (1, 2)
    - Pinky (finger 4): 1 key (1)

    A chord must have at least one key pressed.

    Returns:
        List of all valid chord strings
    """
    chords = []

    def generate_recursive(finger_idx: int, current_chord: str):
        if finger_idx == num_fingers:
            # Check if at least one key is pressed
            if any(c != "0" for c in current_chord):
                chords.append(current_chord)
            return

        # Get number of keys for this finger
        num_keys = FINGER_KEY_COUNT.get(finger_idx, 1)

        # Try not pressing (0) and all possible key positions (1, 2, 3, ...)
        for key_pos in range(num_keys + 1):
            generate_recursive(finger_idx + 1, current_chord + str(key_pos))

    generate_recursive(0, "")
    return chords


def generate_random_layout(characters: Set[str], num_fingers: int = 5) -> KeyerLayout:
    """
    Generate a random keyboard layout by assigning chords to characters.

    Args:
        characters: Set of characters that need to be typeable
        num_fingers: Number of fingers (default 5)

    Returns:
        KeyerLayout with random chord assignments
    """
    # Generate all possible chords
    all_chords = generate_all_possible_chords(num_fingers)

    # Shuffle chords for random assignment
    random.shuffle(all_chords)

    # Assign one chord per character
    key_map = {}
    for i, char in enumerate(sorted(characters)):
        if i < len(all_chords):
            key_map[char] = [all_chords[i]]
        else:
            # If we run out of chords, reuse random ones
            key_map[char] = [random.choice(all_chords)]

    return KeyerLayout(num_fingers=num_fingers, key_map=key_map)


def load_corpus(pattern: str = "corpus/*", qwerty_compatible: bool = False) -> str:
    """
    Load all files matching the pattern and combine.

    Args:
        pattern: Glob pattern for files to load (default "corpus/*")
        qwerty_compatible: If True, process with QwertyKeys (default False)

    Returns:
        Combined text/key sequences from all matching files
    """
    corpus = []
    files = glob.glob(pattern, recursive=True)

    for filepath in files:
        try:
            with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                content = f.read()
                if qwerty_compatible:
                    # Convert to QWERTY key sequences
                    content = QwertyKeys(content, modifiers={"Shift": "", "Alt": "T"})
                corpus.append(content)
        except (IOError, OSError) as e:
            print(f"Warning: Could not read {filepath}: {e}")
            continue

    return "".join(corpus)


def evaluate_layout(
    layout: KeyerLayout, key_sequence: str, verbose: bool = False
) -> float:
    """
    Evaluate a keyboard layout by scoring it against a key sequence.

    Args:
        layout: KeyerLayout to evaluate
        key_sequence: Key sequence to type
        verbose: If True, print detailed scoring information

    Returns:
        Total cost in milliseconds
    """
    if verbose:
        print(f"Key sequence size: {len(key_sequence)} keys")
        print(f"Layout has mappings for {len(layout.key_map)} characters")

    # Score the layout using native C++ simulator
    total_cost = keyer_simulator_native.score_layout(layout.key_map, key_sequence)

    if verbose:
        print(f"Total cost: {total_cost:.1f}ms")
        if len(key_sequence) > 0:
            print(f"Average cost per key: {total_cost / len(key_sequence):.2f}ms")

    return total_cost


def analyze_corpus_characters(key_sequence: str) -> Dict[str, int]:
    """
    Analyze which characters appear in the key sequence and their frequencies.

    Args:
        key_sequence: Key sequence to analyze

    Returns:
        Dictionary mapping characters to their occurrence counts
    """
    # Count character frequencies
    char_counts = Counter(key_sequence)

    return dict(char_counts)


class KeyboardLayoutAntGenerator:
    """
    Ant Colony Optimization generator for keyboard layouts.

    Maintains a pheromone matrix that maps characters to chords, and uses
    weighted random selection to generate layouts based on pheromone levels.
    """

    # Characters to use for visualizing pheromone levels
    PHEROMONE_BARS = " ▁▂▃▄▅▆▇█"

    def __init__(
        self,
        characters: List[str],
        chords: List[str],
        initial_pheromone: float = 1.0,
        evaporation_rate: float = 0.1,
        pheromone_boost: float = 1.0,
        forced_assignments: Dict[str, str] = None,
        assign_all_chords_as_aliases: bool = True,
    ):
        """
        Initialize the ant colony optimizer.

        Args:
            characters: List of characters that need chord assignments
            chords: List of all available chords
            initial_pheromone: Initial pheromone level for all paths
            evaporation_rate: Rate at which pheromone evaporates (0-1)
            pheromone_boost: Amount to boost pheromone for winning layout
            forced_assignments: Dict mapping characters to forced chords (optional)
            assign_all_chords_as_aliases: Bool to enable assignment of all chords
        """
        self.characters = sorted(characters)
        self.chords = chords
        self.forced_assignments = forced_assignments or {}
        self.assign_all_chords_as_aliases = assign_all_chords_as_aliases

        # Pheromone matrix: dict[(char, chord)] -> pheromone_level
        self.pheromone = {}
        for char in self.characters:
            for chord in self.chords:
                self.pheromone[(char, chord)] = initial_pheromone

        # Zero out pheromone for forced assignments to prevent aliases
        # For forced characters: zero all their pheromones except the forced chord
        # For forced chords: zero all characters except the forced one
        for forced_char, forced_chord in self.forced_assignments.items():
            if forced_char in self.characters:
                # Zero out entire row for this character (all chords)
                for chord in self.chords:
                    self.pheromone[(forced_char, chord)] = 0.0

                # Restore only the forced assignment
                if (forced_char, forced_chord) in self.pheromone:
                    self.pheromone[(forced_char, forced_chord)] = initial_pheromone

                # Zero out entire column for this chord (all characters)
                for char in self.characters:
                    self.pheromone[(char, forced_chord)] = 0.0

                # Restore only the forced assignment
                if (forced_char, forced_chord) in self.pheromone:
                    self.pheromone[(forced_char, forced_chord)] = initial_pheromone

        self.evaporation_rate = evaporation_rate
        self.pheromone_boost = pheromone_boost

    def _weighted_choice(self, items: List[any], weights: List[float]) -> any:
        """Weighted random choice using cumulative distribution."""
        total = sum(weights)
        normalized = [w / total for w in weights]
        cumulative = []
        acc = 0.0
        for w in normalized:
            acc += w
            cumulative.append(acc)

        r = random.random()
        for i, threshold in enumerate(cumulative):
            if r <= threshold:
                return items[i]
        return items[-1]

    def generate_layout(self, num_fingers: int = 5) -> KeyerLayout:
        """
        Generate a keyboard layout using pheromone-weighted random selection.

        First assigns forced chords, then assigns unique chords to remaining characters,
        then assigns remaining chords as aliases.

        Args:
            num_fingers: Number of fingers (default 5)

        Returns:
            KeyerLayout with pheromone-weighted chord assignments
        """
        key_map = {}
        used_chords = set()
        used_ogonki = set()

        # Phase 0: Assign forced chords first
        for char, forced_chord in self.forced_assignments.items():
            if char in self.characters:
                key_map[char] = [forced_chord]
                used_chords.add(forced_chord)

        # Phase 0.5: Assign ogonki to unique chords X012, X021, X101, X102, X112, X120, X201, X210, X211
        used_chords.add("0000")
        used_ogonki.add("100")
        used_ogonki.add("200")
        for char in ogonki:
            if char in key_map:
                continue
            available_chords = []
            for thumb in "012":
                for tail in ogonki_chords:
                    if thumb + tail in used_chords:
                        continue
                    if tail in used_ogonki:
                        continue
                    available_chords.append(thumb + tail)

            if not available_chords:
                break

            # Get pheromone weights for available chords
            weights = [self.pheromone[(char, chord)] for chord in available_chords]

            # Weighted random selection based on pheromone
            chosen_chord = self._weighted_choice(available_chords, weights)

            key_map[char] = [chosen_chord]
            used_chords.add(chosen_chord)
            used_ogonki.add(chosen_chord[-3:])

        for tail in used_ogonki:
            used_chords.add("3" + tail)  # Simulates the Alt version of the ogonek

        # Phase 1: Assign unique chords to remaining characters
        for char in self.characters:
            # Skip if already assigned via forced assignment
            if char in key_map:
                continue

            # Get pheromone levels for unused chords only
            available_chords = [c for c in self.chords if c not in used_chords]

            if not available_chords:
                break

            # Get pheromone weights for available chords
            weights = [self.pheromone[(char, chord)] for chord in available_chords]

            # Weighted random selection based on pheromone
            chosen_chord = self._weighted_choice(available_chords, weights)

            key_map[char] = [chosen_chord]
            used_chords.add(chosen_chord)

        # Phase 2: Assign remaining unused chords as aliases to characters
        if self.assign_all_chords_as_aliases:
            unused_chords = [c for c in self.chords if c not in used_chords]
            for chord in unused_chords:
                # Find which character has highest pheromone for this chord
                weights = [self.pheromone[(char, chord)] for char in self.characters]
                chosen_char = self._weighted_choice(self.characters, weights)

                # Add chord as an alias to the chosen character
                if chosen_char not in key_map:
                    key_map[chosen_char] = [chord]
                else:
                    key_map[chosen_char].append(chord)

        return KeyerLayout(num_fingers=num_fingers, key_map=key_map)

    def update_pheromones(self, winning_layout: KeyerLayout):
        """
        Update pheromone matrix based on winning layout.

        Evaporates pheromone everywhere, then boosts pheromone for the
        character-chord pairs present in the winning layout.

        Forced assignments are not affected by evaporation or boosting.

        Args:
            winning_layout: The best layout from the current generation
        """
        # Get all forced cells to protect them from updates
        forced_cells = set()
        for forced_char, forced_chord in self.forced_assignments.items():
            # Protected cells: the forced assignment itself
            forced_cells.add((forced_char, forced_chord))
            # Also protect zeroed rows and columns
            for char in self.characters:
                forced_cells.add((char, forced_chord))
            for chord in self.chords:
                forced_cells.add((forced_char, chord))

        # Evaporation: reduce all pheromone levels (except forced cells)
        for key in self.pheromone:
            if key not in forced_cells:
                self.pheromone[key] *= 1.0 - self.evaporation_rate
                # Ensure no pheromone goes to zero (minimum threshold)
                self.pheromone[key] = max(self.pheromone[key], 0.0001)

        # Boost pheromone for winning paths (except forced cells)
        for char, chords in winning_layout.key_map.items():
            for chord in chords:
                if (char, chord) in self.pheromone and (
                    char,
                    chord,
                ) not in forced_cells:
                    self.pheromone[(char, chord)] += self.pheromone_boost

    def print_pheromone_table(self, max_chords: int = 30):
        """
        Print a visual representation of the pheromone matrix.
        Skips rows and columns that have been force-assigned.
        Rows = letters, Columns = chords (printed vertically).

        Args:
            max_chords: Maximum number of chords to display (to keep table readable)
        """
        # Get forced cells to exclude from statistics
        forced_cells = set()
        forced_chars = set()
        forced_chords = set()
        for forced_char, forced_chord in self.forced_assignments.items():
            forced_cells.add((forced_char, forced_chord))
            forced_chars.add(forced_char)
            forced_chords.add(forced_chord)

        # Calculate statistics (excluding forced cells)
        non_forced_values = [
            val
            for key, val in self.pheromone.items()
            if key not in forced_cells and val > 0
        ]
        max_pheromone = max(non_forced_values) if non_forced_values else 1.0
        total_pheromone = sum(self.pheromone.values())

        # Filter out forced characters and chords
        display_characters = [c for c in self.characters if c not in forced_chars]
        display_chords = [c for c in self.chords if c not in forced_chords]

        # Limit number of chords to display
        if len(display_chords) > max_chords:
            display_chords = display_chords[:max_chords]

        # Build table as a string
        output = []

        # Build header with chords (2 rows for 4-digit chord, 3 chars wide per column)
        # Assuming all chords have same length (4 digits)
        chord_len = len(display_chords[0]) if display_chords else 0

        # Build first two digits of each chord (row 1)
        line = "      "  # Offset for row label
        for i, chord in enumerate(display_chords):
            if i > 0:
                line += " "  # Space between columns
            line += f" {chord[0]} "
        output.append(line)

        # Build last two digits of each chord (row 2)
        line = "      "  # Offset for row label
        for i, chord in enumerate(display_chords):
            if i > 0:
                line += " "  # Space between columns
            line += f"{chord[1]}{chord[2]}{chord[3]}"
        output.append(line)

        # Build separator
        separator_width = (
            len(display_chords) * 4 - 1
        )  # 3 chars per column + 1 space between, minus last space

        # Build each character row (excluding forced)
        for char in display_characters:
            char_display = (
                char
                if char not in ["\n", "\t", " "]
                else {"\\n": "↵", "\\t": "⇥", " ": "␣"}[repr(char)[1:-1]]
            )
            line = f"   {char_display} "
            for i, chord in enumerate(display_chords):
                pheromone_level = self.pheromone.get((char, chord), 0.0)
                # Normalize to bar index (0-8)
                if max_pheromone > 0:
                    bar_idx = int(
                        (pheromone_level / max_pheromone)
                        * (len(self.PHEROMONE_BARS) - 1)
                    )
                else:
                    bar_idx = 0
                bar_idx = min(bar_idx, len(self.PHEROMONE_BARS) - 1)
                # Add bar three times for 3-char width
                line += self.PHEROMONE_BARS[bar_idx] * 4
            output.append(line)

        # Build legend
        output.append("")
        output.append(
            f"   Legend: Max pheromone = {max_pheromone:.2f}, Total = {total_pheromone:.1f}"
        )
        if forced_chars or forced_chords:
            output.append(
                f"   Note: {len(forced_chars)} forced characters and {len(forced_chords)} forced chords hidden"
            )
        if len(self.chords) - len(forced_chords) > max_chords:
            output.append(
                f"   Note: Showing first {max_chords} of {len(self.chords) - len(forced_chords)} chords"
            )

        # Print entire table at once
        print("\n".join(output))


def evaluate_layout_wrapper(args):
    """
    Wrapper function for parallel layout evaluation.

    Args:
        args: Tuple of (layout, corpus)

    Returns:
        Tuple of (layout, cost)
    """
    layout, corpus = args
    cost = evaluate_layout(layout, corpus, verbose=False)
    return (layout, cost)


def main():
    """Main function to demonstrate layout generation and evaluation using ACO."""
    print("Chording Keyboard Layout Planner (Ant Colony Optimization)")
    print("=" * 60)

    # Load corpus from corpus directory
    print("\n1. Loading corpus...")
    corpus = load_corpus("corpus/*", qwerty_compatible=True)
    print(f"   Loaded {len(corpus)} characters from corpus/* files")

    # Analyze corpus
    print("\n2. Analyzing corpus characters...")
    char_counts = analyze_corpus_characters(corpus)
    print(f"   Found {len(char_counts)} unique characters")
    print(f"   Top 10 most frequent characters:")
    for char, count in sorted(char_counts.items(), key=lambda x: x[1], reverse=True)[
        :10
    ]:
        char_repr = repr(char) if char in ["\n", "\t", " "] else char
        print(f"      {char_repr}: {count}")

    # Generate all possible chords
    print("\n3. Generating all possible chords...")
    all_chords = generate_all_possible_chords(num_fingers=4)
    print(f"   Total possible chords: {len(all_chords)}")

    # Check if we have enough chords for all characters
    characters_needed = set(char_counts.keys())
    print(f"   Characters needed: {len(characters_needed)}")
    if len(all_chords) < len(characters_needed):
        print(
            f"   WARNING: Not enough chords! Need {len(characters_needed)}, have {len(all_chords)}"
        )
    else:
        print(f"   OK: Enough chords available")

    # remove chords that would tip the keyer in hand (too much pressure on 2nd row)
    for thumb in "0123":
        all_chords.remove(thumb + "222")
        all_chords.remove(thumb + "220")
        all_chords.remove(thumb + "022")
        all_chords.remove(thumb + "202")
        # all_chords.remove(thumb + "221")
        # all_chords.remove(thumb + "122")
        all_chords.remove(thumb + "212")

    all_chords.remove("3100")  # reserved for Win+Enter
    all_chords.remove("3200")  # reserved for Alt+Tab
    all_chords.remove("3101")  # reserved for left
    all_chords.remove("3201")  # reserved for Ctrl+left
    all_chords.remove("3011")  # reserved for right
    all_chords.remove("3021")  # reserved for Ctrl+right
    all_chords.remove("3121")  # reserved for home
    all_chords.remove("3211")  # reserved for end
    all_chords.remove("3102")  # reserved for up
    # all_chords.remove("3202")  # reserved for page up (already removed)
    all_chords.remove("3012")  # reserved for down
    # all_chords.remove("3022")  # reserved for page down (already removed)
    all_chords.remove("3000")  # reserved for ctrl

    # Define forced chord assignments
    forced_assignments = {
        " ": "2000",
        "\x08": "1000",  # actually backspace (shift+backspace = delete)
        "\n": "2100",  # actually enter (shift+enter = escape)
        "\t": "2200",
    }

    # Add forced characters to the character set if not already present
    for char in forced_assignments.keys():
        if char not in characters_needed:
            characters_needed.add(char)

    # Initialize Ant Colony Optimizer
    print("\n4. Initializing Ant Colony Optimizer...")
    print(f"   Forced assignments:")
    for char, chord in forced_assignments.items():
        char_repr = repr(char) if char in ["\n", "\t", " ", "\x08"] else char
        print(f"      {char_repr} -> {chord}")

    ant_generator = KeyboardLayoutAntGenerator(
        characters=list(characters_needed),
        chords=all_chords,
        initial_pheromone=1.0,
        evaporation_rate=0.01,
        pheromone_boost=0.01,
        forced_assignments=forced_assignments,
        assign_all_chords_as_aliases=False,
    )
    print(
        f"   Pheromone paths: {len(ant_generator.pheromone)} ({len(characters_needed)} chars × {len(all_chords)} chords)"
    )

    # Run ACO optimization
    print("\n5. Running Ant Colony Optimization...")
    num_generations = 100000
    layouts_per_generation = 240
    num_workers = cpu_count()
    overall_best_cost = float("inf")
    overall_best_layout = None

    print(f"   Total layouts to evaluate: {num_generations * layouts_per_generation}")
    print(f"   Parallel workers: {num_workers} cores")

    # Print initial pheromone table
    print("\n   Initial pheromone state:")
    ant_generator.print_pheromone_table(max_chords=999999)

    for generation in range(num_generations):
        print(f"\n   Generation {generation + 1}/{num_generations}:")

        # Generate all layouts for this generation
        layouts = [
            ant_generator.generate_layout(num_fingers=4)
            for _ in range(layouts_per_generation)
        ]

        # Prepare arguments for parallel evaluation
        eval_args = [(layout, corpus) for layout in layouts]

        # Evaluate layouts in parallel
        generation_best_cost = float("inf")
        generation_best_layout = None

        with Pool(processes=num_workers) as pool:
            # Use imap_unordered for progress tracking
            for i, (layout, cost) in enumerate(
                pool.imap_unordered(evaluate_layout_wrapper, eval_args)
            ):
                if cost < generation_best_cost:
                    generation_best_cost = cost
                    generation_best_layout = layout

                # Update progress (overwrite same line)
                print(
                    f"\r      Evaluated: {i + 1}/{layouts_per_generation} layouts",
                    end="",
                    flush=True,
                )

        print()  # New line after progress

        # Update pheromones with best layout from this generation
        ant_generator.update_pheromones(generation_best_layout)

        # Track overall best
        if generation_best_cost < overall_best_cost:
            overall_best_cost = generation_best_cost
            overall_best_layout = generation_best_layout
            print(
                f"      Best cost: {generation_best_cost:.1f}ms *** NEW OVERALL BEST ***"
            )

            # Save best layout to file
            with open("best_layout.txt", "w") as f:
                f.write(f"Best Keyboard Layout\n")
                f.write(f"=" * 60 + "\n")
                f.write(f"Generation: {generation + 1}\n")
                f.write(f"Cost: {overall_best_cost:.1f}ms\n")
                f.write(
                    f"Average cost per character: {overall_best_cost / len(corpus):.2f}ms\n"
                )
                f.write(f"\nChord Assignments:\n")
                f.write(f"-" * 60 + "\n")

                # Sort by character for readability
                for char in sorted(overall_best_layout.key_map.keys()):
                    chords = overall_best_layout.key_map[char]
                    char_repr = repr(char) if char in ["\n", "\t", " "] else char
                    f.write(f"{char_repr:5s} -> {', '.join(chords)}\n")

                f.write(f"\n" + "=" * 60 + "\n")
                f.write(
                    f"Total unique characters: {len(overall_best_layout.key_map)}\n"
                )
                total_chords = sum(
                    len(chords) for chords in overall_best_layout.key_map.values()
                )
                f.write(f"Total chord assignments: {total_chords}\n")

            print(f"      Saved to best_layout.txt")
        else:
            print(f"      Best cost: {generation_best_cost:.1f}ms")

        print(f"      Overall best: {overall_best_cost:.1f}ms")

        # Print pheromone table after generation
        print(f"\n   Pheromone state after generation {generation + 1}:")
        ant_generator.print_pheromone_table(max_chords=999999)

    print("\n" + "=" * 60)
    print(f"Final best layout cost: {overall_best_cost:.1f}ms")
    print(f"Average cost per character: {overall_best_cost / len(corpus):.2f}ms")
    print(f"Total layouts evaluated: {num_generations * layouts_per_generation}")

    # Show some example mappings from best layout
    print("\nSample chord mappings from best layout:")
    for i, (char, chords) in enumerate(
        sorted(overall_best_layout.key_map.items())[:15]
    ):
        char_repr = repr(char) if char in ["\n", "\t", " "] else char
        print(f"   {char_repr}: {chords[0]}")


if __name__ == "__main__":
    main()
