#!/usr/bin/env python3
"""
Chord sequence optimizer.

WARNING: this file was vibe coded and likely contains bugs.

Finds the best n-gram sequences to assign to the 42 free chords, using:
  - The actual finger transition cost model from fingers.cpp
  - Greedy re-counting selection (each round removes consumed occurrences)
  - Rearrangement-inequality optimal chord↔sequence pairing

Usage:
    python chord_sequence_optimizer.py [--layout PATH] [--corpus DIR] [--max-n N]
"""

import argparse
import glob
import os
import sys
from collections import Counter
from typing import Dict, List, Optional, Tuple

# ── Cost constants (mirrors fingers.cpp) ─────────────────────────────────────
NUM_FINGERS = 4

TRAVEL = [80, 100, 110, 150]       # ms per row of movement: Thumb/Index/Middle/Ring

PRESS = [                           # ms per press, indexed [finger][row]
    [60, 40, 60],   # Thumb:  row0=60  row1=40  row2=60
    [50, 130,  0],  # Index:  row0=50  row1=130
    [55, 140,  0],  # Middle: row0=55  row1=140
    [60, 150,  0],  # Ring:   row0=60  row1=150
]

# Default resting position: thumb at row1, others at row0
DEFAULT_ROWS = (1, 0, 0, 0)

SENTINEL = '\x00'   # marks positions already consumed by a selected chord

# Estimated cost of the shift modifier (pinky press while holding main chord).
# Used to score sequences that contain uppercase letters not in the primary layout.
SHIFT_OVERHEAD_MS = 100


# ── Chord parsing ─────────────────────────────────────────────────────────────

def parse_chord(s: str) -> Tuple[int, Tuple[int, ...]]:
    """'2001' → (pressed_bitmask, rows_tuple).  '0' = finger not pressed."""
    pressed = 0
    rows = list(DEFAULT_ROWS)
    for i in range(min(NUM_FINGERS, len(s))):
        v = int(s[i])
        if v:
            pressed |= 1 << i
            rows[i] = v - 1     # chord digit is 1-based row number
    return pressed, tuple(rows)


# ── Finger state machine (mirrors Fingers::transition_to in fingers.cpp) ─────

class State:
    __slots__ = ('pressed', 'rows')

    def __init__(self):
        self.pressed: int = 0
        self.rows: List[int] = list(DEFAULT_ROWS)

    def copy(self) -> 'State':
        s = State()
        s.pressed = self.pressed
        s.rows = list(self.rows)
        return s

    def step(self, t_pressed: int, t_rows: Tuple[int, ...]) -> int:
        """Transition to target chord. Returns cost in ms. Mutates self."""
        cost = 0
        re_press_needed = (self.pressed != 0)

        # Phase 1: move fingers that need to reach new positions
        tmp = t_pressed
        while tmp:
            fi = (tmp & -tmp).bit_length() - 1
            tmp &= tmp - 1
            curr, tgt = self.rows[fi], t_rows[fi]
            if curr != tgt:
                if self.pressed & (1 << fi):   # this finger was held down
                    re_press_needed = False
                    self.pressed ^= 1 << fi    # unpress it (movement = release)
                self.rows[fi] = tgt
                cost += TRAVEL[fi] * abs(curr - tgt)

        simple_release = self.pressed & ~t_pressed

        # Phase 2: handle re-press if the previous chord hasn't been released yet
        if re_press_needed:
            new_press = t_pressed & ~self.pressed
            if not (simple_release and new_press):
                # No rolling motion: must explicitly re-press cheapest held finger
                candidates = self.pressed & t_pressed
                best_fi, best_rc = -1, 10**9
                tmp2 = candidates
                while tmp2:
                    fi = (tmp2 & -tmp2).bit_length() - 1
                    tmp2 &= tmp2 - 1
                    rc = PRESS[fi][self.rows[fi]]
                    if rc < best_rc:
                        best_fi, best_rc = fi, rc
                if best_fi >= 0:
                    self.pressed ^= 1 << best_fi
                    cost += best_rc * 2     # penalty for re-press

        # Phase 3: release, then press
        self.pressed &= ~simple_release
        tmp = t_pressed & ~self.pressed
        while tmp:
            fi = (tmp & -tmp).bit_length() - 1
            tmp &= tmp - 1
            self.pressed |= 1 << fi
            cost += PRESS[fi][t_rows[fi]]

        return cost


