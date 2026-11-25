"""A87m = A85m firmware + the hnef stub (kind 25, 0x1034F65A, research/aspect/a87m_fx43.S) with the A87 module logic
(aspect_fw.inc): the RAW branch of the playback decode setup 0x1034F5F6 gets area 8 for a file entry of the current
ratio (A86 camera: NEF-only playback configurations stayed at area 0), and the instant review of the shot just
taken, whose file the file service cannot describe yet (0 x 0), takes the current ratio (A86 DIAG14 39988 ms).

    python -B research/aspect/z5_aspect43_a87m_fw.py <a3704c6f EG165050.BIN> --a56 <A56 7477fde9 EG165050.BIN> \
        --module-src <release worktree>/src/card_module --out output/<new dir>/EG165050.BIN

Checks: everything of A86 (A84 / A84m / A85m checks, the query matrix with the A86 rule, nef_main), plus
`hnef` (the replaced window with and without the module: registers, the [r4] store and area 8 only while enabled
for a file entry of the current ratio) and `review` (the fallback only within 6 s of a remapped shot, only when
the service gives nothing, with the ratio of that shot). camera_verified: false.
"""
import itertools
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from unicorn.arm_const import UC_ARM_REG_CPSR, UC_ARM_REG_R0, UC_ARM_REG_SP

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import z5_aspect43_a86m_fw as a86  # noqa: E402

a85, m, probe, BaseCtx = a86.a85, a86.m, a86.probe, a86.BaseCtx
mm = a85.mm
HNEF = 0x1034F65A
CLOCK = probe.RAM + 0x3BF00
COPY = probe.RAM + 0x20000          # the module copy enable() uses; group-1 pointer = COPY + 32
PTR = 0xC18C7598


def assemble_stubs(address):
    with tempfile.TemporaryDirectory(dir=probe.ROOT / 'output') as tmp:
        def run(*args):
            return subprocess.check_output(args, cwd=tmp, text=True)
        run('arm-none-eabi-gcc', '-mcpu=cortex-a9', '-mthumb', '-c', str(HERE / 'a87m_fx43.S'), '-o', 'h.o')
        run('arm-none-eabi-ld', f'-Ttext={address:#x}', '-e', 'cfg', '-o', 'h.elf', 'h.o')
        run('arm-none-eabi-objcopy', '-O', 'binary', 'h.elf', 'h.bin')
        symbols = {}
        for line in run('arm-none-eabi-nm', 'h.elf').splitlines():
            value, _, name = line.split()
            symbols[name] = int(value, 16)
        return (Path(tmp) / 'h.bin').read_bytes(), symbols


mm.HARNESS = mm.HARNESS.replace('kind <= AF_K_EXT', 'kind <= AF_K_NEF').replace(
    '#include "aspect_fw.inc"', f'#define CM_MS_CLOCK {CLOCK:#x}u\n#include "aspect_fw.inc"')
assert 'AF_K_NEF' in mm.HARNESS and 'CM_MS_CLOCK' in mm.HARNESS
a85.assemble_stubs = assemble_stubs


def set_on(ctx, ratio, shot_ms=None, now=100000):
    """Aspect on with a parameter block of this ratio; log[1..3] = one remapped shot at shot_ms."""
    u = ctx.u
    u.mem_write(PTR, struct.pack('<I', COPY + 32))
    u.mem_write(COPY + 0x200, struct.pack('<I', m.MAGIC) + bytes(16) + bytes([3 if ratio == 1 else 2, ratio]))
    u.mem_write(COPY + 224, struct.pack('<4I', 1, 1 if shot_ms is not None else 0, shot_ms or 0, ratio))
    u.mem_write(CLOCK, struct.pack('<I', now))


def set_off(ctx):
    ctx.u.mem_write(PTR, struct.pack('<I', 0x10000000))


def run_hnef(ctx, info, meta):
    u = ctx.u
    regs = [0x11110000 + i for i in range(13)]
    regs[4] = info; regs[6] = 0
    for i, v in enumerate(regs):
        u.reg_write(UC_ARM_REG_R0 + i, v)
    u.mem_write(0x5000F000, struct.pack('<I', meta))
    u.reg_write(UC_ARM_REG_CPSR, 0x33); u.reg_write(UC_ARM_REG_SP, 0x5000F000)
    u.emu_start(HNEF | 1, HNEF + 4, count=4000)
    r = [u.reg_read(UC_ARM_REG_R0 + i) for i in range(13)] + [u.reg_read(UC_ARM_REG_SP)]
    return r[:12] + r[13:], bytes(u.mem_read(info, 1)), u.mem_read(info + 0x200, 1)[0]


