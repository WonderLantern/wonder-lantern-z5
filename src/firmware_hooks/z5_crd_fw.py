"""Z5 C 1.50: A39 grain firmware + a gate at the end of the NEF XMP crd: block (A48).
MAIN only, through the official update; boot area untouched.

    python -B src/firmware_hooks/z5_crd_fw.py firmware-input/Z_5_0150.bin --out output/<new dir>/fw/EG165050.BIN
    (phase-0 venv; arm-none-eabi on PATH)

0x101BCDF4 writes the NEF's <rdf:Description xmlns:crd=...> block through a writer at ctx + 0x83C
({cursor, bytes left, overflow flag}, started as {ctx[0xE8], 0x7FF, 0}; r7 = writer, r4 = ctx). Each append
(0x101BC9E0) formats at the cursor and advances it; on overflow it sets the flag and the block is dropped. Its last step, at 0x101BCFA6, is 8 bytes:
    adr r1, "</rdf:Description>" (0x101BD104); mov r0, r7; bl 0x101BCFC0 (append)
They become a branch to a stub. The stub calls the card module only while the module is installed and sets
bit 6 (0x40) of the release flag word P + 0x14 (= 0xBF5A1C00 | bits), through the portrait-UI entry with kind 18
and r1 = the saved frame {r0-r7, r12, lr} (r4 = ctx, r7 = writer). The module may add <crd:...> elements with the
firmware's own element writer 0x101BCFD2(writer, name, fmt, value). Then the stub replays the 8 bytes with
absolute addresses and continues at 0x101BCFAE. Without the module, or bit 6 clear, the block is stock.
camera_verified: false.
"""
import hashlib
import json
import pathlib
import struct
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import z5_grain_fw as gr  # noqa: E402

rel, vh, dd, gz, mtp = gr.rel, gr.vh, gr.dd, gr.gz, gr.mtp
from z5mod_fw.engine import Edit, Feature  # noqa: E402

A39_SHA256 = '2e940a5b157822d2bb2909c463225b547e0d93cd3f4ba8f3547828c19f75428f'
SITE = 0x101BCFA6
EXPECTED = bytes.fromhex('57a1384600f009f8')
RESUME = SITE + len(EXPECTED)
CLOSE = 0x101BD104                  # "</rdf:Description>"
APPEND = 0x101BCFC0
ELEMENT = 0x101BCFD2
CRD_BIT = 0x40
KIND = 18

STUB = f'''
    .balign 4
    .global crd_gate
    .thumb_func
crd_gate:
    push    {{r0-r7, r12, lr}}
{rel.flag_check(CRD_BIT, 'crd_out')}
    movs    r0, #{KIND}
    mov     r1, sp
    bl      vui_common
crd_out:
    pop     {{r0-r7, r12, lr}}
    ldr     r1, ={CLOSE:#x}
    mov     r0, r7
    ldr     lr, ={RESUME | 1:#x}
    ldr     pc, ={APPEND | 1:#x}
    .ltorg
'''


def source():
    return gr.source() + STUB


def feature(stock, at):
    f, syms = gr.feature(stock, at)
    v = vh.VENEERS + 8 * (len(vh.HOOKS) + 2) + 8 * 4      # after the grain veneer
    if stock[SITE - vh.BASE:RESUME - vh.BASE] != EXPECTED:
        raise SystemExit('crd block end differs from the expected bytes')
    if stock[CLOSE - vh.BASE:CLOSE - vh.BASE + 19] != b'</rdf:Description>\0':
        raise SystemExit('closing string differs')
    if stock[ELEMENT - vh.BASE:ELEMENT - vh.BASE + 4] != bytes.fromhex('0fb40a46'):
        raise SystemExit('element writer differs')
    edits = f.edits + (
        Edit('crd_veneer', v - vh.BASE, bytes(8), bytes.fromhex('dff800f0') + struct.pack('<I', syms['crd_gate'] | 1)),
        Edit('crd_gate', SITE - vh.BASE, EXPECTED, vh.branch(SITE, v, False) + bytes.fromhex('00bf00bf')),
    )
    return Feature(name=f.name, description=f.description + '; NEF crd block gate (module kind 18, flag bit 6)',
                   camera_verified=False, edits=edits, appends=f.appends), syms


