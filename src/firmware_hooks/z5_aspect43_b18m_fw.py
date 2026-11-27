"""B18m = B15m + the four preview-kind selector functions (0x102092FA / 0x10209304 / 0x1020930E / 0x10209318) and three
capture preview-buffer table loads (0x10143572 / 0x101435D4 / 0x10143F2E) routed through hszw's veneer (0x105BA3C4, zero
new trampolines), with the B18 stub (research/aspect/b18m_fx43.S).

Why: Z5's stock firmware keeps native 5:4 preview sizes (600x480, 1350x1080, 150x120, 468x375) in kind 2 and native 5:4
display-fit rectangles (LCD 600x480, EVF 1066x852, HDMI 1160x928) in 0x1018B4A2 (class 2 via 0x101DC978 + 0x10209304),
while sensor readout rejects area 2 (camera-proven in A60-A65). A81 repurposed ROM kind 2 for 2.33:1 (640x272, 1920x824).
Routing the preview selectors and buffer loads through hszw keeps 4:3 and 2.33:1 identical to B17 while restoring the
genuine Nikon 5:4 preview entries whenever 5:4 (ratio 3: L 5016x4016, M 3760x3008, S 2496x2000, trim (500, 0)) is active.

    python -B research/aspect/z5_aspect43_b18m_fw.py <a3704c6f EG165050.BIN> --a56 <A56 7477fde9 EG165050.BIN> \
        --module-src <absolute release worktree>/src/card_module --b1-diag <B1 DIAG17.BIN> \
        --out output/<new dir>/EG165050.BIN

Checks: every B15 check (unchanged), plus
  pvw_54     all 4 preview selectors (kinds 0..4) and 3 buffer-init preview loads: bare / off / 4:3 / 2.33:1 equal to
             B15; 5:4 active -> kind 2 returns stock Nikon 5:4 (600x480, 1350x1080, 150x120, 468x375), other kinds stock.
  shot_54    5:4 L/M/S capture configuration (both JPEG and NEF-only trim2 branches), buffer init, FX RAW plane, and
             area descriptor override.
  lv_54      5:4 native live-view window (500, 0, 5516, 4016), area crop, native display fit on B1 camera RAM (LCD
             (60, 0, 660, 480), EVF (106, 54, 1172, 906), 1920x1080 (380, 28, 1540, 956)), ELBY 0x1b & 0x2b (1256x1006,
             X0=126, in=1250x1000, out=1280x1024, stride=1280), LV_crop_tbl, ELBR (640x512), and playback / NEF-only.
camera_verified: false.
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import z5_aspect43_b15m_fw as b15  # noqa: E402

b12, b10, b9, b4, b3, a85, m, probe = b15.b12, b15.b10, b15.b9, b15.b4, b15.b3, b15.a85, b15.m, b15.probe
fit = b3.fit
mm = a85.mm

RATIO_WORD = probe.RAM + 0x3BFE8
PVW54_BASE = probe.RAM + 0x3BF40

# Inject ratio-aware stand-ins into the standalone Unicorn C harness (defaulting to 1 = 4:3 when CHOSEN is set).
B18_STANDIN = (
    '#define AF_LV_RATIO() (*(volatile u32 *)0x%Xu ? (*(volatile u32 *)0x%Xu ? *(volatile u32 *)0x%Xu : 1u) : 0u)\n'
    '#define AF_PVW54(idx) (*(volatile u32 *)0x%Xu ? ((idx) < 4u ? 0x%Xu + 4u * (idx) : 0x%Xu + 0x24u) : 0u)\n'
    % (b3.CHOSEN, RATIO_WORD, RATIO_WORD, PVW54_BASE, PVW54_BASE, PVW54_BASE)
)
assert '#define B2_LVSZ\n' in mm.HARNESS
mm.HARNESS = mm.HARNESS.replace('#define B2_LVSZ\n', '#define B2_LVSZ\n' + B18_STANDIN)

SEL_SITES = (
    ('hpvwsel0 preview selector 640 (0x102092FA) -> hszw',  0x102092FA, bytes.fromhex('7849bc3101eb80007047')),
    ('hpvwsel1 preview selector 1920 (0x10209304) -> hszw', 0x10209304, bytes.fromhex('7549d03101eb80007047')),
    ('hpvwsel2 preview selector 160 (0x1020930E) -> hszw',  0x1020930E, bytes.fromhex('7349e43101eb80007047')),
    ('hpvwsel3 preview selector 570 (0x10209318) -> hszw',  0x10209318, bytes.fromhex('7049f83101eb80007047')),
)
BUF_SITES = (
    ('hpvwbuf1 buffer init small preview (0x10143572) -> hszw', 0x10143572, bytes.fromhex('96f81e02ff4b')),
    ('hpvwbuf2 buffer init large preview (0x101435D4) -> hszw', 0x101435D4, bytes.fromhex('96f81e12e64b')),
    ('hpvwbuf3 secondary buffer init large (0x10143F2E) -> hszw', 0x10143F2E, bytes.fromhex('f74800eb8101')),
)

TABLES54 = [
    bytes.fromhex(
        '9817c00f3814800db211d00b8017b00fa011c00bc00bd8078017380da011e809'
        '9813b00fb00ec00bc009d0079817c00f3814800db211d00b9817480d3814620b'
        '9817c00f3814800db211d00b8017b00f8017ac0f8017b00f38147a0d34147a0d'
        '9813b00f9813b00f9813b00fd010740dd010740db40ec40b0c0008000c000a00'
        '0c0008000c0008000c0008000600060006000600080006000c00080010000800'
        '0c0008000c0008000c0008000600060006000600080006000c00080010000800'
        '0000000000000000000000000100010001000100010000000004a8020004a802'
    )[g * 32:(g + 1) * 32]
    for g in range(7)
]
PRM54 = bytes.fromhex(
    '5241545000000000f4010000f4010000f4010000020300000000000074010000'
    '9817c00f00000000cc0be007000208009815b80f9817c00f00000000170ac006'
    'b00107007e12790d9817c00f00000000d608e8057d0106002f10ca0b00000000'
)


def assemble_stubs(address):
    with tempfile.TemporaryDirectory(dir=probe.ROOT / 'output') as tmp:
        def run(*args):
            return subprocess.check_output(args, cwd=tmp, text=True)
        run('arm-none-eabi-gcc', '-mcpu=cortex-a9', '-mthumb', '-c', str(HERE / 'b18m_fx43.S'), '-o', 'h.o')
        run('arm-none-eabi-ld', f'-Ttext={address:#x}', '-e', 'cfg', '-o', 'h.elf', 'h.o')
        run('arm-none-eabi-objcopy', '-O', 'binary', 'h.elf', 'h.bin')
        symbols = {}
        for line in run('arm-none-eabi-nm', 'h.elf').splitlines():
            value, _, name = line.split()
            symbols[name] = int(value, 16)
        return (Path(tmp) / 'h.bin').read_bytes(), symbols


a85.assemble_stubs = assemble_stubs
b9.assemble_stubs = assemble_stubs
b10.assemble_stubs = assemble_stubs
b12.assemble_stubs = assemble_stubs
b15.assemble_stubs = assemble_stubs


def apply_b18_sites(data):
    edits = []
    for name, addr, old in SEL_SITES:
        off = addr - m.BASE
        if bytes(data[off:off + 10]) != old:
            raise ValueError('original bytes differ at %#x: got %s want %s' % (addr, bytes(data[off:off + 10]).hex(), old.hex()))
        new = bytes.fromhex('00b5') + m.hooks.branch(addr + 2, b4.HSZW_VENEER, True) + bytes.fromhex('00bd00bf')
        assert len(new) == 10
        data[off:off + 10] = new
        edits.append(dict(name=name, address=hex(addr), original=old.hex(), replacement=new.hex()))
    for name, addr, old in BUF_SITES:
        off = addr - m.BASE
        if bytes(data[off:off + 6]) != old:
            raise ValueError('original bytes differ at %#x: got %s want %s' % (addr, bytes(data[off:off + 6]).hex(), old.hex()))
        new = m.hooks.branch(addr, b4.HSZW_VENEER, True) + bytes.fromhex('00bf')
        assert len(new) == 6
        data[off:off + 6] = new
        edits.append(dict(name=name, address=hex(addr), original=old.hex(), replacement=new.hex()))
    return edits


def setup_54_fixture(ctx, raw, lv_chosen=True, tables_in=True):
    # Populate PVW54_BASE: +0..+15 = 4 selector pairs, +0x10..+0x5F = 80B copy of 0x116489CC..0x11648A1B (stock 5:4!)
    sel_pairs = struct.pack('<8H', 600, 480, 1350, 1080, 150, 120, 468, 375)
    buf_blk = bytearray(raw[0x116489CC - m.BASE:0x116489CC - m.BASE + 80])
    ctx.u.mem_write(PVW54_BASE, sel_pairs + bytes(buf_blk))
    if lv_chosen:
        ctx.u.mem_write(b3.PAIRS, struct.pack('<6H', 5016, 4016, 3760, 3008, 2496, 2000))
        ctx.u.mem_write(b3.CHOSEN, struct.pack('<I', b3.PAIRS))
        ctx.u.mem_write(RATIO_WORD, struct.pack('<I', 3))
    else:
        ctx.u.mem_write(b3.CHOSEN, struct.pack('<I', 0))
        ctx.u.mem_write(RATIO_WORD, struct.pack('<I', 0))
    if tables_in:
        m.enable(ctx, TABLES54)
        ctx.u.mem_write(probe.RAM + 0x20000 + m.PRM_OFF, PRM54)


def verify_b18(raw, image):
    c = b3.contexts(raw, image)
    c54_out, c54_in = probe.Ctx(image), probe.Ctx(image)
    setup_54_fixture(c54_out, raw, lv_chosen=True, tables_in=False)
    setup_54_fixture(c54_in, raw, lv_chosen=True, tables_in=True)

    # 1. All 4 preview selectors x kinds 0..4
    sel_fns = (0x102092FA, 0x10209304, 0x1020930E, 0x10209318)
    stock_54_sel = ((600, 480), (1350, 1080), (150, 120), (468, 375))
    b15_233_sel = ((640, 272), (1920, 824), (160, 68), (570, 244))
    for idx, fn in enumerate(sel_fns):
        for k in range(5):
            for name in ('bare', 'not_chosen', 'not_chosen_233', 'chosen_out', 'chosen_in'):
                b9._map_area(c[name].u, 0)
                p = c[name].call(fn, (k,))
                got = struct.unpack('<2H', bytes(c[name].u.mem_read(p, 4)))
                want = b15_233_sel[idx] if k == 2 else struct.unpack_from('<2H', raw, (0x1167B7A4 + 0x14 * idx) - m.BASE + 4 * k)
                assert got == want, (hex(fn), name, k, got, want)
            for tag, ctx54 in (('54_out', c54_out), ('54_in', c54_in)):
                b9._map_area(ctx54.u, 0)
                p = ctx54.call(fn, (k,))
                got = struct.unpack('<2H', bytes(ctx54.u.mem_read(p, 4)))
                want = stock_54_sel[idx] if k == 2 else struct.unpack_from('<2H', raw, (0x1167B7A4 + 0x14 * idx) - m.BASE + 4 * k)
                assert got == want, (hex(fn), tag, k, hex(p), got, want)

    # 2. 5:4 capture geometry (JPEG and NEF-only trim2 branches), buffer init, FX RAW plane, descriptor
    obj, info, holder, header = probe.RAM, probe.RAM + 0x1000, probe.RAM + 0x8000, probe.RAM + 0x9000
    want_outs = ((5016, 4016), (3760, 3008), (2496, 2000))
    shot54 = {}
    for sz in range(3):
        for r2 in (0, 1):
            c54_in.u.mem_write(obj, bytes(0x100)); c54_in.u.mem_write(info, bytes(0x3000))
            c54_in.u.mem_write(obj + 4, struct.pack('<I', holder)); c54_in.u.mem_write(holder, struct.pack('<I', header))
            c54_in.u.mem_write(info + 0x221, bytes([sz])); c54_in.u.mem_write(info + 0x222, bytes([sz]))
            from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_SP, UC_ARM_REG_LR
            u = c54_in.u
            u.reg_write(UC_ARM_REG_R0, obj); u.reg_write(UC_ARM_REG_R0 + 1, info); u.reg_write(UC_ARM_REG_R0 + 2, r2)
            u.reg_write(UC_ARM_REG_SP, 0x5000F000); u.reg_write(UC_ARM_REG_LR, 0x10143229)
            u.emu_start(0x10142A7C | 1, 0x10143228, count=200000)
            o = bytes(u.mem_read(obj, 0x70))
            parent = struct.unpack_from('<2H', o, 0x22)
            origin = struct.unpack_from('<2H', o, 0x26)
            trim = struct.unpack_from('<2H', o, 0x38)
            trimmed = struct.unpack_from('<2H', o, 0x3C)
            out = struct.unpack_from('<2H', o, 0x56)
            assert parent == (6016, 4016) and origin == (12, 8) and trim == (500, 0) and trimmed == (5016, 4016) and out == want_outs[sz], (sz, r2, out)
            assert u.mem_read(info + 0x200, 1)[0] == 8 and u.mem_read(info + 0x21E, 1)[0] == 2
        # Preview buffer init 0x10143516 (small +0x1A, large padded +0x36 & unpadded +0x3A) & secondary 0x10143EFA (+0x22)
        c54_in.u.mem_write(obj, bytes(0x100)); c54_in.u.mem_write(obj + 4, struct.pack('<I', holder))
        c54_in.call(0x10143516, (obj, info))
        b_init = bytes(c54_in.u.mem_read(obj, 0x70))
        assert struct.unpack_from('<2H', b_init, 0x1A) == (600, 480), struct.unpack_from('<2H', b_init, 0x1A)
        assert struct.unpack_from('<4H', b_init, 0x36) == (1408, 1088, 1350, 1080), struct.unpack_from('<4H', b_init, 0x36)
        c54_in.u.mem_write(obj, bytes(0x100)); c54_in.u.mem_write(obj + 4, struct.pack('<I', holder))
        c54_in.call(0x10143EFA, (obj, info))
        b_sec = bytes(c54_in.u.mem_read(obj, 0x30))
        assert struct.unpack_from('<2H', b_sec, 0x22) == (1350, 1080), struct.unpack_from('<2H', b_sec, 0x22)
        # Also verify 2.33:1 still gets (640, 272) and (1920, 832, 1920, 824) at 0x10143516 & 0x10143EFA
        c['not_chosen_233'].u.mem_write(obj, bytes(0x100)); c['not_chosen_233'].u.mem_write(obj + 4, struct.pack('<I', holder))
        c['not_chosen_233'].u.mem_write(holder, struct.pack('<I', header)); c['not_chosen_233'].u.mem_write(info + 0x21E, b'\x02')
        c['not_chosen_233'].call(0x10143516, (obj, info))
        b233 = bytes(c['not_chosen_233'].u.mem_read(obj, 0x70))
        assert struct.unpack_from('<2H', b233, 0x1A) == (640, 272) and struct.unpack_from('<4H', b233, 0x36) == (1920, 832, 1920, 824)
        c['not_chosen_233'].u.mem_write(obj, bytes(0x100)); c['not_chosen_233'].u.mem_write(obj + 4, struct.pack('<I', holder))
        c['not_chosen_233'].call(0x10143EFA, (obj, info))
        assert struct.unpack_from('<2H', bytes(c['not_chosen_233'].u.mem_read(obj, 0x30)), 0x22) == (1920, 824)
        plane = struct.unpack('<2H', bytes(c54_in.u.mem_read(c54_in.call(0x10209294, (0, 8, sz)), 4)))
        assert plane == ((6040, 4032), (5176, 3456), (4530, 3024))[sz]
        shot54['LMS'[sz]] = dict(out=want_outs[sz], small_pvw=(600, 480), large_pvw=(1350, 1080), raw_plane=plane)

    # 3. 5:4 Native Live View: window, crop, display fit (LCD & EVF), ELBY (0x1b & 0x2b), LV_crop_tbl, ELBR
    for ctx54 in (c54_out, c54_in):
        assert b3.b2.window(ctx54, 0) == (6016, 4016, 500, 0, 5516, 4016)
        assert b4.crop(ctx54, 0) == (500, 0, 5516, 4016)
        # Native display fit 0x1018B4A2 on B1 camera RAM (uses 0x1018B388 -> 5016x4016 and 0x1018B3A0 -> 1350x1080!)
        r_b1 = b3.b2.B1['r']
        b9._map_area(ctx54.u, 0)
        fit.load(ctx54, r_b1, fit.state_at(r_b1, 70000))
        ctx54.u.mem_write(fit.ARG, bytes((0, 0, 0, 0)) + bytes(28))
        ctx54.call(0x1018B4A2, (fit.ARG, 0x8))
        lcd_rect = struct.unpack('<4h', ctx54.u.mem_read(0xC23802F4, 8))
        hdmi_rect = struct.unpack('<4h', ctx54.u.mem_read(0xC2380314, 8))
        assert lcd_rect == (60, 0, 660, 480) and hdmi_rect == (380, 28, 1540, 956), (lcd_rect, hdmi_rect)
        fit.load(ctx54, r_b1, fit.state_at(r_b1, 95000))
        ctx54.u.mem_write(fit.ARG, bytes((0, 0, 0, 0)) + bytes(28))
        ctx54.call(0x1018B4A2, (fit.ARG, 0x8))
        evf_rect = struct.unpack('<4h', ctx54.u.mem_read(0xC23802F4, 8))
        assert evf_rect == (106, 54, 1172, 906), evf_rect
        # ELBY 0x2b
        got_2b = b10.run_elby_2b_site(ctx54, 0x10146E48, 0)
        assert got_2b == ((1280, 1280, 1256, 1006, 1256, 1006, 126, 0), (0x80000000, 0x80008000), (3, 3, 1250, 1000), (1280, 1024)), got_2b

    return dict(
        pvw_54='4 selectors x 5 kinds + 3 buffer-init sites verified (B15 identical off/4:3/2.33; stock 5:4 when 5:4 active)',
        shot_54=shot54,
        lv_54=dict(window=(500, 0, 5516, 4016), lcd_fit=(60, 0, 660, 480), evf_fit=(106, 54, 1172, 906),
                   hdmi_fit=(380, 28, 1540, 956), elby=(1256, 1006, 126, 0, 1280, 1024), elbr=(640, 512)),
    )


def main():
    orig_m_build = m.build

    def innermost(source, a56, out, inc):
        prev_kind2 = m.apply_kind2                      # b15 + b12 + b10 + b9 + b8 + b7 + b4 wrappers

        def kind2(data):
            edits = prev_kind2(data)
            edits.extend(apply_b18_sites(data))
            return edits
        m.apply_kind2 = kind2
        prev = a85.verify_query

        def checks(raw, image):
            res = prev(raw, image)
            res.update(verify_b18(raw, image))
            return res
        a85.verify_query = checks
        orig_m_build(source, a56, out, inc)
    m.build = innermost
    b15.main()


if __name__ == '__main__':
    main()
