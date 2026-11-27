"""B4m = B3m + a third live-view call through the same kind-28 stub: the live-view area crop 0x10190598, whose size
query `bl 0x102092A6` at 0x10190620 (lr 0x10190625) becomes `bl` to hszw's veneer 0x105BA3C4. No new veneer slot (the
stub tells the sites apart by lr in the module). Module: aspect_fw.inc AF_LV_CROP.

Why (B3 camera, DIAG01-02, 2026-11-26): window +0x5E and the fit rectangles were 4:3 and the live picture was still
squeezed, so neither is the crop source. A72's selector trace (DIAG05) shows 0x10190598 reading the image-area setting
0x169E and querying the FX size (lr 0x1019061B) and the area size (lr 0x10190625) on every live-view (re)start; it
centres the area in the FX frame and hands the rectangle to 0x10188C28 (LV state + 0x128). Unicorn: stock 16:9
(0, 316, 6016, 3700), 1:1 (1000, 0, 5016, 4016), DX (0, 0, 6016, 4016).

    python -B research/aspect/z5_aspect43_b4m_fw.py <a3704c6f EG165050.BIN> --a56 <A56 7477fde9 EG165050.BIN> \
        --module-src <absolute release worktree>/src/card_module --b1-diag <B1 DIAG17.BIN> \
        --out output/<new dir>/EG165050.BIN

Checks: every B3 check (lv_window, lv_fit, lv_size, lv_other and all A90 checks), plus
  lv_crop  0x10190598 with the area setting 0 / 1 / 4 / 8 (0x10188C06 and the sink 0x10188C28 stubbed): stock, no
           module and not chosen equal to the release firmware; 4:3 chosen (tables out or in): FX -> (332, 0, 5684,
           4016), other areas (DX also queries the FX size there) as the release firmware with the same tables state;
           the FX query (lr 0x1019061B) stays 6016 x 4016.
camera_verified: false.
"""
import argparse
from pathlib import Path
import struct
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import z5_aspect43_b3m_fw as b3  # noqa: E402

b2, a90, a85, m, probe, fit = b3.b2, b3.a90, b3.a85, b3.m, b3.probe, b3.fit
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1  # noqa: E402

CROP_SITE, CROP_OLD = 0x10190620, bytes.fromhex('78f041fe')       # bl 0x102092A6
CROP_FN = 0x10190598
HSZW_VENEER = m.STUBS + 9 * 8                                     # hszw is SITES entry 9
LV_STATE = probe.RAM + 0x10000


def apply_crop_site(data):
    off = CROP_SITE - m.BASE
    if bytes(data[off:off + 4]) != CROP_OLD:
        raise ValueError('original bytes differ at %#x' % CROP_SITE)
    new = m.hooks.branch(CROP_SITE, HSZW_VENEER, True)
    data[off:off + 4] = new
    return dict(name='hszl live-view crop size -> hszw veneer', address=hex(CROP_SITE), original=CROP_OLD.hex(),
                replacement=new.hex())


def crop(ctx, area):
    """Run 0x10190598 with the area setting stubbed; returns (rect, FX size seen by lr 0x1019061B)."""
    u = ctx.u
    for page in (0xC18A6000, 0xC2504000):
        try:
            u.mem_map(page, 0x1000)
        except Exception:
            pass
    u.mem_write(0xC18A60E0 + 0x80, struct.pack('<I', 1))
    u.mem_write(0xC25043A4, struct.pack('<I', area))              # the module's AF_AREA_NOW (setting 0x169E RAM)
    u.mem_write(LV_STATE, bytes(0x600))
    got = {}

    def hook(mu, address, size, data):
        lr = mu.reg_read(UC_ARM_REG_LR)
        if address == 0x10188C06:                                 # setting 0x169E -> area
            mu.reg_write(UC_ARM_REG_R0, area); mu.reg_write(UC_ARM_REG_PC, lr)
        elif address == 0x1019061A:                               # after the FX query (lr 0x1019061B)
            got['fx'] = struct.unpack('<2H', bytes(mu.mem_read(mu.reg_read(UC_ARM_REG_R0), 4)))
        elif address == 0x10188C28:                               # the sink: keep the rectangle
            got['rect'] = struct.unpack('<4h', bytes(mu.mem_read(mu.reg_read(UC_ARM_REG_R1), 8)))
            mu.reg_write(UC_ARM_REG_PC, lr)
    hooks = [u.hook_add(UC_HOOK_CODE, hook, begin=a, end=a) for a in (0x10188C06, 0x1019061A, 0x10188C28)]
    ctx.call(CROP_FN, (LV_STATE,))
    for h in hooks:
        u.hook_del(h)
    assert u.mem_read(LV_STATE + 0x4CC, 1)[0] == area
    assert got['fx'] == (6016, 4016), got                         # the function's FX query is never ours
    return got['rect']


def verify_lv_crop(raw, image):
    c = b3.contexts(raw, image)
    res = {}
    for area in (0, 1, 4, 8):
        ref = crop(c['stock'], area)
        assert crop(c['bare'], area) == ref and crop(c['not_chosen'], area) == ref, ('off', area)
        assert crop(c['not_chosen_233'], area) == crop(c['stock233'], area), ('2.33', area)
        out, inn = crop(c['chosen_out'], area), crop(c['chosen_in'], area)
        if area == 0:
            assert out == inn == (332, 0, 5684, 4016), (out, inn)
        else:
            assert out == ref and inn == crop(c['stock_on'], area), ('other area', area, out, inn)
        res['area %d' % area] = dict(stock=ref, chosen=out)
    assert res['area 8']['stock'] == (1000, 0, 5016, 4016) and res['area 4']['stock'] == (0, 316, 6016, 3700)
    return res


def main():
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument('--a56', type=Path)
    ap.add_argument('--b1-diag', type=Path, required=True)
    known, rest = ap.parse_known_args()
    sys.argv = [sys.argv[0]] + rest + ['--a56', str(known.a56)]
    b2.TABLES.extend(m.a56_tables(known.a56.read_bytes()))
    b2.B1['r'] = fit.decode_b1.parse(known.b1_diag.read_bytes())

    real_kind2 = m.apply_kind2

    def apply_kind2(data):
        edits = real_kind2(data)
        edits.append(apply_crop_site(data))
        return edits
    m.apply_kind2 = apply_kind2

    orig_build = m.build

    def build(source, a56, out, inc):
        assert 'hfull' in m.SITES and not set(b2.SITES_B2) & set(m.SITES)
        m.SITES.update(b2.SITES_B2)                    # veneer slots 9 (hszw, also the crop site) and 10 (hszf)
        assert list(m.SITES).index('hszw') == 9
        prev = a85.verify_query

        def checks(raw, image):
            res = prev(raw, image)
            res['lv_window'] = b3.verify_lv_window(raw, image)
            res['lv_fit'] = b3.verify_lv_fit(raw, image)
            res['lv_size'] = b3.verify_lv_size(raw, image)
            res['lv_other'] = b3.verify_lv_other(raw, image)
            res['lv_crop'] = verify_lv_crop(raw, image)
            return res
        a85.verify_query = checks
        orig_build(source, a56, out, inc)
    m.build = build
    a90.main()


if __name__ == '__main__':
    main()