# ---------------------------------------------------------------- Unicorn checks
WRITER, BUF, NAME, FMT, MODRET = 0x50100000, 0x50100100, 0x50100A00, 0x50100A40, 0x50000700
PRE = b'<rdf:Description rdf:about="" xmlns:crd="http://ns.adobe.com/camera-raw-defaults/1.0/">'


def run(image, flagword, module, length=len(PRE)):
    """Enter at SITE with r7 = writer; stop at RESUME. Returns (calls, regs, lr, sp, stack, text)."""
    from unicorn import UC_HOOK_CODE
    from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, \
        UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R7
    u = rel.machine(image, None)
    u.mem_map(WRITER, 0x1000)
    u.mem_write(WRITER, struct.pack('<III', BUF + length, 0x7FF - length, 0))
    u.mem_write(BUF, (PRE + b'x' * 2048)[:length] + b'\0')
    u.mem_write(NAME, b'GrainAmount\0'); u.mem_write(FMT, b'%u\0')
    if module != 'none':
        u.mem_write(gz.ANCHOR, struct.pack('<II', gr.BLOCK, ~gr.BLOCK & 0xFFFFFFFF))
        if flagword is not None:
            u.mem_write(gr.BLOCK + 0x14, struct.pack('<I', flagword))
        u.mem_write(gr.BLOCK + 0x0C, struct.pack('<I', gr.QBLOCK))
        u.mem_write(gr.QBLOCK, struct.pack('<II', vh.CM_MAGIC ^ (1 if module == 'bad_magic' else 0),
                                           0 if module == 'no_fn' else gr.MODFN | 1))
    regs = [UC_ARM_REG_R0 + i for i in range(13)]
    calls, back = [], []

    def hook(mu, pc, size, unused):
        if pc == gr.MODFN:
            sp = mu.reg_read(UC_ARM_REG_R1)
            frame = struct.unpack('<10I', bytes(mu.mem_read(sp, 40)))
            calls.append((mu.reg_read(UC_ARM_REG_R0), frame, sp & 7))
            if module == 'emit':        # what the module does: one element through the firmware's writer
                back.append(mu.reg_read(UC_ARM_REG_LR))
                mu.reg_write(UC_ARM_REG_R0, frame[7]); mu.reg_write(UC_ARM_REG_R1, NAME)
                mu.reg_write(UC_ARM_REG_R2, FMT); mu.reg_write(UC_ARM_REG_R3, 56)
                mu.reg_write(UC_ARM_REG_LR, MODRET | 1); mu.reg_write(UC_ARM_REG_PC, ELEMENT | 1)
                return
            for r in range(4):
                mu.reg_write(UC_ARM_REG_R0 + r, 0xDEAD0000 + r)
            mu.reg_write(UC_ARM_REG_R0 + 12, 0xDEAD000C)
            mu.reg_write(UC_ARM_REG_PC, mu.reg_read(UC_ARM_REG_LR))
        elif pc == MODRET:
            mu.reg_write(UC_ARM_REG_PC, back.pop())
        elif pc == RESUME:
            mu.emu_stop()

    u.hook_add(UC_HOOK_CODE, hook)
    for i, r in enumerate(regs):
        u.reg_write(r, 0x88880000 + i)
    u.reg_write(UC_ARM_REG_R7, WRITER)
    u.reg_write(UC_ARM_REG_SP, gr.STACK); u.reg_write(UC_ARM_REG_LR, 0x10111111)
    u.emu_start(SITE | 1, 0, count=20000)
    assert u.reg_read(UC_ARM_REG_PC) == RESUME, hex(u.reg_read(UC_ARM_REG_PC))
    sp = u.reg_read(UC_ARM_REG_SP)
    cursor, left, over = struct.unpack('<III', bytes(u.mem_read(WRITER, 12)))
    text = bytes(u.mem_read(BUF, cursor - BUF + 1)) + bytes([over])
    return calls, tuple(u.reg_read(r) for r in regs), u.reg_read(UC_ARM_REG_LR), sp, \
        bytes(u.mem_read(sp, gr.STACK - sp)), text


