"""B15m = B12m + the live-view manager's fit call 0x1018D268 (`bl 0x1018B4A2`, lr 0x1018D26D) routed through hszw's
veneer (0x105BA3C4, zero new trampolines), with the B15 stub (research/aspect/b15m_fx43.S). Module (release worktree
aspect_fw.inc AF_LV_FITCALL): for native 2.33:1 live view it runs the stock fit and then turns the 16:9-class
rectangles into 2.33:1 (the stock fit classifies by aspect, 0x101DC978: > 1.64 is 16:9, so a 6016 x 2576 pair gets the
16:9 frame (0, 36, 720, 442), offline run of 0x1018B4A2 via b1_fit_replay); for everything else the stub tails to the
stock 0x1018B4A3.

Why (B14 camera OK for 4:3, 2026-11-27; user asked to carry native live view over to 2.33:1).

    python -B research/aspect/z5_aspect43_b15m_fw.py <a3704c6f EG165050.BIN> --a56 <A56 7477fde9 EG165050.BIN> \
        --module-src <absolute release worktree>/src/card_module --b1-diag <B1 DIAG17.BIN> \
        --out output/<new dir>/EG165050.BIN

Checks: every B12 check, plus
  lv_fitcall  the 0x1018D268 call in every context reaches 0x1018B4A2 with r0..r12, sp, lr equal to stock (the
              offline harness has no 2.33:1 choice, so the module never handles it there).
camera_verified: false.
"""
from pathlib import Path
import struct
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import z5_aspect43_b12m_fw as b12  # noqa: E402

b10, b9 = b12.b10, b12.b9
b4, b3, a85, m, probe = b9.b4, b9.b3, b9.a85, b9.m, b9.probe
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.arm_const import (  # noqa: E402
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6,
    UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12,
    UC_ARM_REG_SP, UC_ARM_REG_CPSR,
)

SITE = ('hlvfit live-view manager fit -> hszw veneer', 0x1018D268, bytes.fromhex('fef71bf9'))
REGS = b12.REGS


def assemble_stubs(address):
    import subprocess
    import tempfile
    with tempfile.TemporaryDirectory(dir=probe.ROOT / 'output') as tmp:
        def run(*args):
            return subprocess.check_output(args, cwd=tmp, text=True)
        run('arm-none-eabi-gcc', '-mcpu=cortex-a9', '-mthumb', '-c', str(HERE / 'b15m_fx43.S'), '-o', 'h.o')
        run('arm-none-eabi-ld', f'-Ttext={address:#x}', '-e', 'cfg', '-o', 'h.elf', 'h.o')
        run('arm-none-eabi-objcopy', '-O', 'binary', 'h.elf', 'h.bin')
        symbols = {}
        for line in run('arm-none-eabi-nm', 'h.elf').splitlines():
            value, _, name = line.split()
            symbols[name] = int(value, 16)
        return (Path(tmp) / 'h.bin').read_bytes(), symbols


a85.assemble_stubs = assemble_stubs
b10.assemble_stubs = assemble_stubs
b12.assemble_stubs = assemble_stubs


def apply_b15_site(data):
    name, addr, old = SITE
    off = addr - m.BASE
    if bytes(data[off:off + 4]) != old:
        raise ValueError('original bytes differ at %#x: got %s want %s' % (addr, bytes(data[off:off + 4]).hex(), old.hex()))
    new = m.hooks.branch(addr, b4.HSZW_VENEER, True)
    data[off:off + 4] = new
    return [dict(name=name, address=hex(addr), original=old.hex(), replacement=new.hex())]


def run_fitcall(ctx):
    u = ctx.u
    try:
        u.mem_map(0xC2504000, 0x1000)               # the area setting the module reads (AF_AREA_NOW)
    except Exception:
        pass
    u.mem_write(0xC25043A4, struct.pack('<I', 0))   # FX
    frame = probe.RAM + 0x16000
    u.mem_write(frame, bytes(0x100))
    u.reg_write(UC_ARM_REG_R0, frame + 8)
    u.reg_write(UC_ARM_REG_R1, 8)
    for k, reg in enumerate((UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
                             UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12)):
        u.reg_write(reg, 0x2000 + k)
    u.reg_write(UC_ARM_REG_SP, frame)
    u.reg_write(UC_ARM_REG_CPSR, 0x33)
    stop = u.hook_add(UC_HOOK_CODE, lambda mu, a, s, d: mu.emu_stop(), begin=0x1018B4A2, end=0x1018B4A2)
    u.emu_start(0x1018D268 | 1, 0x1018B4A2, count=60000)
    u.hook_del(stop)
    assert u.reg_read(UC_ARM_REG_CPSR) & 0x20, 'not thumb at 0x1018B4A2'
    return tuple(u.reg_read(r) for r in REGS), bytes(u.mem_read(frame, 0x100))


def verify_b15(raw, image):
    c = b3.contexts(raw, image)
    ref = run_fitcall(c['stock'])
    assert ref[0][14] == 0x1018D26D, hex(ref[0][14])
    for name in ('bare', 'not_chosen', 'not_chosen_233', 'chosen_out', 'chosen_in'):
        got = run_fitcall(c[name])
        assert got == ref, (name, [hex(x) for x in got[0]], [hex(x) for x in ref[0]])
    return dict(lv_fitcall=dict(lr=hex(ref[0][14]), regs_equal=True, frame_unchanged=True))


def main():
    orig_m_build = m.build

    def innermost(source, a56, out, inc):
        prev_kind2 = m.apply_kind2                      # b12 + b10 + b9 + b8 + b7 + b4 wrappers

        def kind2(data):
            edits = prev_kind2(data)
            edits.extend(apply_b15_site(data))
            return edits
        m.apply_kind2 = kind2
        prev = a85.verify_query

        def checks(raw, image):
            res = prev(raw, image)
            res.update(verify_b15(raw, image))
            return res
        a85.verify_query = checks
        orig_m_build(source, a56, out, inc)
    m.build = innermost
    b12.main()


if __name__ == '__main__':
    main()
