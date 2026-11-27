"""B7m = B4m with the B7 stub (research/aspect/b7m_fx43.S) and one more site through hszw's veneer: the aspect-class query
of the live-view area crop, `bl 0x10190576` at 0x101905BC (lr 0x101905C1; area -> class, stored at capture info
+ 0x4E0). The stub tails to 0x10190576 for that lr (the firmware without the module stays stock), to 0x102092A6 for the
other sites. Module (aspect_fw.inc AF_LV_CLASS): FX with 4:3 chosen -> class 3.

Why (B6 camera, DIAG11-13, 2026-11-26): with B4's 4:3 the live-view pipeline widths stayed FX (1536 / 1920) and only
the last display stage went to 640, so the full 3:2 picture was squeezed into 640 x 480; stock 1:1 narrows the whole
pipeline (1536 -> 1024) with class 4 at capture info + 0x4E0. The ISP config 0x100C0C4A maps class 0 / 1 / 2 / 3 / 4
to mode 1 / 2 / 0 / 4 / 3 (+0x27E): a 4:3 class exists there, the area map 0x10190576 never returns it.

    python -B research/aspect/z5_aspect43_b7m_fw.py <a3704c6f EG165050.BIN> --a56 <A56 7477fde9 EG165050.BIN> \
        --module-src <absolute release worktree>/src/card_module --b1-diag <B1 DIAG17.BIN> \
        --out output/<new dir>/EG165050.BIN

Checks: every B4 check (lv_window, lv_fit, lv_size, lv_other, lv_crop and all A90 checks), plus
  lv_class  0x10190598 with the area setting 0 / 1 / 4 / 8: capture info + 0x4E0 as the release firmware (0 / 0 / 1 /
            4) without the module, with the module not chosen and for every area but FX when chosen; FX with 4:3 chosen
            (tables out or in) -> 3. The stub's class tail: without the module 0x10190598 stores the stock class for
            every area 0-8 (not a size pointer).
camera_verified: false.
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import z5_aspect43_b4m_fw as b4  # noqa: E402

b3, b2, a90, a85, m, probe = b4.b3, b4.b2, b4.a90, b4.a85, b4.m, b4.probe
CLASS_SITE, CLASS_OLD = 0x101905BC, bytes.fromhex('fff7dbff')    # bl 0x10190576
STOCK_CLASS = {0: 0, 1: 0, 2: 2, 3: 0, 4: 1, 5: 1, 6: 1, 7: 1, 8: 4}


def assemble_stubs(address):
    with tempfile.TemporaryDirectory(dir=probe.ROOT / 'output') as tmp:
        def run(*args):
            return subprocess.check_output(args, cwd=tmp, text=True)
        run('arm-none-eabi-gcc', '-mcpu=cortex-a9', '-mthumb', '-c', str(HERE / 'b7m_fx43.S'), '-o', 'h.o')
        run('arm-none-eabi-ld', f'-Ttext={address:#x}', '-e', 'cfg', '-o', 'h.elf', 'h.o')
        run('arm-none-eabi-objcopy', '-O', 'binary', 'h.elf', 'h.bin')
        symbols = {}
        for line in run('arm-none-eabi-nm', 'h.elf').splitlines():
            value, _, name = line.split()
            symbols[name] = int(value, 16)
        return (Path(tmp) / 'h.bin').read_bytes(), symbols


a85.assemble_stubs = assemble_stubs


def apply_class_site(data):
    off = CLASS_SITE - m.BASE
    if bytes(data[off:off + 4]) != CLASS_OLD:
        raise ValueError('original bytes differ at %#x' % CLASS_SITE)
    new = m.hooks.branch(CLASS_SITE, b4.HSZW_VENEER, True)
    data[off:off + 4] = new
    return dict(name='hcls live-view aspect class -> hszw veneer', address=hex(CLASS_SITE), original=CLASS_OLD.hex(),
                replacement=new.hex())


def klass(ctx, area):
    b4.crop(ctx, area)
    return ctx.u.mem_read(b4.LV_STATE + 0x4E0, 1)[0]


def verify_lv_class(raw, image):
    c = b3.contexts(raw, image)
    res = {}
    for area in (0, 1, 4, 8):
        ref = klass(c['stock'], area)
        assert ref == STOCK_CLASS[area], (area, ref)
        assert klass(c['bare'], area) == ref and klass(c['not_chosen'], area) == ref, ('off', area)
        assert klass(c['not_chosen_233'], area) == ref, ('2.33', area)
        out, inn = klass(c['chosen_out'], area), klass(c['chosen_in'], area)
        if area == 0:
            assert out == inn == 3, (out, inn)
        else:
            assert out == inn == ref, ('other area', area, out, inn)
        res['area %d' % area] = dict(stock=ref, chosen=out)
    # the stub's class tail without the module: the whole function with every area 0-8 stores the stock class
    for area in range(9):
        assert klass(c['bare'], area) == STOCK_CLASS[area] == klass(c['stock'], area), ('bare tail', area)
    res['bare_tail'] = 'stock class for areas 0-8'
    return res


def main():
    """Our wrapper sits innermost (installed before b4.main's): by the time it runs, m.apply_kind2 is b4's crop-site
    wrapper and a85.verify_query carries every B4 check; we add the class site and lv_class on top."""
    orig_m_build = m.build

    def innermost(source, a56, out, inc):
        b4_kind2 = m.apply_kind2

        def kind2(data):
            edits = b4_kind2(data)
            edits.append(apply_class_site(data))
            return edits
        m.apply_kind2 = kind2
        prev = a85.verify_query

        def checks(raw, image):
            res = prev(raw, image)
            res['lv_class'] = verify_lv_class(raw, image)
            return res
        a85.verify_query = checks
        orig_m_build(source, a56, out, inc)
    m.build = innermost
    b4.main()


if __name__ == '__main__':
    main()
