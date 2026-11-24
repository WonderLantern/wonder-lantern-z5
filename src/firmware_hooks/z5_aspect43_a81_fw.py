"""A81 = A78 generalised to several photo aspects on one crop path (area 8), with 2.33:1 added
(research/aspect/a81_fx43.S; parameter block at copy + 0x200; kind-2 preview entries -> 2.33 sizes).
Without a parameter block it behaves as A78 (all A68..A78 checks below run that way). Below: A78.

A78 = A77 + playback decode info at area 8 for 4:3-shaped files while 4:3 is enabled (hplay 0x1034F6D4;
research/aspect/a78_fx43.S). Below: A77.

A77 = A76 + playback preview / main sizes for our 4:3 files (hpvw 0x1034E5E4, hsel328 0x10209328, only
for the checked playback callers and 4:3 main shapes; research/aspect/a77_fx43.S). Below: A76.

A76 = A75 with the rewrite limited to the shot's first configuration call (caller 0x10143229; playback's
0x1014421B calls at area 0 stay stock). Checks run with that caller as the return address. Below: A75.

A75 = A68 + the area descriptor 0x101DC3E8 rebuilt after the 4:3 rewrite (research/aspect/a75_fx43.S).
MakerNote 0x1B then carries A56's window (5376 x 4032, offset 332) instead of FX's. Below: the A68 description.

A68: 4:3 photos from the FX image area (Z30-style), firmware part: power-on geometry configuration wrapped. Release MAIN a3704c6f + three hooks
(research/aspect/a66_fx43.S) that are inert unless the card module enables them.

    python -B research/aspect/z5_aspect43_fx_fw.py <a3704c6f EG165050.BIN> --a56 <A56 7477fde9 EG165050.BIN> \
        --out output/<new dir>/EG165050.BIN [--module-inc <new path>/aspect43_fx_tables.inc]

Why: A60-A65 put 4:3 into the hidden 5:4 area 2; the read-out never configures that area (camera: black / stripes /
shutter locked even with every table equal to FX). Z30 never selects an area its read-out lacks: it stays in a stock
area and swaps only the capture geometry. Here the camera stays in FX (live view, AF, read-out stock); the card
module copies A56's camera-proven 4:3 tables (area 8 of A56: L 5352x4016, M 4008x3008, S 2664x2000, trim 332) to
the heap and points FX's seven groups of the RAM pointer table 0xC18C7554 at them; three ROM-only reads follow:
  offset 0x1020933E  area offset (0x1167B7F4)           FX -> (332, 0)
  trim   0x10143032  configuration trim (0x11648258)    FX -> (332, 0) x 3
  kind   0x101905BC  aspect kind of the capture state   FX -> 3 (stock 4:3 previews)
"Enabled" = FX's group-1 pointer (0xC18C7578) points into RAM; stock it points into ROM, so without the module the
camera behaves exactly as the release firmware. Verified in Unicorn: (a) disabled: every hook site replays the stock
instructions with identical registers for all areas / sizes; (b) enabled: FX buffer, configuration (photo branch),
area descriptor, selectors and offset equal A56's area 8, aspect kind 3; areas 1, 4, 5, 8 unchanged.
camera_verified: false.
"""
import argparse
import binascii
import hashlib
import itertools
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from unicorn.arm_const import UC_ARM_REG_CPSR, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_SP

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'vertical-ui'))
import z5_geometry_probe as probe  # noqa: E402
import z5_vui_hooks as hooks  # noqa: E402

RELEASE_SHA = 'a3704c6f7f7891b5d691985b6148af43d7bf4d58a35d862659661b1bd9a6c754'
A56_SHA = '7477fde9'
BASE = probe.BASE
STUBS = 0x105BA37C
LIMIT = 0x12120FFE
PTR_RAM = 0xC18C7554
GROUP_BYTES = 32           # per group copy: A56 area 8's entries plus what an out-of-range index reads after them
COUNTERS = 224             # module copy: base + 224 = counters written by the info hook
SITES = {
    'offset': (0x1020933E, '6949 01eb 8000 7047', False),
    'trim': (0x10143032, '95f8 0012 fa4a 95f8 2102 01eb 4101 02eb 8101', False),
    'cfg': (0x10142A7C, '2de9 f84f', False),
    'hpvw': (0x1034E5E4, '70b5 eab1', False),
    'hsel328': (0x10209328, '10b1 6c48', False),
    'hplay': (0x1034F6D4, '84f8 0002', False),
}


