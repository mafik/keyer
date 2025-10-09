#!/usr/bin/env python3
"""
Beam search optimizer for chording keyboard layouts.

Uses beam search to explore the layout space, keeping the top N candidates
at each iteration.
"""

import glob
from typing import Dict, Set
from multiprocessing import Pool, cpu_count

from qwerty_analysis import QwertyKeys
import keyer_simulator_native
from layout import load_layout, save_layout
from mutator import mutate_layout


def load_corpus(pattern: str = "corpus/*", qwerty_compatible: bool = True) -> str:
    """
    Load corpus from files.

    Args:
        pattern: Glob pattern for corpus files
        qwerty_compatible: Whether to process corpus with QwertyKeys

    Returns:
        Combined corpus string
    """
    corpus = []
    files = glob.glob(pattern, recursive=True)

    for filepath in files:
        try:
            with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                content = f.read()
                if qwerty_compatible:
                    content = QwertyKeys(content, modifiers={"Shift": "", "Alt": "T"})
                corpus.append(content)
        except (IOError, OSError) as e:
            print(f"Warning: Could not read {filepath}: {e}")
            continue

    return "".join(corpus)


def evaluate_layout(
    layout: Dict[str, str], corpus: str, verbose: bool = False
) -> float:
    """
    Evaluate layout using keyer_simulator_native.

    Args:
        layout: Dictionary mapping characters to chords
        corpus: Corpus text to evaluate against
        verbose: Print detailed information

    Returns:
        Total cost in milliseconds
    """
    # Convert to KeyerLayout format (each char gets a list with one chord)
    key_map = {}
    for char, chord in layout.items():
        if len(char) != 1:
            print(f"ERROR: Invalid key length: {repr(char)} (len={len(char)})")
            continue
        key_map[char] = [chord]

    # Score using native simulator
    cost = keyer_simulator_native.score_layout(key_map, corpus)

    if verbose:
        print(f"Layout cost: {cost:.1f}ms")
        print(f"Average per character: {cost / len(corpus):.2f}ms")

    return cost


def evaluate_layout_wrapper(args):
    """
    Wrapper function for parallel layout evaluation.

    Args:
        args: Tuple of (layout_dict, corpus)

    Returns:
        Tuple of (layout_dict, cost)
    """
    layout, corpus = args
    cost = evaluate_layout(layout, corpus, verbose=False)
    return (layout, cost)


def get_layout_hash(layout: Dict[str, str]) -> str:
    """
    Compute compact hashable representation of layout.

    Args:
        layout: Layout to hash

    Returns:
        String hash that uniquely identifies the layout
    """
    # Sort by character to ensure consistent ordering
    sorted_items = sorted(layout.items())
    return "".join(f"{char}:{chord};" for char, chord in sorted_items)


def main():
    """Main beam search optimization."""
    print("Chording Keyboard Layout Beam Optimizer")
    print("=" * 60)

    # Load corpus
    print("\nLoading corpus...")
    corpus = load_corpus("corpus/*", qwerty_compatible=True)
    print(f"Loaded corpus: {len(corpus)} characters")
    print(f"Unique characters: {len(set(corpus))}")

    # Load initial layout
    print("\nLoading initial layout from best_layout.txt...")
    initial_layout = load_layout("best_layout.txt")
    print(f"Loaded {len(initial_layout)} character mappings")

    # Verify initial score matches file
    print("\nVerifying initial layout score...")
    initial_score = evaluate_layout(initial_layout, corpus, verbose=True)

    # Read expected score from file
    with open("best_layout.txt", "r") as f:
        for line in f:
            if line.startswith("Cost:"):
                expected_score = float(line.split(":")[1].strip().replace("ms", ""))
                print(f"Expected score from file: {expected_score:.1f}ms")
                print(f"Match: {abs(initial_score - expected_score) < 1.0}")
                break

    # Beam search parameters
    beam_width = 1000
    max_iterations = 5000

    print("\n" + "=" * 60)
    print("Running beam search optimization...")
    print(f"Beam width: {beam_width}, Max iterations: {max_iterations}")

    num_workers = cpu_count()
    print(f"Parallel workers: {num_workers} cores")

    # Initialize beam with the initial layout
    beam = [(initial_layout, initial_score)]

    global_best_layout = initial_layout
    global_best_score = initial_score

    visited_layouts: Set[str] = set()
    visited_layouts.add(get_layout_hash(initial_layout))

    for iteration in range(max_iterations):
        # Take the best layout from the beam
        best_in_beam_layout, best_in_beam_score = beam[0]

        # Generate variants from the best layout in the beam
        new_variants = []
        for idx, variant in enumerate(mutate_layout(best_in_beam_layout), 1):
            variant_hash = get_layout_hash(variant)

            if variant_hash in visited_layouts:
                continue

            visited_layouts.add(variant_hash)
            new_variants.append(variant)

            # Update progress in-place every 100 variants
            if idx % 100 == 0:
                print(
                    f"\r  Generating variants: {len(new_variants)} new ({idx} checked)",
                    end="",
                    flush=True,
                )
        print(
            f"\r  Generating variants: {len(new_variants)} new ({idx} checked)"
        )  # Final count

        if not new_variants:
            print(f"No new candidates found at iteration {iteration + 1}")
            break

        # Prepare arguments for parallel evaluation
        eval_args = [(variant, corpus) for variant in new_variants]

        # Evaluate variants in parallel
        all_candidates = []
        total_variants = len(eval_args)
        with Pool(processes=num_workers) as pool:
            for idx, (variant, score) in enumerate(
                pool.imap_unordered(evaluate_layout_wrapper, eval_args), 1
            ):
                all_candidates.append((variant, score))
                # Update progress in-place
                print(
                    f"\r  Evaluating: {idx}/{total_variants} ({100 * idx // total_variants}%)",
                    end="",
                    flush=True,
                )
        print()  # Newline after progress complete

        # Sort all candidates by score
        all_candidates.sort(key=lambda x: x[1])

        # Keep top beam_width candidates
        beam = all_candidates[:beam_width]

        # Track the global best
        if beam[0][1] < global_best_score:
            global_best_layout = beam[0][0]
            global_best_score = beam[0][1]
            print(
                f"Iteration {iteration + 1}: New global best score: {global_best_score:.1f}ms"
            )

            # Save immediately
            save_layout(
                layout=global_best_layout,
                score=global_best_score,
                corpus_length=len(corpus),
                generation=iteration + 1,
                filepath="beam_best.txt",
            )

        print(
            f"Iteration {iteration + 1}: Evaluated {len(all_candidates)} candidates, "
            f"beam best: {beam[0][1]:.1f}ms"
        )

    best_layout = global_best_layout
    best_score = global_best_score

    # Print results
    print("\n" + "=" * 60)
    print("Optimization Results:")
    print(f"Best score: {best_score:.1f}ms")
    print(f"Improvement: {initial_score - best_score:.1f}ms")
    print(f"Improvement %: {(initial_score - best_score) / initial_score * 100:.2f}%")


if __name__ == "__main__":
    main()
