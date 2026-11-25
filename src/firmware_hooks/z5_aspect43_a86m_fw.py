"""A86 = the A85m firmware (unchanged, research/aspect/a85m_fx43.S) with the A86 module logic: NEF-only files (type 1)
recorded with the FX main size 6016 x 4016 get our L size (5352 x 4016 / 6016 x 2576) in the file record + 0x398,
the file entry + 12 / 14 and the decode descriptor + 32 / 36 when their large preview (file service kind 5) is
1440 x 1080 / 1920 x 824 (aspect_fw.inc af_nef_main). Rebuilds the A85m image (must be byte-identical) and runs every
A84 / A84m / A85m check with the A86 module logic, the A85 query matrix with the A86 rule, and `nef_main`.

    python -B research/aspect/z5_aspect43_a86m_fw.py <a3704c6f EG165050.BIN> --a56 <A56 7477fde9 EG165050.BIN> \
        --module-src <release worktree>/src/card_module --out output/<new dir>/EG165050.BIN

camera_verified: false.
"""
import itertools
from pathlib import Path
import struct
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import z5_aspect43_a85m_fw as a85  # noqa: E402

m, probe, BaseCtx = a85.m, a85.probe, a85.BaseCtx
A85_SHA = 'd658d722e4d8c4e99d9e955e395f3cd52f629c77d69bc56686cb56c7d5799189'
NEF_L = {(1440, 1080): (5352, 4016), (1920, 824): (6016, 2576)}
DESC_CALLERS = (0x10354FAD, 0x10354FE5, 0x10354F03, 0x10354F43)
PREP_CALLERS = (0x1034E9CB, 0x1034E94D)


def nef_new_main(typ, main, ans):
    if typ != 1 or main != (6016, 4016) or not ans or ans[0]:
        return None
    return NEF_L.get(ans[1:])


_shape = a85.shape_size


def verify_query(raw, image):
    """The A85 matrix with the A86 rule: a type-1 FX-sized record whose large preview is ours takes the shape path
    of the rewritten size after one kind-5 query."""
    real_usable = a85.usable
    stock, on, off = a85.with_svc(BaseCtx(raw)), probe.Ctx(image), a85.bare(image)
    ctxobj, rec, out = probe.RAM + 0x30000, probe.RAM + 0x31000, probe.RAM + 0x34000
    handle = 0x5A5A0001
    mains = ((6016, 4016), (6040, 4032), (5352, 4016), (6016, 2576), (0, 0))
    answers = (None, (0, 640, 480), (0, 1440, 1080), (0, 640, 272), (0, 1920, 824), (0, 640, 424), (0, 1620, 1080),
               (0, 0, 0), (0, 2000, 1100), (5, 1440, 1080))
    n = dict(shape=0, query=0, stock=0, other_caller=0, nef_main=0)
    for fn, callers, ncls in ((a85.STD, a85.STD_CALLERS, 10), (a85.EXT, a85.EXT_CALLERS, 6)):
        for lr, typ, small, cls, main, ans in itertools.product(callers + (0x1034E9CF, 0x10354F01), (0, 1, 2, 3),
                                                                 (0, 1), range(ncls), mains, answers):
            if cls % 3 and ans not in (None, (0, 1440, 1080)):
                continue
            res = []
            for ctx in (stock, off, on):
                ctx.u.mem_write(ctxobj, bytes(0x40))
                ctx.u.mem_write(ctxobj, struct.pack('<IB', handle, typ))
                ctx.u.mem_write(ctxobj + 12, struct.pack('<I', rec))
                ctx.u.mem_write(rec, bytes(0x400)); ctx.u.mem_write(rec + 0x398, struct.pack('<2H', *main))
                ctx.u.mem_write(out, bytes(8))
                a85.FAKE['answer'] = ans; a85.FAKE['calls'] = []
                regs = [out, cls, small, 0x11110003, ctxobj] + [0x11110000 + i for i in range(5, 13)]
                r = m.run_with(ctx, fn, regs, lr)
                res.append((struct.unpack('<2H', bytes(ctx.u.mem_read(out, 4))), r[4:12] + r[13:],
                            list(a85.FAKE['calls']), struct.unpack('<2H', bytes(ctx.u.mem_read(rec + 0x398, 4)))))
            assert res[1][:3] == res[0][:3] and not res[0][2] and res[1][3] == main, ('no module', hex(fn), hex(lr))
            assert res[2][1] == res[0][1], ('registers', hex(fn), hex(lr), typ, small, cls, main, ans)
            if lr not in callers:
                assert res[2][0] == res[0][0] and not res[2][2] and res[2][3] == main, ('other caller', hex(lr))
                n['other_caller'] += 1
                continue
            new = nef_new_main(typ, main, ans)
            if new:
                assert res[2][3] == new and res[2][2] == [(5, 1, handle)], ('nef main', main, ans, res[2])
                assert res[2][0] == _shape(*new, small), ('nef main shape', res[2][0])
                n['nef_main'] += 1
                continue
            assert res[2][3] == main, ('main kept', hex(fn), typ, main, ans)
            want = _shape(*main, small)
            if want:
                assert res[2][0] == want and not res[2][2], ('shape', main, small, res[2])
                n['shape'] += 1
                continue
            calls = res[2][2]
            if typ == 1 and main == (6016, 4016):            # A86's kind-5 probe came first
                assert calls[0] == (5, 1, handle), calls
                calls = calls[1:]
            if typ in (1, 2):
                assert calls == [(4 if small else 5, typ, handle)], ('svc args', calls)
            else:
                assert not calls, ('svc for type', typ)
            got = real_usable(ans, small) if typ in (1, 2) else None
            if got:
                assert res[2][0] == got, ('query', hex(fn), typ, small, cls, main, ans, res[2][0])
                n['query'] += 1
            else:
                assert res[2][0] == res[0][0], ('stock', hex(fn), typ, small, cls, main, ans)
                n['stock'] += 1
    return n


