"""A85m = A84m + the extended preview-size function 0x1034E692 as a module stub (kind 24) and, in the module, the
Z30 v43 fix for NEF playback (WonderLantern Z30 docs/aspect-raw-playback-v43.md, Z30 C1.20 0x10340B02 / 0x10340B82 /
0x103A1FA8 = Z5 0x1034E5E4 / 0x1034E692 / 0x103BC7E0): when the shape check does not take a file, files of type 1 / 2
get the preview size the file itself reports (file service kind 4 small / 5 large), if it is non-zero, within
640 x 480 / 1920 x 1080 and not 3:2. research/aspect/a85m_fx43.S; module aspect_fw.inc (release worktree).

Why (A84 camera, DIAG06-08, DSC_1814-1817, 2026-11-25): NEF-only geometry is now right (332,0 / 5352x4016 and
0,720 / 6016x2576) and the NEF's own previews are right (640x480 / 1440x1080 and 640x272 / 1920x824, not
stretched), but the camera's instant review and playback of NEF-only shots are stretched / broken: the playback
prep takes the preview size from the stock aspect class (FX: 640x424 / 1620x1080), and the record's main size of a
NEF is not one of our JPEG sizes, so the A77 shape check never takes it.

    python -B research/aspect/z5_aspect43_a85m_fw.py <a3704c6f EG165050.BIN> --a56 <A56 7477fde9 EG165050.BIN> \
        --module-src <release worktree>/src/card_module --out output/<new dir>/EG165050.BIN

Checks: every A84 / A84m check (module logic compiled from aspect_fw.inc and installed behind vui_common), plus
`preview_query`: both size functions, allowed and other callers, types 0-3, small / large, every class, five record
main sizes and nine file-service answers (emulated at 0x103BC7E0): shape files as A84, query only for types 1 / 2
with the right (kind, type, handle), result used only when valid and not 3:2, otherwise identical to the release
firmware; r4-r11 and sp as stock. Without a module the image equals the release in the same matrix.
camera_verified: false.
"""
import argparse
import hashlib
import itertools
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, \
    UC_ARM_REG_R3

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import z5_aspect43_a84m_fw as mm  # noqa: E402

m = mm.m
probe = m.probe
BaseCtx = mm.BaseCtx
SVC = 0x103BC7E0
STD, EXT = 0x1034E5E4, 0x1034E692
STD_CALLERS = (0x1034E9CB, 0x10354FAD, 0x10354FE5)
EXT_CALLERS = (0x1034E94D, 0x10354F03, 0x10354F43)
FAKE = {'answer': None, 'calls': []}       # answer: (status, w, h) or None = error


def svc_hook(u, address, size, user):
    kind, typ, handle, out = (u.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3))
    FAKE['calls'].append((kind, typ, handle))
    ans = FAKE['answer']
    status, w, h = ans if ans else (0x80000001, 0, 0)
    u.mem_write(out, struct.pack('<2H', w, h))
    u.reg_write(UC_ARM_REG_R0, status)
    u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))


def with_svc(ctx):
    ctx.u.hook_add(UC_HOOK_CODE, svc_hook, begin=SVC, end=SVC)
    return ctx


def assemble_stubs(address):
    with tempfile.TemporaryDirectory(dir=probe.ROOT / 'output') as tmp:
        def run(*args):
            return subprocess.check_output(args, cwd=tmp, text=True)
        run('arm-none-eabi-gcc', '-mcpu=cortex-a9', '-mthumb', '-c', str(HERE / 'a85m_fx43.S'), '-o', 'h.o')
        run('arm-none-eabi-ld', f'-Ttext={address:#x}', '-e', 'cfg', '-o', 'h.elf', 'h.o')
        run('arm-none-eabi-objcopy', '-O', 'binary', 'h.elf', 'h.bin')
        symbols = {}
        for line in run('arm-none-eabi-nm', 'h.elf').splitlines():
            value, _, name = line.split()
            symbols[name] = int(value, 16)
        return (Path(tmp) / 'h.bin').read_bytes(), symbols


mm.HARNESS = mm.HARNESS.replace('kind <= AF_K_PLAY', 'kind <= AF_K_EXT')
assert 'AF_K_EXT' in mm.HARNESS


def ctx_factory(image):
    return with_svc(mm.ctx_factory(image))


def bare(image):
    return with_svc(BaseCtx(image))


def shape_size(w, h, small):
    if (w, h) in ((5352, 4016), (4008, 3008), (2664, 2000)):
        return (640, 480) if small else (1440, 1080)
    if (w, h) in ((6016, 2576), (4512, 1936), (3008, 1288)):
        return (640, 272) if small else (1920, 824)
    return None


def usable(ans, small):
    if not ans or ans[0]:
        return None
    w, h = ans[1:]
    if not w or not h or w > (640 if small else 1920) or h > (480 if small else 1080):
        return None
    if abs(2 * w - 3 * h) * 64 <= 2 * w:
        return None
    return (w, h)


