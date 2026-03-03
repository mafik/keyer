#!/usr/bin/env python3
"""Parse keyer.cpp and generate an HTML cheat sheet for the chording keyboard layout."""

import re
import html
import base64
from pathlib import Path

ROOT = Path(__file__).parent
KEYER_CPP = ROOT / "src" / "keyer.cpp"
OUTPUT_HTML = ROOT / "layout.html"

SVG_NAMES = ['thumb', 'index', 'middle', 'ring', 'little']


def load_svg_data_uris():
    """Load SVG files and return dict of name -> data URI string."""
    uris = {}
    for name in SVG_NAMES:
        path = ROOT / f"{name}.svg"
        if path.exists():
            b64 = base64.b64encode(path.read_bytes()).decode()
            uris[name] = f'data:image/svg+xml;base64,{b64}'
        else:
            uris[name] = ''
    return uris

IBM_KEY_SYMBOLS = {
    "BACKSPACE": "⌫ Backspace",
    "DELETE": "⌦ Delete",
    "ENTER": "⏎ Enter",
    "TAB": "⇥ Tab",
    "ESC": "Esc",
    "LEFT_ARROW": "← Left",
    "RIGHT_ARROW": "→ Right",
    "UP_ARROW": "↑ Up",
    "DOWN_ARROW": "↓ Down",
    "HOME": "Home",
    "END": "End",
    "PAGE_UP": "PgUp",
    "PAGE_DOWN": "PgDn",
    "F1": "F1", "F2": "F2", "F3": "F3", "F4": "F4",
    "F5": "F5", "F6": "F6", "F7": "F7", "F8": "F8",
    "F9": "F9", "F10": "F10", "F11": "F11", "F12": "F12",
}

MOD_NAMES = {
    "MOD_CTRL": "Ctrl",
    "MOD_ALT": "Alt",
    "MOD_SUPER": "Super",
    "MOD_SHIFT": "Shift",
}


def parse_action(action_str):
    """Parse an action string and return a human-readable label and category."""
    s = action_str.strip().rstrip(';')

    if 'SECRET_SNIPPET' in s:
        return None, None

    # TemporaryOgonekAction
    if 'TemporaryOgonekAction' in s:
        return 'Ogonek', 'modifier'

    # Seq("text")
    m = re.match(r'Seq\("([^"]*)"\)', s)
    if m:
        return m.group(1).replace(' ', '␣'), 'sequence'

    # Mod(MOD_X, Key(...))
    m = re.match(r'Mod\((\w+),\s*Key\((.+)\)\)', s)
    if m:
        mod = MOD_NAMES.get(m.group(1), m.group(1))
        key_arg = m.group(2).strip()
        key_label = parse_key_arg(key_arg)
        return f'{mod}+{key_label}', 'editing'

    # Mod(MOD_X, Key(IBM_Key::...))  already covered above

    # Mod(MOD_X)
    m = re.match(r'Mod\((\w+)\)$', s)
    if m:
        return MOD_NAMES.get(m.group(1), m.group(1)), 'modifier'

    # Hold(BUTTON, MOD_X, Key(...))
    m = re.match(r'Hold\(\w+,\s*(\w+),\s*Key\((.+)\)\)', s)
    if m:
        mod = MOD_NAMES.get(m.group(1), m.group(1))
        key_label = parse_key_arg(m.group(2).strip())
        return f'{mod}(hold)+{key_label}', 'editing'

    # Hold(BUTTON, MOD_X, base) - shift variants auto-generated, skip
    m = re.match(r'Hold\(\w+,\s*(\w+),\s*\w+\)', s)
    if m:
        return None, None

    # Key(IBM_Key::X)
    m = re.match(r'Key\(IBM_Key::(\w+)\)', s)
    if m:
        label = IBM_KEY_SYMBOLS.get(m.group(1), m.group(1))
        return label, 'editing'

    # Key('x') or Key('\\n') or Key('\'') or Key('\\\\') etc.
    m = re.match(r"Key\('(\\.)'\)", s) or re.match(r"Key\('(.)'\)", s)
    if m:
        ch = m.group(1)
        c_escapes = {'\\n': ('⏎ Enter', 'editing'), '\\t': ('⇥ Tab', 'editing'),
                     '\\\\': ('\\', 'punctuation'), "\\'": ("'", 'punctuation')}
        if ch in c_escapes:
            return c_escapes[ch]
        elif ch == ' ':
            return '␣ Space', 'editing'
        else:
            return ch, categorize_char(ch)

    # Fn(...)
    if s.startswith('Fn('):
        return None, None  # skip function actions

    # Mod(MOD_SUPER, Key('\\n'))
    m = re.match(r"Mod\((\w+),\s*Key\('(\\?.?)'\)\)", s)
    if m:
        mod = MOD_NAMES.get(m.group(1), m.group(1))
        ch = m.group(2)
        if ch == '\\n':
            key_label = 'Enter'
        elif ch == '\\t':
            key_label = 'Tab'
        else:
            key_label = ch
        return f'{mod}+{key_label}', 'editing'

    return None, None


def parse_key_arg(arg):
    """Parse a key argument like IBM_Key::LEFT_ARROW or '\\n'."""
    m = re.match(r"IBM_Key::(\w+)", arg)
    if m:
        return IBM_KEY_SYMBOLS.get(m.group(1), m.group(1))
    m = re.match(r"'(\\?.?)'", arg)
    if m:
        ch = m.group(1)
        if ch == '\\n':
            return 'Enter'
        elif ch == '\\t':
            return 'Tab'
        return ch
    return arg


