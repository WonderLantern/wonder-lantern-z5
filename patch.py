#!/usr/bin/env python3
"""Build the Z5 extension firmware from your own copy of Nikon's Z 5 C 1.50 update.

    python patch.py --source Z_5_0150.bin --output output/

No firmware is stored in this repository. The tool checks the SHA-256 of the file
you supply, decodes its MAIN module, verifies the original bytes of every edit,
applies them, and refuses to write anything unless the result matches the build
tested on a camera. Your input file is never changed and nothing is overwritten.

Decoding uses z5mod (https://github.com/youyli03/z5mod). It is downloaded once, at
a fixed commit with checked hashes, into third_party/. Use --z5mod to point at a
checkout you already have.

Python 3.10 or newer, standard library only.
"""
import argparse
import binascii
import hashlib
import importlib
import io
import json
import shutil
import struct
import sys
import tarfile
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
DATA = HERE / 'patch_data' / 'patches.json'
CARD_MODULE = HERE / 'card' / 'Z5APP.BIN'
THIRD_PARTY = HERE / 'third_party' / 'z5mod'
MENU_DAT = struct.pack('<II', 0x4D53355A, 1 | (1 << 8) | (5 << 16))  # on, type 1, volume 5


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def fetch_z5mod(pin):
    """Download only the decoder files of z5mod at the pinned commit."""
    url = 'https://codeload.github.com/youyli03/z5mod/tar.gz/' + pin['commit']
    print('Downloading the z5mod decoder at commit %s ...' % pin['commit'][:7])
    with urllib.request.urlopen(url, timeout=120) as response:
        blob = response.read()
    wanted = pin['files']
    found = {}
    with tarfile.open(fileobj=io.BytesIO(blob), mode='r:gz') as tar:
        for member in tar.getmembers():
            relative = member.name.split('/', 1)[-1]
            if relative in wanted and member.isfile():
                found[relative] = tar.extractfile(member).read()
    if set(found) != set(wanted):
        raise SystemExit('z5mod download is missing: %s' % sorted(set(wanted) - set(found)))
    for relative, data in found.items():
        if sha256(data) != wanted[relative]:
            raise SystemExit('z5mod file hash differs: %s' % relative)
    for relative, data in found.items():
        target = THIRD_PARTY / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)


def load_decoder(args, pin):
    if args.z5mod:
        source = Path(args.z5mod) / 'firmware' / 'src'
    else:
        source = THIRD_PARTY / 'firmware' / 'src'
        missing = [f for f in pin['files'] if not (THIRD_PARTY / f).exists()]
        if missing:
            fetch_z5mod(pin)
    if not (source / 'z5mod_fw' / 'container.py').exists():
        raise SystemExit('z5mod decoder not found under %s' % source)
    sys.path.insert(0, str(source))
    return importlib.import_module('z5mod_fw.container')


def apply_patches(module, data):
    """Check every original byte, apply the edits and appends, reseal the CRC."""
    if sha256(module) != data['stock_main_sha256']:
        raise SystemExit('The MAIN module is not the expected Z 5 C 1.50 module; nothing was written.')
    if binascii.crc_hqx(module, 0):
        raise SystemExit('The MAIN module CRC is not valid; nothing was written.')
    body = bytearray(module[:-2])
    edits = sorted(data['edits'], key=lambda e: int(e['offset'], 16))
    position = 0
    for edit in edits:
        offset = int(edit['offset'], 16)
        expected, replacement = bytes.fromhex(edit['expected']), bytes.fromhex(edit['replacement'])
        if offset < position or len(expected) != len(replacement) or offset + len(expected) > len(body):
            raise SystemExit('Patch data is inconsistent at %s.' % edit['name'])
        if body[offset:offset + len(expected)] != expected:
            raise SystemExit('Original bytes differ at %s; nothing was written.' % edit['name'])
        position = offset + len(expected)
    for append in data['appends']:
        align = append['align']
        start = (len(body) + align - 1) & ~(align - 1)
        body += bytes(start - len(body)) + bytes.fromhex(append['data'])
    for edit in edits:
        offset = int(edit['offset'], 16)
        replacement = bytes.fromhex(edit['replacement'])
        body[offset:offset + len(replacement)] = replacement
    body += binascii.crc_hqx(body, 0).to_bytes(2, 'big')
    if binascii.crc_hqx(body, 0):
        raise SystemExit('Internal error: the new CRC does not verify; nothing was written.')
    return bytes(body)


def write_new(path, data):
    if path.exists():
        raise SystemExit('%s already exists; choose a new --output folder.' % path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def main():
    parser = argparse.ArgumentParser(description='Build the Z5 extension firmware from your own Z_5_0150.bin.')
    parser.add_argument('--source', required=True, type=Path, help='Nikon Z 5 C 1.50 update file (Z_5_0150.bin)')
    parser.add_argument('--output', default=Path('output'), type=Path, help='new or empty folder for the results')
    parser.add_argument('--z5mod', help='use this z5mod checkout instead of downloading')
    args = parser.parse_args()

    data = json.loads(DATA.read_text(encoding='utf-8'))
    raw = args.source.read_bytes()
    if sha256(raw) != data['stock_package_sha256']:
        raise SystemExit('This is not the Nikon Z 5 C 1.50 update file (SHA-256 differs); nothing was written.')
    container = load_decoder(args, data['z5mod'])
    _, modules = container.load(args.source)
    main_module = container.main_module(modules)
    output = apply_patches(main_module.data, data)
    if sha256(output) != data['output_sha256']:
        raise SystemExit('The result does not match the tested build; nothing was written.')
    card = CARD_MODULE.read_bytes()
    if sha256(card) != data['card_module_sha256']:
        raise SystemExit('card/Z5APP.BIN does not match the tested build; nothing was written.')

    write_new(args.output / 'EG165050.BIN', output)
    write_new(args.output / 'card' / 'Z5APP.BIN', card)
    write_new(args.output / 'card' / 'SHUTTER' / 'MENU.DAT', MENU_DAT)
    print('Built %s' % (args.output / 'EG165050.BIN'))
    print('  SHA-256 %s' % data['output_sha256'])
    print('Card files are in %s' % (args.output / 'card'))
    print('Add your own sounds as card/SHUTTER/1.WAV ... 5.WAV. See README.md before flashing.')


if __name__ == '__main__':
    main()
