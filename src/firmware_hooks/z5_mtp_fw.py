"""Z5 C 1.50: release firmware bf821031 + MTP property-list speed-up (port of WonderLantern Z30 `mtp-fast`).
MAIN only, through the official update; boot area untouched.

    python -B src/firmware_hooks/z5_mtp_fw.py firmware-input/Z_5_0150.bin --out output/<new dir>/fw/EG165050.BIN
    (run with the phase-0 venv; arm-none-eabi on PATH, as for the release build)

Stock GetObjectPropList builds each object's list in 0x105A40B4 (same code as Z30 0x1059E1B8); every
property accessor acquires the object's file record and releases it again, and the release frees it, so one
object costs one directory query per property. The wrapper holds one extra reference for the duration of the
per-object builder (as on Z30): handle -> file 0x103388D6, acquire 0x104580E0, release 0x10457DC0 (Z30
0x10336526 / 0x10461E40 / 0x10461B20, instruction for instruction the same). Folders (handle >> 27 == 2),
virtual objects (7) and the count-only pass (0xC29C5CBC + 0x7C != 0) run stock.

Switch: on while the card module is installed (same test as the portrait-UI entries: anchor pair P, ~P valid,
module block Q = [P + 0x0C] in RAM with magic 'CMNU' and a non-zero function pointer at Q + 4), so the camera's
A19 module (7d31b279) turns it on unchanged. Off-switch for later modules: the release flag word
P + 0x14 = 0xBF5A1C00 | bits with bit 4 (0x10) set. Without the module the builder entry runs exactly as stock
(registers and stack untouched).
camera_verified: false.
"""
import hashlib
import importlib.util
import json
import pathlib
import struct
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import z5_burst_fw as bf  # noqa: E402
import z5_burst_release_fw as rel  # noqa: E402
vh, dd, gz = rel.vh, rel.dd, rel.gz
from z5mod_fw.engine import Edit, Feature  # noqa: E402

RELEASE_SHA256 = 'bf821031e6d0b7d5f8762eba7178db94adda90f91c8d078985039f4f1cad558e'
BUILDER = 0x105A40B4
EXPECTED = bytes.fromhex('2de9f05f92468346')      # push.w {r4-r12, lr}; mov sl, r2; mov fp, r0
HANDLE_TO_FILE, ACQUIRE, RELEASE = 0x103388D6, 0x104580E0, 0x10457DC0
COUNT_ONLY = 0xC29C5CBC + 0x7C
FOLDER_KIND, VIRTUAL_KIND = 2, 7
MTP_OFF_BIT = 0x10
FUNC_PREFIX = {HANDLE_TO_FILE: '7cb50546', ACQUIRE: '10b50446', RELEASE: '10b50400'}

STUB = f'''
    .balign 4
    .global mtp_wrap
    .thumb_func
mtp_wrap:
    push    {{r0-r3}}
    ldr     r3, ={gz.ANCHOR:#x}
    ldr     r0, [r3]
    ldr     r1, [r3, #4]
    mvns    r1, r1
    cmp     r0, r1
    bne     mtp_stock
    ldr     r2, ={gz.RAM_LO:#x}
    cmp     r0, r2
    blo     mtp_stock
    ldr     r3, ={gz.RAM_HI:#x}
    cmp     r0, r3
    bhs     mtp_stock
    ldr     r1, [r0, #0xc]
    cmp     r1, r2
    blo     mtp_stock
    cmp     r1, r3
    bhs     mtp_stock
    ldr     r2, [r1]
    ldr     r3, ={vh.CM_MAGIC:#x}
    cmp     r2, r3
    bne     mtp_stock
    ldr     r2, [r1, #4]
    cmp     r2, #0
    beq     mtp_stock
    ldr     r1, [r0, #0x14]
    ldr     r2, ={rel.FLAG_MAGIC:#x}
    cmp.w   r2, r1, lsr #8
    bne     mtp_on
    tst     r1, #{MTP_OFF_BIT}
    bne     mtp_stock
mtp_on:
    pop     {{r0-r3}}
    push    {{r3-r7, lr}}
    mov     r4, r0
    mov     r5, r1
    mov     r6, r2
    movs    r7, #0
    lsrs    r0, r4, #27
    cmp     r0, #{FOLDER_KIND}
    beq     mtp_run
    cmp     r0, #{VIRTUAL_KIND}
    beq     mtp_run
    ldr     r0, ={COUNT_ONLY:#x}
    ldr     r0, [r0]
    cmp     r0, #0
    bne     mtp_run
    mov     r0, r4
    ldr     r3, ={HANDLE_TO_FILE | 1:#x}
    blx     r3
    ldr     r3, ={ACQUIRE | 1:#x}
    blx     r3
    mov     r7, r0
mtp_run:
    mov     r0, r4
    mov     r1, r5
    mov     r2, r6
    bl      mtp_body
    mov     r4, r0
    cmp     r7, #0
    beq     mtp_done
    mov     r0, r7
    ldr     r3, ={RELEASE | 1:#x}
    blx     r3
mtp_done:
    mov     r0, r4
    pop     {{r3-r7, pc}}
mtp_stock:
    pop     {{r0-r3}}
mtp_body:
    .byte   {', '.join(f'{b:#04x}' for b in EXPECTED)}
    ldr     pc, ={(BUILDER + len(EXPECTED)) | 1:#x}
    .ltorg
'''


