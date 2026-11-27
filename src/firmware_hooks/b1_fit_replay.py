"""Replay the stock display-fit caller 0x1018B4A2 on the RAM state the B1 readout captured on the camera.

    python -B research/aspect/b1_fit_replay.py --image <C 1.50 MAIN .bi> --diag <B1 DIAG.BIN>

The watched ranges (view block 0xC2380280.., parameters 0xC2380BA0..) are rebuilt at a chosen ms from the record's
baseline and log; other RAM is zero apart from the geometry pointer table (z5_geometry_probe.Ctx). Stock cases must
give the rectangles the camera itself held at that moment; then the area size query at 0x1018B388 (the only size
the fit reads) is replaced by 4:3 / 2.33:1 sizes on the FX state, LCD and EVF. Nothing is written to any image.
camera_verified: false (the predictions).
"""
import argparse
import hashlib
from pathlib import Path
import struct
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import decode_b1  # noqa: E402
from z5_geometry_probe import Ctx, RAM  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0  # noqa: E402

MAIN_SHA256 = '1d9742a951ea8a8ebe695fa9bb1f29a3be73e16dfe4a8372d997e903bbc58e98'
FIT_CALLER = 0x1018B4A2
SIZE_SELECTOR = 0x102092A6
SIZE_RETURN = 0x1018B38D                 # bl at 0x1018B388 (Thumb return address)
OUTS = {'LCD/EVF rect C23802F4': 0xC23802F4, '1920x1080 rect C2380314': 0xC2380314,
        'rect C2380678': 0xC2380678, 'rect C2380684': 0xC2380684}
ARG = RAM + 0x3000
FIXTURE = RAM + 0x3100


def state_at(r, ms):
    cur = list(r['base'])
    for t, i, v in r['events']:
        if t <= ms:
            cur[i] = v
    return cur


def load(ctx, r, cur):
    for page in (0xC2380000, 0xC2504000, 0xC1D61000):
        try:
            ctx.u.mem_map(page, 0x1000)
        except Exception:
            pass
    base = 0
    for a, n in r['ranges']:
        ctx.u.mem_write(a, struct.pack('<%dH' % (n // 2), *cur[base:base + n // 2]))
        base += n // 2


def run(image, r, ms, area, size=None):
    ctx = Ctx(image)
    load(ctx, r, state_at(r, ms))
    for a in OUTS.values():
        ctx.u.mem_write(a, b'\xEE' * 8)
    calls = []

    def on_size(mu, address, sz, data):
        lr = mu.reg_read(UC_ARM_REG_LR)
        calls.append(lr)
        if lr == SIZE_RETURN and size is not None:
            mu.reg_write(UC_ARM_REG_R0, FIXTURE)
            mu.reg_write(UC_ARM_REG_PC, lr)
    ctx.u.mem_write(FIXTURE, struct.pack('<2H', *(size or (0, 0))))
    h = ctx.u.hook_add(UC_HOOK_CODE, on_size, begin=SIZE_SELECTOR, end=SIZE_SELECTOR)
    ctx.u.mem_write(ARG, bytes((0, 0, area, 0)) + bytes(28))
    ctx.call(FIT_CALLER, (ARG, 0x8))
    ctx.u.hook_del(h)
    out = {k: struct.unpack('<4h', ctx.u.mem_read(a, 8)) for k, a in OUTS.items()}
    return out, calls


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--image', type=Path, required=True)
    ap.add_argument('--diag', type=Path, required=True)
    a = ap.parse_args()
    image = a.image.read_bytes()
    if hashlib.sha256(image).hexdigest() != MAIN_SHA256:
        ap.error('expected the unmodified Z5 C 1.50 MAIN')
    r = decode_b1.parse(a.diag.read_bytes())
    camera = lambda ms: {k: tuple(v - 0x10000 if v >= 0x8000 else v for v in
                                  state_at(r, ms)[decode_b1.index_of(r['ranges'], addr):
                                                  decode_b1.index_of(r['ranges'], addr) + 4])
                         for k, addr in OUTS.items()}
    # B1 camera run (DIAG17, 2026-11-26): FX (LCD) until 23720, 16:9 from 23720, 1:1 from 37783, FX from 53259,
    # EVF from 93533 to 97321
    stock = (('FX LCD', 20000, 0), ('16:9 LCD', 30000, 4), ('1:1 LCD', 45000, 8), ('FX EVF', 95000, 0))
    ok = True
    for name, ms, area in stock:
        out, calls = run(image, r, ms, area)
        cam = camera(ms)
        same = all(out[k] == cam[k] for k in ('LCD/EVF rect C23802F4', '1920x1080 rect C2380314'))
        ok &= same
        print('%-9s replay %s %s | camera %s %s  %s' % (name, out['LCD/EVF rect C23802F4'],
              out['1920x1080 rect C2380314'], cam['LCD/EVF rect C23802F4'], cam['1920x1080 rect C2380314'],
              'same' if same else 'DIFFERENT'))
    print('stock replay %s' % ('matches the camera' if ok else 'does NOT match: predictions below are not usable'))
    for name, ms, size in (('4:3 LCD', 70000, (5352, 4016)), ('2.33 LCD', 85000, (6016, 2576)),
                           ('4:3 EVF', 95000, (5352, 4016)), ('2.33 EVF', 95000, (6016, 2576))):
        out, calls = run(image, r, ms, 0, size)
        assert SIZE_RETURN in calls, [hex(c) for c in calls]
        print('%-9s predicted %s %s' % (name, out['LCD/EVF rect C23802F4'], out['1920x1080 rect C2380314']))


if __name__ == '__main__':
    main()
