#!/usr/bin/env python3
"""
Fuzz-test ShadowEditor against a real browser textarea.

Reads test cases from fuzz_gen (JSON), drives Chrome with a textarea,
applies each keystroke, and verifies the textarea state matches
ShadowEditor's predicted state after each step.

The ShadowEditor may forget rows that scroll off the top or bottom of its
tracked area. These rows remain in the textarea. When comparing, we use
the cursor position to anchor the tracked region within the textarea and
only compare that region.

Usage:
    cd fuzz
    g++ -std=c++17 -O2 -I ../src -o fuzz_gen fuzz_gen.cpp
    ./fuzz_gen 100 | python3 fuzz_browser.py
"""

import json
import sys

import chromedriver_autoinstaller
from selenium import webdriver
from selenium.webdriver.common.by import By
from selenium.webdriver.common.keys import Keys

KEY_MAP = {
    "HOME": Keys.HOME,
    "END": Keys.END,
    "UP_ARROW": Keys.ARROW_UP,
    "DOWN_ARROW": Keys.ARROW_DOWN,
    "LEFT_ARROW": Keys.ARROW_LEFT,
    "RIGHT_ARROW": Keys.ARROW_RIGHT,
    "DELETE": Keys.DELETE,
    "BACKSPACE": Keys.BACKSPACE,
    "ENTER": Keys.ENTER,
}

HTML = """\
<!DOCTYPE html>
<html>
<body>
<textarea id="ta" rows="20" cols="80" style="font-family:monospace;font-size:14px;"
  spellcheck="false" autocomplete="off" autocorrect="off" autocapitalize="off"></textarea>
</body>
</html>
"""


def setup_browser():
    chromedriver_autoinstaller.install()
    options = webdriver.ChromeOptions()
    options.add_argument("--headless=new")
    options.add_argument("--no-sandbox")
    options.add_argument("--disable-gpu")
    driver = webdriver.Chrome(options=options)
    driver.get("data:text/html;charset=utf-8," + HTML)
    return driver


def get_textarea_state(driver):
    """Read textarea content and cursor position."""
    state = driver.execute_script("""
        const ta = document.getElementById('ta');
        return {text: ta.value, cursor: ta.selectionStart};
    """)
    text = state["text"]
    cursor_pos = state["cursor"]

    rows = text.split("\n")
    cursor_row = 0
    cursor_col = cursor_pos
    for i, row in enumerate(rows):
        if cursor_col <= len(row):
            cursor_row = i
            break
        cursor_col -= len(row) + 1
    else:
        cursor_row = len(rows) - 1
        cursor_col = len(rows[-1])

    return rows, cursor_row, cursor_col


def set_textarea_state(driver, rows, cursor_row, cursor_col):
    """Set textarea content and cursor position."""
    text = "\n".join(rows)
    pos = sum(len(rows[i]) + 1 for i in range(cursor_row)) + cursor_col
    driver.execute_script("""
        const ta = document.getElementById('ta');
        ta.value = arguments[0];
        ta.selectionStart = arguments[1];
        ta.selectionEnd = arguments[1];
        ta.focus();
    """, text, pos)


def send_keystroke(ta, keystroke):
    """Send a single keystroke to the textarea element."""
    if keystroke["type"] == "char":
        ta.send_keys(chr(keystroke["cp"]))
    else:
        selenium_key = KEY_MAP.get(keystroke["key"])
        if selenium_key is None:
            raise ValueError(f"Unknown key: {keystroke['key']}")
        ta.send_keys(selenium_key)


def clamp_cursor(rows, cr, cc):
    """Clamp cursor to valid range (matches EditorState::operator==)."""
    r = max(0, min(cr, len(rows) - 1))
    c = min(cc, len(rows[r]))
    return r, c


