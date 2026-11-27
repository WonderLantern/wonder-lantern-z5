"""B3m = B2m with a handled path in the kind-28 stub (research/aspect/b3m_fx43.S) and the B3 module logic
(aspect_fw.inc af_lvsz + aspect_menu.inc a43_lv_pair): while 4:3 is chosen and prepared, the FX size query of the
live-view window updater (0x10197FFC) and display fit (0x1018B388) returns the module's own 4:3 L / M / S size, whether
or not the area-8 tables are switched in yet.

Why (B2 camera, DIAG18-20, 2026-11-26): B2 took area 8 only while the tables were switched in. The window updater runs
the moment the area becomes FX, ~50 ms before the module switches them in: the window stayed FX, the fit went 4:3 on a
later EVF switch, and the picture was squeezed horizontally (user).

    python -B research/aspect/z5_aspect43_b3m_fw.py <a3704c6f EG165050.BIN> --a56 <A56 7477fde9 EG165050.BIN> \
        --module-src <absolute release worktree>/src/card_module --b1-diag <B1 DIAG17.BIN> \
        --out output/<new dir>/EG165050.BIN

The firmware check compiles aspect_fw.inc on its own; a43_lv_pair is replaced by a stand-in that returns a fixture
pair (5352 x 4016, 4008 x 3008, 2664 x 2000) when the "chosen" word at RAM + 0x3BFF0 is set.
Checks: every A90 check, plus
  lv_window  0x10197FDC, areas 0 / 1 / 4 / 8: stock, no module and module not chosen (also with the 2.33 tables in)
             equal to the release firmware; chosen with the tables NOT switched in (the B2 race) and chosen with them
             in: FX -> (332, 0, 5684, 4016), other areas as the release firmware with the same tables state
  lv_fit     0x1018B4A2 on the B1 camera RAM: not chosen = release firmware; chosen (tables out or in): FX LCD
             (40, 0, 680, 480) / (342, 28, 1580, 956), FX EVF (72, 54, 1208, 906); 16:9 and 1:1 unchanged
  lv_size    the stub's answer for image sizes 0 / 1 / 2 (the M / S pairs) and 3 (stock)
  lv_other   other callers of 0x102092A6 with 4:3 chosen: the stock FX size
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
import z5_aspect43_b2m_fw as b2  # noqa: E402

a87, a85, m, probe, fit = a90.a87, a90.a85, a90.m, a90.probe, b2.fit
mm = a85.mm
CHOSEN = probe.RAM + 0x3BFF0
PAIRS = probe.RAM + 0x3BF00
OURS = (5352, 4016, 4008, 3008, 2664, 2000)


def assemble_stubs(address):
    with tempfile.TemporaryDirectory(dir=probe.ROOT / 'output') as tmp:
        def run(*args):
            return subprocess.check_output(args, cwd=tmp, text=True)
        run('arm-none-eabi-gcc', '-mcpu=cortex-a9', '-mthumb', '-c', str(HERE / 'b3m_fx43.S'), '-o', 'h.o')
        run('arm-none-eabi-ld', f'-Ttext={address:#x}', '-e', 'cfg', '-o', 'h.elf', 'h.o')
        run('arm-none-eabi-objcopy', '-O', 'binary', 'h.elf', 'h.bin')
        symbols = {}
        for line in run('arm-none-eabi-nm', 'h.elf').splitlines():
            value, _, name = line.split()
            symbols[name] = int(value, 16)
        return (Path(tmp) / 'h.bin').read_bytes(), symbols


STANDIN = ('#define AF_LV43_PAIR(size) (*(volatile u32 *)0x%Xu && (size) <= 2u ? *(volatile u32 *)0x%Xu + '
           '(size) * 4u : 0u)\\n' % (CHOSEN, CHOSEN))
assert '#define B2_LVSZ' in mm.HARNESS                 # b2's import added it
mm.HARNESS = mm.HARNESS.replace('#define B2_LVSZ\n', '#define B2_LVSZ\n' + STANDIN.replace('\\n', '\n'))
assert 'AF_LV43_PAIR' in mm.HARNESS
a85.assemble_stubs = assemble_stubs


def choose(ctx, on=True):
    ctx.u.mem_write(PAIRS, struct.pack('<6H', *OURS))
    ctx.u.mem_write(CHOSEN, struct.pack('<I', PAIRS if on else 0))


def contexts(raw, image):
    c = {}
    c['stock'] = probe.Ctx(raw)
    c['stock_on'] = probe.Ctx(raw); m.enable(c['stock_on'], b2.TABLES)
    c['stock233'] = probe.Ctx(raw); m.enable(c['stock233'], b2.TABLES); b2.set_233(c['stock233'])
    c['bare'] = a85.bare(image)
    c['not_chosen'] = probe.Ctx(image); choose(c['not_chosen'], False)
    c['not_chosen_233'] = probe.Ctx(image); m.enable(c['not_chosen_233'], b2.TABLES); b2.set_233(c['not_chosen_233'])
    choose(c['not_chosen_233'], False)
    c['chosen_out'] = probe.Ctx(image); choose(c['chosen_out'])                      # the B2 race
    c['chosen_in'] = probe.Ctx(image); m.enable(c['chosen_in'], b2.TABLES); choose(c['chosen_in'])
    return c


def verify_lv_window(raw, image):
    c = contexts(raw, image)
    res = {}
    for area in (0, 1, 4, 8):
        ref = b2.window(c['stock'], area)
        assert b2.window(c['bare'], area) == ref and b2.window(c['not_chosen'], area) == ref, ('off', area)
        assert b2.window(c['not_chosen_233'], area) == b2.window(c['stock233'], area), ('2.33', area)
        out, inn = b2.window(c['chosen_out'], area), b2.window(c['chosen_in'], area)
        if area == 0:
            assert out == inn == (6016, 4016, 332, 0, 5684, 4016), (out, inn)
        else:
            assert out == ref and inn == b2.window(c['stock_on'], area), ('other area', area, out, inn)
        res['area %d' % area] = dict(stock=ref[2:], chosen_tables_out=out[2:], chosen_tables_in=inn[2:])
    return res


def verify_lv_fit(raw, image):
    r = b2.B1['r']
    want = {'FX LCD': ((40, 0, 680, 480), (342, 28, 1580, 956)), 'FX EVF': ((72, 54, 1208, 906), (342, 28, 1580, 956))}
    res = {}
    for name, ms, area in (('FX LCD', 20000, 0), ('16:9 LCD', 30000, 4), ('1:1 LCD', 45000, 8), ('FX EVF', 95000, 0)):
        c = contexts(raw, image)
        ref = b2.fit_run(c['stock'], r, ms, area)
        assert b2.fit_run(c['bare'], r, ms, area) == ref and b2.fit_run(c['not_chosen'], r, ms, area) == ref, name
        out, inn = b2.fit_run(c['chosen_out'], r, ms, area), b2.fit_run(c['chosen_in'], r, ms, area)
        if name in want:
            assert out == inn == want[name], (name, out, inn)
        else:
            assert out == ref and inn == b2.fit_run(c['stock_on'], r, ms, area), (name, out, inn)
        res[name] = dict(stock=ref, chosen=out)
    return res


def verify_lv_size(raw, image):
    """The stub itself (site 0x1018B388 -> veneer -> hszf), r0 = FX, r1 = image size."""
    c = contexts(raw, image)
    res = {}
    for size in range(4):
        regs = [0, size] + [0x11110000 + i for i in range(2, 13)]
        got = []
        for name in ('stock', 'not_chosen', 'chosen_out'):
            ctx = c[name]
            r = m.run_with(ctx, 0x1018B388, regs, 0x1018B38D)       # runs the bl at the site, stops on return
            got.append(struct.unpack('<2H', bytes(ctx.u.mem_read(r[0], 4))))
            s_ = m.run_with(c['stock'], 0x1018B388, regs, 0x1018B38D)
            assert r[1] == s_[1] and r[3:14] == s_[3:14], (name, size)   # r2 is scratch (the stock body sets it)
        assert got[1] == got[0], size
        if size <= 2:
            assert got[2] == OURS[2 * size:2 * size + 2], (size, got)
        else:
            assert got[2] == got[0], (size, got)
        res['size %d' % size] = dict(stock=got[0], chosen=got[2])
    return res


def verify_lv_other(raw, image):
    c = contexts(raw, image)
    out = []
    for lr in (0x10197FE9, 0x1014322B, 0x10355DD1):
        regs = [0, 0] + [0x11110000 + i for i in range(2, 13)]
        for name in ('stock', 'chosen_out', 'chosen_in'):
            p = m.run_with(c[name], 0x102092A6, regs, lr)[0]
            want = (6016, 4016)
            assert struct.unpack('<2H', bytes(c[name].u.mem_read(p, 4))) == want, (hex(lr), name)
        out.append(hex(lr))
    return dict(callers=out, size='6016 x 4016')


def main():
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument('--a56', type=Path)
    ap.add_argument('--b1-diag', type=Path, required=True)
    known, rest = ap.parse_known_args()
    sys.argv = [sys.argv[0]] + rest + ['--a56', str(known.a56)]
    b2.TABLES.extend(m.a56_tables(known.a56.read_bytes()))
    b2.B1['r'] = fit.decode_b1.parse(known.b1_diag.read_bytes())

    orig_build = m.build

    def build(source, a56, out, inc):
        assert 'hfull' in m.SITES and not set(b2.SITES_B2) & set(m.SITES)
        m.SITES.update(b2.SITES_B2)                    # veneer slots 9 and 10
        prev = a85.verify_query

        def checks(raw, image):
            res = prev(raw, image)
            res['lv_window'] = verify_lv_window(raw, image)
            res['lv_fit'] = verify_lv_fit(raw, image)
            res['lv_size'] = verify_lv_size(raw, image)
            res['lv_other'] = verify_lv_other(raw, image)
            return res
        a85.verify_query = checks
        orig_build(source, a56, out, inc)
    m.build = build
    a90.main()


if __name__ == '__main__':
    main()