def verify_hnef(raw, image):
    stock, off, on = BaseCtx(raw), a85.bare(image), probe.Ctx(image)
    info, meta = probe.RAM + 0x30000, probe.RAM + 0x31000
    mains = ((5352, 4016), (6016, 2576), (6016, 4016), (4008, 3008), (3008, 1288), (4016, 4016), (0, 0))
    n = dict(area8=0, unchanged=0)
    for main, ratio, enabled in itertools.product(mains, (1, 2), (0, 1)):
        res = []
        for ctx in (stock, off, on):
            ctx.u.mem_write(info, b'\x77' + bytes(0x3FF)); ctx.u.mem_write(info + 0x200, b'\x55')
            ctx.u.mem_write(meta, bytes(0x20)); ctx.u.mem_write(meta + 12, struct.pack('<2h', *main))
            if enabled:
                set_on(ctx, ratio)
            else:
                set_off(ctx)
            res.append(run_hnef(ctx, info, meta))
        assert res[1] == res[0], ('no module', main)
        assert res[2][:2] == res[0][:2] and res[0][1] == b'\x00', ('registers / [r4]', main, ratio, enabled)
        shape = 1 if main in ((5352, 4016), (4008, 3008)) else 2 if main in ((6016, 2576), (3008, 1288)) else 0
        if enabled and shape == ratio:
            assert res[2][2] == 8, ('area 8', main, ratio)
            n['area8'] += 1
        else:
            assert res[2][2] == 0x55, ('area kept', main, ratio, enabled, res[2][2])
            n['unchanged'] += 1
    return n


def verify_review(raw, image):
    on = probe.Ctx(image)
    ctxobj, rec, out = probe.RAM + 0x30000, probe.RAM + 0x31000, probe.RAM + 0x34000
    n = dict(fallback=0, none=0)
    for ratio, age, ans, typ, small in itertools.product((1, 2), (500, 5900, 6100, None), (None, (0, 0, 0), (0, 1440, 1080)),
                                                         (1, 2), (0, 1)):
        u = on.u
        set_on(on, ratio, None if age is None else 100000 - age)
        u.mem_write(ctxobj, bytes(0x40)); u.mem_write(ctxobj, struct.pack('<IB', 0x5A5A0001, typ))
        u.mem_write(ctxobj + 12, struct.pack('<I', rec))
        u.mem_write(rec, bytes(0x400)); u.mem_write(rec + 0x398, struct.pack('<2H', 6016, 4016))
        u.mem_write(out, bytes(8))
        a85.FAKE['answer'] = ans; a85.FAKE['calls'] = []
        m.run_with(on, a85.STD, [out, 0, small, 0x11110003, ctxobj] + [0x11110000 + i for i in range(5, 13)],
                   a85.STD_CALLERS[1])
        got = struct.unpack('<2H', bytes(u.mem_read(rec + 0x398, 4)))
        written = struct.unpack('<2H', bytes(u.mem_read(out, 4)))
        if typ == 1 and ans and ans[1:] == (1440, 1080):
            assert got == (5352, 4016), ('file says 4:3', got)           # A86 path, independent of the shot
            continue
        fallback = typ == 1 and (not ans or ans[1:] == (0, 0)) and age is not None and age <= 6000
        if fallback:
            want = (5352, 4016) if ratio == 1 else (6016, 2576)
            assert got == want, ('fallback', ratio, age, ans, got)
            assert written == a85.shape_size(*want, small), ('fallback preview', written)
            n['fallback'] += 1
        else:
            assert got == (6016, 4016), ('no fallback', ratio, age, ans, typ, got)
            n['none'] += 1
    return n


def main():
    real_both = a85.verify_query    # replaced by a86.main; wrap at build time

    orig_build = m.build

    def build(source, a56, out, inc):
        m.SITES['hnef'] = (HNEF, '2670 0098', False)       # after A85's hext: A85's veneer slots unchanged
        real_run_site = m.run_site

        def run_site(ctx, name, regs0):
            return [] if name == 'hnef' else real_run_site(ctx, name, regs0)
        m.run_site = run_site
        prev = a85.verify_query

        def checks(raw, image):
            res = prev(raw, image)
            res['hnef'] = verify_hnef(raw, image)
            res['review'] = verify_review(raw, image)
            return res
        a85.verify_query = checks
        orig_build(source, a56, out, inc)
    m.build = build
    assert real_both
    # a86.main installs its own m.build wrapper (A85 hash check) around ours: skip that wrapper, keep its checks
    real_a86_build = None

    def a86_main_without_hash():
        nonlocal real_a86_build
        real_verify_query = a85.verify_query

        def both(raw, image):
            assert real_verify_query
            res = a86.verify_query(raw, image)
            res['nef_main_check'] = a86.verify_nef_main(raw, image)
            return res
        a85.verify_query = both
        a85.main()
    a86_main_without_hash()


if __name__ == '__main__':
    main()
