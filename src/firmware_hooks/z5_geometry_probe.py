"""Run the Z5 C1.50 stock image-area geometry functions in Unicorn (offline, no firmware written).

    python -B research/aspect/z5_geometry_probe.py [--image <MAIN .bi>] [--out <new json>]

Functions (Z30 C1.20 counterparts used by WonderLantern aspect-profiles, matched instruction for instruction):
  buffer init   0x1014337A(obj, info)      Z30 0x10142BEA
  config        0x10142A7C(obj, info, 0)   Z30 0x1014237A
  area desc     0x101DC3E8(info, obj)      Z30 0x101D5B36
  size selector 0x10209294(0, area, 0)     Z30 0x1020BFC8
Inputs follow the Z30 tool, with Z5's info offsets (8 lower in this range): area at info+0x200 (Z30 +0x208),
size at info+0x221 (Z30 +0x229); the area descriptor helper takes area at info+0 and size at info+0x22.
The RAM pointer table 0xC18C7554 (7 groups x 9 areas) is filled from its ROM init copy 0x120EB528.
camera_verified: false.
"""
import argparse
import json
import pathlib
import struct

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASE = 0x10020000
PTR_RAM, PTR_ROM, PTR_WORDS = 0xC18C7554, 0x120EB528, 63
RAM = 0xC3000000
AREAS = {0: 'FX', 1: 'DX', 4: 'FX16:9', 5: 'DX16:9', 7: 'area7', 8: '1:1'}


class Ctx:
    def __init__(self, image):
        from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_MEM_READ, UC_HOOK_MEM_UNMAPPED
        from unicorn.arm_const import UC_CPU_ARM_CORTEX_A15
        self.image = image
        u = self.u = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A15)
        u.mem_map(BASE, (len(image) + 0xFFF) & ~0xFFF)
        u.mem_write(BASE, image)
        u.mem_map(0xC1800000, 0x200000)                 # globals incl. the pointer table
        u.mem_map(RAM, 0x40000)
        u.mem_map(0x50000000, 0x10000)                  # stack
        u.mem_write(PTR_RAM, image[PTR_ROM - BASE:PTR_ROM - BASE + 4 * PTR_WORDS])
        self.reads = []

        def rd(mu, access, address, size, value, extra):
            if 0x11640000 <= address < 0x11680000 or 0xC18C7554 <= address < 0xC18C7554 + 4 * PTR_WORDS:
                self.reads.append(address)
        u.hook_add(UC_HOOK_MEM_READ, rd)

    def call(self, fn, args):
        from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_SP, UC_ARM_REG_LR
        u = self.u
        for i, a in enumerate(args):
            u.reg_write(UC_ARM_REG_R0 + i, a)
        u.reg_write(UC_ARM_REG_SP, 0x5000F000); u.reg_write(UC_ARM_REG_LR, 0x50000001)
        u.emu_start(fn | 1, 0x50000000, count=200000)
        return u.reg_read(UC_ARM_REG_R0)


def measure(image):
    c = Ctx(image)
    obj, info, holder, header = RAM, RAM + 0x1000, RAM + 0x8000, RAM + 0x9000

    def reset(area, size):
        c.u.mem_write(obj, bytes(256)); c.u.mem_write(info, bytes(0x3000))
        c.u.mem_write(obj + 4, struct.pack('<I', holder)); c.u.mem_write(holder, struct.pack('<I', header))
        c.u.mem_write(info + 0x200, bytes([area])); c.u.mem_write(info + 0x221, bytes([size]))
        c.reads.clear()

    cases = []
    for area in AREAS:
        for size in range(3):
            row = dict(area=area, name=AREAS[area], size='LMS'[size])
            try:
                reset(area, size); c.call(0x1014337A, (obj, info))
                buf = bytes(c.u.mem_read(obj, 44))
                row.update(buffer_padded=struct.unpack_from('<2H', buf, 0x10), buffer_out=struct.unpack_from('<2H', buf, 0x14),
                           buffer_hex=buf.hex())
            except Exception as e:  # noqa: BLE001
                row['buffer_error'] = str(e)
            try:
                reset(area, size); c.call(0x10142A7C, (obj, info, 0))
                cfg = bytes(c.u.mem_read(obj, 0x70))
                row.update(cfg_parent=struct.unpack_from('<2H', cfg, 0x22), cfg_origin=struct.unpack_from('<2H', cfg, 0x26),
                           cfg_trim=struct.unpack_from('<2H', cfg, 0x38), cfg_trimmed=struct.unpack_from('<2H', cfg, 0x3C),
                           cfg_out=struct.unpack_from('<2H', cfg, 0x56), cfg_hex=cfg.hex(),
                           cfg_reads=sorted({hex(a) for a in c.reads}))
            except Exception as e:  # noqa: BLE001
                row['cfg_error'] = str(e)
            try:
                reset(area, size); c.u.mem_write(info, bytes([area])); c.u.mem_write(info + 0x22, bytes([size]))
                c.call(0x101DC3E8, (info, obj))
                row['desc_u16'] = struct.unpack('<14H', bytes(c.u.mem_read(obj, 28)))
            except Exception as e:  # noqa: BLE001
                row['desc_error'] = str(e)
            try:
                p = c.call(0x10209294, (0, area, 0))
                row['selector_ptr'] = hex(p)
                row['selector_wh'] = struct.unpack('<2H', bytes(c.u.mem_read(p, 4)))
            except Exception as e:  # noqa: BLE001
                row['selector_error'] = str(e)
            cases.append(row)
    return cases


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--image', default=str(ROOT / 'output/stage3-c4-20261106-185724/eg1650_015001.bi'))
    ap.add_argument('--out')
    a = ap.parse_args()
    cases = measure(pathlib.Path(a.image).read_bytes())
    for r in cases:
        print(r['name'], r['size'], 'buf', r.get('buffer_out', r.get('buffer_error')), r.get('buffer_padded', ''),
              '| cfg parent', r.get('cfg_parent', r.get('cfg_error')), 'origin', r.get('cfg_origin'), 'trim', r.get('cfg_trim'),
              'trimmed', r.get('cfg_trimmed'), 'out', r.get('cfg_out'),
              '| desc', r.get('desc_u16', r.get('desc_error')), '| sel', r.get('selector_wh', r.get('selector_error')))
    if a.out:
        out = pathlib.Path(a.out)
        if out.exists():
            raise SystemExit(f'{out} exists')
        out.write_text(json.dumps(cases, indent=1) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
