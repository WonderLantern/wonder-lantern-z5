"""Z5 C 1.50: histogram on the level page, gated by a RAM mask the card module sets.

    python -B research/overlay-investigation/z5_dual_display.py firmware-input/Z_5_0150.bin --out output/<new dir>/EG165050.BIN
    (needs arm-none-eabi binutils and Unicorn: output/phase0-*/enter-environment.ps1)

Builds on the camera-verified stage0_c4 baseline and adds two inline hooks at the
native histogram gates. Mechanism from wonder-lantern-z30 liveview_dual; every Z5
address is checked here. The hooks accept DISP state 3 only when the first histogram
visibility mask (RAM 0xC1826118) has bit 3 set. Stock value 0x444 keeps the stock
result; only the card module writes 0xCCC, so without the module nothing changes.

Writes only new files. camera_verified: false.
"""
import hashlib
import itertools
import json
import pathlib
import re
import struct
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'reference' / 'z5mod' / 'firmware' / 'src'))
sys.path.insert(0, str(ROOT / 'research' / 'autostart'))

from z5mod_fw import container, engine  # noqa: E402
from z5mod_fw.engine import Append, Edit, Feature  # noqa: E402
from z5mod_fw.features import stage0  # noqa: E402
from z5mod_fw.platform import z5_150 as z5  # noqa: E402
import stage0_multi  # noqa: E402

BASE = stage0.BASE
BASELINE_SHA256 = 'a62531fd1df1c8c79cc96376bb54a0b979f1f57ebcff42f157c7c401c0357f3d'
ROM_END = 0x12121000 - 2
SNAPSHOT, DISP_OFFSET = 0xC20282B4, 0x18E
DESCRIPTORS, HISTOGRAM_INDEX, HISTOGRAM = 0x10748504, 32, 0xC1826114
MASKS = HISTOGRAM + 4
SCATTER = (0x12023FD4, 0xC1800000, 0xF8120)
SAMPLING = dict(name='sampling', site=0x1042E1BC, expected='10b5002490f88e01022812d1',
                yes=0x1042E1C8, no=0x1042E1EE)
WIDGET = dict(name='widget', site=0x103482A4, expected='fc4c90f88e01022803d1',
              yes=0x103482AE, no=0x103482B6)
HOOKS = (SAMPLING, WIDGET)

SOURCE = r'''
    .syntax unified
    .thumb
    .macro GATE yes, no
    ldrb.w  r0, [r0, #0x18e]
    cmp     r0, #2
    beq     1f
    cmp     r0, #3
    bne     2f
    push    {r1}
    ldr     r1, =%(masks)#x
    ldr     r1, [r1]
    tst.w   r1, #8              @ 0x444 clear, 0xCCC set
    pop     {r1}                @ flags unchanged
    beq     2f
    movs    r0, #2              @ continue exactly as native DISP 2
1:  cmp     r0, #2              @ native flags: equal
    ldr     pc, =\yes
2:  cmp     r0, #2              @ native flags: not equal
    ldr     pc, =\no
    .ltorg
    .endm

    .balign 4
    .global sampling
    .thumb_func
sampling:
    push    {r4, lr}            @ replayed
    movs    r4, #0              @ replayed
    GATE    %(s_yes)#x, %(s_no)#x

    .balign 4
    .global widget
    .thumb_func
widget:
    ldr     r4, =%(descriptors)#x   @ replayed PC-relative load
    GATE    %(w_yes)#x, %(w_no)#x
    .balign 4
    .global tail_end
tail_end:
'''


