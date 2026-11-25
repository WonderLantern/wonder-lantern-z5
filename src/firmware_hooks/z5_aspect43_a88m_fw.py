"""A88 = the A87m firmware (unchanged) with the A88 module logic (aspect_fw.inc af_nef_main): the type-1 (NEF) decode
descriptor + 32 / 36, which the stock code fills from the area table 0x10209328 with the RAW plane 6040 x 4032, gets
our JPEG size + (24, 16) (5376 x 4032 for 4:3, 6040 x 2592 for 2.33) when the file is one of ours.

Why (A87 camera, DIAG15-17 + video, 2026-11-25): NEF zoom put the full-size JPEG into a 6040 x 4032 canvas (4:3:
garbage strip of about 688 / 6040 on the right; 2.33: image at the top, about 1456 / 4032 garbage at the bottom).
A86 only rewrote the descriptor when it read 6016 x 4016, which the stock table never gives (Unicorn: 0x10209328 and
0x10209294 return the plane for FX and for our area 8 alike; the zoom window 0x1034F56E is therefore the plane in
both areas). The kind-25 stub (0x1034F65A) was never called; 0x1034F6DE, its sibling, turned out to be a movie path
(3840 x 2160 / 1920 x 1080 / 1280 x 720 frame tags), so A88 adds no firmware stub.

    python -B research/aspect/z5_aspect43_a88m_fw.py <a3704c6f EG165050.BIN> --a56 <A56 7477fde9 EG165050.BIN> \
        --module-src <release worktree>/src/card_module --out output/<new dir>/EG165050.BIN

Checks: every A87 check with the A88 module logic (the rebuilt firmware must equal A87m 8c423e52), with `nef_main`
replaced by the A88 rule (descriptor 6040 x 4032 -> ours + (24, 16), 6016 x 4016 -> ours, others untouched; only
for the descriptor-prep callers). The zoom window 0x1034F56E is not checked here: the harness's enable_ratio installs
A81-style tables (custom plane 5376 x 4032 for area 8), not the stock-plane tables the A82+ module writes.
camera_verified: false.
"""
import hashlib
import itertools
from pathlib import Path
import struct
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import z5_aspect43_a87m_fw as a87  # noqa: E402

a86, a85, m, probe, BaseCtx = a87.a86, a87.a85, a87.m, a87.probe, a87.BaseCtx
A87_SHA = '8c423e522f3d3cadfc1423532ef4aa034eeade9ee5fc351e880503ac6b5e1060'
PLANE = {(5352, 4016): (5376, 4032), (6016, 2576): (6040, 2592)}


def verify_nef_main(raw, image):
    on = probe.Ctx(image)
    ctxobj, rec, out, desc, meta = (probe.RAM + 0x30000, probe.RAM + 0x31000, probe.RAM + 0x34000,
                                     probe.RAM + 0x35000, probe.RAM + 0x36000)
    stack = 0x5000F000
    n = 0
    for fn, lr in [(a85.STD, v) for v in a85.STD_CALLERS] + [(a85.EXT, v) for v in a85.EXT_CALLERS]:
        for ans, meta_main, desc_main, typ in itertools.product(((0, 1440, 1080), (0, 1920, 824), (0, 1620, 1080)),
                                                               ((6016, 4016), (5352, 4016)),
                                                               ((6040, 4032), (6016, 4016), (4040, 4032)), (1, 2)):
            u = on.u
            a87.set_off(on)
            u.mem_write(ctxobj, bytes(0x40))
            u.mem_write(ctxobj, struct.pack('<IB', 0x5A5A0001, typ))
            u.mem_write(ctxobj + 12, struct.pack('<I', rec)); u.mem_write(ctxobj + 28, struct.pack('<I', desc))
            u.mem_write(rec, bytes(0x400)); u.mem_write(rec + 0x398, struct.pack('<2H', 6016, 4016))
            u.mem_write(desc, bytes(0x80)); u.mem_write(desc + 32, struct.pack('<2I', *desc_main))
            u.mem_write(meta, bytes(0x20)); u.mem_write(meta + 12, struct.pack('<2H', *meta_main))
            u.mem_write(stack, bytes(0x20))
            u.mem_write(stack + (0 if lr in a86.PREP_CALLERS else 12), struct.pack('<I', meta))
            u.mem_write(out, bytes(8))
            a85.FAKE['answer'] = ans; a85.FAKE['calls'] = []
            m.run_with(on, fn, [out, 0, 0, 0x11110003, ctxobj] + [0x11110000 + i for i in range(5, 13)], lr)
            new = a86.NEF_L.get(ans[1:]) if typ == 1 else None
            got_rec = struct.unpack('<2H', bytes(u.mem_read(rec + 0x398, 4)))
            got_meta = struct.unpack('<2H', bytes(u.mem_read(meta + 12, 4)))
            got_desc = struct.unpack('<2I', bytes(u.mem_read(desc + 32, 8)))
            assert got_rec == (new or (6016, 4016)), ('rec', hex(lr), ans, typ, got_rec)
            assert got_meta == (new if new and meta_main == (6016, 4016) else meta_main), ('meta', hex(lr), got_meta)
            want = desc_main
            if new and lr in a86.DESC_CALLERS:
                if desc_main == (6040, 4032):
                    want = PLANE[new]
                elif desc_main == (6016, 4016):
                    want = new
            assert got_desc == want, ('desc', hex(lr), ans, desc_main, got_desc, want)
            n += 1
    return dict(cases=n, plane_rule={f'{k[0]}x{k[1]}': f'{v[0]}x{v[1]}' for k, v in PLANE.items()})


def main():
    a86.verify_nef_main = verify_nef_main
    real = a85.verify_query

    orig_build = m.build

    def build(source, a56, out, inc):
        orig_build(source, a56, out, inc)
    m.build = build
    assert real
    a87.main()
    out = Path(sys.argv[sys.argv.index('--out') + 1])
    h = hashlib.sha256(out.read_bytes()).hexdigest()
    assert h == A87_SHA, ('firmware must stay A87m', h)
    print('firmware identical to A87m', h)


if __name__ == '__main__':
    main()