def verify_query(raw, image):
    stock, on, off = with_svc(BaseCtx(raw)), probe.Ctx(image), bare(image)
    ctxobj, rec, out = probe.RAM + 0x30000, probe.RAM + 0x31000, probe.RAM + 0x34000
    handle = 0x5A5A0001
    mains = ((6016, 4016), (6040, 4032), (5352, 4016), (6016, 2576), (0, 0))
    answers = (None, (0, 640, 480), (0, 1440, 1080), (0, 640, 272), (0, 1920, 824), (0, 640, 424), (0, 1620, 1080),
               (0, 0, 0), (0, 2000, 1100), (5, 1440, 1080))
    n = dict(shape=0, query=0, stock=0, other_caller=0, svc_calls=0)
    for fn, callers, ncls in ((STD, STD_CALLERS, 10), (EXT, EXT_CALLERS, 6)):
        for lr, typ, small, cls, main, ans in itertools.product(callers + (0x1034E9CF, 0x10354F01), (0, 1, 2, 3),
                                                                 (0, 1), range(ncls), mains, answers):
            if cls % 3 and ans not in (None, (0, 1440, 1080)):
                continue                                  # keep the matrix small: every answer on classes 0, 3, 6, 9
            res = []
            for ctx in (stock, off, on):
                ctx.u.mem_write(ctxobj, bytes(0x40))
                ctx.u.mem_write(ctxobj, struct.pack('<IB', handle, typ))
                ctx.u.mem_write(ctxobj + 12, struct.pack('<I', rec))
                ctx.u.mem_write(rec, bytes(0x400)); ctx.u.mem_write(rec + 0x398, struct.pack('<2H', *main))
                ctx.u.mem_write(out, bytes(8))
                FAKE['answer'] = ans; FAKE['calls'] = []
                regs = [out, cls, small, 0x11110003, ctxobj] + [0x11110000 + i for i in range(5, 13)]
                r = m.run_with(ctx, fn, regs, lr)
                res.append((struct.unpack('<2H', bytes(ctx.u.mem_read(out, 4))), r[4:12] + r[13:], list(FAKE['calls'])))
            assert res[1][:2] == res[0][:2] and not res[1][2] and not res[0][2], ('no module', hex(fn), hex(lr))
            assert res[2][1] == res[0][1], ('registers', hex(fn), hex(lr), typ, small, cls, main, ans)
            if lr not in callers:
                assert res[2][0] == res[0][0] and not res[2][2], ('other caller', hex(fn), hex(lr))
                n['other_caller'] += 1
                continue
            want = shape_size(*main, small)
            if want:
                assert res[2][0] == want and not res[2][2], ('shape', hex(fn), main, small, res[2])
                n['shape'] += 1
                continue
            if typ in (1, 2):
                assert res[2][2] == [(4 if small else 5, typ, handle)], ('svc args', res[2][2])
                n['svc_calls'] += 1
            else:
                assert not res[2][2], ('svc for type', typ)
            got = usable(ans, small) if typ in (1, 2) else None
            if got:
                assert res[2][0] == got, ('query', hex(fn), typ, small, cls, main, ans, res[2][0])
                n['query'] += 1
            else:
                assert res[2][0] == res[0][0], ('stock', hex(fn), typ, small, cls, main, ans, res[2][0], res[0][0])
                n['stock'] += 1
    return dict(n, svc=hex(SVC), std_callers=[hex(v) for v in STD_CALLERS], ext_callers=[hex(v) for v in EXT_CALLERS],
                no_module_equals_release='all cases', camera_verified=False)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path)
    ap.add_argument('--a56', required=True, type=Path)
    ap.add_argument('--module-src', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--module-inc', type=Path)
    a = ap.parse_args()
    code, entry = mm.compile_module(a.module_src)
    m.SITES['hext'] = (EXT, '10b5 dab1', False)
    m.assemble = assemble_stubs
    m.enable = mm.enable_with_block
    probe.Ctx = ctx_factory
    real_run_site = m.run_site

    def run_site(ctx, name, regs0):
        if name == 'hext':                 # a whole function: covered by preview_query
            return []
        return real_run_site(ctx, name, regs0)
    m.run_site = run_site
    real_verify = m.verify

    def verify(raw_, image, tables, a56):
        mm.MODULE[hashlib.sha256(image).hexdigest()] = (code, entry)
        res = real_verify(raw_, image, tables, a56)
        res['no_module'] = mm.no_module_equals_release(raw_, image)
        res['preview_query'] = verify_query(raw_, image)
        res['module_logic'] = dict(bytes=len(code), entry=hex(entry), sha256=hashlib.sha256(code).hexdigest(),
                                   source='aspect_fw.inc')
        return res
    m.verify = verify
    m.verify_trace = lambda raw_, image, tables: dict(
        note='kind 19 / 22 / 24 readout runs inside the module (a83_trace.inc, aspect_fw.inc); not in this harness')
    m.build(a.source, a.a56, a.out, a.module_inc)


if __name__ == '__main__':
    main()