def assemble(at):
    src = SOURCE % dict(masks=MASKS, descriptors=DESCRIPTORS, s_yes=SAMPLING['yes'] | 1,
                        s_no=SAMPLING['no'] | 1, w_yes=WIDGET['yes'] | 1, w_no=WIDGET['no'] | 1)
    with tempfile.TemporaryDirectory(dir=ROOT / 'output') as t:
        t = pathlib.Path(t)
        (t / 'h.S').write_text(src, encoding='ascii')
        run = lambda *a: subprocess.run(a, cwd=t, check=True, capture_output=True, text=True).stdout
        run('arm-none-eabi-as', '-mcpu=cortex-a9', '-mthumb', '-o', 'h.o', 'h.S')
        run('arm-none-eabi-ld', '-Ttext=0x%08x' % at, '-e', 'sampling', '-o', 'h.elf', 'h.o')
        run('arm-none-eabi-objcopy', '-O', 'binary', 'h.elf', 'h.bin')
        syms = {}
        for line in run('arm-none-eabi-nm', 'h.elf').splitlines():
            a, _, n = line.split()
            syms[n] = int(a, 16)
        return (t / 'h.bin').read_bytes(), syms


def windows():
    return [(h['site'], h['site'] + len(bytes.fromhex(h['expected']))) for h in HOOKS]


def audit_incoming(stock):
    """Thumb branches anywhere in the image that land inside a replaced window, after its start."""
    found = []
    n = len(stock) - 4
    for off in range(0, n, 2):
        pc = BASE + off
        h1, h2 = struct.unpack_from('<HH', stock, off)
        targets = []
        if h1 >> 11 == 0b11100:                                   # B
            imm = h1 & 0x7FF
            targets.append(pc + 4 + ((imm - 0x800 if imm & 0x400 else imm) << 1))
        elif h1 >> 12 == 0b1101 and (h1 >> 8) & 15 < 14:          # Bcc
            imm = h1 & 0xFF
            targets.append(pc + 4 + ((imm - 0x100 if imm & 0x80 else imm) << 1))
        elif h1 & 0xF500 == 0xB100:                               # CBZ/CBNZ
            targets.append(pc + 4 + ((((h1 >> 9) & 1) << 6) | (((h1 >> 3) & 31) << 1)))
        if h1 >> 11 == 0b11110 and h2 >> 15 == 1:
            s, imm10, j1, j2, imm11 = (h1 >> 10) & 1, h1 & 0x3FF, (h2 >> 13) & 1, (h2 >> 11) & 1, h2 & 0x7FF
            if (h2 >> 12) & 1:                                    # B.W / BL / BLX
                i1, i2 = 1 ^ j1 ^ s, 1 ^ j2 ^ s
                v = (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1)
                targets.append(pc + 4 + (v - (1 << 25) if s else v))
            elif (h1 >> 6) & 15 < 14:                             # Bcc.W
                imm6 = h1 & 0x3F
                v = (s << 20) | (j2 << 19) | (j1 << 18) | (imm6 << 12) | (imm11 << 1)
                targets.append(pc + 4 + (v - (1 << 21) if s else v))
        for target in targets:
            for lo, hi in windows():
                if lo < target < hi and not lo <= pc < hi:
                    found.append(dict(source=hex(pc), target=hex(target)))
    return found


def feature(module, at):
    code, syms = assemble(at)
    edits = []
    for h in HOOKS:
        expected = bytes.fromhex(h['expected'])
        entry = syms[h['name']] | 1
        jump = bytes.fromhex('dff800f0') + struct.pack('<I', entry) + bytes.fromhex('00bf') * ((len(expected) - 8) // 2)
        edits.append(Edit('dual_%s_gate' % h['name'], h['site'] - BASE, expected, jump))
    return Feature(name='dual_display_gates', description='Histogram on the level page when RAM mask bit 3 is set',
                   camera_verified=False, edits=tuple(edits), appends=(Append('dual_gates', code, align=8),)), syms


def checks(stock):
    if struct.unpack_from('<3I', stock, 0x14000) != SCATTER:
        raise SystemExit('initialized-data mapping differs')
    if struct.unpack_from('<I', stock, DESCRIPTORS - BASE + 4 * HISTOGRAM_INDEX)[0] != HISTOGRAM:
        raise SystemExit('histogram descriptor binding differs')
    rom = SCATTER[0] + MASKS - SCATTER[1]
    if struct.unpack_from('<9I', stock, rom - BASE) != (0x444,) * 9:
        raise SystemExit('histogram masks differ')


def machine(image):
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB
    from unicorn.arm_const import UC_CPU_ARM_CORTEX_A15
    u = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A15)
    u.mem_map(BASE, (len(image) + 4095) & ~4095)
    u.mem_write(BASE, image)
    u.mem_map(SNAPSHOT & ~4095, 4096)
    u.mem_map(MASKS & ~4095, 4096)
    u.mem_map(0x50000000, 4096)
    return u


