"""Z5 C1.50 trial firmware: the stock 1:1 image area (area 8) produces 4:3 instead (A56). Data-only edits on the
current release firmware a3704c6f (MAIN module, official update; boot area untouched; no code changed).

    python -B research/aspect/z5_aspect43_fw.py <a3704c6f EG165050.BIN> --out output/<new dir>/EG165050.BIN

Why 1:1: on Z5 the stock 1:1 area is already "full FX height, crop left and right" (parent 6016x4016, trim 1000 each
side -> 4016x4016, research/aspect/z5_geometry_probe.py), the same geometry as 4:3. So only numbers change:
trim 1000 -> 332, widths 4016 -> 5352 (L 5352x4016, M 4008x3008, S 2664x2000; heights as stock 1:1).
Tables (all read through the RAM pointer table 0xC18C7554, group x 9 areas, ROM init 0x120EB528, or by fixed address):
  area offset  0x1167B7F4 + 8*4   (1000, 0)                        -> (332, 0)
  internal     0x1167B884 (g0), 0x1167B95C (g2)   L/M/S widths 4040/3462/3030 -> 5376/4614/4038 (height +24 / +6 rule)
  output       0x1167B8F0 (g1)    4016^2 / 3008^2 / 2000^2          -> 5352x4016 / 4008x3008 / 2664x2000
  trimmed      0x1167BA28 (g3)    4016x4016 x3                      -> 5352x4016 x3
  M/S regions  0x1167BA34 (g3[3..5]) 3444x3444 x2, 3014x3012         -> 4592x3444 x2, 4016x3012
  trim (cfg)   0x11648258 + 24*4  (1000, 0) x3                      -> (332, 0) x3
Embedded previews: area 8 maps to preview kind 4 (1:1) in 0x10190576 (no "kind 3 = 4:3" branch for any area), so the
kind-4 entries of the preview size tables take the stock kind-3 (4:3) values:
  0x116489CC 480x480 -> 640x480, 0x116489E0 960 -> 1280, 0x116489F4 1080x1080 -> 1440x1080, 0x11648A08 2160 -> 2880,
  0x1167B7A4 480x480 -> 640x480, 0x1167B7B8 1080x1080 -> 1440x1080, 0x1167B7CC 120x120 -> 160x120,
  0x1167B7E0 374x374 -> 502x375.
Effects: in the camera's own menu, choosing 1:1 now gives 4:3 (JPEG and RAW; menu label stays "1:1"). Every other
area is unchanged (checked in Unicorn). Live-view still shows the stock 1:1 frame. Old 1:1 photos may play back wrong
on this firmware (their preview buffers no longer match). camera_verified: false.
"""
import binascii
import hashlib
import json
import pathlib
import struct
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import z5_geometry_probe as probe  # noqa: E402

BASE = 0x10020000
INPUT_SHA256 = 'a3704c6f'  # prefix check; full hash recorded in the report
L, M, S = (5352, 4016), (4008, 3008), (2664, 2000)
TRIM = 332


def pairs(*xy):
    return b''.join(struct.pack('<HH', *p) for p in xy)


EDITS = [
    ('area offset', 0x1167B814, pairs((1000, 0)), pairs((TRIM, 0))),
    ('internal g0', 0x1167B884, pairs((4040, 4032), (3462, 3456), (3030, 3024)), pairs((5376, 4032), (4614, 3456), (4038, 3024))),
    ('output g1', 0x1167B8F0, pairs((4016, 4016), (3008, 3008), (2000, 2000)), pairs(L, M, S)),
    ('internal g2', 0x1167B95C, pairs((4040, 4032), (3462, 3456), (3030, 3024)), pairs((5376, 4032), (4614, 3456), (4038, 3024))),
    ('trimmed g3', 0x1167BA28, pairs((4016, 4016), (4016, 4016), (4016, 4016)), pairs(L, L, L)),
    # g3 has six entries per area: [3..5] are the M / S sampling regions the area descriptor reads (FX: 5176x3450,
    # 5172x3450, 4514x3012; 1:1: 3444x3444, 3444x3444, 3014x3012). Same heights, 4:3 widths.
    ('M/S region g3[3..5]', 0x1167BA34, pairs((3444, 3444), (3444, 3444), (3014, 3012)), pairs((4592, 3444), (4592, 3444), (4016, 3012))),
    ('cfg trim', 0x116482B8, pairs((1000, 0), (1000, 0), (1000, 0)), pairs((TRIM, 0), (TRIM, 0), (TRIM, 0))),
    # A58: the second per-area table set the configuration takes when info+0xFC != 0 (live-view / AF geometry;
    # A57 on camera: the focus point stayed within columns 5..15, the 1:1 limits). Area 8 entries only.
    ('alt offset T0', 0x116493CC, pairs((1000, 0)), pairs((TRIM, 0))),
    ('alt size T1', 0x116493F0, pairs((4016, 4016)), pairs(L)),
    ('alt internal T2', 0x11649414, pairs((4040, 4032)), pairs((5376, 4032))),
    ('alt trim T3', 0x11649438, pairs((1000, 0)), pairs((TRIM, 0))),
    ('alt size T4', 0x1164945C, pairs((4016, 4016)), pairs(L)),
    ('alt offset T5', 0x11649480, pairs((1000, 0)), pairs((TRIM, 0))),
    ('alt cfg trim', 0x11648324, pairs((1000, 0), (1000, 0), (1000, 0)), pairs((TRIM, 0), (TRIM, 0), (TRIM, 0))),
    ('preview 640 (buf)', 0x116489CC + 16, pairs((480, 480)), pairs((640, 480))),
    ('preview 640 width x2 (buf)', 0x116489E0 + 16, pairs((960, 0)), pairs((1280, 0))),
    ('preview 1620 (buf)', 0x116489F4 + 16, pairs((1080, 1080)), pairs((1440, 1080))),
    ('preview 1620 width x2 (buf)', 0x11648A08 + 16, pairs((2160, 0)), pairs((2880, 0))),
    ('preview 640 (sel)', 0x1167B7A4 + 16, pairs((480, 480)), pairs((640, 480))),
    ('preview 1620 (sel)', 0x1167B7B8 + 16, pairs((1080, 1080)), pairs((1440, 1080))),
    ('thumbnail (sel)', 0x1167B7CC + 16, pairs((120, 120)), pairs((160, 120))),
    ('preview 570-class (sel)', 0x1167B7E0 + 16, pairs((374, 374)), pairs((502, 375))),
]


