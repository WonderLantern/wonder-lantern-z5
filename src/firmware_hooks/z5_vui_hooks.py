"""Z5 C 1.50: portrait UI hooks on top of the group 36 firmware.

    python -B src/firmware_hooks/z5_vui_hooks.py Z_5_0150.bin --out <new dir>/EG165050.BIN
    (needs arm-none-eabi binutils, Unicorn and the z5mod decoder; patch.py does not need any of this)

Twelve small entries call one function in the card module. Stubs find it through the SDK anchor
(pair P, ~P, zeroed by the group 35 firmware at every boot), the state block at P + 0x0C, its magic,
and the function pointer at +4. With no module, or the module's switch off, nothing changes.
Writes only new files. camera_verified: false.
"""
import hashlib
import json
import pathlib
import struct
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
OVERLAY = HERE   # the dual-display, zero-anchor and grid/zebra builders sit next to this file
sys.path.insert(0, str(OVERLAY))

import z5_dual_display as dd  # noqa: E402
import z5_grid_zebra as gz  # noqa: E402
import z5_zero_anchor as za  # noqa: E402
from z5mod_fw import container, engine  # noqa: E402
from z5mod_fw.engine import Append, Edit, Feature  # noqa: E402
from z5mod_fw.platform import z5_150 as z5  # noqa: E402
import stage0_multi  # noqa: E402

BASE = dd.BASE
ROOT = HERE.parents[1]
GROUP36_SHA256 = 'e7e4ec5477a301f6128d30352429f2e6d31eaaa22f13255ceebf531215e24d61'
CM_MAGIC = 0x554E4D43
VENEERS = 0x105BA2C4
VG_LOAD_MATRIX = 0x1053AD3A
# Group 54: the level-page histogram gate also accepts mask bit 27, which no widget uses for visibility (all
# 576 stock masks fit in 24 bits). In portrait the module keeps only that bit, so the counts stay fresh while
# the stock histogram is hidden.
GATE_TST = 'tst.w   r1, #8              @ 0x444 clear, 0xCCC set'
GATE_BIT27 = 'orr.w   r1, r1, r1, lsr #24 @ bit 27 counts as bit 3\n    ' + GATE_TST
MASK_BIT27 = 0x08000000
SKIP = '    cmp     r0, #0\n    it      eq\n    bxeq    lr\n'   # the module cleared r0: it drew this itself

HOOKS = (
    dict(name='image', kind=0, site=0x104A673C, expected='2de9f04f', replay='push.w {r4-r11, lr}', skip=True),
    dict(name='rect', kind=1, site=0x104A5EDC, expected='2de9f041', replay='push.w {r4-r8, lr}'),
    dict(name='clear', kind=2, site=0x104A641E, expected='f0b589b0', replay='push {r4-r7, lr}\n    sub sp, #0x24'),
    dict(name='frame', kind=3, site=0x104A629C, expected='2de9f04f', replay='push.w {r4-r11, lr}'),
    dict(name='setter', kind=4, site=0x10411418, expected='2de9fc41', replay='push.w {r2-r8, lr}'),
    dict(name='image2', kind=6, site=0x104A7292, expected='2de9f04f', replay='push.w {r4-r11, lr}', skip=True),
    dict(name='image3', kind=7, site=0x104A7AC2, expected='2de9f04f', replay='push.w {r4-r11, lr}', skip=True),
    dict(name='key', kind=8, site=0x1031239E, expected='01467748', replay='mov r1, r0\n    ldr r0, =0xC18289DC'),
    dict(name='path_grad', kind=9, site=0x104A6064, expected='2de9ff4f', replay='push.w {r0-r11, lr}'),
    dict(name='path_ellipse', kind=10, site=0x104A65BE, expected='2de9f04f', replay='push.w {r4-r11, lr}'),
    dict(name='path_line', kind=11, site=0x104A7F8E, expected='2de9f04f', replay='push.w {r4-r11, lr}'),
)
TEXT = dict(name='text', kind=5, site=0x1056A5EE)


