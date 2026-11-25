"""A84m = A84 with the hook logic moved into the card module (user 2026-11-25). MAIN keeps thin stubs
(research/aspect/a84m_fx43.S) that call the release's vui_common with a register frame; the logic is the module's
aspect_fw.inc (release worktree src/card_module). Same sites, data edits and module tables as A84
(z5_aspect43_a84_fw.py, whose checks are reused here).

    python -B research/aspect/z5_aspect43_a84m_fw.py <a3704c6f EG165050.BIN> --a56 <A56 7477fde9 EG165050.BIN> \
        --module-src <release worktree>/src/card_module --out output/<new dir>/EG165050.BIN \
        [--module-inc <new dir>/aspect43_fx_tables.inc]

Verification in Unicorn: aspect_fw.inc is compiled on its own (cortex-a9 Thumb, the same source the module
includes) and installed behind vui_common (anchor 0xC18F8120 -> CM_MAGIC block -> entry) in every context built
on the A84m image; then every A84 check runs unchanged against it (stock equality while off, A56-reference
geometry for both trim tables, descriptor, other callers, playback sizes / area, A82 geometry, NEF trim). The
"enabled" helper also writes the 4:3 parameter block, as the module always does. Separately, the A84m image
without any module equals the release firmware (sites, geometry, configuration with both trim paths, playback).
camera_verified: false.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import z5_aspect43_a84_fw as m  # noqa: E402

probe = m.probe
BaseCtx = probe.Ctx
MODULE = {}                 # image sha -> (code, entry)


def assemble_stubs(address):
    with tempfile.TemporaryDirectory(dir=probe.ROOT / 'output') as tmp:
        def run(*args):
            return subprocess.check_output(args, cwd=tmp, text=True)
        run('arm-none-eabi-gcc', '-mcpu=cortex-a9', '-mthumb', '-c', str(HERE / 'a84m_fx43.S'), '-o', 'h.o')
        run('arm-none-eabi-ld', f'-Ttext={address:#x}', '-e', 'cfg', '-o', 'h.elf', 'h.o')
        run('arm-none-eabi-objcopy', '-O', 'binary', 'h.elf', 'h.bin')
        symbols = {}
        for line in run('arm-none-eabi-nm', 'h.elf').splitlines():
            value, _, name = line.split()
            symbols[name] = int(value, 16)
        return (Path(tmp) / 'h.bin').read_bytes(), symbols


CODE_AT = probe.RAM + 0x3C200
HARNESS = r'''
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef signed short s16;
#define RAM_LO 0xC1800000u
#define RAM_HI 0xC8700000u
#define IMG_FN __attribute__((noinline, used))
#define FW(t, a) ((t)((a) | 1u))
#include "aspect_fw.inc"
u32 entry(u32 kind, u32 *r) { if (kind >= AF_K_CFG && kind <= AF_K_PLAY) return af_hook(kind, r); return kind; }
'''


def compile_module(src_dir):
    with tempfile.TemporaryDirectory(dir=probe.ROOT / 'output') as tmp:
        (Path(tmp) / 'h.c').write_text(HARNESS, encoding='ascii')
        def run(*args):
            return subprocess.check_output(args, cwd=tmp, text=True)
        run('arm-none-eabi-gcc', '-mcpu=cortex-a9', '-mthumb', '-Os', '-ffreestanding', '-fno-builtin', '-nostdlib',
            '-Wall', '-Werror', f'-I{src_dir}', '-c', 'h.c', '-o', 'h.o')
        run('arm-none-eabi-ld', f'-Ttext={CODE_AT:#x}', '-e', 'entry', '-o', 'h.elf', 'h.o')
        run('arm-none-eabi-objcopy', '-O', 'binary', '-j', '.text', 'h.elf', 'h.bin')
        syms = {l.split()[2]: int(l.split()[0], 16) for l in run('arm-none-eabi-nm', 'h.elf').splitlines()
                if len(l.split()) == 3}
        undefined = run('arm-none-eabi-nm', '-u', 'h.elf').strip()
        assert not undefined, undefined
        code = (Path(tmp) / 'h.bin').read_bytes()
    assert len(code) < 0x3C00, len(code)
    return code, syms['entry']


def install(ctx, code, entry):
    p, q = probe.RAM + 0x3C000, probe.RAM + 0x3C100
    ctx.u.mem_write(CODE_AT, code)
    ctx.u.mem_write(m.ANCHOR, struct.pack('<2I', p, ~p & 0xFFFFFFFF))
    ctx.u.mem_write(p + 0xC, struct.pack('<I', q))
    ctx.u.mem_write(q, struct.pack('<2I', m.CM_MAGIC, entry | 1))


def ctx_factory(image):
    ctx = BaseCtx(image)
    mod = MODULE.get(hashlib.sha256(image).hexdigest())
    if mod:
        install(ctx, *mod)
    return ctx


BLOCK43 = struct.pack('<I2H6H2B2x', m.MAGIC, 332, 0, *((332, 0) * 3), 3, 1)
_enable = m.enable


def enable_with_block(ctx, tables):
    _enable(ctx, tables)
    ctx.u.mem_write(probe.RAM + 0x20000 + m.PRM_OFF, BLOCK43.ljust(m.PRM_BYTES, b'\0'))


def no_module_equals_release(raw, image):
    """The A84m image with no module behind vui_common: everything as the release firmware."""
    stock, bare = BaseCtx(raw), BaseCtx(image)
    info = probe.RAM + 0x1000
    n = 0
    for area in range(9):
        for size in range(3):
            for ctx in (stock, bare):
                ctx.u.mem_write(info, bytes(0x400))
                ctx.u.mem_write(info + 0x200, bytes([area])); ctx.u.mem_write(info + 0x221, bytes([size]))
            regs = [0x11110000 + i for i in range(13)]
            regs[0] = area; regs[4] = probe.RAM + 0x6000; regs[5] = info; regs[9] = size
            for name in ('offset', 'trim', 'trim2'):
                assert m.run_site(stock, name, regs) == m.run_site(bare, name, regs), (name, area, size)
                n += 1
    for area in (0, 1, 2, 4, 5, 7, 8):
        assert m.measure(stock, area) == m.measure(bare, area), ('no module', area)
    for size in range(3):
        for k220, r2 in ((0, 0), (1, 0), (2, 0), (3, 0), (0, 1), (1, 1)):
            assert m.run_cfg(stock, size, k220, r2) == m.run_cfg(bare, size, k220, r2), (size, k220, r2)
    return dict(site_replays=n, geometry_areas=[0, 1, 2, 4, 5, 7, 8], cfg_paths=18)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path)
    ap.add_argument('--a56', required=True, type=Path)
    ap.add_argument('--module-src', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--module-inc', type=Path)
    a = ap.parse_args()
    code, entry = compile_module(a.module_src)
    raw = a.source.read_bytes()
    m.assemble = assemble_stubs
    m.enable = enable_with_block
    probe.Ctx = ctx_factory

    # the image is only known inside build(): register the module for it when verify() first builds a context
    real_verify = m.verify

    def verify(raw_, image, tables, a56):
        MODULE[hashlib.sha256(image).hexdigest()] = (code, entry)
        res = real_verify(raw_, image, tables, a56)
        res['no_module'] = no_module_equals_release(raw_, image)
        res['module_logic'] = dict(bytes=len(code), entry=hex(entry),
                                   sha256=hashlib.sha256(code).hexdigest(), source='aspect_fw.inc')
        return res
    m.verify = verify
    m.verify_trace = lambda raw_, image, tables: dict(
        note='kind 19 trace runs inside the module (a83_trace.inc, A84T layout); not in this harness')
    m.build(a.source, a.a56, a.out, a.module_inc)


if __name__ == '__main__':
    main()
