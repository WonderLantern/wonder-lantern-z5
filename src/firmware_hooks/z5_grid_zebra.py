"""Z5 C 1.50: grid styles and photo zebra hooks, gated by words the card module sets.

    python -B research/overlay-investigation/z5_grid_zebra.py firmware-input/Z_5_0150.bin --out output/<new dir>/EG165050.BIN
    (needs arm-none-eabi binutils and Unicorn: output/phase0-*/enter-environment.ps1)

Builds on the group 35 firmware (camera-verified) and adds three hooks:
  grid   0x10388DCA  uses a card-module template when the anchor pair is valid, else the ROM one;
  zebra  0x101782B2, 0x101783EA  lets photo live view through the stock highlight gates when
         the card module has set its word. The zebra rule follows wonder-lantern-z30 photo_zebra;
         the 0x2A1 flag mask is carried over from Z30 and is not confirmed on Z5.

Control words are in the SDK anchor gap, which the group 35 firmware overwrites at every boot:
  +0x14 ZEBRA_ON when photo zebra is on; +0x18/+0x1C template pointer and its complement.
Without the card module nothing is set and every hook takes the stock result.

Writes only new files. camera_verified: false.
"""
import hashlib
import itertools
import json
import pathlib
import struct
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import z5_dual_display as dd  # noqa: E402
import z5_zero_anchor as za  # noqa: E402
from z5mod_fw import container, engine  # noqa: E402
from z5mod_fw.engine import Append, Edit, Feature  # noqa: E402
from z5mod_fw.platform import z5_150 as z5  # noqa: E402
import stage0_multi  # noqa: E402

BASE = dd.BASE
ROOT = HERE.parents[1]
GROUP35_SHA256 = 'cc007df8b3f52b948b00a472e6cd8a269f8de5bbf69815f4df3d16edab48a980'
ANCHOR = 0xC18F8120
ZEBRA_WORD, TEMPLATE_WORD = ANCHOR + 0x14, ANCHOR + 0x18
ZEBRA_ON = 0x5A454252
RAM_LO, RAM_HI = 0xC1800000, 0xC8700000
PHOTO_REJECT_FLAGS = 0x2A1
ROM_TEMPLATE = 0x1074AD10
STOCK_COUNT = 3

GRID = dict(name='grid', site=0x10388DCA, expected='fe4800eb461778898146', back=0x10388DD4)
ZEBRA_A = dict(name='zebra_a', site=0x101782B2, expected='b9f1000f13d0032c11d106e0', peak='r4',
               accept=0x101782CC, reject=0x101782E0)
ZEBRA_B = dict(name='zebra_b', site=0x101783EA, expected='b9f1000f13d0032e11d106e0', peak='r6',
               accept=0x10178404, reject=0x10178418)
SITES = (GRID, ZEBRA_A, ZEBRA_B)

SOURCE = r'''
    .syntax unified
    .thumb
    .macro ZEBRA name, peak, accept, reject
    .balign 4
    .global \name
    .thumb_func
\name:
    cmp.w   r9, #0
    bne     1f                  @ movie path: the stock test follows
    push    {r1, r2}
    ldr     r1, =%(zebra_word)#x
    ldr     r1, [r1]
    ldr     r2, =%(zebra_on)#x
    cmp     r1, r2
    bne     3f
    movw    r1, #%(flags)#x
    tst     r5, r1
    pop     {r1, r2}
    bne     2f
    b       1f
3:  pop     {r1, r2}
    b       2f
1:  cmp     \peak, #3
    bne     2f
    ldr     pc, =\accept
2:  ldr     pc, =\reject
    .ltorg
    .endm

    ZEBRA   zebra_a, r4, %(a_accept)#x, %(a_reject)#x
    ZEBRA   zebra_b, r6, %(b_accept)#x, %(b_reject)#x

    .balign 4
    .global grid
    .thumb_func
grid:
    push    {r1, r2}
    ldr     r1, =%(template_word)#x
    ldr     r0, [r1]
    ldr     r2, [r1, #4]
    mvns    r2, r2
    cmp     r0, r2
    bne     1f
    ldr     r2, =%(ram_lo)#x
    cmp     r0, r2
    blo     1f
    ldr     r2, =%(ram_hi)#x
    cmp     r0, r2
    blo     2f
1:  ldr     r0, =%(rom_template)#x
2:  pop     {r1, r2}
    add.w   r7, r0, r6, lsl #5  @ replayed
    ldrh    r0, [r7, #10]       @ replayed
    mov     r9, r0              @ replayed
    ldr     pc, =%(grid_back)#x
    .ltorg
    .balign 4
'''