def source():
    body = ['''
    .syntax unified
    .thumb
    .balign 4
    .thumb_func
vui_common:
    ldr     r3, =%(anchor)#x
    ldr     r12, [r3]
    ldr     r2, [r3, #4]
    mvns    r2, r2
    cmp     r12, r2
    bne     9f
    ldr     r2, =%(lo)#x
    cmp     r12, r2
    blo     9f
    ldr     r3, =%(hi)#x
    cmp     r12, r3
    bhs     9f
    ldr     r12, [r12, #0xc]
    cmp     r12, r2
    blo     9f
    cmp     r12, r3
    bhs     9f
    ldr     r3, [r12]
    ldr     r2, =%(magic)#x
    cmp     r3, r2
    bne     9f
    ldr     r12, [r12, #4]
    cmp     r12, #0
    beq     9f
    bx      r12
9:  bx      lr
    .ltorg
''' % dict(anchor=gz.ANCHOR, lo=gz.RAM_LO, hi=gz.RAM_HI, magic=CM_MAGIC)]
    for h in HOOKS:
        body.append(f'''
    .balign 4
    .global {h['name']}
    .thumb_func
{h['name']}:
    push    {{r0-r3, r12, lr}}
    movs    r0, #{h['kind']}
    mov     r1, sp
    bl      vui_common
    pop     {{r0-r3, r12, lr}}
{SKIP if h.get('skip') else ''}    {h['replay']}
    ldr     pc, ={(h['site'] + 4) | 1:#x}
    .ltorg
''')
    body.append(f'''
    .balign 4
    .global text
    .thumb_func
text:
    push    {{r0-r3, r12, lr}}
    movs    r0, #{TEXT['kind']}
    mov     r1, sp
    bl      vui_common
    pop     {{r0-r3, r12, lr}}
    ldr     pc, ={VG_LOAD_MATRIX | 1:#x}
    .ltorg
    .balign 4
''')
    return ''.join(body)


def assemble(at):
    with tempfile.TemporaryDirectory() as t:
        t = pathlib.Path(t)
        (t / 'h.S').write_text(source(), encoding='ascii')
        run = lambda *a: subprocess.run(a, cwd=t, check=True, capture_output=True, text=True).stdout
        run('arm-none-eabi-as', '-mcpu=cortex-a9', '-mthumb', '-o', 'h.o', 'h.S')
        run('arm-none-eabi-ld', '-Ttext=0x%08x' % at, '-e', 'image', '-o', 'h.elf', 'h.o')
        run('arm-none-eabi-objcopy', '-O', 'binary', 'h.elf', 'h.bin')
        syms = {}
        for line in run('arm-none-eabi-nm', 'h.elf').splitlines():
            a, _, n = line.split()
            syms[n] = int(a, 16)
        return (t / 'h.bin').read_bytes(), syms


def branch(site, target, link):
    imm = target - (site + 4)
    if not -(1 << 24) <= imm < (1 << 24) or imm & 1:
        raise SystemExit(f'branch out of range {site:#x} -> {target:#x}')
    s = (imm >> 24) & 1
    i1, i2 = (imm >> 23) & 1, (imm >> 22) & 1
    j1, j2 = (~(i1 ^ s)) & 1, (~(i2 ^ s)) & 1
    return struct.pack('<HH', 0xF000 | (s << 10) | ((imm >> 12) & 0x3FF),
                       (0xD000 if link else 0x9000) | (j1 << 13) | (j2 << 11) | ((imm >> 1) & 0x7FF))


def feature(stock, at):
    code, syms = assemble(at)
    edits = []
    veneer = VENEERS
    for h in HOOKS + (TEXT,):
        body = bytes.fromhex('dff800f0') + struct.pack('<I', syms[h['name']] | 1)
        edits.append(Edit(f"vui_veneer_{h['name']}", veneer - BASE, bytes(8), body))
        if h is TEXT:
            expected = branch(h['site'], VG_LOAD_MATRIX, True)
            found = stock[h['site'] - BASE:h['site'] - BASE + 4]
            if found != expected:
                raise SystemExit(f'text call differs: {found.hex()}')
            edits.append(Edit('vui_text', h['site'] - BASE, expected, branch(h['site'], veneer, True)))
        else:
            edits.append(Edit(f"vui_{h['name']}", h['site'] - BASE, bytes.fromhex(h['expected']), branch(h['site'], veneer, False)))
        veneer += 8
    return Feature(name='vertical_ui', description='Portrait UI hooks, inert without the card module', camera_verified=False,
                   edits=tuple(edits), appends=(Append('vertical_ui', code, align=8),)), syms


def verify_bit27(stock, image):
    """Mask bit 27 alone: DISP 3 continues exactly as stock DISP 2 at both gates; every other DISP value as stock."""
    from unicorn import UC_HOOK_CODE
    from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R4, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_CPSR
    stack, count = 0x50000000, 0
    for spec in dd.HOOKS:
        results = []
        for img, mask in ((stock, 0x444), (image, MASK_BIT27)):
            u = dd.machine(img)
            hits = []
            def stop(mu, pc, size, unused):
                if pc in (spec['yes'], spec['no']):
                    hits.append(pc)
                    mu.emu_stop()
            u.hook_add(UC_HOOK_CODE, stop)
            u.mem_write(dd.MASKS, struct.pack('<I', mask))
            states = {}
            for disp, flags in ((v, f) for v in range(256) for f in (0, 0xF8000000)):
                hits.clear()
                u.mem_write(dd.SNAPSHOT + dd.DISP_OFFSET, bytes([disp]))
                for r in range(13):
                    u.reg_write(UC_ARM_REG_R0 + r, 0x11110000 + r)
                u.reg_write(UC_ARM_REG_R0, dd.SNAPSHOT)
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
                                       bytes(u.mem_read(u.reg_read(UC_ARM_REG_SP), stack + 0x800 - u.reg_read(UC_ARM_REG_SP))))
                count += 1
            results.append(states)
        native, on = results
        for disp, flags in native:
            assert on[disp, flags] == native[(2 if disp == 3 else disp), flags], ('bit 27 differs', spec['name'], disp)
    return dict(passed=True, executions=count, mask=hex(MASK_BIT27))


