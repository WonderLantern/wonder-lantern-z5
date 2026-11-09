"""Z5 C 1.50: clear the SDK anchor at every boot (group 28 firmware + one scatter-table edit).

    python -B research/overlay-investigation/z5_zero_anchor.py firmware-input/Z_5_0150.bin --out output/<new dir>/EG165050.BIN
    (needs arm-none-eabi binutils and Unicorn: output/phase0-*/enter-environment.ps1)

The anchor 0xC18F8120..0xC18F813F lies between the copied data and the zeroed data, so
boot leaves it alone and a short power-off can leave the loader's "already ran" pair in
place; the loader then never reads the card. The firmware has two start-up paths
(camera, group 34: a quick wake still kept the anchor with only the first one changed).
The zero-init entry at 0x100340B0 is widened by 32 bytes. The second path clears memory
with a hardware engine (0x1002C234), left alone; its plain .data copy loop at 0x1002BE74
is lengthened by 32 bytes instead, so fixed ROM words replace the anchor.

Writes only new files. camera_verified: false.
"""
import hashlib
import json
import pathlib
import struct
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import z5_dual_display as dd  # noqa: E402
from z5mod_fw import container, engine  # noqa: E402
from z5mod_fw.engine import Edit, Feature  # noqa: E402
from z5mod_fw.platform import z5_150 as z5  # noqa: E402
import stage0_multi  # noqa: E402

BASE = dd.BASE
GROUP28_SHA256 = '9a8c7d3e9a6f356530ba222240cb9bf7d74d1b631aea9aeee09f288c3eef48d1'
ENTRY = 0x100340B0
ZERO_HANDLER = 0x10020088
OLD_DST, OLD_LEN = 0xC18F8140, 0x1144A90
NEW_DST, NEW_LEN = 0xC18F8120, 0x1144AB0
COPY_END = 0xC1800000 + 0xF8120
GROUP33_SHA256 = 'bf0854f23b636c15030f04a74e9fe7ccf5fe427cd0bba4ab2ec18fc09ff64043'
WARM_COPY, WARM_COPY_END = 0x1002BE74, 0x1002BECC
WARM_COPY_LEN = 0x1002C020
DATA_SRC, DATA_DST, DATA_LEN = 0x12023FD4, 0xC1800000, 0xF8120


def zero_feature(warm=True):
    expected = struct.pack('<2I', OLD_DST, OLD_LEN)
    replacement = struct.pack('<2I', NEW_DST, NEW_LEN)
    edits = [Edit('zi_entry_dst_len', ENTRY + 4 - BASE, expected, replacement)]
    if warm:     # second start-up path: its plain .data copy also overwrites the gap
        edits.append(Edit('warm_data_copy_len', WARM_COPY_LEN - BASE,
                          struct.pack('<I', DATA_LEN), struct.pack('<I', DATA_LEN + 0x20)))
    return Feature(name='zero_sdk_anchor', description='Overwrite the 32-byte SDK anchor gap on both start-up paths',
                   camera_verified=False, edits=tuple(edits))


def check_warm_path(image):
    """The length literal must be loaded only by the .data copy of the second path."""
    words = struct.unpack_from('<3I', image, WARM_COPY - BASE)
    if words != (0xE59F019C, 0xE59F119C, 0xE59F219C):
        raise SystemExit('second start-up path differs')
    src, dst = struct.unpack_from('<2I', image, WARM_COPY_LEN - 8 - BASE)
    if (src, dst) != (DATA_SRC, DATA_DST):
        raise SystemExit('second start-up copy differs')
    users = []
    for pc in range(BASE, WARM_COPY_LEN, 4):
        w = struct.unpack_from('<I', image, pc - BASE)[0]
        if (w & 0x0F7F0000) == 0x051F0000 and w >> 28 == 0xE:
            imm = w & 0xFFF
            if pc + 8 + (imm if w & 0x00800000 else -imm) == WARM_COPY_LEN:
                users.append(hex(pc))
    if users != [hex(WARM_COPY + 8)]:
        raise SystemExit('length literal has other users: %s' % users)
    gap = struct.unpack_from('<8I', image, DATA_SRC + DATA_LEN - BASE)
    if gap[0] ^ gap[1] == 0xFFFFFFFF or (gap[0] == 0 and gap[1] >= 8) or (gap[3] & 0xFFFF0000) == 0x53540000:
        raise SystemExit('the ROM words would look like a valid anchor: %s' % [hex(x) for x in gap])
    return dict(copy_site=hex(WARM_COPY), length_literal=hex(WARM_COPY_LEN), length_users=users,
                gap_source=hex(DATA_SRC + DATA_LEN), gap_words=[hex(x) for x in gap])