def verify_nef_main(raw, image):
    """Record, file entry (caller stack) and decode descriptor ([ctx + 28] + 32 / 36) for the NEF rewrite."""
    on = probe.Ctx(image)
    ctxobj, rec, out, desc, meta = (probe.RAM + 0x30000, probe.RAM + 0x31000, probe.RAM + 0x34000,
                                     probe.RAM + 0x35000, probe.RAM + 0x36000)
    stack = 0x5000F000
    n = 0
    for fn, lr in [(a85.STD, v) for v in a85.STD_CALLERS] + [(a85.EXT, v) for v in a85.EXT_CALLERS]:
        for ans, meta_main, desc_main, typ in itertools.product(((0, 1440, 1080), (0, 1920, 824), (0, 1620, 1080)),
                                                               ((6016, 4016), (5352, 4016)),
                                                               ((6016, 4016), (6040, 4032)), (1, 2)):
            u = on.u
            u.mem_write(ctxobj, bytes(0x40))
            u.mem_write(ctxobj, struct.pack('<IB', 0x5A5A0001, typ))
            u.mem_write(ctxobj + 12, struct.pack('<I', rec)); u.mem_write(ctxobj + 28, struct.pack('<I', desc))
            u.mem_write(rec, bytes(0x400)); u.mem_write(rec + 0x398, struct.pack('<2H', 6016, 4016))
            u.mem_write(desc, bytes(0x80)); u.mem_write(desc + 32, struct.pack('<2I', *desc_main))
            u.mem_write(meta, bytes(0x20)); u.mem_write(meta + 12, struct.pack('<2H', *meta_main))
            u.mem_write(stack, bytes(0x20))
            u.mem_write(stack + (0 if lr in PREP_CALLERS else 12), struct.pack('<I', meta))
            u.mem_write(out, bytes(8))
            a85.FAKE['answer'] = ans; a85.FAKE['calls'] = []
            m.run_with(on, fn, [out, 0, 0, 0x11110003, ctxobj] + [0x11110000 + i for i in range(5, 13)], lr)
            new = NEF_L.get(ans[1:]) if typ == 1 else None
            got_rec = struct.unpack('<2H', bytes(u.mem_read(rec + 0x398, 4)))
            got_meta = struct.unpack('<2H', bytes(u.mem_read(meta + 12, 4)))
            got_desc = struct.unpack('<2I', bytes(u.mem_read(desc + 32, 8)))
            assert got_rec == (new or (6016, 4016)), ('rec', hex(lr), ans, typ, got_rec)
            assert got_meta == (new if new and meta_main == (6016, 4016) else meta_main), ('meta', hex(lr), got_meta)
            want_desc = new if new and lr in DESC_CALLERS and desc_main == (6016, 4016) else desc_main
            assert got_desc == want_desc, ('desc', hex(lr), ans, desc_main, got_desc)
            n += 1
    return dict(cases=n, desc_callers=[hex(v) for v in DESC_CALLERS], prep_callers=[hex(v) for v in PREP_CALLERS])


def main():
    real_verify_query = a85.verify_query

    def both(raw, image):
        assert real_verify_query                       # A85's own expectations do not hold for type-1 FX records
        res = verify_query(raw, image)
        res['nef_main_check'] = verify_nef_main(raw, image)
        return res
    a85.verify_query = both
    orig_build = m.build

    def build(source, a56, out, inc):
        orig_build(source, a56, out, inc)
        import hashlib
        h = hashlib.sha256(Path(out).read_bytes()).hexdigest()
        assert h == A85_SHA, ('firmware must stay A85m', h)
        print('firmware identical to A85m', h)
    m.build = build
    a85.main()


if __name__ == '__main__':
    main()