def build(package, out):
    report_path = out.with_name(out.name + '.json')
    for p in (out, report_path):
        if p.exists():
            raise SystemExit(f'{p} exists; refusing to overwrite')
    _, mods = container.load(package)
    main = container.main_module(mods)
    stock = main.data
    dd.windows = lambda: [(h['site'], h['site'] + 4) for h in HOOKS + (TEXT,)]
    incoming = dd.audit_incoming(stock)
    if incoming:
        print(json.dumps(incoming, indent=1))
        raise SystemExit('branches land inside a replaced window')
    base = [stage0_multi.feature('c4')]
    probe, _ = dd.feature(stock, 0x12120668)
    at = next(int(a['address'], 16) for a in engine.apply(stock, base + [probe], BASE, expected_sha256=z5.MODULE_MAIN_SHA256).report['appends'] if a['name'] == 'dual_gates')
    dual, _ = dd.feature(stock, at)
    chain = base + [dual, za.zero_feature()]
    probe2, _ = gz.feature(0x12120800)
    at2 = next(int(a['address'], 16) for a in engine.apply(stock, chain + [probe2], BASE, expected_sha256=z5.MODULE_MAIN_SHA256).report['appends'] if a['name'] == 'grid_zebra')
    grid, _ = gz.feature(at2)
    chain.append(grid)
    group36 = engine.apply(stock, chain, BASE, expected_sha256=z5.MODULE_MAIN_SHA256)
    if group36.report['output']['sha256'] != GROUP36_SHA256:
        raise SystemExit('the chain no longer rebuilds to the group 36 firmware')
    # Same chain, the dual-display gates rebuilt with bit 27.
    if dd.SOURCE.count(GATE_TST) != 1:
        raise SystemExit('dual-display gate source differs')
    dd.SOURCE = dd.SOURCE.replace(GATE_TST, GATE_BIT27)
    dual, _ = dd.feature(stock, at)
    chain = base + [dual, za.zero_feature()]
    at2 = next(int(a['address'], 16) for a in engine.apply(stock, chain + [probe2], BASE, expected_sha256=z5.MODULE_MAIN_SHA256).report['appends'] if a['name'] == 'grid_zebra')
    grid, _ = gz.feature(at2)
    chain.append(grid)
    gated = engine.apply(stock, chain, BASE, expected_sha256=z5.MODULE_MAIN_SHA256).output
    gate_report = dict(stock_and_level=dd.verify_gates(stock, gated), sampling=dd.verify_sampling(stock, gated),
                       bit27=verify_bit27(stock, gated))
    probe3, _ = feature(stock, 0x12120A00)
    at3 = next(int(a['address'], 16) for a in engine.apply(stock, chain + [probe3], BASE, expected_sha256=z5.MODULE_MAIN_SHA256).report['appends'] if a['name'] == 'vertical_ui')
    final, syms = feature(stock, at3)
    result = engine.apply(stock, chain + [final], BASE, expected_sha256=z5.MODULE_MAIN_SHA256)
    placed = next(a for a in result.report['appends'] if a['name'] == 'vertical_ui')
    if int(placed['address'], 16) != at3 or at3 + placed['size'] > dd.ROM_END:
        raise SystemExit('append moved or exceeds the ROM limit')
    report = dict(package=dict(name=package.name, sha256=hashlib.sha256(package.read_bytes()).hexdigest()),
                  module=dict(name=main.name, size=main.length, sha256=main.sha256), variant='group36+vertical_ui',
                  group36_sha256=GROUP36_SHA256, gate_bit27=gate_report, symbols={k: hex(v) for k, v in syms.items()},
                  incoming_branch_audit=dict(found=incoming), camera_verified=False, **result.report)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(result.output)
    report_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'wrote {out} ({len(result.output)} bytes, sha256 {report["output"]["sha256"]})')
    print('appended', placed, 'rom end', hex(dd.ROM_END))


if __name__ == '__main__':
    args = sys.argv[1:]
    if len(args) != 3 or args[1] != '--out':
        raise SystemExit(__doc__)
    build(pathlib.Path(args[0]), pathlib.Path(args[2]))