def assemble(address):
    with tempfile.TemporaryDirectory(dir=probe.ROOT / 'output') as tmp:
        def run(*args):
            return subprocess.check_output(args, cwd=tmp, text=True)
        run('arm-none-eabi-gcc', '-mcpu=cortex-a9', '-mthumb', '-c', str(HERE / 'a81_fx43.S'), '-o', 'h.o')
        run('arm-none-eabi-ld', f'-Ttext={address:#x}', '-e', 'cfg', '-o', 'h.elf', 'h.o')
        run('arm-none-eabi-objcopy', '-O', 'binary', 'h.elf', 'h.bin')
        symbols = {}
        for line in run('arm-none-eabi-nm', 'h.elf').splitlines():
            value, _, name = line.split()
            symbols[name] = int(value, 16)
        return (Path(tmp) / 'h.bin').read_bytes(), symbols


def a56_tables(a56):
    """A56's area-8 4:3 entries of the seven groups (32 bytes each), through A56's own pointer table."""
    ptrs = struct.unpack_from('<63I', a56, probe.PTR_ROM - BASE)
    return [a56[ptrs[g * 9 + 8] - BASE:ptrs[g * 9 + 8] - BASE + GROUP_BYTES] for g in range(7)]


def enable(ctx, tables):
    """What the card module does: heap copies of A56's 4:3 tables, area 8's pointers -> copies."""
    heap = probe.RAM + 0x20000
    ctx.u.mem_write(heap + COUNTERS, bytes(288))
    for g, t in enumerate(tables):
        ctx.u.mem_write(heap + g * GROUP_BYTES, t)
        ctx.u.mem_write(PTR_RAM + 4 * (g * 9 + 8), struct.pack('<I', heap + g * GROUP_BYTES))


def measure(ctx, area):
    obj, info, holder, header = probe.RAM, probe.RAM + 0x1000, probe.RAM + 0x8000, probe.RAM + 0x9000
    out = {}

    def reset(size, k220=0):
        ctx.u.mem_write(obj, bytes(0x100)); ctx.u.mem_write(info, bytes(0x3000))
        ctx.u.mem_write(obj + 4, struct.pack('<I', holder)); ctx.u.mem_write(holder, struct.pack('<I', header))
        ctx.u.mem_write(info + 0x200, bytes([area])); ctx.u.mem_write(info + 0x221, bytes([size]))
        ctx.u.mem_write(info + 0x220, bytes([k220]))
    for size in range(3):
        for k220 in range(4):
            reset(size, k220); ctx.call(0x10142A7C, (obj, info, 0))
            out[('cfg', size, k220)] = bytes(ctx.u.mem_read(obj, 0x70))
        reset(size); ctx.call(0x1014337A, (obj, info))
        out[('buf', size)] = bytes(ctx.u.mem_read(obj, 0x2C))
        reset(size); ctx.u.mem_write(info, bytes([area])); ctx.u.mem_write(info + 0x22, bytes([size]))
        ctx.call(0x101DC3E8, (info, obj))
        out[('desc', size)] = bytes(ctx.u.mem_read(obj, 28))
        for fn, args in ((0x10209294, (0, area, size)), (0x102092A6, (area, size)), (0x10209328, (0, area, size)),
                         (0x1020939C, (0, area, 0, 0))):
            out[('sel', fn, size)] = bytes(ctx.u.mem_read(ctx.call(fn, args), 8))
    for r2, r3 in itertools.product(range(3), range(3)):
        out[('g3', r2, r3)] = bytes(ctx.u.mem_read(ctx.call(0x10209346, (0, area, r2, r3)), 4))
    out['offset'] = bytes(ctx.u.mem_read(ctx.call(0x1020933E, (area,)), 4))
    return out


def run_site(ctx, name, regs0):
    site, old, _ = SITES[name]
    end = site + len(bytes.fromhex(old.replace(' ', '')))
    if name == 'trim':
        end = 0x10142F7E
    if name == 'info':
        end = 0x101DC672
    for i, v in enumerate(regs0):
        ctx.u.reg_write(UC_ARM_REG_R0 + i, v)
    ctx.u.reg_write(UC_ARM_REG_CPSR, 0x00000033)
    ctx.u.reg_write(UC_ARM_REG_SP, 0x5000F000)
    ctx.u.reg_write(UC_ARM_REG_LR, 0x50000001)
    if name == 'offset':                      # a function: run to its return
        ctx.u.emu_start(site | 1, 0x50000000, count=200)
    else:
        ctx.u.emu_start(site | 1, end, count=400)
    regs = [ctx.u.reg_read(UC_ARM_REG_R0 + i) for i in range(13)]
    regs.append(ctx.u.reg_read(UC_ARM_REG_SP))
    return regs