def chord_standalone_cost(chord_str: str) -> int:
    """Cost of pressing a chord from the default resting state."""
    st = State()
    tp, tr = parse_chord(chord_str)
    return st.step(tp, tr)


# ── Free chords (as provided) ─────────────────────────────────────────────────

FREE_CHORDS = [
    "1002", "2002", "3002",
    "0012", "1012", "2012",
    "0022", "1022", "2022",
    "1102", "2102",
    "0112", "1112", "2112", "3112",
    "1121",
    "0122", "1122", "2122", "3122",
    "0202", "1202", "2202",
    "3210",
    "1211", "2211",
    "0212", "1212", "2212", "3212",
    "0220", "1220", "2220", "3220",
    "0221", "1221", "2221", "3221",
    "0222", "1222", "2222", "3222",
]

assert len(FREE_CHORDS) == 42, f"Expected 42 free chords, got {len(FREE_CHORDS)}"


# ── Layout I/O ────────────────────────────────────────────────────────────────

def load_layout(path: str) -> Dict[str, str]:
    """Return {char: chord_str} from a best_layout.txt file."""
    layout: Dict[str, str] = {}
    with open(path, encoding='utf-8') as f:
        in_block = False
        for line in f:
            line = line.strip()
            if 'Chord Assignments:' in line:
                in_block = True
                continue
            if not in_block or not line or set(line) <= set('=-'):
                continue
            if '->' not in line:
                continue
            lhs, rhs = line.split('->', 1)
            chord = rhs.strip().split(',')[0].strip()
            lhs = lhs.strip()
            if lhs.startswith("'") and lhs.endswith("'") and len(lhs) > 2:
                try:
                    char = eval(lhs)
                except Exception:
                    continue
            else:
                char = lhs[0] if lhs else None
            if char and len(char) == 1:
                layout[char] = chord
    return layout


def load_corpus(directory: str, max_chars: int = 2_000_000) -> str:
    parts, total = [], 0
    for path in sorted(glob.glob(os.path.join(directory, '*'))):
        if total >= max_chars:
            break
        try:
            with open(path, encoding='utf-8', errors='ignore') as f:
                chunk = f.read(max_chars - total)
                parts.append(chunk)
                total += len(chunk)
        except OSError:
            pass
    return ''.join(parts)


# ── Optimizer ─────────────────────────────────────────────────────────────────

