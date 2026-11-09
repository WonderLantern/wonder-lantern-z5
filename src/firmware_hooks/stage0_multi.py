"""stage0_multi: stage0 + a power-up trigger on 17 widget draw classes (boot_multi.S).

    python research/autostart/stage0_multi.py assemble [--variant multi|c4]   # needs arm-none-eabi binutils
    python research/autostart/stage0_multi.py build <Z_5_0150.bin> --out <new dir>/EG165050.BIN [--variant multi|c4]

Variants: 'multi' = 17 classes (camera-verified 2026-11-07); 'c4' = classes 0 and 4 only.

`assemble` writes boot_multi.json (loader bytes + symbol addresses, linked at
0x12120248); `build` needs only that file. Every ROM edit is one draw-callback
literal, checked against its original bytes by the engine before it is replaced
with the address of that class's stub. camera_verified: false.
"""
import hashlib
import json
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'reference' / 'z5mod' / 'firmware' / 'src'))
sys.path.insert(0, str(HERE))

from z5mod_fw import container, engine  # noqa: E402
from z5mod_fw.engine import Append, Edit, Feature  # noqa: E402
from z5mod_fw.features import stage0  # noqa: E402
from z5mod_fw.platform import z5_150 as z5  # noqa: E402
import stage0_boot  # noqa: E402

LOADER_AT = 0x12120248
VARIANTS = {'multi': None, 'c4': (0, 4)}           # None: every class in CLASSES

# (class index, original callback, literal address) - Ghidra census of callbacks
# stored at widget +0x4C; 0x103AA8D1 (index 8) left out: its value occurs 6 times.
CLASSES = [
    (0, 0x104B1F59, 0x104B2224), (1, 0x102BDA9D, 0x102BDE34), (2, 0x102BDCE1, 0x102BDE3C),
    (3, 0x102BE7BB, 0x102BED58), (4, 0x103864E1, 0x10386D20), (5, 0x10386BD5, 0x10387428),
    (6, 0x10387179, 0x10387430), (7, 0x103873F9, 0x103876D4), (9, 0x103B400B, 0x1033DCEC),
    (10, 0x104B1B1B, 0x104B1C04), (11, 0x104B1C1D, 0x104B221C), (12, 0x104B2657, 0x104B2980),
    (13, 0x104B28C7, 0x104B2CC8), (14, 0x104B2B1D, 0x104B2CCC), (15, 0x10525C17, 0x10525C30),
    (16, 0x1056121F, 0x10561484), (17, 0x105613B3, 0x10561488),
]


def classes(variant):
    keep = VARIANTS[variant]
    return [c for c in CLASSES if keep is None or c[0] in keep]


def json_path(variant):
    return HERE / ('boot_%s.json' % variant)


def assemble(variant) -> None:
    lines = '\n'.join('    CLS %d, 0x%08X' % (i, o) for i, o, _ in classes(variant))
    src = (HERE / 'boot_multi.S').read_text(encoding='utf-8').replace('@@CLASSES@@', lines)
    with tempfile.TemporaryDirectory(dir=ROOT / 'output') as t:
        t = pathlib.Path(t)
        (t / 'boot_multi.S').write_text(src, encoding='utf-8')
        (t / 'code_patched.bin').write_bytes(stage0_boot.body())
        run = lambda *a: subprocess.run(a, cwd=t, check=True, capture_output=True, text=True).stdout
        run('arm-none-eabi-as', '-mcpu=cortex-a9', '-mthumb', '-o', 'm.o', 'boot_multi.S')
        run('arm-none-eabi-ld', '-Ttext=0x%08x' % LOADER_AT, '-e', 'entry', '-o', 'm.elf', 'm.o')
        run('arm-none-eabi-objcopy', '-O', 'binary', 'm.elf', 'm.bin')
        syms = {}
        for line in run('arm-none-eabi-nm', 'm.elf').splitlines():
            a, _, n = line.split()
            syms[n] = int(a, 16)
        data = (t / 'm.bin').read_bytes()
    json_path(variant).write_text(json.dumps(dict(variant=variant, at=hex(LOADER_AT), size=len(data), sha256=hashlib.sha256(data).hexdigest(),
                                    symbols={k: hex(v) for k, v in sorted(syms.items(), key=lambda x: x[1])},
                                    hex=data.hex()), indent=1) + '\n')
    print(f'wrote {json_path(variant).name}: {len(data)} bytes')


def loader(variant='multi'):
    j = json.loads(json_path(variant).read_text())
    data = bytes.fromhex(j['hex'])
    assert hashlib.sha256(data).hexdigest() == j['sha256'] and int(j['at'], 16) == LOADER_AT
    return data, {k: int(v, 16) for k, v in j['symbols'].items()}


def feature(variant='multi') -> Feature:
    data, sym = loader(variant)
    edits = []
    for idx, orig, lit in classes(variant):
        target = sym['entry'] if idx == 0 else sym['cls%d' % idx]
        edits.append(Edit('draw_literal_%d' % idx, lit - stage0.BASE, struct.pack('<I', orig),
                          struct.pack('<I', target | 1)))
    return Feature(name='stage0_' + variant,
                   description='stage0 + run the module once after power-up on the first draw of widget classes %s'
                               % [c[0] for c in classes(variant)],
                   camera_verified=False, edits=tuple(edits),
                   appends=(Append('loader', stage0.PATH + data, align=8),))


def build(package: pathlib.Path, out: pathlib.Path, variant='multi') -> None:
    report_path = out.with_name(out.name + '.json')
    for p in (out, report_path):
        if p.exists():
            raise SystemExit(f'{p} exists; refusing to overwrite')
    _, mods = container.load(package)
    main = container.main_module(mods)
    result = engine.apply(main.data, [feature(variant)], stage0.BASE, expected_sha256=z5.MODULE_MAIN_SHA256)
    ap = result.report['appends'][0]
    if int(ap['offset'], 16) + stage0.BASE + len(stage0.PATH) != LOADER_AT:
        raise SystemExit('loader did not land at 0x%08x: %s' % (LOADER_AT, ap))
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(result.output)
    report = dict(package=dict(name=package.name, sha256=hashlib.sha256(package.read_bytes()).hexdigest()),
                  module=dict(name=main.name, size=main.length, sha256=main.sha256),
                  variant=variant, loader_at=hex(LOADER_AT), camera_verified=False, **result.report)
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    print(f'wrote {out} ({len(result.output)} bytes, sha256 {report["output"]["sha256"]})')


if __name__ == '__main__':
    variant = 'multi'
    if '--variant' in sys.argv:
        i = sys.argv.index('--variant'); variant = sys.argv[i + 1]; del sys.argv[i:i + 2]
    if variant not in VARIANTS:
        sys.exit('variant must be one of %s' % list(VARIANTS))
    if sys.argv[1:2] == ['assemble']:
        assemble(variant)
    elif len(sys.argv) == 5 and sys.argv[1] == 'build' and sys.argv[3] == '--out':
        build(pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[4]), variant)
    else:
        sys.exit(__doc__)