def verify(raw, image, tables, a56):
    stock, patched = probe.Ctx(raw), probe.Ctx(image)
    info = probe.RAM + 0x1000
    # (a) disabled: identical register state after every site, all areas and sizes
    for area in range(9):
        for size in range(3):
            for ctx in (stock, patched):
                ctx.u.mem_write(info, bytes(0x400))
                ctx.u.mem_write(info + 0x200, bytes([area])); ctx.u.mem_write(info + 0x221, bytes([size]))
            regs = [0x11110000 + i for i in range(13)]
            regs[0] = area; regs[4] = probe.RAM + 0x6000; regs[5] = info
            for name in SITES:
                if name in ('cfg', 'hpvw', 'hsel328', 'hplay'):
                    continue                  # whole function: covered by the geometry equality below
                a, b = run_site(stock, name, regs), run_site(patched, name, regs)
                assert a == b, (name, area, size)
    for area in (0, 1, 2, 4, 5, 7, 8):
        assert measure(stock, area) == measure(patched, area), ('disabled', area)
    # (b) enabled: the FX photo-branch configuration equals A56's area 8 (all modes, sizes, +0x220); FX live-view
    # calls (+0xFC set) and the other areas stay stock
    enable(patched, tables)
    good = probe.Ctx(a56)

    def cfg(ctx, area, mode, size, k220, fc):
        obj, info, holder, header = probe.RAM, probe.RAM + 0x1000, probe.RAM + 0x8000, probe.RAM + 0x9000
        ctx.u.mem_write(obj, bytes(0x100)); ctx.u.mem_write(info, bytes(0x3000))
        ctx.u.mem_write(obj + 4, struct.pack('<I', holder)); ctx.u.mem_write(holder, struct.pack('<I', header))
        ctx.u.mem_write(info, bytes([mode])); ctx.u.mem_write(info + 0x200, bytes([area]))
        ctx.u.mem_write(info + 0x221, bytes([size])); ctx.u.mem_write(info + 0x220, bytes([k220]))
        ctx.u.mem_write(info + 0xFC, struct.pack('<I', fc))
        ctx.u.mem_write(info + 0x21E, bytes([ctx.call(0x10190576, (area,))]))
        ctx.call(0x10142A7C, (obj, info, 0))
        return (bytes(ctx.u.mem_read(obj, 0x70)), ctx.u.mem_read(info + 0x200, 1)[0],
                ctx.u.mem_read(info + 0x21E, 1)[0])
    n_eq = 0
    for mode, size, k220 in itertools.product(range(8), range(3), range(4)):
        got, area_after, kind_after = cfg(patched, 0, mode, size, k220, 0)
        want = cfg(good, 8, mode, size, k220, 0)[0]
        want_kind = 3 if True else 0
        assert got == want and (area_after, kind_after) == (8, want_kind), (mode, size, k220)
        n_eq += 1
        assert cfg(patched, 0, mode, size, k220, 1) == cfg(stock, 0, mode, size, k220, 1), ('lv', mode, size, k220)
        for area in (1, 4, 5):
            assert cfg(patched, area, mode, size, k220, 0) == cfg(stock, area, mode, size, k220, 0), (area, mode)
        assert cfg(patched, 8, mode, size, k220, 0)[0] == want, ('1:1 while enabled = A56 4:3', mode)
    log = struct.unpack('<3I', patched.u.mem_read(probe.RAM + 0x20000 + COUNTERS, 12))
    assert log[1] == n_eq and log[0] == n_eq * 6, log   # per combination: FX, FX live view, DX, 16:9, DX 16:9, 1:1
    assert struct.unpack('<2H', bytes(patched.u.mem_read(patched.call(0x1020933E, (8,)), 4))) == (332, 0)
    return dict(disabled='offset/trim replay x 9 areas x 3 sizes; geometry of areas 0-2, 4, 5, 7, 8 identical',
                enabled_fx_photo_cfg_equals_a56_area8=n_eq, fx_liveview_cfg_stock=n_eq,
                other_areas_stock=[1, 4, 5], one_to_one_while_enabled='A56 4:3', log=list(log))


