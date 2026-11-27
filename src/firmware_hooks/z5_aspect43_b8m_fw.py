"""B8m = B7m + the second aspect-class site `bl 0x10190576` at 0x1019702E (lr 0x10197033, in 0x10196FE0) through hszw's
veneer, with the B8 stub (research/aspect/b8m_fx43.S: class tail for both class lrs). Module: aspect_fw.inc AF_LV_CLASS2.

Why (B7 camera, DIAG14-15, 2026-11-26): the first class site answered 3 twenty times, yet capture info + 0x4E0 was 0
at the save: 0x10196FE0 re-derives it from + 0x488 on a live-view mode change. Writers of + 0x4E0: 0x101905C4 (B7),
0x10197032 (this), 0x10158AA0 (the shot's kind + 0x21E), 0x10153256 / 0x10192278 (reset to r5).

    python -B research/aspect/z5_aspect43_b8m_fw.py <a3704c6f EG165050.BIN> --a56 <A56 7477fde9 EG165050.BIN> \
        --module-src <absolute release worktree>/src/card_module --b1-diag <B1 DIAG17.BIN> \
        --out output/<new dir>/EG165050.BIN

Checks: every B7 check, plus lv_class2: 0x10197026 .. 0x10197036 (ldrb +0x488, strb +0x4CC, the site, strb +0x4E0)
for areas 0-8: stock, no module and not chosen = the stock class; chosen with the area setting FX -> 3 for area 0, the
other areas stock. camera_verified: false.
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import z5_aspect43_b7m_fw as b7  # noqa: E402

b4, b3, a85, m, probe = b7.b4, b7.b3, b7.a85, b7.m, b7.probe
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R4, UC_ARM_REG_SP, UC_ARM_REG_CPSR  # noqa: E402

SITE2, OLD2 = 0x1019702E, bytes.fromhex('f9f7a2fa')


def assemble_stubs(address):
    with tempfile.TemporaryDirectory(dir=probe.ROOT / 'output') as tmp:
        def run(*args):
            return subprocess.check_output(args, cwd=tmp, text=True)
        run('arm-none-eabi-gcc', '-mcpu=cortex-a9', '-mthumb', '-c', str(HERE / 'b8m_fx43.S'), '-o', 'h.o')
        run('arm-none-eabi-ld', f'-Ttext={address:#x}', '-e', 'cfg', '-o', 'h.elf', 'h.o')
        run('arm-none-eabi-objcopy', '-O', 'binary', 'h.elf', 'h.bin')
        symbols = {}
        for line in run('arm-none-eabi-nm', 'h.elf').splitlines():
            value, _, name = line.split()
            symbols[name] = int(value, 16)
        return (Path(tmp) / 'h.bin').read_bytes(), symbols


a85.assemble_stubs = assemble_stubs


def apply_site2(data):
    off = SITE2 - m.BASE
    if bytes(data[off:off + 4]) != OLD2:
        raise ValueError('original bytes differ at %#x' % SITE2)
    new = m.hooks.branch(SITE2, b4.HSZW_VENEER, True)
    data[off:off + 4] = new
    return dict(name='hcls2 live-view aspect class (mode change) -> hszw veneer', address=hex(SITE2),
                original=OLD2.hex(), replacement=new.hex())


def class2(ctx, area):
    """Run 0x10197026 .. 0x10197036 with r4 = a state whose + 0x488 = area; the area setting RAM = area."""
    u = ctx.u
    try:
        u.mem_map(0xC2504000, 0x1000)
    except Exception:
        pass
    u.mem_write(0xC25043A4, struct.pack('<I', area))
    st = b4.LV_STATE
    u.mem_write(st, bytes(0x600)); u.mem_write(st + 0x488, bytes([area]))
    u.reg_write(UC_ARM_REG_R4, st); u.reg_write(UC_ARM_REG_SP, 0x5000F000); u.reg_write(UC_ARM_REG_CPSR, 0x33)
    stop = u.hook_add(UC_HOOK_CODE, lambda mu, a, s, d: mu.emu_stop(), begin=0x10197036, end=0x10197036)
    u.emu_start(0x10197026 | 1, 0x10197036, count=20000)
    u.hook_del(stop)
    return u.mem_read(st + 0x4CC, 1)[0], u.mem_read(st + 0x4E0, 1)[0]


def verify_lv_class2(raw, image):
    c = b3.contexts(raw, image)
    res = {}
    for area in range(9):
        ref = class2(c['stock'], area)
        assert ref == (area, b7.STOCK_CLASS[area]), (area, ref)
        for name in ('bare', 'not_chosen', 'not_chosen_233'):
            assert class2(c[name], area) == ref, (name, area)
        for name in ('chosen_out', 'chosen_in'):
            got = class2(c[name], area)
            assert got == ((0, 3) if area == 0 else ref), (name, area, got)
        res['area %d' % area] = dict(stock=ref[1], chosen=class2(c['chosen_out'], area)[1])
    return res


def main():
    orig_m_build = m.build

    def innermost(source, a56, out, inc):
        prev_kind2 = m.apply_kind2                      # b7 + b4 wrappers (crop and first class site)

        def kind2(data):
            edits = prev_kind2(data)
            edits.append(apply_site2(data))
            return edits
        m.apply_kind2 = kind2
        prev = a85.verify_query

        def checks(raw, image):
            res = prev(raw, image)
            res['lv_class2'] = verify_lv_class2(raw, image)
            return res
        a85.verify_query = checks
        orig_m_build(source, a56, out, inc)
    m.build = innermost
    b7.main()


if __name__ == '__main__':
    main()