def run_warm_copy(image):
    """Run the firmware's own .data copy loop with the new length and check both edges."""
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
    mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    size = (len(image) + 0xFFF) & ~0xFFF
    mu.mem_map(BASE, size)
    mu.mem_write(BASE, image)
    ram = DATA_DST
    ram_size = 0x200000
    mu.mem_map(ram, ram_size)
    mu.mem_write(ram, b'\xA5' * ram_size)
    mu.emu_start(WARM_COPY, WARM_COPY_END)
    rom = image[DATA_SRC - BASE:DATA_SRC - BASE + DATA_LEN + 0x20]
    result = dict(data_copied=bytes(mu.mem_read(DATA_DST, DATA_LEN)) == rom[:DATA_LEN],
                  gap_overwritten=bytes(mu.mem_read(NEW_DST, 0x20)) == rom[DATA_LEN:],
                  next_region_kept=bytes(mu.mem_read(NEW_DST + 0x20, 0x20)) == b'\xA5' * 0x20)
    if not all(result.values()):
        raise SystemExit('copy loop result differs: %s' % result)
    return dict(passed=True, **result)


def check_entry(image):
    src, dst, length, handler = struct.unpack_from('<4I', image, ENTRY - BASE)
    if handler != ZERO_HANDLER or (dst, length) != (NEW_DST, NEW_LEN):
        raise SystemExit('edited entry is not the expected zero-init entry')
    if dst != COPY_END or dst + length != OLD_DST + OLD_LEN:
        raise SystemExit('edited entry does not span exactly the gap plus the old region')
    return dict(source=hex(src), destination=hex(dst), length=hex(length), handler=hex(handler))


def run_zero_handler(image):
    """Run the firmware's zero routine on the edited range and check both edges."""
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB
    from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_SP
    mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    code = BASE
    mu.mem_map(code, 0x1000)
    mu.mem_write(code, image[:0x1000])
    ram = NEW_DST & ~0xFFFFF
    size = ((NEW_DST + NEW_LEN + 0x100000) & ~0xFFF) - ram
    mu.mem_map(ram, size)
    mu.mem_write(ram, b'\xA5' * size)
    stack, stop = 0x50000000, 0x50001000
    mu.mem_map(stack, 0x2000)
    mu.reg_write(UC_ARM_REG_SP, stack + 0x800)
    mu.reg_write(UC_ARM_REG_LR, stop | 1)
    src = struct.unpack_from('<I', image, ENTRY - BASE)[0]
    mu.reg_write(UC_ARM_REG_R0, src)
    mu.reg_write(UC_ARM_REG_R1, NEW_DST)
    mu.reg_write(UC_ARM_REG_R2, NEW_LEN)
    mu.emu_start(ZERO_HANDLER | 1, stop)
    before = bytes(mu.mem_read(NEW_DST - 0x20, 0x20))
    gap = bytes(mu.mem_read(NEW_DST, 0x40))
    last = bytes(mu.mem_read(NEW_DST + NEW_LEN - 0x20, 0x20))
    after = bytes(mu.mem_read(NEW_DST + NEW_LEN, 0x20))
    result = dict(preceding_data_kept=before == b'\xA5' * 0x20, anchor_and_next_zero=gap == b'\0' * 0x40,
                  region_end_zero=last == b'\0' * 0x20, following_data_kept=after == b'\xA5' * 0x20)
    if not all(result.values()):
        raise SystemExit('zero routine result differs: %s' % result)
    return dict(passed=True, **result)