def verify_descriptor(raw, image, tables, a56):
    """Enabled FX photo cfg: info + 0x202 .. 0x21E equals A56's descriptor for area 8 (window / offset); FX live
    view and other areas: the descriptor bytes stay as the caller left them; disabled: untouched."""
    patched, good, stock = probe.Ctx(image), probe.Ctx(a56), probe.Ctx(raw)
    obj, info, holder, header = probe.RAM, probe.RAM + 0x1000, probe.RAM + 0x8000, probe.RAM + 0x9000

    def setup(ctx, area, size, fc, k220=0):
        ctx.u.mem_write(obj, bytes(0x100)); ctx.u.mem_write(info, bytes(0x3000))
        ctx.u.mem_write(obj + 4, struct.pack('<I', holder)); ctx.u.mem_write(holder, struct.pack('<I', header))
        ctx.u.mem_write(info + 0x200, bytes([area])); ctx.u.mem_write(info + 0x221, bytes([size]))
        ctx.u.mem_write(info + 0x222, bytes([size])); ctx.u.mem_write(info + 0x220, bytes([k220]))
        ctx.u.mem_write(info + 0xFC, struct.pack('<I', fc))
        ctx.u.mem_write(info + 0x202, bytes(range(1, 29)))      # marker: what the earlier FX descriptor left

    def desc(ctx, area, size):
        setup(ctx, area, size, 0)
        ctx.call(0x101DC3E8, (info + 0x200, info + 0x202))
        return bytes(ctx.u.mem_read(info + 0x202, 28))
    a56_want = {s_: desc(good, 8, s_) for s_ in range(3)}
    # disabled: cfg never touches the descriptor
    for area, size in itertools.product((0, 1, 4, 8), range(3)):
        setup(patched, area, size, 0); patched.call(0x10142A7C, (obj, info, 0))
        assert bytes(patched.u.mem_read(info + 0x202, 28)) == bytes(range(1, 29)), ('disabled', area, size)
    enable(patched, tables)
    rows = {}
    for size in range(3):
        setup(patched, 0, size, 0); patched.call(0x10142A7C, (obj, info, 0))
        got = bytes(patched.u.mem_read(info + 0x202, 28))
        assert got == a56_want[size], ('enabled FX photo', size, got.hex(), a56_want[size].hex())
        rows[size] = struct.unpack('<14H', got)
        for fc, area in ((1, 0), (0, 1), (0, 4)):
            setup(patched, area, size, fc); patched.call(0x10142A7C, (obj, info, 0))
            assert bytes(patched.u.mem_read(info + 0x202, 28)) == bytes(range(1, 29)), ('untouched', fc, area, size)
    assert rows[0][:6] == (6040, 4032, 5376, 4032, 332, 0), rows[0]   # = A56's MakerNote 0x1B after the type
    return dict(descriptor_l=rows[0], descriptor_m=rows[1], descriptor_s=rows[2],
                disabled_untouched='areas 0/1/4/8 x L/M/S', live_view_and_other_areas_untouched=True)


SHOT_CALLER = 0x10143229


def _call_from(self, fn, args, lr=SHOT_CALLER):
    """probe.Ctx.call, but returning to `lr` (the hook compares its caller), stopping there."""
    from unicorn.arm_const import UC_ARM_REG_R0 as R0, UC_ARM_REG_SP as SP, UC_ARM_REG_LR as LR
    u = self.u
    for i, a in enumerate(args):
        u.reg_write(R0 + i, a)
    u.reg_write(SP, 0x5000F000); u.reg_write(LR, lr)
    u.emu_start(fn | 1, lr & ~1, count=200000)
    return u.reg_read(R0)


probe.Ctx.call = _call_from


def verify_caller(image, tables):
    """Enabled, FX photo branch, area 0, but called from playback's 0x1014421B: no rewrite, descriptor untouched,
    geometry equal to the release firmware's FX."""
    patched = probe.Ctx(image); enable(patched, tables)
    obj, info, holder, header = probe.RAM, probe.RAM + 0x1000, probe.RAM + 0x8000, probe.RAM + 0x9000
    out = {}
    for lr in (0x1014421B, 0x1014327B, 0x101432CF):
        for size in range(3):
            patched.u.mem_write(obj, bytes(0x100)); patched.u.mem_write(info, bytes(0x3000))
            patched.u.mem_write(obj + 4, struct.pack('<I', holder)); patched.u.mem_write(holder, struct.pack('<I', header))
            patched.u.mem_write(info + 0x221, bytes([size])); patched.u.mem_write(info + 0x202, bytes(range(1, 29)))
            _call_from(patched, 0x10142A7C, (obj, info, 0), lr)
            a, k = patched.u.mem_read(info + 0x200, 1)[0], patched.u.mem_read(info + 0x21E, 1)[0]
            assert (a, k) == (0, 0), ('other caller remapped', hex(lr), size, a, k)
            assert bytes(patched.u.mem_read(info + 0x202, 28)) == bytes(range(1, 29)), ('descriptor touched', hex(lr))
            out[f'{lr:#x}/{"LMS"[size]}'] = 'not remapped'
    return out


PLAYBACK_CALLERS = (0x1034E9CB, 0x10354FAD, 0x10354FE5)
SHAPES43 = ((5352, 4016), (4008, 3008), (2664, 2000), (5376, 4032))


def run_with(ctx, fn, regs, lr):
    from unicorn.arm_const import UC_ARM_REG_R0 as R0, UC_ARM_REG_SP as SP, UC_ARM_REG_LR as LR, \
        UC_ARM_REG_CPSR as CPSR
    u = ctx.u
    for i, v in enumerate(regs):
        u.reg_write(R0 + i, v)
    u.reg_write(CPSR, 0x33); u.reg_write(SP, 0x5000F000); u.reg_write(LR, lr)
    u.emu_start(fn | 1, lr & ~1, count=20000)
    return [u.reg_read(R0 + i) for i in range(13)] + [u.reg_read(SP)]