FLAGS = (None, 0, 0xBF5A1C00, 0xBF5A1C20, 0xBF5A1C23, 0xBF5A1C40, 0xBF5A1C63, 0xBF5A1D40, 0x3F5A1C40, 0xFFFFFFFF)
MODULES = ('none', 'bad_magic', 'no_fn', 'ok', 'emit')


def verify(stock, image):
    n = 0
    for length in (len(PRE), 913, 0x7F0):
        want = run(stock, None, 'none', length)
        assert not want[0]
        assert want[5].endswith(b'</rdf:Description>\0\0') or length >= 0x7F0, want[5][-40:]
        for module in MODULES:
            for fw in FLAGS:
                got = run(image, fw, module, length); n += 1
                on = (module in ('ok', 'emit') and fw is not None
                      and (fw >> 8) == rel.FLAG_MAGIC and fw & CRD_BIT)
                case = (length, module, fw)
                if on:
                    assert len(got[0]) == 1 and got[0][0][0] == KIND, case
                    frame = got[0][0][1]
                    assert frame[4] == 0x88880004 and frame[7] == WRITER and frame[9] == 0x10111111, (case, frame)
                    assert got[0][0][2] == 0, ('stack not 8-aligned at the module call', case)
                else:
                    assert not got[0], case
                if on and module == 'emit':
                    if length < 0x7F0:
                        assert got[5] == want[5][:-20] + b'<crd:GrainAmount>56</crd:GrainAmount></rdf:Description>\0\0', \
                            (case, got[5][-80:])
                    assert got[1][4:12] == want[1][4:12] and got[3] == want[3], ('callee-saved state differs', case)
                else:
                    assert got[1:] == want[1:], ('state at resume differs', case)
    return dict(passed=True, runs=n)


def build(package, out):
    rp = out.with_name(out.name + '.json')
    for p in (out, rp):
        if p.exists():
            raise SystemExit(f'{p} exists; refusing to overwrite')
    vh.source, vh.feature = source, feature
    vh.build(package, out)
    from z5mod_fw import container
    _, mods = container.load(package)
    stock = container.main_module(mods).data
    image = out.read_bytes()
    dd.windows = lambda: [(gr.bf.SITE, gr.bf.SITE + 4), (rel.GO_SITE, rel.GO_SITE + 4), (rel.LV_SITE, rel.LV_SITE + 6),
                          (mtp.BUILDER, mtp.BUILDER + len(mtp.EXPECTED)), (gr.GATE, gr.GATE + len(gr.EXPECTED)),
                          (SITE, RESUME)]
    incoming = dd.audit_incoming(stock)
    if incoming:
        raise SystemExit(f'branches land inside a replaced window: {incoming}')
    report = dict(base_a39=A39_SHA256, burst_rate=gr.bf.verify(stock, image),
                  switches=dict(unicorn=rel.verify_switches(stock, image)),
                  mtp_fast=dict(unicorn=mtp.verify(stock, image)),
                  grain_gate=dict(gate=hex(gr.GATE), kind=gr.KIND, unicorn=gr.verify(stock, image)),
                  crd_gate=dict(site=hex(SITE), expected=EXPECTED.hex(), resume=hex(RESUME), kind=KIND,
                                element_writer=hex(ELEMENT),
                                switch='module installed and P+0x14 = 0xBF5A1C00 | bit 6',
                                incoming_branch_audit=dict(found=incoming), unicorn=verify(stock, image),
                                camera_verified=False))
    data = json.loads(rp.read_text(encoding='utf-8'))
    data.update(report)
    rp.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    print('sha256', hashlib.sha256(image).hexdigest())
    print(json.dumps({k: report[k] for k in ('grain_gate', 'crd_gate')}, indent=1))


if __name__ == '__main__':
    args = sys.argv[1:]
    if len(args) != 3 or args[1] != '--out':
        raise SystemExit(__doc__)
    build(pathlib.Path(args[0]), pathlib.Path(args[2]))