def verify_gates(stock, candidate):
    from unicorn import UC_HOOK_CODE
    from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R4, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_CPSR
    stack, count = 0x50000000, 0
    for spec in HOOKS:
        results = []
        for image, mask in ((stock, 0x444), (candidate, 0x444), (candidate, 0xCCC)):
            u = machine(image)
            hits = []
            def stop(mu, pc, size, unused):
                if pc in (spec['yes'], spec['no']):
                    hits.append(pc)
                    mu.emu_stop()
            u.hook_add(UC_HOOK_CODE, stop)
            u.mem_write(MASKS, struct.pack('<I', mask))
            states = {}
            for disp, flags in itertools.product(range(256), (0, 0xF8000000)):
                hits.clear()
                u.mem_write(SNAPSHOT + DISP_OFFSET, bytes([disp]))
                for r in range(13):
                    u.reg_write(UC_ARM_REG_R0 + r, 0x11110000 + r)
                u.reg_write(UC_ARM_REG_R0, SNAPSHOT)
                u.reg_write(UC_ARM_REG_R4, 0x12345678)
                u.reg_write(UC_ARM_REG_SP, stack + 0x800)
                u.reg_write(UC_ARM_REG_LR, stack + 0x101)
                cpsr = u.reg_read(UC_ARM_REG_CPSR)
                u.reg_write(UC_ARM_REG_CPSR, (cpsr & ~0xF8000000) | flags)
                u.emu_start(spec['site'] | 1, 0, count=100)
                assert len(hits) == 1, (spec['name'], disp)
                states[disp, flags] = (hits[0], tuple(u.reg_read(UC_ARM_REG_R0 + r) for r in range(13)),
                                       u.reg_read(UC_ARM_REG_CPSR) & 0xF8000000, u.reg_read(UC_ARM_REG_SP),
                                       u.reg_read(UC_ARM_REG_LR),
                                       # live stack only: a temporary push leaves stale bytes below SP
                                       bytes(u.mem_read(u.reg_read(UC_ARM_REG_SP),
                                                        stack + 0x800 - u.reg_read(UC_ARM_REG_SP))))
                count += 1
            results.append(states)
        native, off, on = results
        for key in native:
            disp, flags = key
            assert off[key] == native[key], ('mask off differs', spec['name'], key)
            reference = native[(2 if disp == 3 else disp), flags]
            assert on[key] == reference, ('mask on differs', spec['name'], key)
    return dict(passed=True, executions=count, disp_values=256, masks=['0x444', '0xCCC'],
                scope='Continuation, registers, flags, stack; no drawing')