def verify_playback(raw, image):
    stock, patched = probe.Ctx(raw), probe.Ctx(image)
    ctxobj, rec, out = probe.RAM + 0x30000, probe.RAM + 0x31000, probe.RAM + 0x34000
    shapes = list(SHAPES43) + [(4016, 4016), (6016, 4016), (3936, 2624), (6016, 3376), (5352, 4000), (0, 0)]
    n_hit = n_stock = 0
    for ctx in (stock, patched):
        ctx.u.mem_write(ctxobj, bytes(0x40)); ctx.u.mem_write(ctxobj + 12, struct.pack('<I', rec))
    for (w, h), lr, cls, small in itertools.product(shapes, PLAYBACK_CALLERS + (0x1034E9CF, 0x10354F01), range(10),
                                                    (0, 1)):
        res = []
        for ctx in (stock, patched):
            ctx.u.mem_write(rec, bytes(0x400)); ctx.u.mem_write(rec + 0x398, struct.pack('<2H', w, h))
            ctx.u.mem_write(out, bytes(8))
            regs = [out, cls, small] + [0x11110003] + [ctxobj] + [0x11110000 + i for i in range(5, 13)]
            r = run_with(ctx, 0x1034E5E4, regs, lr)
            res.append((struct.unpack('<2H', bytes(ctx.u.mem_read(out, 4))), r[4:], r[0]))
        if (w, h) in SHAPES43 and lr in PLAYBACK_CALLERS:
            assert res[1][0] == ((640, 480) if small else (1440, 1080)), (w, h, hex(lr), cls, small, res[1][0])
            keep = lambda r: r[:8] + r[9:]   # r4-r11 and sp (r12 is a scratch register)
            assert keep(res[1][1]) == keep(res[0][1]) and res[1][2] == res[0][2], 'registers'
            n_hit += 1
        else:
            k = lambda r: r[:8] + r[9:]   # r12 is scratch for the caller check
            assert res[0][0] == res[1][0] and res[0][2] == res[1][2] and k(res[0][1]) == k(res[1][1]), ('stock', w, h, hex(lr), cls, small)
            n_stock += 1
    m_hit = m_stock = 0
    for (w, h), lr, cls, size, flag in itertools.product(shapes, (0x10355011, 0x10355015), range(9), range(3), (0, 1)):
        res = []
        for ctx in (stock, patched):
            ctx.u.mem_write(rec, bytes(0x400)); ctx.u.mem_write(rec + 0x398, struct.pack('<2H', w, h))
            regs = [flag, cls, size, 0x11110003, ctxobj] + [0x11110000 + i for i in range(5, 13)]
            r = run_with(ctx, 0x10209328, regs, lr)
            res.append((r[0], r[1:4], r[4:]))
        if (w, h) in SHAPES43 and lr == 0x10355011:
            keep = lambda r: r[:8] + r[9:]   # r4-r11 and sp
            assert res[1][0] == rec + 0x398 and keep(res[1][2]) == keep(res[0][2]), ('main size', w, h, cls, size, flag)
            m_hit += 1
        else:
            k = lambda r: r[:8] + r[9:]
            assert res[0][0] == res[1][0] and res[0][1] == res[1][1] and k(res[0][2]) == k(res[1][2]), ('stock main', w, h, hex(lr), cls, size, flag)
            m_stock += 1
    return dict(preview_hits=n_hit, preview_stock=n_stock, main_hits=m_hit, main_stock=m_stock,
                callers=[hex(v) for v in PLAYBACK_CALLERS], main_caller='0x10355011', shapes=list(SHAPES43))



def verify_play(raw, image, tables):
    """Run 0x1034F6D4..0x1034F6D8 with r0 = class, r4 = info, [sp] = file record: area 8 only for 4:3 shapes while
    enabled; otherwise the class as stock. Registers r1-r12 and sp as stock."""
    from unicorn.arm_const import UC_ARM_REG_R0 as R0, UC_ARM_REG_SP as SP, UC_ARM_REG_CPSR as CPSR
    stock, off, on = probe.Ctx(raw), probe.Ctx(image), probe.Ctx(image)
    enable(on, tables)
    info, frec = probe.RAM + 0x30000, probe.RAM + 0x34000
    shapes = list(SHAPES43) + [(4016, 4016), (6016, 4016), (3936, 2624), (3840, 2160), (5352, 3008)]
    hits = same = 0
    for (w, h), cls in itertools.product(shapes, range(9)):
        out = []
        for ctx in (stock, off, on):
            u = ctx.u
            u.mem_write(info, bytes(0x400)); u.mem_write(frec, bytes(0x40))
            u.mem_write(frec + 12, struct.pack('<2h', w, h))
            u.mem_write(0x5000F000, struct.pack('<I', frec))
            regs = [cls] + [0x11110000 + i for i in range(1, 13)]
            regs[4] = info
            for i, v in enumerate(regs):
                u.reg_write(R0 + i, v)
            u.reg_write(CPSR, 0x33); u.reg_write(SP, 0x5000F000)
            u.emu_start(0x1034F6D5, 0x1034F6D8, count=2000)
            r = [u.reg_read(R0 + i) for i in range(13)] + [u.reg_read(SP)]
            out.append((u.mem_read(info + 0x200, 1)[0], r[1:12] + r[13:], r[0]))
        assert out[1][:2] == out[0][:2], ('disabled', w, h, cls)
        if (w, h) in SHAPES43:
            assert out[2][0] == 8 and out[2][1] == out[0][1], ('enabled 4:3', w, h, cls, out[2][0])
            hits += 1
        else:
            assert out[2][:2] == out[0][:2], ('enabled other', w, h, cls)
            same += 1
    return dict(enabled_43_area8=hits, other_or_disabled_stock=same, disabled_equal_all=len(shapes) * 9)