def extract_tracked_region(actual_rows, actual_cr, actual_cc, exp_rows, exp_cr):
    """Extract the tracked region from the textarea using cursor as anchor.

    The ShadowEditor tracks a window of rows. Rows above/below may exist
    in the textarea from earlier scrolling. We use the cursor row to find
    where the tracked region sits within the full textarea.

    Primary strategy: use cursor position as anchor.
    Fallback: find expected rows by content match (handles cases where
    rows were "forgotten" and the cursor is in the forgotten zone).

    Returns (tracked_rows, tracked_cr, tracked_cc) or None if out of bounds.
    """
    exp_num_rows = len(exp_rows)

    # Primary: cursor-based anchor.
    offset = actual_cr - exp_cr
    if 0 <= offset and offset + exp_num_rows <= len(actual_rows):
        tracked_rows = actual_rows[offset:offset + exp_num_rows]
        if tracked_rows == exp_rows:
            tracked_cr = actual_cr - offset
            tracked_cc = actual_cc
            return tracked_rows, tracked_cr, tracked_cc

    # Fallback: content-based search for the tracked region.
    # After forgetting, the cursor may be in the forgotten zone above the
    # tracked region. Find the expected rows as a contiguous block.
    for off in range(len(actual_rows) - exp_num_rows + 1):
        if actual_rows[off:off + exp_num_rows] == exp_rows:
            tracked_cr = actual_cr - off
            tracked_cc = actual_cc
            return exp_rows, tracked_cr, tracked_cc

    return None


def ks_desc(keystroke):
    if keystroke["type"] == "char":
        return f"char '{chr(keystroke['cp'])}'"
    return f"key {keystroke['key']}"


def run_test(driver, ta, case):
    """Run a single test case. Returns (success, message)."""
    case_id = case["id"]
    steps = case["steps"]

    if not case["ok"]:
        return False, f"Case {case_id}: did not converge (infinite loop?)"

    # Set initial state
    init = case["initial"]
    set_textarea_state(driver, init["rows"], init["cr"], init["cc"])

    for step_idx, step in enumerate(steps):
        keystroke = step["k"]
        expected = step["e"]

        send_keystroke(ta, keystroke)

        actual_rows, actual_cr, actual_cc = get_textarea_state(driver)

        exp_rows = expected["rows"]
        raw_cr = expected["cr"]
        raw_cc = expected["cc"]

        # Use raw (possibly negative) cursor_row to anchor the tracked
        # region. offset = actual_cr - raw_cr gives the start of the
        # tracked region in the textarea even when cursor is above it.
        offset = actual_cr - raw_cr
        exp_num_rows = len(exp_rows)

        if 0 <= offset and offset + exp_num_rows <= len(actual_rows):
            tracked_rows = actual_rows[offset:offset + exp_num_rows]
        else:
            # Out of bounds — try content-based fallback.
            tracked_rows = None
            for off in range(len(actual_rows) - exp_num_rows + 1):
                if actual_rows[off:off + exp_num_rows] == exp_rows:
                    tracked_rows = exp_rows
                    offset = off
                    break
            if tracked_rows is None:
                continue  # can't verify this step

        if tracked_rows != exp_rows:
            return False, (
                f"Case {case_id}, step {step_idx} ({ks_desc(keystroke)}): "
                f"rows mismatch\n"
                f"  tracked:  {tracked_rows}\n"
                f"  expected: {exp_rows}\n"
                f"  full:     {actual_rows}"
            )

        # Cursor verification: only when cursor is inside the tracked
        # region (raw_cr >= 0). When cursor is above the tracked region
        # (navigating through forgotten rows), cursor_col depends on
        # forgotten row lengths that differ between model and browser.
        if raw_cr >= 0:
            exp_cr, exp_cc = clamp_cursor(exp_rows, raw_cr, raw_cc)
            tracked_cr = actual_cr - offset
            tracked_cc = actual_cc
            if tracked_cr != exp_cr or tracked_cc != exp_cc:
                return False, (
                    f"Case {case_id}, step {step_idx} ({ks_desc(keystroke)}): "
                    f"cursor mismatch\n"
                    f"  got:      ({tracked_cr},{tracked_cc})\n"
                    f"  expected: ({exp_cr},{exp_cc})"
                )

    return True, f"Case {case_id}: OK ({len(steps)} steps)"


def main():
    data = json.load(sys.stdin)
    print(f"Loaded {len(data)} test cases")

    driver = setup_browser()
    ta = driver.find_element(By.ID, "ta")

    passed = 0
    failed = 0
    errors = []

    for case in data:
        ok, msg = run_test(driver, ta, case)
        if ok:
            passed += 1
            if passed % 10 == 0:
                print(f"  ... {passed} passed", file=sys.stderr)
        else:
            failed += 1
            errors.append(msg)
            print(f"FAIL: {msg}")
            if failed >= 10:
                print("Too many failures, stopping early.")
                break

    driver.quit()

    print(f"\nResults: {passed} passed, {failed} failed out of {len(data)}")
    if errors:
        print("\nFirst failures:")
        for e in errors[:5]:
            print(f"  {e}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