def optimize(layout_path: str, corpus_dir: str, max_n: int = 6) -> None:
    print("Loading layout ...")
    layout = load_layout(layout_path)
    print(f"  {len(layout)} chars mapped")

    print("Loading corpus ...")
    corpus = load_corpus(corpus_dir)
    print(f"  {len(corpus):,} chars")

    # Precompute char → (pressed, rows) for fast simulation
    char_chord: Dict[str, Tuple[int, Tuple[int, ...]]] = {
        c: parse_chord(ch) for c, ch in layout.items()
    }
    known = set(layout.keys())

    # Cache: ngram string → typing cost from default state (None = unknown char)
    cost_cache: Dict[str, Optional[int]] = {}

    def typing_cost(ngram: str) -> Optional[int]:
        cached = cost_cache.get(ngram)
        if cached is not None:
            return cached
        if ngram in cost_cache:     # explicitly stored None
            return None
        st = State()
        total = 0
        for c in ngram:
            info = char_chord.get(c)
            if info is None:
                # Try lowercase: uppercase letter typed via shift modifier
                info = char_chord.get(c.lower())
                if info is None:
                    cost_cache[ngram] = None
                    return None
                total += SHIFT_OVERHEAD_MS
            total += st.step(*info)
        cost_cache[ngram] = total
        return total

    # Sort free chords cheapest → most expensive
    sorted_chords = sorted(FREE_CHORDS, key=chord_standalone_cost)
    chord_costs = {ch: chord_standalone_cost(ch) for ch in sorted_chords}

    print("\nFree chord costs (cheapest first):")
    for ch in sorted_chords:
        print(f"  {ch}  {chord_costs[ch]} ms")

    # Replace truly unknown chars with sentinel so they break n-gram windows.
    # Uppercase letters whose lowercase form is in the layout are kept as-is:
    # they are typeable via the shift modifier and should not hide longer
    # sequences (e.g. keeping 'P' prevents 'Pulpit/' from being masked to
    # '\x00ulpit/', which would otherwise inflate the count of 'ulpit/').
    text = ''.join(c if (c in known or c.lower() in known) else SENTINEL for c in corpus)

    print(f"\nGreedy selection (max n={max_n}, {len(sorted_chords)} chords) ...\n")
    hdr = f"{'Rnd':>3}  {'Sequence':^14}  {'Len':>3}  {'Freq':>6}  "
    hdr += f"{'Type ms':>7}  {'Save/occ':>8}  {'Total save':>10}"
    print(hdr)
    print('-' * len(hdr))

    selected: List[Tuple[str, int, int]] = []   # (ngram, freq, typing_cost)

    for rnd in range(len(sorted_chords)):
        # The cheapest remaining chord determines the profitability threshold
        min_cc = chord_costs[sorted_chords[rnd]]

        # Count all valid n-grams in the current (partially consumed) text
        counts: Counter = Counter()
        L = len(text)
        for n in range(2, max_n + 1):
            for i in range(L - n + 1):
                ng = text[i: i + n]
                if SENTINEL not in ng:
                    counts[ng] += 1

        if not counts:
            print(f"  Round {rnd + 1}: no valid n-grams left.")
            break

        # Rank by freq × typing_cost; only consider sequences that beat the
        # cheapest available chord
        best_ng, best_score, best_tc = None, 0.0, 0
        for ng, freq in counts.items():
            tc = typing_cost(ng)
            if tc is None or tc <= min_cc:
                continue
            score = freq * tc       # total effort that can be eliminated
            if score > best_score:
                best_score, best_ng, best_tc = score, ng, tc

        if best_ng is None:
            print(f"  Round {rnd + 1}: no profitable n-grams remain.")
            break

        freq = counts[best_ng]
        selected.append((best_ng, freq, best_tc))

        save_per = best_tc - min_cc
        print(f"{rnd + 1:>3}  {repr(best_ng):^14}  {len(best_ng):>3}  "
              f"{freq:>6}  {best_tc:>7}  {save_per:>8}  "
              f"{freq * save_per / 1000:>9.1f}s")

        # Mark consumed occurrences with sentinels (same length → no positional shift)
        text = text.replace(best_ng, SENTINEL * len(best_ng))

    # ── Optimal chord assignment (rearrangement inequality) ──────────────────
    # Minimize Σ freq_i × chord_cost_i  →  pair highest-freq with cheapest chord.
    selected_sorted = sorted(selected, key=lambda x: -x[1])   # freq descending
    chords_sorted   = sorted_chords[:len(selected_sorted)]     # cost ascending

    results = []
    for (ng, freq, tc), chord in zip(selected_sorted, chords_sorted):
        cc = chord_costs[chord]
        saves = freq * (tc - cc)
        results.append((ng, freq, tc, chord, cc, saves))

    # ── Report ────────────────────────────────────────────────────────────────
    print()
    print("=" * 72)
    total_saves_s = sum(r[5] for r in results) / 1000
    print(f"Estimated total savings over corpus: {total_saves_s:.1f} s\n")

    print(f"{'Sequence':^14}  {'Chord':>6}  {'Freq':>6}  "
          f"{'Type ms':>7}  {'Chrd ms':>7}  {'Save/occ':>8}  {'Total':>9}")
    print('-' * 72)
    for ng, freq, tc, chord, cc, saves in sorted(results, key=lambda r: -r[5]):
        print(f"{repr(ng):^14}  {chord:>6}  {freq:>6}  "
              f"{tc:>7}  {cc:>7}  {tc - cc:>8}  {saves / 1000:>8.1f}s")

    # ── best_layout.txt additions ─────────────────────────────────────────────
    print()
    print("Additions for best_layout.txt:")
    print('-' * 40)
    for ng, freq, tc, chord, cc, saves in sorted(results, key=lambda r: repr(r[0])):
        print(f"{repr(ng):10s} -> {chord}")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Chord sequence optimizer')
    parser.add_argument('--layout', default='best_layout.txt',
                        help='Path to best_layout.txt (default: best_layout.txt)')
    parser.add_argument('--corpus', default='corpus',
                        help='Corpus directory (default: corpus)')
    parser.add_argument('--max-n', type=int, default=10,
                        help='Maximum n-gram length to consider (default: 10)')
    args = parser.parse_args()

    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    optimize(args.layout, args.corpus, args.max_n)