PRM_OFF = 0x200
MAGIC = 0x50544152
SHAPES233 = ((6016, 2576), (4512, 1936), (3008, 1288), (6040, 2592), (6048, 2592))
# kind 2 (5:4, no Z5 area maps to it in use) preview entries -> 2.33:1
KIND2_EDITS = [
    ('preview 640 (buf) kind 2', 0x116489CC + 8, (600, 480), (640, 272)),
    ('preview 640 width x2 (buf) kind 2', 0x116489E0 + 8, (1200, 0), (1280, 0)),
    ('preview 1920 (buf) kind 2', 0x116489F4 + 8, (1350, 1080), (1920, 824)),
    ('preview 1920 width x2 (buf) kind 2', 0x11648A08 + 8, (2700, 0), (3840, 0)),
    ('preview 640 (sel) kind 2', 0x1167B7A4 + 8, (600, 480), (640, 272)),
    ('preview 1920 (sel) kind 2', 0x1167B7B8 + 8, (1350, 1080), (1920, 824)),
    ('thumbnail (sel) kind 2', 0x1167B7CC + 8, (150, 120), (160, 68)),
    ('preview 570-class (sel) kind 2', 0x1167B7E0 + 8, (468, 375), (570, 244)),
]
# 2.33:1 values for the area-8 group entries (first pairs; the rest of each 32-byte window stays stock area 8)
T233 = {0: ((6040, 2592), (5176, 2226), (4530, 1948)), 1: ((6016, 2576), (4512, 1936), (3008, 1288)),
        2: ((6040, 2592), (5176, 2226), (4530, 1948)),
        3: ((6016, 2576), (6016, 2576), (6016, 2576), (5184, 2224), (5184, 2220), (4514, 1936))}
STOCK_A8 = (0x1167B884, 0x1167B8F0, 0x1167B95C, 0x1167BA28, 0x1167BB00, 0x1167BB00, 0x1167BBF0)


def tables233(raw):
    out = []
    for g, addr in enumerate(STOCK_A8):
        t = bytearray(raw[addr - BASE:addr - BASE + GROUP_BYTES])
        for i, (w, h) in enumerate(T233.get(g, ())):
            struct.pack_into('<2H', t, 4 * i, w, h)
        out.append(bytes(t))
    return out


def params_block(ratio):
    off, trim, kind = ((332, 0), (332, 0), 3) if ratio == 1 else ((0, 720), (0, 0), 2)
    return struct.pack('<I2H6H2B2x', MAGIC, *off, *(trim * 3), kind, ratio)


def enable_ratio(ctx, tables, ratio):
    enable(ctx, tables)
    ctx.u.mem_write(probe.RAM + 0x20000 + PRM_OFF, params_block(ratio))


def apply_kind2(data):
    edits = []
    for name, addr, old, new in KIND2_EDITS:
        off = addr - BASE
        if bytes(data[off:off + 4]) != struct.pack('<2H', *old):
            raise ValueError(f'original bytes differ at {addr:#x} ({name})')
        data[off:off + 4] = struct.pack('<2H', *new)
        edits.append(dict(name=name, address=hex(addr), original=struct.pack('<2H', *old).hex(),
                          replacement=struct.pack('<2H', *new).hex()))
    return edits