def source():
    return rel.source() + STUB


def feature(stock, at):
    f, syms = rel.feature(stock, at)
    v = vh.VENEERS + 8 * (len(vh.HOOKS) + 2) + 8 * 2      # after the burst_go / burst_lv veneers
    if stock[BUILDER - vh.BASE:BUILDER - vh.BASE + len(EXPECTED)] != EXPECTED:
        raise SystemExit('MTP builder entry differs from the expected bytes')
    for a, p in FUNC_PREFIX.items():
        if stock[a - vh.BASE:a - vh.BASE + len(p) // 2].hex() != p:
            raise SystemExit(f'MTP helper {a:#x} differs from the expected bytes')
    edits = f.edits + (
        Edit('mtp_veneer', v - vh.BASE, bytes(8), bytes.fromhex('dff800f0') + struct.pack('<I', syms['mtp_wrap'] | 1)),
        Edit('mtp_fast', BUILDER - vh.BASE, EXPECTED, vh.branch(BUILDER, v, False) + bytes.fromhex('00bf00bf')),
    )
    return Feature(name=f.name, description=f.description + '; MTP property-list record hold (on with the card module)',
                   camera_verified=False, edits=edits, appends=f.appends), syms


# ---------------------------------------------------------------- Unicorn checks
STACK = 0x50000800
RET = 0x50000101
BLOCK, QBLOCK = 0xC18F9000, 0xC18F9800
MODULE_CASES = ('none', 'no_block', 'bad_magic', 'no_fn', 'ok')


def run(image, flagword, handle, count_only, acq_result, module):
    """Enter the builder; stubs for the helpers; at BUILDER + 8 record the state and act as the builder's body
    (r0 = 0x1234 returned through its own pop). Returns a trace of what happened and the final state."""
    from unicorn import UC_HOOK_CODE
    from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC
    u = rel.machine(image, None)
    if module != 'none':
        u.mem_write(gz.ANCHOR, struct.pack('<II', BLOCK, ~BLOCK & 0xFFFFFFFF))
        if flagword is not None:
            u.mem_write(BLOCK + 0x14, struct.pack('<I', flagword))
        u.mem_write(BLOCK + 0x0C, struct.pack('<I', 0x00001000 if module == 'no_block' else QBLOCK))
        if module != 'no_block':
            u.mem_write(QBLOCK, struct.pack('<II', vh.CM_MAGIC ^ (1 if module == 'bad_magic' else 0),
                                            0 if module == 'no_fn' else 0x12345679))
    try:
        u.mem_map(COUNT_ONLY & ~4095, 4096)
    except Exception:
        pass
    u.mem_write(COUNT_ONLY, struct.pack('<I', count_only))
    regs = [UC_ARM_REG_R0 + i for i in range(13)]
    trace = []

    def ret(mu, value):
        mu.reg_write(UC_ARM_REG_R0, value)
        mu.reg_write(UC_ARM_REG_PC, mu.reg_read(UC_ARM_REG_LR))

    def hook(mu, pc, size, unused):
        if pc == HANDLE_TO_FILE:
            trace.append(('h2f', mu.reg_read(UC_ARM_REG_R0))); ret(mu, 0x77000000 | (mu.reg_read(UC_ARM_REG_R0) & 0xFFFF))
        elif pc == ACQUIRE:
            trace.append(('acq', mu.reg_read(UC_ARM_REG_R0))); ret(mu, acq_result)
        elif pc == RELEASE:
            trace.append(('rel', mu.reg_read(UC_ARM_REG_R0))); ret(mu, 0)
        elif pc == BUILDER + len(EXPECTED):
            sp = mu.reg_read(UC_ARM_REG_SP)
            trace.append(('body', tuple(mu.reg_read(r) for r in regs), sp, bytes(mu.mem_read(sp, 40))))
            words = struct.unpack('<10I', bytes(mu.mem_read(sp, 40)))
            for i, r in enumerate(range(4, 13)):
                mu.reg_write(UC_ARM_REG_R0 + r, words[i])
            mu.reg_write(UC_ARM_REG_R0, 0x1234)
            mu.reg_write(UC_ARM_REG_SP, sp + 40)
            mu.reg_write(UC_ARM_REG_PC, words[9])
        elif pc == RET & ~1:
            mu.emu_stop()

    u.hook_add(UC_HOOK_CODE, hook)
    for i, r in enumerate(regs):
        u.reg_write(r, 0x88880000 + i)
    u.reg_write(UC_ARM_REG_R0, handle); u.reg_write(UC_ARM_REG_R1, 0x51000000); u.reg_write(UC_ARM_REG_R2, 0x200)
    u.reg_write(UC_ARM_REG_SP, STACK); u.reg_write(UC_ARM_REG_LR, RET)
    u.emu_start(BUILDER | 1, 0, count=600)
    final = tuple(u.reg_read(r) for r in regs), u.reg_read(UC_ARM_REG_SP), u.reg_read(UC_ARM_REG_PC)
    return trace, final


FLAGS = (None, 0, 0xBF5A1C00, 0xBF5A1C03, 0xBF5A1C08, 0xBF5A1C10, 0xBF5A1C13, 0xBF5A1D10, 0x3F5A1C10, 0xFFFFFFFF)
HANDLES = (0x00000001, 0x08000123, 0x10000005, 0x20000001, 0x38000010, 0x40000002, 0x7FFFFFFF, 0x80000000)


def verify(stock, image):
    n = 0
    for handle in HANDLES:
        for count_only in (0, 1):
            want = run(stock, None, handle, count_only, 0, 'none')
            assert [t[0] for t in want[0]] == ['body'], want[0]
            wbody = want[0][0]
            for module in MODULE_CASES:
                for fw in FLAGS:
                    for acq in (0, 0xC3001000):
                        off = fw is not None and (fw >> 8) == rel.FLAG_MAGIC and fw & MTP_OFF_BIT
                        on = module == 'ok' and not off
                        trace, final = run(image, fw, handle, count_only, acq, module); n += 1
                        hold = on and (handle >> 27) not in (FOLDER_KIND, VIRTUAL_KIND) and count_only == 0
                        names = [t[0] for t in trace]
                        exp = (['h2f', 'acq', 'body'] + (['rel'] if acq else [])) if hold else ['body']
                        case = (hex(handle), count_only, module, fw, acq)
                        assert names == exp, (case, names)
                        body = next(t for t in trace if t[0] == 'body')
                        # The builder sees the same arguments (r0-r2, and r8, r10, r11 after the replayed prefix).
                        for r in (0, 1, 2, 8, 10, 11):
                            assert body[1][r] == wbody[1][r], ('arg', r, case)
                        if not on:      # stock path: every register and the stack exactly as stock
                            assert body[1:] == wbody[1:], ('stock path differs', case)
                        if hold:
                            assert trace[0][1] == handle and trace[1][1] == (0x77000000 | (handle & 0xFFFF)), case
                            if acq:
                                assert trace[3][1] == acq, case
                        # Return value, callee-saved registers, stack: as stock.
                        assert final[0][0] == 0x1234 and final[0][4:12] == want[1][0][4:12] and final[1] == want[1][1], \
                            ('exit differs', case)
    return dict(passed=True, calls=n)


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
    dd.windows = lambda: [(bf.SITE, bf.SITE + 4), (rel.GO_SITE, rel.GO_SITE + 4), (rel.LV_SITE, rel.LV_SITE + 6),
                          (BUILDER, BUILDER + len(EXPECTED))]
    incoming = dd.audit_incoming(stock)
    if incoming:
        raise SystemExit(f'branches land inside a replaced window: {incoming}')
    report = dict(base_release=RELEASE_SHA256, burst_rate=bf.verify(stock, image),
                  switches=dict(unicorn=rel.verify_switches(stock, image)),
                  mtp_fast=dict(builder=hex(BUILDER), expected=EXPECTED.hex(),
                                helpers=dict(handle_to_file=hex(HANDLE_TO_FILE), acquire=hex(ACQUIRE), release=hex(RELEASE)),
                                count_only=hex(COUNT_ONLY),
                                switch='on while the card module is installed; off if P+0x14 = 0xBF5A1C00 | bit 4',
                                z30_source='WonderLantern wonder-lantern-z30 b602735 features/mtp_fast.py (MIT)',
                                incoming_branch_audit=dict(found=incoming), unicorn=verify(stock, image),
                                camera_verified=False))
    data = json.loads(rp.read_text(encoding='utf-8'))
    data.update(report)
    rp.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    print('sha256', hashlib.sha256(image).hexdigest())
    print(json.dumps(report['mtp_fast'], indent=1))


if __name__ == '__main__':
    args = sys.argv[1:]
    if len(args) != 3 or args[1] != '--out':
        raise SystemExit(__doc__)
    build(pathlib.Path(args[0]), pathlib.Path(args[2]))