def alt_branch(image):
    """Configuration with info+0xFC = 1 (the second table set), all areas, L/M/S."""
    c = probe.Ctx(image)
    obj, info, holder, header = probe.RAM, probe.RAM + 0x1000, probe.RAM + 0x8000, probe.RAM + 0x9000
    res = {}
    for area in probe.AREAS:
        for size in range(3):
            c.u.mem_write(obj, bytes(256)); c.u.mem_write(info, bytes(0x3000))
            c.u.mem_write(obj + 4, struct.pack('<I', holder)); c.u.mem_write(holder, struct.pack('<I', header))
            c.u.mem_write(info + 0x200, bytes([area])); c.u.mem_write(info + 0x221, bytes([size]))
            c.u.mem_write(info + 0xFC, struct.pack('<I', 1))
            c.call(0x10142A7C, (obj, info, 0))
            o = bytes(c.u.mem_read(obj, 0x70))
            res[(area, size)] = dict(parent=struct.unpack_from('<2H', o, 0x22), origin=struct.unpack_from('<2H', o, 0x26),
                                     trim=struct.unpack_from('<2H', o, 0x38), trimmed=struct.unpack_from('<2H', o, 0x3C),
                                     out=struct.unpack_from('<2H', o, 0x56), raw=o.hex())
    return res


def build(src, out):
    if out.exists():
        raise SystemExit(f'{out} exists')
    data = bytearray(src.read_bytes())
    sha_in = hashlib.sha256(data).hexdigest()
    if not sha_in.startswith(INPUT_SHA256) or binascii.crc_hqx(bytes(data), 0):
        raise SystemExit(f'input is not a3704c6f with a valid CRC ({sha_in})')
    report = []
    for name, addr, old, new in EDITS:
        off = addr - BASE
        if bytes(data[off:off + len(old)]) != old:
            raise SystemExit(f'{name} at {addr:#x}: expected {old.hex()}, found {data[off:off + len(old)].hex()}')
        data[off:off + len(new)] = new
        report.append(dict(name=name, address=hex(addr), offset=hex(off), original=old.hex(), replacement=new.hex()))
    body = bytes(data[:-2])
    data[-2:] = struct.pack('>H', binascii.crc_hqx(body, 0))
    assert binascii.crc_hqx(bytes(data), 0) == 0
    stock = probe.measure(src.read_bytes())
    trial = probe.measure(bytes(data))
    checks = []
    for a, b in zip(stock, trial):
        if a['area'] != 8:
            for k in a:
                assert a[k] == b[k], ('other area changed', a['name'], a['size'], k)
            continue
        i = 'LMS'.index(b['size'])
        want = (L, M, S)[i]
        assert b['buffer_out'] == want, b
        assert b['cfg_out'] == want and b['cfg_trimmed'] == L and b['cfg_trim'] == (TRIM, 0) and b['cfg_parent'] == (6016, 4016), b
        d = b['desc_u16']
        assert d[:6] == (6040, 4032, 5376, 4032, TRIM, 0), d
        if i:
            ew, eh = d[10] - d[8], d[11] - d[9]
            assert abs(ew * 3 / (eh * 4) - 1) < 0.01, ('M/S sampling region not 4:3', b['size'], d)
        checks.append(dict(size=b['size'], buffer=b['buffer_out'], padded=b['buffer_padded'], cfg_parent=b['cfg_parent'],
                           cfg_origin=b['cfg_origin'], cfg_trim=b['cfg_trim'], cfg_trimmed=b['cfg_trimmed'],
                           cfg_out=b['cfg_out'], desc=d, selector=b['selector_wh']))
    alt = []
    for img, tag in ((src.read_bytes(), 'stock'), (bytes(data), 'trial')):
        alt.append(alt_branch(img))
    for (area, size), b in alt[1].items():
        if area != 8:
            assert b == alt[0][(area, size)], ('alt branch changed for another area', area, size)
        else:
            assert b['trim'] == (TRIM, 0) and b['trimmed'] == L and b['parent'] == (6016, 4016), ('alt branch', size, b)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(bytes(data))
    sha_out = hashlib.sha256(data).hexdigest()
    rep = dict(input_sha256=sha_in, output_sha256=sha_out, edits=report, area8_after=checks,
               alt_branch_area8={str(k): v for k, v in alt[1].items() if k[0] == 8},
               other_areas_unchanged=[r['name'] for r in stock if r['area'] != 8],
               camera_verified=False)
    out.with_name(out.name + '.json').write_text(json.dumps(rep, indent=1) + '\n', encoding='utf-8')
    print('output', sha_out)
    for c in checks:
        print(c)
    for k, v in alt[1].items():
        if k[0] == 8:
            print('alt', k, {x: v[x] for x in ('parent', 'origin', 'trim', 'trimmed', 'out')})


if __name__ == '__main__':
    a = sys.argv[1:]
    if len(a) != 3 or a[1] != '--out':
        raise SystemExit(__doc__)
    build(pathlib.Path(a[0]), pathlib.Path(a[2]))