def verify_ratios(raw, image, tables43):
    """With the parameter block: 4:3 equals A78's 4:3 (no block); 2.33 equals the area-8 2.33 probe geometry,
    kind 2, descriptor window 6040 x 2592 offset (0, 720); playback sizes and decode area per ratio."""
    t233 = tables233(raw)
    ref, p43, p233 = probe.Ctx(image), probe.Ctx(image), probe.Ctx(image)
    enable(ref, tables43); enable_ratio(p43, tables43, 1); enable_ratio(p233, t233, 2)
    obj, info, holder, header = probe.RAM, probe.RAM + 0x1000, probe.RAM + 0x8000, probe.RAM + 0x9000

    def shot(ctx, size):
        ctx.u.mem_write(obj, bytes(0x100)); ctx.u.mem_write(info, bytes(0x3000))
        ctx.u.mem_write(obj + 4, struct.pack('<I', holder)); ctx.u.mem_write(holder, struct.pack('<I', header))
        ctx.u.mem_write(info + 0x221, bytes([size])); ctx.u.mem_write(info + 0x222, bytes([size]))
        ctx.call(0x10142A7C, (obj, info, 0))
        o = bytes(ctx.u.mem_read(obj, 0x70))
        return dict(parent=struct.unpack_from('<2H', o, 0x22), origin=struct.unpack_from('<2H', o, 0x26),
                    trim=struct.unpack_from('<2H', o, 0x38), trimmed=struct.unpack_from('<2H', o, 0x3C),
                    out=struct.unpack_from('<2H', o, 0x56), area=ctx.u.mem_read(info + 0x200, 1)[0],
                    kind=ctx.u.mem_read(info + 0x21E, 1)[0],
                    desc=struct.unpack('<6H', bytes(ctx.u.mem_read(info + 0x202, 12))), raw=o)
    res = {}
    for size in range(3):
        a, b = shot(ref, size), shot(p43, size)
        assert a == b, ('4:3 with block differs from A78', size)
        c = shot(p233, size)
        want = ((6016, 2576), (4512, 1936), (3008, 1288))[size]
        assert c['out'] == want and c['parent'] == (6016, 2576) and c['origin'] == (12, 728) and \
            c['trim'] == (0, 0) and c['trimmed'] == (6016, 2576), ('2.33 geometry', size, c)
        assert (c['area'], c['kind']) == (8, 2), ('2.33 area / kind', c['area'], c['kind'])
        assert c['desc'] == (6040, 4032, 6040, 2592, 0, 720), ('2.33 descriptor', c['desc'])
        res['LMS'[size]] = dict(out=c['out'], parent=c['parent'], origin=c['origin'], desc=c['desc'])
    # previews of kind 2 / 3 as the encoder reads them (selector tables)
    for kind, small, large in ((2, (640, 272), (1920, 824)), (3, (640, 480), (1440, 1080)), (1, (640, 360), (1920, 1080))):
        assert struct.unpack('<2H', image[0x1167B7A4 - BASE + 4 * kind:0x1167B7A4 - BASE + 4 * kind + 4]) == small
        assert struct.unpack('<2H', image[0x1167B7B8 - BASE + 4 * kind:0x1167B7B8 - BASE + 4 * kind + 4]) == large
    # playback preview sizes and decode area
    ctxobj, rec, out, frec = probe.RAM + 0x30000, probe.RAM + 0x31000, probe.RAM + 0x34000, probe.RAM + 0x35000
    pv = {}
    for (w, h) in SHAPES233 + SHAPES43:
        for small in (0, 1):
            p233.u.mem_write(ctxobj + 12, struct.pack('<I', rec)); p233.u.mem_write(rec + 0x398, struct.pack('<2H', w, h))
            p233.u.mem_write(out, bytes(4))
            run_with(p233, 0x1034E5E4, [out, 8, small, 0, ctxobj] + [0] * 8, PLAYBACK_CALLERS[0])
            got = struct.unpack('<2H', bytes(p233.u.mem_read(out, 4)))
            want = ((640, 272) if small else (1920, 824)) if (w, h) in SHAPES233 else ((640, 480) if small else (1440, 1080))
            assert got == want, ('playback preview', w, h, small, got)
            pv[f'{w}x{h}/{"small" if small else "large"}'] = got
    from unicorn.arm_const import UC_ARM_REG_R0 as R0, UC_ARM_REG_SP as SP, UC_ARM_REG_CPSR as CPSR
    areas = {}
    for ctx, tag in ((p43, '4:3 on'), (p233, '2.33 on')):
        for (w, h) in ((6016, 2576), (5352, 4016), (6016, 4016)):
            u = ctx.u
            u.mem_write(info, bytes(0x400)); u.mem_write(frec, bytes(0x40)); u.mem_write(frec + 12, struct.pack('<2h', w, h))
            u.mem_write(0x5000F000, struct.pack('<I', frec))
            for i in range(13):
                u.reg_write(R0 + i, 0)
            u.reg_write(R0 + 4, info); u.reg_write(CPSR, 0x33); u.reg_write(SP, 0x5000F000)
            u.emu_start(0x1034F6D5, 0x1034F6D8, count=4000)
            areas[f'{tag} {w}x{h}'] = u.mem_read(info + 0x200, 1)[0]
    assert areas == {'4:3 on 6016x2576': 0, '4:3 on 5352x4016': 8, '4:3 on 6016x4016': 0,
                     '2.33 on 6016x2576': 8, '2.33 on 5352x4016': 0, '2.33 on 6016x4016': 0}, areas
    return dict(geometry_233=res, playback_previews=pv, playback_decode_area=areas)