def categorize_char(ch):
    if ch.isalpha():
        return 'letter'
    elif ch.isdigit():
        return 'digit'
    else:
        return 'punctuation'


def parse_keyer_cpp():
    """Parse keyer.cpp and return list of (label, category, thumb, index, middle, ring, little)."""
    source = KEYER_CPP.read_text()
    entries = []

    pattern = re.compile(
        r'CHORDS\[(\d+)\]\[(\d+)\]\[(\d+)\]\[(\d+)\]\[(\d+)\]\s*=\s*(.+?);',
        re.DOTALL
    )

    for m in pattern.finditer(source):
        t, i, mi, r, l = int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(5))
        action = m.group(6).strip()
        label, category = parse_action(action + ';')
        if label is None:
            continue
        entries.append((label, category, t, i, mi, r, l))

    return entries


CATEGORY_ORDER = [
    ('letter', 'Letters'),
    ('digit', 'Digits'),
    ('punctuation', 'Punctuation'),
    ('editing', 'Editing'),
    ('modifier', 'Modifiers'),
    ('sequence', 'Sequences'),
]

POSITION_COLORS = {
    0: '#e0e0e0',
    1: '#81c784',
    2: '#64b5f6',
    3: '#ffb74d',
}

POSITION_LABELS = {
    0: '·',
    1: '1',
    2: '2',
    3: '3',
}


def finger_cell(pos):
    color = POSITION_COLORS[pos]
    label = POSITION_LABELS[pos]
    if pos == 0:
        return f'<td class="pos0">{label}</td>'
    return f'<td class="pos{pos}">{label}</td>'


def generate_html(entries):
    svgs = load_svg_data_uris()

    def th_icon(name):
        uri = svgs.get(name, '')
        if uri:
            return f'<th><img src="{uri}" alt="{name}" class="finger-icon"></th>'
        return f'<th>{name[0].upper()}</th>'

    # Separate little-finger chords
    normal = [e for e in entries if e[6] == 0]
    little = [e for e in entries if e[6] != 0]

    groups = {}
    for e in normal:
        cat = e[1]
        groups.setdefault(cat, []).append(e)

    # Sort letters and digits
    for cat in groups:
        if cat == 'letter':
            groups[cat].sort(key=lambda e: e[0].lower())
        elif cat == 'digit':
            groups[cat].sort(key=lambda e: e[0])
        # else keep source order

    parts = []
    parts.append('''<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Keyer Layout Cheat Sheet</title>
<link href="https://fonts.googleapis.com/css?family=Coming+Soon" rel="stylesheet" type="text/css">
<style>
  body { font-family: 'Coming Soon', sans-serif; margin: 2em; background: #fafafa; color: #333; }
  h1 { text-align: center; margin-bottom: 0.5em; }
  .masonry { columns: 210px; column-gap: 1.5em; }
  .card { break-inside: avoid; margin-bottom: 1.5em; }
  .group-name { text-align: left !important; font-size: 0.85em; }
  table { border-collapse: collapse; width: 100%; }
  th { background: #e8e8e8; color: #555; padding: 0; text-align: center; font-size: 0.8em; }
  th:first-child { text-align: left; }
  td { padding: 3px 8px; text-align: center; border-bottom: 1px solid #ddd; font-size: 0.85em; }
  td:first-child { text-align: left; font-weight: 600; }
  tr:hover { background: #f0f0f0; }
  .pos0 { color: #bbb; }
  .pos1 { background: #c8e6c9; font-weight: bold; }
  .pos2 { background: #bbdefb; font-weight: bold; }
  .pos3 { background: #ffe0b2; font-weight: bold; }
  .finger-icon { height: 24px; width: 24px; vertical-align: middle; transform: rotate(180deg); }

  @media print {
    body { margin: 0; }
    td { padding-top: 2px; padding-bottom: 2px; }
  }
</style>
</head>
<body>
<div class="masonry">
''')

    for cat, title in CATEGORY_ORDER:
        if cat not in groups:
            continue
        parts.append('<div class="card">')
        parts.append(f'<table><tr><th class="group-name">{html.escape(title)}</th>{th_icon("thumb")}{th_icon("index")}{th_icon("middle")}{th_icon("ring")}</tr>')
        for label, _, t, i, mi, r, l in groups[cat]:
            escaped = html.escape(label)
            parts.append(f'<tr><td>{escaped}</td>{finger_cell(t)}{finger_cell(i)}{finger_cell(mi)}{finger_cell(r)}</tr>')
        parts.append('</table></div>')

    if little:
        parts.append('<div class="card">')
        parts.append(f'<table><tr><th class="group-name">Special</th>{th_icon("thumb")}{th_icon("index")}{th_icon("middle")}{th_icon("ring")}{th_icon("little")}</tr>')
        for label, _, t, i, mi, r, l in little:
            escaped = html.escape(label)
            parts.append(f'<tr><td>{escaped}</td>{finger_cell(t)}{finger_cell(i)}{finger_cell(mi)}{finger_cell(r)}{finger_cell(l)}</tr>')
        parts.append('</table></div>')

    parts.append('</div>\n</body></html>')
    return '\n'.join(parts)


def main():
    entries = parse_keyer_cpp()
    html_content = generate_html(entries)
    OUTPUT_HTML.write_text(html_content)
    print(f"Generated {OUTPUT_HTML} with {len(entries)} chords")


if __name__ == '__main__':
    main()