def verify_sampling(stock, candidate):
    from unicorn import UC_HOOK_CODE
    from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R4, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC
    stack, exit_pc, count = 0x50000000, 0x50000100, 0
    outcome = []
    for image, mask in ((stock, 0x444), (candidate, 0x444), (candidate, 0xCCC)):
        u = machine(image)
        u.mem_write(MASKS, struct.pack('<I', mask))
        ctx = {}
        def services(mu, pc, size, unused):
            if pc == exit_pc:
                ctx['returned'] = True
                mu.emu_stop()
            elif pc == 0x10456098:
                mu.reg_write(UC_ARM_REG_R0, {0x168C: ctx['mode'], 0x1128: ctx['apply']}[mu.reg_read(UC_ARM_REG_R0)])
                mu.reg_write(UC_ARM_REG_PC, mu.reg_read(UC_ARM_REG_LR))
            elif pc in (0x10368070, 0x1042E190):
                mu.reg_write(UC_ARM_REG_R0, ctx['a'] if pc == 0x10368070 else ctx['b'])
                mu.reg_write(UC_ARM_REG_PC, mu.reg_read(UC_ARM_REG_LR))
        u.hook_add(UC_HOOK_CODE, services)
        seen = {}
        for values in itertools.product(range(256), range(2), range(2), range(2), range(2)):
            disp, mode, apply, a, b = values
            ctx.update(mode=mode, apply=apply, a=a, b=b, returned=False)
            u.mem_write(SNAPSHOT + DISP_OFFSET, bytes([disp]))
            u.reg_write(UC_ARM_REG_R4, 0x12345678)
            u.reg_write(UC_ARM_REG_SP, stack + 0x800)
            u.reg_write(UC_ARM_REG_LR, exit_pc | 1)
            u.emu_start(0x1042E1BB, 0, count=300)
            assert ctx['returned'] and u.reg_read(UC_ARM_REG_R4) == 0x12345678 and u.reg_read(UC_ARM_REG_SP) == stack + 0x800
            seen[values] = u.reg_read(UC_ARM_REG_R0)
            count += 1
        outcome.append(seen)
    native, off, on = outcome
    assert off == native
    for values, result in on.items():
        disp, *rest = values
        assert result == native[(2 if disp == 3 else disp, *rest)]
    return dict(passed=True, executions=count, scope='Complete native sampling function; four modelled service inputs; no pixels')


def build(package, out):
    report_path = out.with_name(out.name + '.json')
    for p in (out, report_path):
        if p.exists():
            raise SystemExit(f'{p} exists; refusing to overwrite')
    _, mods = container.load(package)
    main = container.main_module(mods)
    checks(main.data)
    incoming = audit_incoming(main.data)
    if incoming:
        print(json.dumps(incoming, indent=1))
        raise SystemExit('branches land inside a replaced window')
    base = [stage0_multi.feature('c4')]
    baseline = engine.apply(main.data, base, BASE, expected_sha256=z5.MODULE_MAIN_SHA256)
    if baseline.report['output']['sha256'] != BASELINE_SHA256:
        raise SystemExit('stage0_c4 no longer rebuilds to the camera-verified baseline')
    probe, _ = feature(main.data, 0x12120668)
    trial = engine.apply(main.data, base + [probe], BASE, expected_sha256=z5.MODULE_MAIN_SHA256)
    at = int([a for a in trial.report['appends'] if a['name'] == 'dual_gates'][0]['address'], 16)
    final, syms = feature(main.data, at)
    result = engine.apply(main.data, base + [final], BASE, expected_sha256=z5.MODULE_MAIN_SHA256)
    placed = [a for a in result.report['appends'] if a['name'] == 'dual_gates'][0]
    if int(placed['address'], 16) != at or at + placed['size'] > ROM_END:
        raise SystemExit('append moved or exceeds the ROM limit')
    report = dict(package=dict(name=package.name, sha256=hashlib.sha256(package.read_bytes()).hexdigest()),
                  module=dict(name=main.name, size=main.length, sha256=main.sha256),
                  variant='c4+dual_display_gates', baseline_sha256=BASELINE_SHA256,
                  symbols={k: hex(v) for k, v in syms.items()}, incoming_branch_audit=dict(found=incoming),
                  gate_verification=verify_gates(main.data, result.output),
                  sampling_verification=verify_sampling(main.data, result.output),
                  camera_verified=False, rendered_in_emulator=False, **result.report)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(result.output)
    report_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'wrote {out} ({len(result.output)} bytes, sha256 {report["output"]["sha256"]})')
    print(json.dumps({k: report[k] for k in ('symbols', 'incoming_branch_audit', 'gate_verification', 'sampling_verification')}, indent=1))


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[2] == '--out':
        build(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[3]))
    else:
        sys.exit(__doc__)