def module_inc233(raw):
    lines = ['/* Generated by research/aspect/z5_aspect43_a81_fw.py - do not edit. 2.33:1 values for the area-8 group',
             ' * entries (stock area 8 elsewhere): L 6016x2576, M 4512x1936, S 3008x1288, offset (0, 720), trim 0. */',
             'static const unsigned char a233_tables[A43_GROUPS * A43_GROUP_BYTES] = {']
    for t in tables233(raw):
        lines.append('    ' + ', '.join(f'0x{b:02X}' for b in t) + ',')
    lines.append('};')
    return '\n'.join(lines) + '\n'


def module_inc(tables):
    lines = ['/* Generated by research/aspect/z5_aspect43_fx_fw.py - do not edit. A56 area-8 4:3 geometry (camera-',
             ' * proven 2026-11-23: L 5352x4016, M 4008x3008, S 2664x2000), 32 bytes per pointer-table group 0..6. */',
             f'#define A43_GROUPS 7u', f'#define A43_GROUP_BYTES {GROUP_BYTES}u',
             'static const unsigned char a43_tables[A43_GROUPS * A43_GROUP_BYTES] = {']
    for t in tables:
        lines.append('    ' + ', '.join(f'0x{b:02X}' for b in t) + ',')
    lines.append('};')
    return '\n'.join(lines) + '\n'


def build(source, a56_path, output, inc):
    raw = source.read_bytes()
    a56 = a56_path.read_bytes()
    input_hash = hashlib.sha256(raw).hexdigest()
    if input_hash != RELEASE_SHA or binascii.crc_hqx(raw, 0):
        raise ValueError(f'expected the release MAIN a3704c6f, got {input_hash}')
    if not hashlib.sha256(a56).hexdigest().startswith(A56_SHA):
        raise ValueError('expected the A56 MAIN 7477fde9 for the 4:3 tables')
    report_path = output.with_name(output.name + '.json')
    if output.exists() or report_path.exists() or (inc and inc.exists()):
        raise FileExistsError(output)
    tables = a56_tables(a56)
    data = bytearray(raw[:-2])
    kind2 = apply_kind2(data)
    start = BASE + len(data)
    code, symbols = assemble(start)
    if start + len(code) > LIMIT:
        raise ValueError('hooks exceed the MAIN append area')
    windows = [(site, site + len(bytes.fromhex(old.replace(' ', '')))) for site, old, _ in SITES.values()]
    old_windows = hooks.dd.windows
    try:
        hooks.dd.windows = lambda: windows
        incoming = hooks.dd.audit_incoming(raw)
    finally:
        hooks.dd.windows = old_windows
    if incoming:
        raise ValueError(f'entry into replaced instructions: {incoming}')
    edits = []
    for index, (name, (site, old, link)) in enumerate(SITES.items()):
        original = bytes.fromhex(old.replace(' ', ''))
        veneer = STUBS + index * 8
        redirect = hooks.branch(site, veneer, link) + bytes.fromhex('00bf') * ((len(original) - 4) // 2)
        tail = bytes.fromhex('dff800f0') + struct.pack('<I', symbols[name] | 1)
        for address, old_bytes, new_bytes in ((site, original, redirect), (veneer, bytes(8), tail)):
            offset = address - BASE
            if data[offset:offset + len(old_bytes)] != old_bytes:
                raise ValueError(f'original bytes differ at {address:#x}')
            data[offset:offset + len(old_bytes)] = new_bytes
            edits.append(dict(name=name, address=hex(address), original=old_bytes.hex(), replacement=new_bytes.hex()))
    data.extend(code)
    data.extend(struct.pack('>H', binascii.crc_hqx(bytes(data), 0)))
    image = bytes(data)
    assert binascii.crc_hqx(image, 0) == 0
    checks = verify(raw, image, tables, a56)
    checks['descriptor'] = verify_descriptor(raw, image, tables, a56)
    checks['other_callers'] = verify_caller(image, tables)
    checks['playback'] = verify_playback(raw, image)
    checks['play_area'] = verify_play(raw, image, tables)
    checks['ratios'] = verify_ratios(raw, image, tables)
    report = dict(input_sha256=input_hash, a56_sha256=hashlib.sha256(a56).hexdigest(),
                  output_sha256=hashlib.sha256(image).hexdigest(), edits=edits, data_edits=kind2,
                  append=dict(address=hex(start), size=len(code), sha256=hashlib.sha256(code).hexdigest()),
                  tables=[t.hex() for t in tables], checks=checks, camera_verified=False)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('xb') as stream:
        stream.write(image)
    with report_path.open('x', encoding='utf-8') as stream:
        json.dump(report, stream, indent=2)
        stream.write('\n')
    if inc:
        inc.write_text(module_inc(tables), encoding='utf-8', newline='\n')
        inc.with_name('aspect233_tables.inc').write_text(module_inc233(raw), encoding='utf-8', newline='\n')
    print(json.dumps(dict(output_sha256=report['output_sha256'], append=report['append'], checks=checks), indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--a56', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--module-inc', type=Path)
    args = parser.parse_args()
    build(args.source, args.a56, args.out, args.module_inc)
