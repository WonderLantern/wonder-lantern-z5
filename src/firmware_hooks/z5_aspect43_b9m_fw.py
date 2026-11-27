"""B9m = B8m + the five live-view pipeline buffer/crop call sites routed through hszw's veneer (0x105BA3C4, zero new
trampolines), with the B9 stub (research/aspect/b9m_fx43.S: stock tail dispatch for ELBY_fill, LV_crop_tbl, and
0x1020949A). Module: aspect_fw.inc AF_LV_ELBY1/2/3, AF_LV_LVCROP, AF_LV_ELBR.

Why (offline disassembly + B5 camera RAMD2/RAMD3, 2026-11-27): B7/B8 only changed capture info + 0x4E0 (aspect class),
which only affects preview buffer sizes (1620 -> 1440) and ISP mode byte + 0x27E. The live-view pipeline itself reads
+ 0x4CC (image area) and indexes the stock ROM area table 0x11648330 (ELBY 0x101460F2, LV_crop_tbl 0x10145354) and
0x1020949A (ELBR 0x10146B4C). For FX with 4:3 chosen, the module runs the stock helper then writes exact 4:3 geometry:
  - ELBY: W=1340, H=1006, X0=84, Y0=0, inW=1334, inH=1000, dx=3, dy=3, outW=1536, outH=1152, stride=1344
  - LV_crop_tbl: sz=(1340, 1006), rect=(87, 3, 1421, 1003)
  - ELBR: (896, 672)
Unicorn verification of the stock warp mesh builder (0x10254D84 + 0x100E7D8C) and resizer (0x100E7D68) confirms
1536 x 1152 yields a 24 x 144 cell grid (1536 x 1152 exact) and 1.00000 H/V resizer ratio on LCD (640x480),
EVF (1136x852), and ELBR (896x672).

    python -B research/aspect/z5_aspect43_b9m_fw.py <a3704c6f EG165050.BIN> --a56 <A56 7477fde9 EG165050.BIN> \
        --module-src <absolute release worktree>/src/card_module --b1-diag <B1 DIAG17.BIN> \
        --out output/<new dir>/EG165050.BIN

Checks: every B8 check, plus
  lv_elby    all three ELBY_fill call sites (0x101464CA / 0x1014659A / 0x101465D2) across areas 0, 1, 4, 5, 8 and
             non-still modes (info[0]=1, info[1]=1, info[0x10]=1): stock, bare, not_chosen, not_chosen_233 = stock;
             chosen (tables out/in) + FX still -> 1340x1006, X0=84, in=1334x1000, out=1536x1152, stride=1344, plane_cb
             aligned; all other areas / modes unchanged.
  lv_lvcrop  0x101455C6 (0x10145354) across areas 0, 1, 4, 5, 8 and non-still modes: only chosen + FX still ->
             sz=(1340, 1006), rect=(87, 3, 1421, 1003).
  lv_elbr    0x10146B4C (ELBR_fill calling 0x10146BA2 -> 0x1020949A) across areas 0, 1, 4, 8: only chosen + FX ->
             (896, 672) (stride 896 x 672).
camera_verified: false.
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import z5_aspect43_b8m_fw as b8  # noqa: E402

b7, b4, b3, a85, m, probe = b8.b7, b8.b4, b8.b3, b8.a85, b8.m, b8.probe
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.arm_const import (  # noqa: E402
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_CPSR,
)

SITES = (
    ('helby1 live-view ELBY_fill site 1 -> hszw veneer', 0x101464CA, bytes.fromhex('fff712fe')),
    ('helby2 live-view ELBY_fill site 2 -> hszw veneer', 0x1014659A, bytes.fromhex('fff7aafd')),
    ('helby3 live-view ELBY_fill site 3 -> hszw veneer', 0x101465D2, bytes.fromhex('fff78efd')),
    ('hlvcrop live-view crop table -> hszw veneer',      0x101455C6, bytes.fromhex('fff7c5fe')),
    ('helbr live-view ELBR size query -> hszw veneer',   0x10146BA2, bytes.fromhex('c2f07afc')),
)


def assemble_stubs(address):
    with tempfile.TemporaryDirectory(dir=probe.ROOT / 'output') as tmp:
        def run(*args):
            return subprocess.check_output(args, cwd=tmp, text=True)
        run('arm-none-eabi-gcc', '-mcpu=cortex-a9', '-mthumb', '-c', str(HERE / 'b9m_fx43.S'), '-o', 'h.o')
        run('arm-none-eabi-ld', f'-Ttext={address:#x}', '-e', 'cfg', '-o', 'h.elf', 'h.o')
        run('arm-none-eabi-objcopy', '-O', 'binary', 'h.elf', 'h.bin')
        symbols = {}
        for line in run('arm-none-eabi-nm', 'h.elf').splitlines():
            value, _, name = line.split()
            symbols[name] = int(value, 16)
        return (Path(tmp) / 'h.bin').read_bytes(), symbols


a85.assemble_stubs = assemble_stubs


def apply_b9_sites(data):
    edits = []
    for name, addr, old in SITES:
        off = addr - m.BASE
        if bytes(data[off:off + 4]) != old:
            raise ValueError('original bytes differ at %#x: got %s want %s' % (addr, bytes(data[off:off + 4]).hex(), old.hex()))
        new = m.hooks.branch(addr, b4.HSZW_VENEER, True)
        data[off:off + 4] = new
        edits.append(dict(name=name, address=hex(addr), original=old.hex(), replacement=new.hex()))
    return edits


def _map_area(u, area):
    try:
        u.mem_map(0xC2504000, 0x1000)
    except Exception:
        pass
    u.mem_write(0xC25043A4, struct.pack('<I', area))


def run_elby_site(ctx, site_addr, area, mode0=0, mode1=0, flag10=0):
    """Execute the `bl` at site_addr (one of 0x101464CA / 0x1014659A / 0x101465D2) and inspect the 0x68-byte ELBY desc."""
    u = ctx.u
    _map_area(u, area)
    desc = probe.RAM + 0x12000
    info = probe.RAM + 0x13000
    buf_pair = probe.RAM + 0x12800
    u.mem_write(desc, bytes(0x80))
    u.mem_write(buf_pair, struct.pack('<2I', 0xC4000100, 0x00400000))
    u.mem_write(desc + 4, struct.pack('<I', buf_pair))
    u.mem_write(info, bytes(0x600))
    u.mem_write(info + 0, bytes([mode0, mode1]))
    u.mem_write(info + 0x10, struct.pack('<I', flag10))
    u.mem_write(info + 0x4CC, bytes([area]))
    u.reg_write(UC_ARM_REG_R0, desc)
    u.reg_write(UC_ARM_REG_R1, info)
    u.reg_write(UC_ARM_REG_R2, 0x1B)
    u.reg_write(UC_ARM_REG_SP, 0x5000F000)
    u.reg_write(UC_ARM_REG_CPSR, 0x33)
    stop_addr = site_addr + 4
    stop = u.hook_add(UC_HOOK_CODE, lambda mu, a, s, d: mu.emu_stop(), begin=stop_addr, end=stop_addr)
    u.emu_start(site_addr | 1, stop_addr, count=60000)
    u.hook_del(stop)
    raw_d = bytes(u.mem_read(desc, 0x68))
    return (
        struct.unpack_from('<8H', raw_d, 0x1A),     # stride, stride, W, H, W, H, X0, Y0
        struct.unpack_from('<2I', raw_d, 0x2C),     # plane_y, plane_cb
        struct.unpack_from('<4H', raw_d, 0x38),     # dx, dy, inW, inH
        struct.unpack_from('<2H', raw_d, 0x44),     # outW, outH
    )


def run_lvcrop(ctx, area, mode0=0, mode1=0, flag10=0):
    """Execute the `bl` at 0x101455C6 (0x10145354) and return (out_sz, out_rect)."""
    u = ctx.u
    _map_area(u, area)
    info = probe.RAM + 0x13000
    out_sz = probe.RAM + 0x14000
    out_rect = 0x5000EFC0                           # on stack, just like 0x10145590
    u.mem_write(info, bytes(0x600))
    u.mem_write(info + 0, bytes([mode0, mode1]))
    u.mem_write(info + 0x10, struct.pack('<I', flag10))
    u.mem_write(info + 0x4CC, bytes([area]))
    u.mem_write(out_sz, bytes(8))
    u.mem_write(out_rect, bytes(8))
    u.reg_write(UC_ARM_REG_R0, info)
    u.reg_write(UC_ARM_REG_R1, out_sz)
    u.reg_write(UC_ARM_REG_R2, out_rect)
    u.reg_write(UC_ARM_REG_SP, 0x5000EFC0)
    u.reg_write(UC_ARM_REG_CPSR, 0x33)
    stop_addr = 0x101455CA
    stop = u.hook_add(UC_HOOK_CODE, lambda mu, a, s, d: mu.emu_stop(), begin=stop_addr, end=stop_addr)
    u.emu_start(0x101455C6 | 1, stop_addr, count=40000)
    u.hook_del(stop)
    return struct.unpack('<2H', bytes(u.mem_read(out_sz, 4))), struct.unpack('<4H', bytes(u.mem_read(out_rect, 8)))


def run_elbr(ctx, area):
    """Run 0x10146B9C .. 0x10146BB2 (ELBR size lookup via 0x10146BA2 -> 0x1020949A) and return (w, h) from [sp+0x10]."""
    u = ctx.u
    _map_area(u, area)
    # 0x1020949A reads RAM pointer table at 0xC18C7554 + 0xFC; Ctx only loads 63 words (0xFC bytes), so copy 3 more words!
    u.mem_write(probe.PTR_RAM + 0xFC, ctx.image[probe.PTR_ROM - m.BASE + 0xFC : probe.PTR_ROM - m.BASE + 0x108])
    info = probe.RAM + 0x13000
    u.mem_write(info, bytes(0x600))
    u.mem_write(info + 0x4CC, bytes([area]))
    sp = 0x5000EF00
    u.mem_write(sp, bytes(0x40))
    u.reg_write(UC_ARM_REG_R6, info)
    u.reg_write(UC_ARM_REG_SP, sp)
    u.reg_write(UC_ARM_REG_CPSR, 0x33)
    stop_addr = 0x10146BB2
    stop = u.hook_add(UC_HOOK_CODE, lambda mu, a, s, d: mu.emu_stop(), begin=stop_addr, end=stop_addr)
    u.emu_start(0x10146B9C | 1, stop_addr, count=40000)
    u.hook_del(stop)
    return struct.unpack('<2H', bytes(u.mem_read(sp + 0x10, 4)))


def verify_b9(raw, image):
    c = b3.contexts(raw, image)
    want_elby = (
        (1344, 1344, 1340, 1006, 1340, 1006, 84, 0),
        (0xC4000200, (0xC4000200 + 1344 * 1006 + 15) & ~15),
        (3, 3, 1334, 1000),
        (1536, 1152),
    )
    elby_res = {}
    for site in (0x101464CA, 0x1014659A, 0x101465D2):
        for area in (0, 1, 4, 5, 8):
            ref = run_elby_site(c['stock'], site, area)
            for name in ('bare', 'not_chosen', 'not_chosen_233'):
                assert run_elby_site(c[name], site, area) == ref, (hex(site), name, area)
            for name in ('chosen_out', 'chosen_in'):
                got = run_elby_site(c[name], site, area)
                assert got == (want_elby if area == 0 else ref), (hex(site), name, area, got)
        # Non-still modes (info[0]=1 movie, info[1]=1, info[0x10]=1) must stay stock even when 4:3 chosen + FX
        for mode0, mode1, flag10 in ((1, 0, 0), (0, 1, 0), (0, 0, 1)):
            ref = run_elby_site(c['stock'], site, 0, mode0, mode1, flag10)
            for name in ('bare', 'chosen_out', 'chosen_in'):
                assert run_elby_site(c[name], site, 0, mode0, mode1, flag10) == ref, (hex(site), name, mode0, mode1, flag10)
        elby_res[hex(site)] = dict(
            fx_stock=run_elby_site(c['stock'], site, 0)[0][2:8] + run_elby_site(c['stock'], site, 0)[3],
            fx_chosen=want_elby[0][2:8] + want_elby[3],
        )

    lvcrop_res = {}
    want_crop = ((1340, 1006), (87, 3, 1421, 1003))
    for area in (0, 1, 4, 5, 8):
        ref = run_lvcrop(c['stock'], area)
        for name in ('bare', 'not_chosen', 'not_chosen_233'):
            assert run_lvcrop(c[name], area) == ref, ('lvcrop', name, area)
        for name in ('chosen_out', 'chosen_in'):
            got = run_lvcrop(c[name], area)
            assert got == (want_crop if area == 0 else ref), ('lvcrop', name, area, got)
        lvcrop_res['area %d' % area] = dict(stock=ref, chosen=run_lvcrop(c['chosen_out'], area))
    for mode0, mode1, flag10 in ((1, 0, 0), (0, 1, 0), (0, 0, 1)):
        ref = run_lvcrop(c['stock'], 0, mode0, mode1, flag10)
        assert run_lvcrop(c['chosen_out'], 0, mode0, mode1, flag10) == ref, ('lvcrop mode', mode0, mode1, flag10)

    elbr_res = {}
    for area in (0, 1, 4, 8):
        ref = run_elbr(c['stock'], area)
        for name in ('bare', 'not_chosen', 'not_chosen_233'):
            assert run_elbr(c[name], area) == ref, ('elbr', name, area)
        for name in ('chosen_out', 'chosen_in'):
            got = run_elbr(c[name], area)
            assert got == ((896, 672) if area == 0 else ref), ('elbr', name, area, got)
        elbr_res['area %d' % area] = dict(stock=ref, chosen=run_elbr(c['chosen_out'], area))

    return dict(lv_elby=elby_res, lv_lvcrop=lvcrop_res, lv_elbr=elbr_res)


def main():
    orig_m_build = m.build

    def innermost(source, a56, out, inc):
        prev_kind2 = m.apply_kind2                      # b8 + b7 + b4 wrappers

        def kind2(data):
            edits = prev_kind2(data)
            edits.extend(apply_b9_sites(data))
            return edits
        m.apply_kind2 = kind2
        prev = a85.verify_query

        def checks(raw, image):
            res = prev(raw, image)
            res.update(verify_b9(raw, image))
            return res
        a85.verify_query = checks
        orig_m_build(source, a56, out, inc)
    m.build = innermost
    b8.main()


if __name__ == '__main__':
    main()