def assemble(at):
    src = SOURCE % dict(zebra_word=ZEBRA_WORD, zebra_on=ZEBRA_ON, flags=PHOTO_REJECT_FLAGS,
                        template_word=TEMPLATE_WORD, ram_lo=RAM_LO, ram_hi=RAM_HI, rom_template=ROM_TEMPLATE,
                        grid_back=GRID['back'] | 1, a_accept=ZEBRA_A['accept'] | 1, a_reject=ZEBRA_A['reject'] | 1,
                        b_accept=ZEBRA_B['accept'] | 1, b_reject=ZEBRA_B['reject'] | 1)
    with tempfile.TemporaryDirectory(dir=ROOT / 'output') as t:
        t = pathlib.Path(t)
        (t / 'h.S').write_text(src, encoding='ascii')
        run = lambda *a: subprocess.run(a, cwd=t, check=True, capture_output=True, text=True).stdout
        run('arm-none-eabi-as', '-mcpu=cortex-a9', '-mthumb', '-o', 'h.o', 'h.S')
        run('arm-none-eabi-ld', '-Ttext=0x%08x' % at, '-e', 'grid', '-o', 'h.elf', 'h.o')
        run('arm-none-eabi-objcopy', '-O', 'binary', 'h.elf', 'h.bin')
        syms = {}
        for line in run('arm-none-eabi-nm', 'h.elf').splitlines():
            a, _, n = line.split()
            syms[n] = int(a, 16)
        return (t / 'h.bin').read_bytes(), syms


def jump_bytes(site, entry, size):
    """`ldr.w pc, [pc, #n]` with its literal on the next aligned word; both sites are 2 mod 4."""
    if site % 4 != 2 or size < 10:
        raise SystemExit('unexpected hook site')
    body = bytes.fromhex('dff804f0') + bytes.fromhex('00bf') + struct.pack('<I', entry)
    return body + bytes.fromhex('00bf') * ((size - len(body)) // 2)


def feature(at):
    code, syms = assemble(at)
    edits = []
    for s in SITES:
        expected = bytes.fromhex(s['expected'])
        edits.append(Edit(f"{s['name']}_hook", s['site'] - BASE, expected,
                          jump_bytes(s['site'], syms[s['name']] | 1, len(expected))))
    return Feature(name='grid_zebra', description='Card-module grid template and photo zebra',
                   camera_verified=False, edits=tuple(edits),
                   appends=(Append('grid_zebra', code, align=8),)), syms


def machine(image):
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB
    from unicorn.arm_const import UC_CPU_ARM_CORTEX_A15
    u = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A15)
    u.mem_map(BASE, (len(image) + 4095) & ~4095)
    u.mem_write(BASE, image)
    u.mem_map(ANCHOR & ~4095, 4096)
    u.mem_map(0xC2000000, 4096)
    u.mem_map(0x50000000, 4096)
    return u


def run_site(u, site, stops, regs, flags=0):
    from unicorn import UC_HOOK_CODE
    from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_CPSR
    hits = []

    def stop(mu, pc, size, unused):
        if pc in stops:
            hits.append(pc)
            mu.emu_stop()
    handle = u.hook_add(UC_HOOK_CODE, stop)
    stack = 0x50000000
    for r in range(13):
        u.reg_write(UC_ARM_REG_R0 + r, regs.get(r, 0x11110000 + r))
    u.reg_write(UC_ARM_REG_SP, stack + 0x800)
    u.reg_write(UC_ARM_REG_LR, stack + 0x101)
    cpsr = u.reg_read(UC_ARM_REG_CPSR)
    u.reg_write(UC_ARM_REG_CPSR, (cpsr & ~0xF8000000) | flags)
    u.emu_start(site | 1, 0, count=200)
    u.hook_del(handle)
    assert len(hits) == 1, (hex(site), regs)
    sp = u.reg_read(UC_ARM_REG_SP)
    return (hits[0], tuple(u.reg_read(UC_ARM_REG_R0 + r) for r in range(13)), sp,
            u.reg_read(UC_ARM_REG_LR), bytes(u.mem_read(sp, stack + 0x800 - sp)))


def verify_zebra(group35, candidate):
    count = 0
    for spec in (ZEBRA_A, ZEBRA_B):
        stops = (spec['accept'], spec['reject'])
        peak = int(spec['peak'][1:])
        native, off, on, wrong = {}, {}, {}, {}
        for image, word, out in ((group35, 0, native), (candidate, 0, off), (candidate, ZEBRA_ON, on),
                                 (candidate, ZEBRA_ON + 1, wrong)):
            u = machine(image)
            u.mem_write(ZEBRA_WORD, struct.pack('<I', word))
            for movie, p, f, r0 in itertools.product((0, 1, 7), range(5),
                    (0, 1, 0x20, 0x80, 0x200, 0x2A1, 0x2000, 0x10000, 0xFFFFFFFF), (0, 1)):
                regs = {9: movie, peak: p, 5: f, 0: r0}
                out[movie, p, f, r0] = run_site(u, spec['site'], stops, regs)
                count += 1
        for key, stock in native.items():
            movie, p, f, r0 = key
            assert off[key] == stock, ('word clear differs', spec['name'], key)
            assert wrong[key] == stock, ('wrong word differs', spec['name'], key)
            if movie:
                assert on[key] == stock, ('movie path differs', spec['name'], key)
            else:
                want = spec['accept'] if p == 3 and not (f & PHOTO_REJECT_FLAGS) else spec['reject']
                assert on[key][0] == want, ('photo decision', spec['name'], key)
                assert on[key][1:] == stock[1:], ('photo registers', spec['name'], key)
    return dict(passed=True, executions=count, scope='Continuation, r0-r12, SP, LR, live stack; flags are dead at both exits')


