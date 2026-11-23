#!/usr/bin/env python3
"""The JPGL header at DIAG 0x33000 is shared: the encode hook writes some words, the FILM menu owns others.

A50-A52 stored the grain kernel number in word 13, which a44_menu_install.inc fills with the FILM strength page
header; after the first shot the strength page opened a bad page and the camera hung (found 2026-11-23).
This check fails if the encode hook assigns any word the menu owns, or if the menu words are written elsewhere.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
CARD = ROOT / 'src/card_module'
vui = (CARD / 'vui_card.inc').read_text(encoding='utf-8')
start = vui.index('IMG_FN static void jpeg_log')
body = vui[start:vui.index('\nIMG_FN static void vui_hook', start)]

# Words the menu and settings own (A44_LOG index): 8 FILM page, 9 style, 11 strength, 13 strength page, 14 NEF write.
MENU = {8, 9, 11, 13, 14}
# Words the encode hook may write: 0-4 header, 5 (A43 style only), 10/12 per-shot style/strength, 15 A50 rotation.
HOOK = {0, 1, 2, 3, 4, 5, 10, 12, 15}

written = {int(n) for n in re.findall(r'\bh\[(\d+)u?\]\s*(?:[-+|&^]?=)(?!=)', body)}
assert written, 'no header writes found: the pattern no longer matches jpeg_log'
assert not written & MENU, f'jpeg_log writes menu-owned JPGL words {sorted(written & MENU)}'
assert written <= HOOK, f'jpeg_log writes unexpected JPGL words {sorted(written - HOOK)}'

owners = {}
for path in CARD.glob('*'):
    if path.suffix not in ('.c', '.inc'):
        continue
    text = path.read_text(encoding='utf-8', errors='replace')
    for n in re.findall(r'A44_LOG\(\w+\)\[(\d+)u?\]\s*(?:[-+|&^]?=)(?!=)', text):
        owners.setdefault(int(n), set()).add(path.name)
for word in (8, 13):
    assert owners.get(word) == {'a44_menu_install.inc'}, (word, owners.get(word))
print(f'PASS: jpeg_log writes JPGL words {sorted(written)} only; FILM page headers 8 and 13 are written only by '
      'a44_menu_install.inc')
