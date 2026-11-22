"""Z5 C 1.50: A38 firmware (release bf821031 + MTP) + a gate at the JPEG encode entry for the film-grain work.
MAIN only, through the official update; boot area untouched.

    python -B src/firmware_hooks/z5_grain_fw.py firmware-input/Z_5_0150.bin --out output/<new dir>/fw/EG165050.BIN
    (phase-0 venv; arm-none-eabi on PATH)

Gate at 0x1009F33A (same entry as the Z30 date stamp, 0x10096EE6): its first 10 bytes
(push.w {r4-r8, lr}; movs r6, #1; mov r5, r0; movs r1, #0) branch to a stub. The stub calls the card module only
  - from the five capture-context call sites 0x100AE756 / E8B4 / EAF0 / EC64 / EDFC (r4 = capture context), and
  - when the module asks for it: release flag word P + 0x14 = 0xBF5A1C00 | bits with bit 5 (0x20) set
    (modules built with -DBURST_EXTRA_BITS including 0x20; A19 and older modules never set it), and
  - through the portrait-UI entry (vui_common: module block magic, function pointer at Q + 4) with kind 17 and
    r1 = the saved frame {r0, r1, r2, r3, r4, r5, r12, lr} (r0 = the encode argument block, r4 = context).
Then it replays the 10 bytes and continues at 0x1009F344. Without the module, or bit 5 clear, or another caller,
the entry runs exactly as stock. The readout module only records; it does not touch pixels.
camera_verified: false.
"""
import hashlib
import json
import pathlib
import struct
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import z5_mtp_fw as mtp  # noqa: E402

rel, vh, dd, gz, bf = mtp.rel, mtp.vh, mtp.dd, mtp.gz, mtp.bf
from z5mod_fw.engine import Edit, Feature  # noqa: E402

A38_SHA256 = 'a8069749f1289a24d9faee150f6f0d571f39381926a04987f4372f45ba4a12ef'
GATE = 0x1009F33A
EXPECTED = bytes.fromhex('2de9f041012605460021')
RESUME = GATE + len(EXPECTED)
SITES = (0x100AE756, 0x100AE8B4, 0x100AEAF0, 0x100AEC64, 0x100AEDFC)
OTHER_SITES = (0x100A7440, 0x100A757C, 0x100A7B46, 0x100A8F24)
GRAIN_BIT = 0x20
KIND = 17

STUB = f'''
    .balign 4
    .global grain_gate
    .thumb_func
grain_gate:
    push    {{r0-r5, r12, lr}}
    mov     r2, lr
''' + ''.join(f'''    ldr     r1, ={s + 5:#x}
    cmp     r2, r1
    beq     grain_hit
''' for s in SITES) + f'''    b       grain_out
grain_hit:
{rel.flag_check(GRAIN_BIT, 'grain_out')}
    movs    r0, #{KIND}
    mov     r1, sp
    bl      vui_common
grain_out:
    pop     {{r0-r5, r12, lr}}
    .byte   {', '.join(f'{b:#04x}' for b in EXPECTED)}
    ldr     pc, ={RESUME | 1:#x}
    .ltorg
'''


def source():
    return mtp.source() + STUB


def feature(stock, at):
    f, syms = mtp.feature(stock, at)
    v = vh.VENEERS + 8 * (len(vh.HOOKS) + 2) + 8 * 3      # after burst_go / burst_lv / mtp veneers
    if stock[GATE - vh.BASE:GATE - vh.BASE + len(EXPECTED)] != EXPECTED:
        raise SystemExit('JPEG encode entry differs from the expected bytes')
    for s in SITES + OTHER_SITES:
        if stock[s - vh.BASE:s - vh.BASE + 4] != vh.branch(s, GATE, True):
            raise SystemExit(f'encode call {s:#x} differs')
    edits = f.edits + (
        Edit('grain_veneer', v - vh.BASE, bytes(8), bytes.fromhex('dff800f0') + struct.pack('<I', syms['grain_gate'] | 1)),
        Edit('grain_gate', GATE - vh.BASE, EXPECTED, vh.branch(GATE, v, False) + bytes.fromhex('00bf00bf00bf')),
    )
    return Feature(name=f.name, description=f.description + '; JPEG encode gate (module kind 17, flag bit 5)',
                   camera_verified=False, edits=edits, appends=f.appends), syms


# ---------------------------------------------------------------- Unicorn checks
BLOCK, QBLOCK, MODFN = 0xC18F9000, 0xC18F9800, 0x50000400
STACK, ARG = 0x50000800, 0x50000600


