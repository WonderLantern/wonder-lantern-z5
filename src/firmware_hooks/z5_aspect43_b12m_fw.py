"""B12m = B10m + the live-view manager message call 0x101904A8 (`bl 0x1018D014`, lr 0x101904AD) routed through hszw's
veneer (0x105BA3C4, zero new trampolines), with the B12 stub (research/aspect/b12m_fx43.S). The stub always ends in the
stock 0x1018D015 for this lr (the module never "handles" it). Module (release worktree aspect_fw.inc AF_LV_MSG,
aspect_menu.inc): B12 readout of each message (DIAG 0x37400, decode_b12.py) and, after a 4:3 toggle, marks setting
0x1624 present (= FX) in the next message, as a DX -> FX switch does, so the manager rebuilds the fit (black frame).

Why (B11 camera DIAG20 / DIAG01, 2026-11-27): the B11 settings-bus post never made the manager rebuild the fit (0 fit
answers), while an EVF switch did (fit (72, 54, 1208, 906) then (40, 0, 680, 480)); the source window stayed 0 in both,
so B11's x0 == 332 test was the wrong target. 0x1018CF28 sets flag bit 3 (fit + window rebuild) when msg + 0x64 (0x1624
present) is set, value byte msg + 0x68.

    python -B research/aspect/z5_aspect43_b12m_fw.py <a3704c6f EG165050.BIN> --a56 <A56 7477fde9 EG165050.BIN> \
        --module-src <absolute release worktree>/src/card_module --b1-diag <B1 DIAG17.BIN> \
        --out output/<new dir>/EG165050.BIN

Checks: every B10 check, plus
  lv_msg  the 0x101904A8 call in every context (stock, bare, not chosen, chosen tables out / in): it reaches
          0x1018D014 with r0..r12, sp, lr equal to stock, and the 0x160-byte message and 0x40-byte second block are
          unchanged (no 4:3 toggle pending in the harness).
camera_verified: false.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import z5_aspect43_b10m_fw as b10  # noqa: E402

b9 = b10.b9
b8, b7, b4, b3, a85, m, probe = b9.b8, b9.b7, b9.b4, b9.b3, b9.a85, b9.m, b9.probe
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.arm_const import (  # noqa: E402
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6,
    UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_CPSR,
)

SITE = ('hlvmsg live-view manager message -> hszw veneer', 0x101904A8, bytes.fromhex('fcf7b4fd'))
REGS = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6,
        UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12,
        UC_ARM_REG_SP, UC_ARM_REG_LR)


def assemble_stubs(address):
    with tempfile.TemporaryDirectory(dir=probe.ROOT / 'output') as tmp:
        def run(*args):
            return subprocess.check_output(args, cwd=tmp, text=True)
        run('arm-none-eabi-gcc', '-mcpu=cortex-a9', '-mthumb', '-c', str(HERE / 'b12m_fx43.S'), '-o', 'h.o')
        run('arm-none-eabi-ld', f'-Ttext={address:#x}', '-e', 'cfg', '-o', 'h.elf', 'h.o')
        run('arm-none-eabi-objcopy', '-O', 'binary', 'h.elf', 'h.bin')
        symbols = {}
        for line in run('arm-none-eabi-nm', 'h.elf').splitlines():
            value, _, name = line.split()
            symbols[name] = int(value, 16)
        return (Path(tmp) / 'h.bin').read_bytes(), symbols


a85.assemble_stubs = assemble_stubs
b10.assemble_stubs = assemble_stubs


def apply_b12_site(data):
    name, addr, old = SITE
    off = addr - m.BASE
    if bytes(data[off:off + 4]) != old:
        raise ValueError('original bytes differ at %#x: got %s want %s' % (addr, bytes(data[off:off + 4]).hex(), old.hex()))
    new = m.hooks.branch(addr, b4.HSZW_VENEER, True)
    data[off:off + 4] = new
    return [dict(name=name, address=hex(addr), original=old.hex(), replacement=new.hex())]


def run_msg(ctx):
    """Execute the bl at 0x101904A8 and stop on 0x1018D014; return registers, message and second block."""
    u = ctx.u
    lv = probe.RAM + 0x14000                       # manager context (r4 in 0x10190438 = r0)
    msg = probe.RAM + 0x15000                      # caller's sp (r1)
    blk = msg + 0x160                              # caller's sp + 0x160 (r2)
    pattern = bytes((i * 37 + 11) & 0xFF for i in range(0x1A0))
    u.mem_write(lv, bytes(0x100))
    u.mem_write(msg, pattern)
    u.reg_write(UC_ARM_REG_R0, lv)
    u.reg_write(UC_ARM_REG_R1, msg)
    u.reg_write(UC_ARM_REG_R2, blk)
    for k, reg in enumerate((UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
                             UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12)):
        u.reg_write(reg, 0x1000 + k)
    u.reg_write(UC_ARM_REG_R4, lv)
    u.reg_write(UC_ARM_REG_SP, msg)
    u.reg_write(UC_ARM_REG_CPSR, 0x33)
    stop = u.hook_add(UC_HOOK_CODE, lambda mu, a, s, d: mu.emu_stop(), begin=0x1018D014, end=0x1018D014)
    u.emu_start(0x101904A8 | 1, 0x1018D014, count=60000)
    u.hook_del(stop)
    assert u.reg_read(UC_ARM_REG_CPSR) & 0x20, 'not thumb at 0x1018D014'
    pc = u.reg_read(probe.UC_ARM_REG_PC) if hasattr(probe, 'UC_ARM_REG_PC') else None
    return tuple(u.reg_read(r) for r in REGS), bytes(u.mem_read(msg, 0x1A0)), pc


def verify_b12(raw, image):
    c = b3.contexts(raw, image)
    ref = run_msg(c['stock'])
    assert ref[0][14] == 0x101904AD, hex(ref[0][14])
    for name in ('bare', 'not_chosen', 'not_chosen_233', 'chosen_out', 'chosen_in'):
        got = run_msg(c[name])
        assert got[0] == ref[0], (name, [hex(x) for x in got[0]], [hex(x) for x in ref[0]])
        assert got[1] == ref[1], (name, 'message changed')
    return dict(lv_msg=dict(lr=hex(ref[0][14]), regs_equal=True, message_unchanged=True))


def main():
    orig_m_build = m.build

    def innermost(source, a56, out, inc):
        prev_kind2 = m.apply_kind2                      # b10 + b9 + b8 + b7 + b4 wrappers

        def kind2(data):
            edits = prev_kind2(data)
            edits.extend(apply_b12_site(data))
            return edits
        m.apply_kind2 = kind2
        prev = a85.verify_query

        def checks(raw, image):
            res = prev(raw, image)
            res.update(verify_b12(raw, image))
            return res
        a85.verify_query = checks
        orig_m_build(source, a56, out, inc)
    m.build = innermost
    b10.main()


if __name__ == '__main__':
    main()
