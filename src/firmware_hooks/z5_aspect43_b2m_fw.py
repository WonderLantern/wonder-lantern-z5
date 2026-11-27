"""B2m = A90m + kind 28 at the two live-view calls of the area size selector 0x102092A6 (research/aspect/b2m_fx43.S)
with the B2 module logic (aspect_fw.inc af_lvsz): while 4:3 is on, the window updater (0x10197FFC) and the display fit
(0x1018B388) query area 8 (our 5352 x 4016) instead of FX, so the stock live view crops and pillarboxes by itself.

Why (B1 camera, DIAG17, 2026-11-26): the stock live view letterboxes by the window's aspect (16:9, 1:1); the stock
fit replayed on the camera's RAM (b1_fit_replay.py) gives (40, 0, 680, 480) on the LCD for 4:3.

    python -B research/aspect/z5_aspect43_b2m_fw.py <a3704c6f EG165050.BIN> --a56 <A56 7477fde9 EG165050.BIN> \
        --module-src <absolute release worktree>/src/card_module --b1-diag <B1 DIAG17.BIN> \
        --out output/<new dir>/EG165050.BIN

Checks: every A90 check (stubs, module logic) on the B2 image, plus
  lv_window  0x10197FDC for areas 0 / 1 / 4 / 8: without the module, with the module off and with a 2.33 area 8
             (6016 x 2576) equal to the release firmware; with 4:3 on, FX -> (332, 0, 5684, 4016), the other areas as
             the release firmware with the same tables
  lv_fit     0x1018B4A2 on the B1 camera RAM (FX LCD, 16:9, 1:1, FX EVF states): without the module / module off
             equal to the release firmware and the camera; 4:3 on: FX LCD (40, 0, 680, 480) / (342, 28, 1580, 956),
             FX EVF (72, 54, 1208, 906), 16:9 and 1:1 unchanged
  lv_other   another caller of 0x102092A6 (lr not one of the two sites) with 4:3 on: the stock FX size
camera_verified: false.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import z5_aspect43_a90m_fw as a90  # noqa: E402
import b1_fit_replay as fit  # noqa: E402

a87, a85, m, probe = a90.a87, a90.a85, a90.m, a90.probe
mm = a85.mm
SITES_B2 = {'hszw': (0x10197FFC, '71f0 53f9', True), 'hszf': (0x1018B388, '7df0 8dff', True)}
VIEW = 0xC23802C8
WINDOW = 0x10197FDC


def assemble_stubs(address):
    with tempfile.TemporaryDirectory(dir=probe.ROOT / 'output') as tmp:
        def run(*args):
            return subprocess.check_output(args, cwd=tmp, text=True)
        run('arm-none-eabi-gcc', '-mcpu=cortex-a9', '-mthumb', '-c', str(HERE / 'b2m_fx43.S'), '-o', 'h.o')
        run('arm-none-eabi-ld', f'-Ttext={address:#x}', '-e', 'cfg', '-o', 'h.elf', 'h.o')
        run('arm-none-eabi-objcopy', '-O', 'binary', 'h.elf', 'h.bin')
        symbols = {}
        for line in run('arm-none-eabi-nm', 'h.elf').splitlines():
            value, _, name = line.split()
            symbols[name] = int(value, 16)
        return (Path(tmp) / 'h.bin').read_bytes(), symbols


mm.HARNESS = mm.HARNESS.replace('kind <= AF_K_FULL', 'kind <= AF_K_LVSZ').replace(
    '#include "aspect_fw.inc"', '#define B2_LVSZ\n#include "aspect_fw.inc"')
assert 'AF_K_LVSZ' in mm.HARNESS and '#define B2_LVSZ' in mm.HARNESS
a85.assemble_stubs = assemble_stubs
TABLES = []
B1 = {}


def map_lv(ctx):
    for page in (VIEW & ~0xFFF, 0xC2504000, 0xC1D61000):
        try:
            ctx.u.mem_map(page, 0x1000)
        except Exception:
            pass


def window(ctx, area):
    map_lv(ctx)
    ctx.u.mem_write(VIEW + 0x5A, bytes(12))
    arg = probe.RAM + 0x3000
    ctx.u.mem_write(arg, bytes((0, 0, area, 0)))
    ctx.call(WINDOW, (arg,))
    return struct.unpack('<6H', bytes(ctx.u.mem_read(VIEW + 0x5A, 12)))


def set_233(ctx):
    """Area 8's size 0 entry -> 6016 x 2576 (as the module's 2.33 copies)."""
    p = ctx.call(0x102092A6, (8, 0))
    ctx.u.mem_write(p, struct.pack('<2H', 6016, 2576))
    assert struct.unpack('<2H', bytes(ctx.u.mem_read(ctx.call(0x102092A6, (8, 0)), 4))) == (6016, 2576)


def contexts(raw, image):
    stock = probe.Ctx(raw)                 # release firmware (ctx_factory: no module registered for it)
    stock_on = probe.Ctx(raw); m.enable(stock_on, TABLES)
    bare = a85.bare(image)                 # B2 image, no module
    mod_off = probe.Ctx(image)             # B2 image, module, aspect off
    on = probe.Ctx(image); m.enable(on, TABLES)
    on233 = probe.Ctx(image); m.enable(on233, TABLES); set_233(on233)
    stock233 = probe.Ctx(raw); m.enable(stock233, TABLES); set_233(stock233)
    return stock, stock_on, bare, mod_off, on, on233, stock233


def verify_lv_window(raw, image):
    stock, stock_on, bare, mod_off, on, on233, stock233 = contexts(raw, image)
    res = {}
    for area in (0, 1, 4, 8):
        ref = window(stock, area)
        assert window(bare, area) == ref and window(mod_off, area) == ref, ('off', area)
        assert window(on233, area) == window(stock233, area), ('2.33', area)
        got = window(on, area)
        if area == 0:
            assert got == (6016, 4016, 332, 0, 5684, 4016), got
            assert window(stock_on, 0) == ref, 'release firmware with the tables keeps FX'
        else:
            assert got == window(stock_on, area), ('other area', area, got)
        res['area %d' % area] = dict(stock=ref[2:], on=got[2:])
    return res


def fit_run(ctx, r, ms, area):
    fit.load(ctx, r, fit.state_at(r, ms))
    for a in fit.OUTS.values():
        ctx.u.mem_write(a, b'\xEE' * 8)
    ctx.u.mem_write(fit.ARG, bytes((0, 0, area, 0)) + bytes(28))
    ctx.call(fit.FIT_CALLER, (fit.ARG, 0x8))
    return tuple(struct.unpack('<4h', bytes(ctx.u.mem_read(fit.OUTS[k], 8)))
                 for k in ('LCD/EVF rect C23802F4', '1920x1080 rect C2380314'))


def verify_lv_fit(raw, image):
    r = B1['r']
    want_on = {'FX LCD': ((40, 0, 680, 480), (342, 28, 1580, 956)), 'FX EVF': ((72, 54, 1208, 906), (342, 28, 1580, 956))}
    res = {}
    for name, ms, area in (('FX LCD', 20000, 0), ('16:9 LCD', 30000, 4), ('1:1 LCD', 45000, 8), ('FX EVF', 95000, 0)):
        stock, stock_on, bare, mod_off, on, on233, stock233 = contexts(raw, image)
        ref = fit_run(stock, r, ms, area)
        assert fit_run(bare, r, ms, area) == ref and fit_run(mod_off, r, ms, area) == ref, ('off', name)
        assert fit_run(on233, r, ms, area) == fit_run(stock233, r, ms, area), ('2.33', name)
        got = fit_run(on, r, ms, area)
        if name in want_on:
            assert got == want_on[name], (name, got)
        else:
            assert got == fit_run(stock_on, r, ms, area), (name, got)
        res[name] = dict(stock=ref, on=got)
    return res


def verify_lv_other(raw, image):
    stock, stock_on, bare, mod_off, on, on233, stock233 = contexts(raw, image)
    out = []
    for lr in (0x10197FE9, 0x1014322B, 0x10355DD1):     # window updater's full-frame query, capture, playback
        regs = [0, 0] + [0x11110000 + i for i in range(2, 13)]
        for ctx in (stock, on):
            p = m.run_with(ctx, 0x102092A6, regs, lr)[0]
            assert struct.unpack('<2H', bytes(ctx.u.mem_read(p, 4))) == (6016, 4016), hex(lr)
        out.append(hex(lr))
    return dict(callers=out, size='6016 x 4016')


def main():
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument('--a56', type=Path)
    ap.add_argument('--b1-diag', type=Path, required=True)
    known, rest = ap.parse_known_args()
    sys.argv = [sys.argv[0]] + rest + ['--a56', str(known.a56)]
    TABLES.extend(m.a56_tables(known.a56.read_bytes()))
    B1['r'] = fit.decode_b1.parse(known.b1_diag.read_bytes())

    orig_build = m.build

    def build(source, a56, out, inc):
        assert 'hfull' in m.SITES and not set(SITES_B2) & set(m.SITES)   # A90m's wrapper ran: hfull is slot 8
        m.SITES.update(SITES_B2)                                       # veneer slots 9 and 10 (the last two)
        prev = a85.verify_query

        def checks(raw, image):
            res = prev(raw, image)
            res['lv_window'] = verify_lv_window(raw, image)
            res['lv_fit'] = verify_lv_fit(raw, image)
            res['lv_other'] = verify_lv_other(raw, image)
            return res
        a85.verify_query = checks
        orig_build(source, a56, out, inc)
    m.build = build
    a90.main()


if __name__ == '__main__':
    main()