def run(image, lr, flagword, module):
    """Enter the gate with the given return address; stop at RESUME. Returns (calls, regs, sp, stack)."""
    from unicorn import UC_HOOK_CODE
    from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC
    u = rel.machine(image, None)
    if module != 'none':
        u.mem_write(gz.ANCHOR, struct.pack('<II', BLOCK, ~BLOCK & 0xFFFFFFFF))
        if flagword is not None:
            u.mem_write(BLOCK + 0x14, struct.pack('<I', flagword))
        u.mem_write(BLOCK + 0x0C, struct.pack('<I', QBLOCK))
        u.mem_write(QBLOCK, struct.pack('<II', vh.CM_MAGIC ^ (1 if module == 'bad_magic' else 0),
                                        0 if module == 'no_fn' else MODFN | 1))
    regs = [UC_ARM_REG_R0 + i for i in range(13)]
    calls = []

    def hook(mu, pc, size, unused):
        if pc == MODFN:
            sp = mu.reg_read(UC_ARM_REG_R1)
            calls.append((mu.reg_read(UC_ARM_REG_R0), struct.unpack('<8I', bytes(mu.mem_read(sp, 32))), sp & 7))
            for r in range(4):                 # the module may clobber caller-saved registers
                mu.reg_write(UC_ARM_REG_R0 + r, 0xDEAD0000 + r)
            mu.reg_write(UC_ARM_REG_R0 + 12, 0xDEAD000C)
            mu.reg_write(UC_ARM_REG_PC, mu.reg_read(UC_ARM_REG_LR))
        elif pc == RESUME:
            mu.emu_stop()

    u.hook_add(UC_HOOK_CODE, hook)
    for i, r in enumerate(regs):
        u.reg_write(r, 0x88880000 + i)
    u.reg_write(UC_ARM_REG_R0, ARG); u.reg_write(UC_ARM_REG_SP, STACK); u.reg_write(UC_ARM_REG_LR, lr)
    u.emu_start(GATE | 1, 0, count=400)
    assert u.reg_read(UC_ARM_REG_PC) == RESUME
    sp = u.reg_read(UC_ARM_REG_SP)
    return calls, tuple(u.reg_read(r) for r in regs), u.reg_read(UC_ARM_REG_LR), sp, bytes(u.mem_read(sp, STACK - sp))


FLAGS = (None, 0, 0xBF5A1C00, 0xBF5A1C10, 0xBF5A1C20, 0xBF5A1C33, 0xBF5A1D20, 0x3F5A1C20, 0xFFFFFFFF)
MODULES = ('none', 'bad_magic', 'no_fn', 'ok')


def verify(stock, image):
    n = 0
    lrs = [s + 5 for s in SITES + OTHER_SITES] + [0x10111111, SITES[0] + 4, SITES[0] + 9]
    for lr in lrs:
        want = run(stock, lr, None, 'none')
        assert not want[0]
        for module in MODULES:
            for fw in FLAGS:
                got = run(image, lr, fw, module); n += 1
                on = (lr - 5 in SITES and module == 'ok' and fw is not None
                      and (fw >> 8) == rel.FLAG_MAGIC and fw & GRAIN_BIT)
                case = (hex(lr), module, fw)
                if on:
                    assert len(got[0]) == 1 and got[0][0][0] == KIND, case
                    frame = got[0][0][1]
                    assert frame[0] == ARG and frame[4] == 0x88880004 and frame[7] == lr, (case, frame)
                    assert got[0][0][2] == 0, ('stack not 8-aligned at the module call', case)
                else:
                    assert not got[0], case
                assert got[1:] == want[1:], ('state at resume differs', case)
    return dict(passed=True, runs=n)


def build(package, out):
    rp = out.with_name(out.name + '.json')
    for p in (out, rp):
        if p.exists():
            raise SystemExit(f'{p} exists; refusing to overwrite')
    vh.source, vh.feature = source, feature
    vh.build(package, out)
    from z5mod_fw import container
    _, mods = container.load(package)
    stock = container.main_module(mods).data
    image = out.read_bytes()
    dd.windows = lambda: [(bf.SITE, bf.SITE + 4), (rel.GO_SITE, rel.GO_SITE + 4), (rel.LV_SITE, rel.LV_SITE + 6),
                          (mtp.BUILDER, mtp.BUILDER + len(mtp.EXPECTED)), (GATE, GATE + len(EXPECTED))]
    incoming = dd.audit_incoming(stock)
    if incoming:
        raise SystemExit(f'branches land inside a replaced window: {incoming}')
    report = dict(base_a38=A38_SHA256, burst_rate=bf.verify(stock, image),
                  switches=dict(unicorn=rel.verify_switches(stock, image)),
                  mtp_fast=dict(unicorn=mtp.verify(stock, image)),
                  grain_gate=dict(gate=hex(GATE), expected=EXPECTED.hex(), resume=hex(RESUME),
                                  sites=[hex(s) for s in SITES], kind=KIND,
                                  switch='module installed and P+0x14 = 0xBF5A1C00 | bit 5',
                                  incoming_branch_audit=dict(found=incoming), unicorn=verify(stock, image),
                                  camera_verified=False))
    data = json.loads(rp.read_text(encoding='utf-8'))
    data.update(report)
    rp.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    print('sha256', hashlib.sha256(image).hexdigest())
    print(json.dumps({k: report[k] for k in ('grain_gate', 'mtp_fast')}, indent=1))


if __name__ == '__main__':
    args = sys.argv[1:]
    if len(args) != 3 or args[1] != '--out':
        raise SystemExit(__doc__)
    build(pathlib.Path(args[0]), pathlib.Path(args[2]))
