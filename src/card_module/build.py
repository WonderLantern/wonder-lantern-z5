#!/usr/bin/env python3
"""Build the cardmenu module (Z5APP.BIN) with the z5mod SDK and check its heap image.

    python research/menu/cardmenu/build.py -o output/<new dir>/Z5APP.BIN

Uses reference/z5mod/sdk/tools/z5build.py and imgcheck.py. Refuses to overwrite.
"""
import hashlib
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SDK = ROOT / 'reference' / 'z5mod' / 'sdk'

if '-o' not in sys.argv:
    sys.exit('usage: build.py -o <new output path>')
out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])
if out.exists():
    sys.exit(f'refusing to overwrite {out}')
out.parent.mkdir(parents=True, exist_ok=True)
defines = [x for a in sys.argv[1:] if a.startswith('-D') for x in ('--define', a[2:])]   # -DCM_SKIP_MENU etc.
subprocess.run([sys.executable, str(SDK / 'tools' / 'z5build.py'), str(HERE / 'main.c'), '-o', str(out)] + defines,
               check=True)
subprocess.run([sys.executable, str(SDK / 'tools' / 'imgcheck.py'), str(out.parent / 'obj' / 'main.elf')], check=True)
print(f'  built {out} sha256 {hashlib.sha256(out.read_bytes()).hexdigest()}')
