"""A90m = A87m + kind 27 at 0x10355DCC (research/aspect/a90m_fx43.S) with the A90 module logic (aspect_fw.inc
af_full): the playback prep's full-size image size (kind-6 entry, desc + 0x60) of our NEF files becomes the file
record's main size (5352 x 4016 / 6016 x 2576) instead of the FX 6016 x 4016 of the output-size table.

Why (A89 camera, DIAG04 snapshots, 2026-11-25): after A88 every size of our NEF files was ours except that entry,
which the descriptor prep fills after the preview hooks (0x1035503C -> 0x10355DCC(class, size) -> 0x102092A6).
Instant review refuses zoom and the navigator draws a 3:2 box around our 4:3 / 2.33 preview.

    python -B research/aspect/z5_aspect43_a90m_fw.py <a3704c6f EG165050.BIN> --a56 <A56 7477fde9 EG165050.BIN> \
        --module-src <release worktree>/src/card_module --out output/<new dir>/EG165050.BIN

Checks: every A87 / A88 check with the A90 module logic, plus `hfull`: 0x10355DCC with and without the module for
both callers and another caller, file types 0-2, record main sizes (ours and stock), classes and sizes: without the
module identical to the release firmware; with it the record's main size only for type 1, our shapes and the two
callers, else stock; r4-r11 and sp as stock. camera_verified: false.
"""
import itertools
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import z5_aspect43_a88m_fw as a88  # noqa: E402

a87, a86, a85, m, probe, BaseCtx = a88.a87, a88.a86, a88.a85, a88.m, a88.probe, a88.BaseCtx
mm = a85.mm
HFULL = 0x10355DCC
CALLERS = (0x10355051, 0x1034EA21)
OURS = ((5352, 4016), (4008, 3008), (2664, 2000), (6016, 2576), (4512, 1936), (3008, 1288))


def assemble_stubs(address):
    with tempfile.TemporaryDirectory(dir=probe.ROOT / 'output') as tmp:
        def run(*args):
            return subprocess.check_output(args, cwd=tmp, text=True)
        run('arm-none-eabi-gcc', '-mcpu=cortex-a9', '-mthumb', '-c', str(HERE / 'a90m_fx43.S'), '-o', 'h.o')
        run('arm-none-eabi-ld', f'-Ttext={address:#x}', '-e', 'cfg', '-o', 'h.elf', 'h.o')
        run('arm-none-eabi-objcopy', '-O', 'binary', 'h.elf', 'h.bin')
        symbols = {}
        for line in run('arm-none-eabi-nm', 'h.elf').splitlines():
            value, _, name = line.split()
            symbols[name] = int(value, 16)
        return (Path(tmp) / 'h.bin').read_bytes(), symbols


mm.HARNESS = mm.HARNESS.replace('kind <= AF_K_NEF', 'kind <= AF_K_FULL')
assert 'AF_K_FULL' in mm.HARNESS
a85.assemble_stubs = assemble_stubs


def verify_hfull(raw, image):
    stock, off, on = BaseCtx(raw), a85.bare(image), probe.Ctx(image)
    ctx, rec, out = probe.RAM + 0x30000, probe.RAM + 0x31000, probe.RAM + 0x34000
    mains = OURS[:1] + OURS[3:4] + ((6016, 4016), (6040, 4032), (0, 0))
    n = dict(ours=0, stock=0)
    for lr, typ, main, cls, size in itertools.product(CALLERS + (0x10355015,), (0, 1, 2), mains, (0, 1, 4), (0, 1, 2)):
        res = []
        for c in (stock, off, on):
            c.u.mem_write(ctx, bytes(0x40)); c.u.mem_write(ctx + 4, bytes([typ]))
            c.u.mem_write(ctx + 12, struct.pack('<I', rec))
            c.u.mem_write(rec, bytes(0x400)); c.u.mem_write(rec + 0x398, struct.pack('<2H', *main))
            c.u.mem_write(out, bytes(8))
            regs = [out, cls, size, 0x11110003, ctx] + [0x11110000 + i for i in range(5, 13)]
            r = m.run_with(c, HFULL, regs, lr)
            res.append((struct.unpack('<2H', bytes(c.u.mem_read(out, 4))), r[4:12] + r[13:]))
        assert res[1] == res[0], ('no module', hex(lr), typ, main, cls, size)
        assert res[2][1] == res[0][1], ('registers', hex(lr), typ, main)
        if lr in CALLERS and typ == 1 and main in OURS:
            assert res[2][0] == main, ('ours', hex(lr), main, res[2][0])
            n['ours'] += 1
        else:
            assert res[2][0] == res[0][0], ('stock', hex(lr), typ, main, cls, size, res[2][0], res[0][0])
            n['stock'] += 1
    return n


def main():
    a86.verify_nef_main = a88.verify_nef_main
    orig_build = m.build

    def build(source, a56, out, inc):
        assert 'hnef' in m.SITES and 'hfull' not in m.SITES        # a87's wrapper ran: hext, hnef, hfull
        m.SITES['hfull'] = (HFULL, '0b46 1146', False)
        real_run_site = m.run_site

        def run_site(ctx, name, regs0):
            return [] if name == 'hfull' else real_run_site(ctx, name, regs0)
        m.run_site = run_site
        prev = a85.verify_query

        def checks(raw, image):
            res = prev(raw, image)
            res['hfull'] = verify_hfull(raw, image)
            return res
        a85.verify_query = checks
        orig_build(source, a56, out, inc)
    m.build = build
    a87.main()


if __name__ == '__main__':
    main()