def diff_spans(old, new):
    if len(old) != len(new):
        raise SystemExit('size changed')
    spans, i = [], 0
    while i < len(old):
        if old[i] != new[i]:
            j = i
            while j < len(old) and old[j] != new[j]:
                j += 1
            spans.append((i, j))
            i = j
        else:
            i += 1
    return spans


def build(package, out):
    report_path = out.with_name(out.name + '.json')
    for p in (out, report_path):
        if p.exists():
            raise SystemExit(f'{p} exists; refusing to overwrite')
    _, mods = container.load(package)
    main = container.main_module(mods)
    dd.checks(main.data)
    base = [stage0_multi.feature('c4')]
    probe, _ = dd.feature(main.data, 0x12120668)
    trial = engine.apply(main.data, base + [probe], BASE, expected_sha256=z5.MODULE_MAIN_SHA256)
    at = int([a for a in trial.report['appends'] if a['name'] == 'dual_gates'][0]['address'], 16)
    final, _ = dd.feature(main.data, at)
    group28 = engine.apply(main.data, base + [final], BASE, expected_sha256=z5.MODULE_MAIN_SHA256)
    if group28.report['output']['sha256'] != GROUP28_SHA256:
        raise SystemExit('the chain no longer rebuilds to the group 28 firmware')
    group33 = engine.apply(main.data, base + [final, zero_feature(warm=False)], BASE, expected_sha256=z5.MODULE_MAIN_SHA256)
    if group33.report['output']['sha256'] != GROUP33_SHA256:
        raise SystemExit('the chain no longer rebuilds to the group 33 firmware')
    result = engine.apply(main.data, base + [final, zero_feature()], BASE, expected_sha256=z5.MODULE_MAIN_SHA256)
    spans = diff_spans(group28.output, result.output)
    allowed = ((ENTRY + 4 - BASE, ENTRY + 12 - BASE), (WARM_COPY_LEN - BASE, WARM_COPY_LEN + 4 - BASE))
    outside = [s for s in spans if not any(a <= s[0] and s[1] <= b for a, b in allowed)]
    if len(outside) > 1 or any(s[0] < len(result.output) - 8 for s in outside):
        raise SystemExit('bytes changed outside the two edits and the CRC: %s' % outside)
    if struct.unpack_from('<I', result.output, WARM_COPY_LEN - BASE)[0] != DATA_LEN + 0x20:
        raise SystemExit('second start-up copy length was not edited')
    report = dict(package=dict(name=package.name, sha256=hashlib.sha256(package.read_bytes()).hexdigest()),
                  module=dict(name=main.name, size=main.length, sha256=main.sha256),
                  variant='c4+dual_display_gates+zero_sdk_anchor', group28_sha256=GROUP28_SHA256,
                  group33_sha256=GROUP33_SHA256,
                  anchor_edits=[dict(address=hex(ENTRY + 4), original=struct.pack('<2I', OLD_DST, OLD_LEN).hex(),
                              replacement=struct.pack('<2I', NEW_DST, NEW_LEN).hex()),
                         dict(address=hex(WARM_COPY_LEN), original=struct.pack('<I', DATA_LEN).hex(),
                              replacement=struct.pack('<I', DATA_LEN + 0x20).hex())],
                  changed_spans_vs_group28=[[hex(a), hex(b)] for a, b in spans],
                  entry=check_entry(result.output), zero_routine_verification=run_zero_handler(result.output),
                  warm_path=check_warm_path(result.output), warm_copy_verification=run_warm_copy(result.output),
                  camera_verified=False, **result.report)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(result.output)
    report_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'wrote {out} ({len(result.output)} bytes, sha256 {report["output"]["sha256"]})')
    print(json.dumps({k: report[k] for k in ('anchor_edits', 'changed_spans_vs_group28', 'entry', 'zero_routine_verification',
                                             'warm_path', 'warm_copy_verification')}, indent=1))


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[2] == '--out':
        build(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[3]))
    else:
        sys.exit(__doc__)
