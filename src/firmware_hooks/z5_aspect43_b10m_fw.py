"""B10m = B9m + the two kind=0x2b ELBY_fill (0x101460F2) call sites that live view uses on real hardware, routed through
hszw's veneer (0x105BA3C4, zero new trampolines), with the B10 stub (research/aspect/b10m_fx43.S). Module:
aspect_fw.inc AF_LV_ELBY4/5 (preserving stock plane_cb = plane_y + 0x8000 for kind 0x2b) and aspect_mask.h (disabling
the 4:3 software overlay mask under -DB2_LVSZ while keeping 2.33:1 mask intact).

Why (B9 camera DIAG16-18 + B5 RAMD2/RAMD3, 2026-11-27):
  1. In B9 on camera, B9CT showed ELBR=921 (896x672 worked) and CROP=30, but ELBY=0 because live view does not use the
     kind=0x1b pool callers (0x101464CA/59A/5D2). Active ELBY descriptors at 0xC2352C9C have plane_cb = plane_y + 0x8000,
     which 0x101460F2 only produces when r2 == 0x2b (call sites 0x10146E48 in 0x10146E26 and 0x10146E5A in 0x10146E50).
  2. When r2 == 0x2b, desc + 0x30 must stay at stock (plane_y + 0x800F) & ~0xF (do not overwrite with stride * H).

    python -B research/aspect/z5_aspect43_b10m_fw.py <a3704c6f EG165050.BIN> --a56 <A56 7477fde9 EG165050.BIN> \
        --module-src <absolute release worktree>/src/card_module --b1-diag <B1 DIAG17.BIN> \
        --out output/<new dir>/EG165050.BIN

Checks: every B9 check, plus
  lv_elby_2b  both kind=0x2b ELBY_fill call sites (0x10146E48 / 0x10146E5A) across areas 0, 1, 4, 5, 8 and non-still
              modes (info[0]=1, info[1]=1, info[0x10]=1): stock, bare, not_chosen, not_chosen_233 = stock; chosen
              (tables out/in) + FX still -> 1340x1006, X0=84, in=1334x1000, out=1536x1152, stride=1344, AND plane_cb
              stays exact plane_y + 0x8000; all other areas / modes unchanged.
camera_verified: false.
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import z5_aspect43_b9m_fw as b9  # noqa: E402

b8, b7, b4, b3, a85, m, probe = b9.b8, b9.b7, b9.b4, b9.b3, b9.a85, b9.m, b9.probe
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.arm_const import (  # noqa: E402
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_SP, UC_ARM_REG_CPSR,
)

SITES = (
    ('helby4 live-view ELBY_fill 0x2b alloc -> hszw veneer',  0x10146E48, bytes.fromhex('fff753f9')),
    ('helby5 live-view ELBY_fill 0x2b refill -> hszw veneer', 0x10146E5A, bytes.fromhex('fff74af9')),
)


def assemble_stubs(address):
    with tempfile.TemporaryDirectory(dir=probe.ROOT / 'output') as tmp:
        def run(*args):
            return subprocess.check_output(args, cwd=tmp, text=True)
        run('arm-none-eabi-gcc', '-mcpu=cortex-a9', '-mthumb', '-c', str(HERE / 'b10m_fx43.S'), '-o', 'h.o')
        run('arm-none-eabi-ld', f'-Ttext={address:#x}', '-e', 'cfg', '-o', 'h.elf', 'h.o')
        run('arm-none-eabi-objcopy', '-O', 'binary', 'h.elf', 'h.bin')
        symbols = {}
        for line in run('arm-none-eabi-nm', 'h.elf').splitlines():
            value, _, name = line.split()
            symbols[name] = int(value, 16)
        return (Path(tmp) / 'h.bin').read_bytes(), symbols


a85.assemble_stubs = assemble_stubs


def apply_b10_sites(data):
    edits = []
    for name, addr, old in SITES:
        off = addr - m.BASE
        if bytes(data[off:off + 4]) != old:
            raise ValueError('original bytes differ at %#x: got %s want %s' % (addr, bytes(data[off:off + 4]).hex(), old.hex()))
        new = m.hooks.branch(addr, b4.HSZW_VENEER, True)
        data[off:off + 4] = new
        edits.append(dict(name=name, address=hex(addr), original=old.hex(), replacement=new.hex()))
    return edits


def run_elby_2b_site(ctx, site_addr, area, mode0=0, mode1=0, flag10=0):
    """Execute the `bl` at site_addr (0x10146E48 or 0x10146E5A) with r2 = 0x2B and inspect the 0x68-byte ELBY desc."""
    u = ctx.u
    b9._map_area(u, area)
    desc = probe.RAM + 0x12000
    info = probe.RAM + 0x13000
    buf_pair = probe.RAM + 0x12800
    u.mem_write(desc, bytes(0x80))
    u.mem_write(buf_pair, struct.pack('<2I', 0x80000000, 0x00400000))
    u.mem_write(desc + 4, struct.pack('<I', buf_pair))
    u.mem_write(info, bytes(0x600))
    u.mem_write(info + 0, bytes([mode0, mode1]))
    u.mem_write(info + 0x10, struct.pack('<I', flag10))
    u.mem_write(info + 0x4CC, bytes([area]))
    u.reg_write(UC_ARM_REG_R0, desc)
    u.reg_write(UC_ARM_REG_R1, info)
    u.reg_write(UC_ARM_REG_R2, 0x2B)
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


def verify_b10(raw, image):
    c = b3.contexts(raw, image)
    want_elby_2b = (
        (1344, 1344, 1340, 1006, 1340, 1006, 84, 0),
        (0x80000000, 0x80008000),                   # r2 == 0x2b keeps stock +0x8000 plane_cb!
        (3, 3, 1334, 1000),
        (1536, 1152),
    )
    elby2b_res = {}
    for site in (0x10146E48, 0x10146E5A):
        for area in (0, 1, 4, 5, 8):
            ref = run_elby_2b_site(c['stock'], site, area)
            for name in ('bare', 'not_chosen', 'not_chosen_233'):
                assert run_elby_2b_site(c[name], site, area) == ref, (hex(site), name, area)
            for name in ('chosen_out', 'chosen_in'):
                got = run_elby_2b_site(c[name], site, area)
                assert got == (want_elby_2b if area == 0 else ref), (hex(site), name, area, got)
        for mode0, mode1, flag10 in ((1, 0, 0), (0, 1, 0), (0, 0, 1)):
            ref = run_elby_2b_site(c['stock'], site, 0, mode0, mode1, flag10)
            for name in ('bare', 'chosen_out', 'chosen_in'):
                assert run_elby_2b_site(c[name], site, 0, mode0, mode1, flag10) == ref, (hex(site), name, mode0, mode1, flag10)
        elby2b_res[hex(site)] = dict(
            fx_stock=run_elby_2b_site(c['stock'], site, 0)[0][2:8] + run_elby_2b_site(c['stock'], site, 0)[3],
            fx_chosen=want_elby_2b[0][2:8] + want_elby_2b[3],
            plane_cb=hex(want_elby_2b[1][1]),
        )
    return dict(lv_elby_2b=elby2b_res)


def main():
    orig_m_build = m.build

    def innermost(source, a56, out, inc):
        prev_kind2 = m.apply_kind2                      # b9 + b8 + b7 + b4 wrappers

        def kind2(data):
            edits = prev_kind2(data)
            edits.extend(apply_b10_sites(data))
            return edits
        m.apply_kind2 = kind2
        prev = a85.verify_query

        def checks(raw, image):
            res = prev(raw, image)
            res.update(verify_b10(raw, image))
            return res
        a85.verify_query = checks
        orig_m_build(source, a56, out, inc)
    m.build = innermost
    b9.main()


if __name__ == '__main__':
    main()