def verify_grid(group35, candidate):
    from_template = lambda base, mode: base + mode * 32
    custom = 0xC2000100
    template = bytearray(96)
    for mode, c in enumerate((2, 1, 5)):
        struct.pack_into('<H', template, mode * 32 + 0x0A, c)
    count = 0
    stops = (GRID['back'],)
    stock_results = {}
    u = machine(group35)
    for mode in range(3):
        stock_results[mode] = run_site(u, GRID['site'], stops, {6: mode})
        assert stock_results[mode][1][7] == from_template(ROM_TEMPLATE, mode)
        assert stock_results[mode][1][9] == STOCK_COUNT
        count += 1
    bad = [(0, 0), (0, 0xFFFFFFFF), (custom, custom), (custom, ~custom & 0xFFFFFFFF ^ 1),
           (0x20000000, ~0x20000000 & 0xFFFFFFFF), (RAM_HI, ~RAM_HI & 0xFFFFFFFF)]
    u = machine(candidate)
    u.mem_write(custom, bytes(template))
    for mode, (p, q) in itertools.product(range(3), bad):
        u.mem_write(TEMPLATE_WORD, struct.pack('<2I', p, q))
        assert run_site(u, GRID['site'], stops, {6: mode}) == stock_results[mode], ('invalid pair', mode, hex(p))
        count += 1
    u.mem_write(TEMPLATE_WORD, struct.pack('<2I', custom, ~custom & 0xFFFFFFFF))
    for mode, want in enumerate((2, 1, 5)):
        r = run_site(u, GRID['site'], stops, {6: mode})
        assert r[1][7] == custom + mode * 32 and r[1][9] == want and r[1][0] == want, ('valid pair', mode)
        count += 1
    return dict(passed=True, executions=count, scope='Continuation and registers; invalid pairs equal the stock result')


def build(package, out):
    report_path = out.with_name(out.name + '.json')
    for p in (out, report_path):
        if p.exists():
            raise SystemExit(f'{p} exists; refusing to overwrite')
    _, mods = container.load(package)
    main = container.main_module(mods)
    dd.windows = lambda: [(s['site'], s['site'] + len(bytes.fromhex(s['expected']))) for s in SITES]
    incoming = dd.audit_incoming(main.data)
    if incoming:
        print(json.dumps(incoming, indent=1))
        raise SystemExit('branches land inside a replaced window')
    base = [stage0_multi.feature('c4')]
    probe, _ = dd.feature(main.data, 0x12120668)
    trial = engine.apply(main.data, base + [probe], BASE, expected_sha256=z5.MODULE_MAIN_SHA256)
    at = int([a for a in trial.report['appends'] if a['name'] == 'dual_gates'][0]['address'], 16)
    dual, _ = dd.feature(main.data, at)
    chain = base + [dual, za.zero_feature()]
    group35 = engine.apply(main.data, chain, BASE, expected_sha256=z5.MODULE_MAIN_SHA256)
    if group35.report['output']['sha256'] != GROUP35_SHA256:
        raise SystemExit('the chain no longer rebuilds to the group 35 firmware')
    probe2, _ = feature(0x12120800)
    trial2 = engine.apply(main.data, chain + [probe2], BASE, expected_sha256=z5.MODULE_MAIN_SHA256)
    at2 = int([a for a in trial2.report['appends'] if a['name'] == 'grid_zebra'][0]['address'], 16)
    final, syms = feature(at2)
    result = engine.apply(main.data, chain + [final], BASE, expected_sha256=z5.MODULE_MAIN_SHA256)
    placed = [a for a in result.report['appends'] if a['name'] == 'grid_zebra'][0]
    if int(placed['address'], 16) != at2 or at2 + placed['size'] > dd.ROM_END:
        raise SystemExit('append moved or exceeds the ROM limit')
    report = dict(package=dict(name=package.name, sha256=hashlib.sha256(package.read_bytes()).hexdigest()),
                  module=dict(name=main.name, size=main.length, sha256=main.sha256),
                  variant='group35+grid_zebra', group35_sha256=GROUP35_SHA256,
                  control_words=dict(zebra=hex(ZEBRA_WORD), zebra_on=hex(ZEBRA_ON), template=hex(TEMPLATE_WORD)),
                  symbols={k: hex(v) for k, v in syms.items()}, incoming_branch_audit=dict(found=incoming),
                  zebra_verification=verify_zebra(group35.output, result.output),
                  grid_verification=verify_grid(group35.output, result.output),
                  camera_verified=False, rendered_in_emulator=False, **result.report)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(result.output)
    report_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'wrote {out} ({len(result.output)} bytes, sha256 {report["output"]["sha256"]})')
    print(json.dumps({k: report[k] for k in ('symbols', 'incoming_branch_audit', 'zebra_verification', 'grid_verification')}, indent=1))
    print('appended', placed)


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[2] == '--out':
        build(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[3]))
    else:
        sys.exit(__doc__)
