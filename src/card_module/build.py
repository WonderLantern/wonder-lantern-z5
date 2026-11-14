#!/usr/bin/env python3
"""Build the L21 custom DISP card module with checked native drawing entry points.

Fixed target: original detailed-information LCD/EVF page, 60 Hz refresh requests.
No firmware changes, flashing, dynamic tests, or full-screen output buffers.
"""
from __future__ import annotations
import argparse
import binascii
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.dont_write_bytecode = True


def sha(data): return hashlib.sha256(data).hexdigest()


def fnv(data):
    h = 2166136261
    for byte in data: h = ((h ^ byte) * 16777619) & 0xffffffff
    return h


def retained_branch_checks(elf):
    """Audit real objdump instructions, including linker-generated branches.

    The pinned checker does not recognize Capstone's immediate '#' spelling
    on all direct branches. Keep its other checks and add an independent pass.
    """
    table = subprocess.check_output(['arm-none-eabi-nm', '-n', str(elf)], text=True)
    symbols = {}
    for line in table.splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = int(fields[0], 16) & ~1
    images = [(name[:-4], symbols[name[:-4]], end) for name, end in symbols.items()
              if name.endswith('_end') and name[:-4] in symbols and end > symbols[name[:-4]]]
    counts = {name: 0 for name, _, _ in images}
    listing = subprocess.check_output(['arm-none-eabi-objdump', '-d', str(elf)], text=True)
    for line in listing.splitlines():
        match = re.match(r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{4,8}\s+)+([a-z][a-z0-9.]*)\s+(.*)', line)
        if not match:
            continue
        address, mnemonic, operands = int(match[1], 16), match[2], match[3]
        if not re.fullmatch(r'b(?:l|lx|x)?(?:eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)?(?:\.[nw])?', mnemonic):
            continue
        target = re.match(r'^(?:#)?(?:0x)?([0-9a-f]+)(?:\s|$)', operands)
        if not target:
            continue  # Native service calls use checked register-indirect targets.
        destination = int(target[1], 16) & ~1
        for name, start, end in images:
            if start <= address < end:
                if not start <= destination < end:
                    raise ValueError('retained branch leaves image: %s at 0x%X -> 0x%X' %
                                     (name, address, destination))
                counts[name] += 1
    return [dict(name=name, size=end-start, direct_branches=counts[name])
            for name, start, end in images]


def target_header(image):
    """Generate checks and bindings for the single supported custom DISP path."""
    profile = json.loads((HERE / 'custom_disp/platform.json').read_text(encoding='ascii'))
    base = profile['base']
    def read(address, length):
        offset = address - base
        if offset < 0 or offset + length > len(image) - 2: raise ValueError('native check outside firmware')
        return image[offset:offset + length]
    actual = struct.unpack('<5I', read(profile['callbacks_image'], 20))
    if tuple(profile['callbacks']) != actual: raise ValueError('native page callbacks differ')
    probes = []
    for address, length in profile['probe_windows']:
        for offset in range(0, length, 2):
            value = int.from_bytes(read(address + offset, 2), 'little')
            probes.append('(*(volatile const u16 *)0x%Xu == 0x%Xu)' % (address + offset, value))
    functions = []; checks = []
    windows = list(profile['function_windows'])
    # The callback-entry stack layout is trusted only with this exact native chain.
    windows += [(0x10446EC0, 14), (0x103287A8, 78),
                (0x10448B20, 30), (0x102BEE12, 96)]
    # Additional native dependencies of the supplemental histogram read and scale calculation.
    windows += [(0x1034B8EC, 140), (0x1042E190, 42), (0x1042E1C8, 40),
                (0x10368070, 120), (0x103682D8, 4),
                (0x10456098, 4), (0x10456B34, 4), (0x1022CF40, 4),
                (0x1016364E, 4), (0x1023A964, 20), (0x1023AAF8, 4)]
    # Audit the RAM k-table binding and original gate on both accepted images.
    windows += [(0x1042C680, 18), (0x1021BD0A, 20), (0x1030D778, 44)]
    ram = profile['ram_init']
    k_initial = read(ram['source'] + 0xC18E74B0 - ram['destination'], 16 * 8)
    if k_initial != struct.pack('<2I', 1, 0) * 16:
        raise ValueError('native k-table initialization differs')
    native = profile['overlay_source']
    def literal_word(address):
        opcode = int.from_bytes(read(address, 2), 'little')
        if opcode & 0xf800 != 0x4800:
            raise ValueError('native GUI gate literal opcode differs')
        at = ((address + 4) & ~3) + (opcode & 255) * 4
        return at, int.from_bytes(read(at, 4), 'little')
    gate_literal, gate_base = literal_word(native['functions']['gui_lock']['entry'] + 2)
    walk_literal, walk_base = literal_word(native['functions']['gui_walk']['entry'] + 22)
    if (native['gui_mode'] != gate_base + 0x30 or
            native['gui_lock'] != gate_base + 0x34 or walk_base != gate_base):
        raise ValueError('native GUI gate binding differs')
    ui_literal, ui_address = literal_word(native['functions']['ui_mode']['entry'])
    if native['ui_mode'] != ui_address:
        raise ValueError('native UI mode binding differs')
    gate_literals = [(gate_literal, gate_base), (walk_literal, walk_base),
                     (ui_literal, ui_address)]
    for item in profile['overlay_source']['functions'].values():
        actual = read(item['entry'], item['length'])
        if sha(actual) != item['sha256']:
            raise ValueError('native source function digest differs')
        windows.append((item['entry'], item['length']))
    for address, length in windows:
        digest = fnv(read(address, length))
        checks.append(dict(address=address, length=length, fnv1a=digest))
        functions.append('{ u32 h = 2166136261u; for (u32 j = 0; j < %du; j++) '
                         'h = (h ^ ((volatile const u8 *)0x%Xu)[j]) * 16777619u; '
                         'if (h != 0x%Xu) return 0; }' % (length, address, digest))
    literals = []
    for address, expected in [*profile['literals'], *gate_literals, (0x1021BD8C, 0xC18E6BAC), (0x1021BDB0, 0xC18E74B0)]:
        if int.from_bytes(read(address, 4), 'little') != expected: raise ValueError('native literal differs')
        literals.append('(*(volatile const u32 *)0x%Xu == 0x%Xu)' % (address, expected))
    dispatch = profile['output_dispatch']
    lcd = int.from_bytes(read(dispatch['lcd_literal'], 4), 'little')
    evf = int.from_bytes(read(dispatch['alternate_literal'], 4), 'little')
    hdmi = evf - dispatch['hdmi_offset']
    if (lcd != profile['runtime']['canvas'] or evf != profile['runtime']['evf_canvas'] or
            hdmi != dispatch['hdmi_canvas']):
        raise ValueError('output canvas profile differs from native dispatcher literals')
    header = (HERE / 'custom_disp/target.h.in').read_text(encoding='ascii')
    for key, value in dict(PROBE='(' + ' && '.join(probes) + ')',
                           FUNCTIONS='do { ' + ' '.join(functions) + ' } while (0)',
                           LITERALS='(' + ' && '.join(literals) + ')',
                           LCD_CANVAS='0x%Xu' % lcd, EVF_CANVAS='0x%Xu' % evf,
                           PRESENT_LOCK='0x%Xu' % profile['overlay_source']['gui_lock'],
                           UI_MODE_GETTER='0x%X' % profile['overlay_source']['functions']['ui_mode']['entry'],
                           PRESENT_MODE='0x%Xu' % profile['overlay_source']['gui_mode'],
                           PRESENT_MAP='0x%Xu' % profile['presentation']['output_map'],
                           PRESENT_POOL='0x%Xu' % profile['presentation']['descriptor_pool']).items():
        if header.count('@' + key + '@') != 1: raise ValueError('target template marker differs')
        header = header.replace('@' + key + '@', value)
    if re.search(r'@[A-Z_]+@', header):
        raise ValueError('unexpanded target template marker')
    return header, checks


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sdk', type=Path, required=True, help='patched z5mod SDK')
    ap.add_argument('--firmware', type=Path, required=True, help='repository group-108 MAIN (read only)')
    ap.add_argument('--legacy-firmware', type=Path, help='optional legacy L3/L6 MAIN; include to reproduce the dual-compatible release')
    ap.add_argument('-o', '--output', type=Path, required=True, help='Z5APP.BIN in a new directory')
    args = ap.parse_args()
    if args.output.name != 'Z5APP.BIN':
        ap.error('output must be named Z5APP.BIN')
    out = args.output.parent.resolve()
    if out.exists():
        ap.error('output directory exists; refusing to overwrite')
    compatibility = json.loads((HERE / 'custom_disp/compatibility.json').read_text())
    manifest = json.loads((ROOT / 'patch_data/patches.json').read_text())
    image = args.firmware.read_bytes()
    if sha(image) != compatibility['firmware_sha256'] or binascii.crc_hqx(image, 0):
        ap.error('requires the exact group-108 MAIN with valid CRC')
    if manifest['output_sha256'] != compatibility['firmware_sha256']:
        ap.error('repository firmware version changed; re-audit the port')
    sdk = args.sdk.resolve()
    sdk_hashes = json.loads((HERE / 'custom_disp/sdk_sources.json').read_text())
    for name, digest in sdk_hashes.items():
        if sha((sdk / name).read_bytes()) != digest:
            ap.error('pinned patched SDK differs: ' + name)
    header, checks = target_header(image)
    legacy_checks = []
    accepted_hashes = [sha(image)]
    if args.legacy_firmware:
        legacy = args.legacy_firmware.read_bytes()
        if sha(legacy) != compatibility['legacy_firmware_sha256'] or binascii.crc_hqx(legacy, 0):
            ap.error('requires the exact legacy L3/L6 MAIN with valid CRC')
        legacy_header, legacy_checks = target_header(legacy)
        names = ('Z5_PROBE_TARGET_MATCH', 'Z5_OVERLAY_CHECK_FUNCTIONS', 'Z5_OVERLAY_CHECK_LITERALS')
        def get_macro(content, name):
            return next(line for line in content.splitlines() if line.startswith('#define ' + name + '() '))
        stripped = header
        stripped_legacy = legacy_header
        for name in names:
            current_line = get_macro(header, name)
            legacy_line = get_macro(legacy_header, name)
            stripped = stripped.replace(current_line, '')
            stripped_legacy = stripped_legacy.replace(legacy_line, '')
            current_body = current_line.split('() ', 1)[1]
            legacy_body = legacy_line.split('() ', 1)[1]
            if name == 'Z5_OVERLAY_CHECK_FUNCTIONS':
                body = ('do { if (*(volatile const u32 *)0x103482A8u == 0x121206A5u) { ' +
                        current_body + '; } else { ' + legacy_body + '; } } while (0)')
            else:
                body = '(' + current_body + ' || ' + legacy_body + ')'
            header = header.replace(current_line, '#define ' + name + '() ' + body)
        if stripped != stripped_legacy:
            ap.error('legacy/current native declarations differ outside the fingerprint checks')
        accepted_hashes.append(sha(legacy))
    profile = json.loads((HERE / 'custom_disp/platform.json').read_text())
    out.mkdir(parents=True)
    (out / 'runtime-profile.json').write_text(json.dumps(profile, indent=2) + '\n')
    (out / 'custom_disp_target.h').write_text(header, encoding='ascii')
    selected = out / 'sdk-src'
    selected.mkdir()
    for name in ('start.S', 'imgarm.S', 'state.c', 'str.c', 'ui.c', 'task.c',
                 'image.c', 'fs.c', 'audio.c', 'audio_arm.c'):
        (selected / name).write_bytes((sdk / 'src' / name).read_bytes())
    content = (HERE / 'custom_disp/display.c').read_text().replace('#include "../custom_disp.h"', '#include "custom_disp.h"')
    (selected / 'custom_disp.c').write_text(content, encoding='ascii')
    spec = importlib.util.spec_from_file_location('current_z5build', sdk / 'tools/z5build.py')
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    builder.SRC = selected
    builder.CFLAGS += ['-std=gnu11', '-Werror', '-Wno-error=unused-but-set-variable',
                       '-I', str(out), '-I', str(HERE / 'custom_disp'),
                       '-I', str(HERE), '-include', str(HERE / 'custom_disp/sdk_compat.h'),
                       '-Wl,--no-fix-cortex-a8']
    # Keep the production group-119 diagnostic recorder and save-record menu.
    builder.build(HERE / 'main.c', args.output, defs=['-DVUI_DIAG_FOCUS'])
    elf = out / 'obj/main.elf'
    subprocess.run([sys.executable, str(sdk / 'tools/imgcheck.py'), str(elf)], check=True)
    branches = retained_branch_checks(elf)
    subprocess.run([sys.executable, str(sdk / 'tools/z5pack.py'), 'check', str(args.output)], check=True)

    # Compile-time layout checks: the firmware reads vui_fn at +4, and the
    # SDK's persistent block must also fit the unchanged audio allocations.
    layout_source = out / 'layout.c'
    layout_source.write_text('#include <stddef.h>\n#include "' + str(HERE / 'main.c') + '"\n'
        '_Static_assert(offsetof(cm_t, vui_fn) == 4, "portrait entry ABI");\n'
        '_Static_assert(((sizeof(cm_t)+7)&~7) + Z5_STATE_RESERVED + '
        '0x60 + 0x40 + 2*6*0x60 <= Z5_STATE_SIZE, "SDK state capacity");\n'
        'const u32 port_layout[] __attribute__((used,section(".port_layout"))) = '
        '{sizeof(cm_t), offsetof(cm_t,vui_fn), offsetof(cm_t,custom_disp), Z5_STATE_SIZE};\n')
    subprocess.run([builder.CC, *builder.CFLAGS, '-DVUI_DIAG_FOCUS', '-c', str(layout_source),
                    '-o', str(out / 'layout.o')], check=True)
    subprocess.run([builder.OBJCOPY, '-O', 'binary', '-j', '.port_layout',
                    str(out / 'layout.o'), str(out / 'layout.bin')], check=True)
    layout = dict(zip(('cm_size', 'vui_fn_offset', 'custom_disp_offset', 'sdk_state_size'),
                      struct.unpack('<4I', (out / 'layout.bin').read_bytes())))
    inputs = [HERE / 'main.c', HERE / 'vui_card.inc', HERE / 'custom_disp.h', Path(__file__).resolve(),
              *(HERE / 'custom_disp').glob('*'), HERE / 'custom_disp_runtime.inc', HERE / 'custom_disp_install.inc', HERE / 'z5mod-sdk.patch']
    report = dict(camera_verified=False, dynamic_tests_run=False,
        firmware_modified=False, compatibility=compatibility, firmware_sha256=sha(image),
        source_cleanup=dict(module="custom_disp", single_rendering_path=True,
            removed_paths=["layer_cache", "lcd_clock", "lcd_async", "native_overlay", "rectangle_level", "source_rebinding", "independent_evf_surface_fallback", "legacy_force3_restore", "unread_evf_window_metrics"],
            removed_unused_stencil_bytes=124944, removed_evf_metadata_words=6, metadata_words=31, state_revision="0x112D000A"),
        compatible_firmware_sha256=accepted_hashes,
        lcd_walk_policy='skip checked first walk; paint second and conditional later walks',
        lcd_walk_identity='callback-entry SP +28 wrapper LR, +44 controller LR; exact native code checks',
        lcd_final_submission_hook=False,
        lcd_diagnostics='L21: same R/U/C/F W/D/K/N metrics on last photo output (L21L/L21E); Q also reports page/activation refusal; H maximum independent-worker statistics-read elapsed ms since boot (hex, saturated FFF); S worker result (4 acquiring,5 failure,6 published)',
        output_sha256=sha(args.output.read_bytes()), output_size=args.output.stat().st_size,
        custom_disp_settings=dict(menu='Extensions > DISP2 Custom', flags=['enabled','level','histogram','detailed_info'], default_flags=15, persistence='MENU.DAT optional tagged third word', visibility_groups=list(range(64)), native_base_when_info_off="DISP1 simple-page mask projected to DISP0 per mode nibble", projected_disp_mask="0x111111", outputs=["LCD","EVF"]),
        target_disp=0, lcd_native_ui='original-detailed-page',
        native_refresh_override=dict(k=1, minimum_interval_ms=0, table='0xC18E74B0', entries=16, stride=8, scope='active detailed photo page, LCD or EVF, normal UI', owner='APPLV page wrapper; override for native callback only, restore immediately on return', restore='compare-and-restore owned entries; preserve paired word and changed native values', saved_state_bytes=68),
        lcd_level_target_hz=60, evf_level_target_hz=60, lcd_request_intervals_ms=[16, 17],
        evf_request_intervals_ms=[16, 17],
        unified_output_pipeline=dict(entry='overlay_source_paint', first_walk='skip on LCD and EVF',
            prepare='restore sparse pixels before native child painters on either output',
            tail='GPU sync, revalidate active output/bank/descriptor, cached histogram, level, sparse publication',
            output_states=2, dirty_row_limit=960, fullscreen_cache_maintenance=False,
            native_descriptor_masks=[1], evf_source='native current buffer, size and mapping; no LCD page-object/descriptor constraints', native_descriptor_map='0xC18100AC',
            histogram_acquisition_interval_ms=120),
        lcd_fullscreen_allocation_bytes=0, lcd_sparse_save_under=True,
        lcd_occlusion='native premultiplied RGBA source-over level',
        lcd_visibility_mask_rules=0, evf_path='shared 60Hz overlay pipeline and sparse cache publication; native EVF size/origin adapter',
        production_vui_source_unchanged=False, production_diagnostic_recorder=True,
        own_histogram=dict(landscape_disp_pages=[0], portrait_disp_pages=[0, 2, 3],
            landscape_requires_portrait_switch=False,
            portrait_position_follows_portrait_switch=True,
            detailed_page_callback='retained cm callback: prepare before GPU sync; cached CPU raster after GPU sync; all supported orientations',
            landscape_box_top_down=[476, 278, 192, 96], landscape_throttle_ms=120,
            detailed_portrait_box_top_down=[144, 480, 192, 96],
            sampling='shared disp_worker/disp_collect, 120ms attempt-start gate and 20ms poll; native 0x10456B34 only on independent worker',
            sampling_failure='keep last published private statistics packet',
            evf_histogram_box_size=[192, 96], evf_histogram_origin='checked native EVF top-left',
            shared_raster='custom_disp/histogram_raster.h', statistics_packets=3, statistics_packet_bytes=1056, statistics_total_bytes=3168,
            gui_statistics_acquisition=False, raster='CPU batch, native EVF palette and bin aggregation, scaled into 192x96',
            raster_publication='merged into the existing level sparse cache ranges',
            lcd_histogram_uses_native_bins_cache=False,
            gui_updates='on new completed packet or dirty/bank/geometry repair; unchanged packet safety repair at 150ms',
            dirty_repair_bypasses_sampling_throttle=False, dirty_repair_bypasses_draw_throttle=True,
            landscape_stock_pages_untouched=[2, 3], portrait_throttle_ms=120,
            full_screen_cache=False, full_repaint_on_bank_change=True),
        build_defines=['VUI_DIAG_FOCUS'], layout=layout,
        native_function_checks=checks, legacy_native_function_checks=legacy_checks,
        retained_branch_checks=branches, sdk_hashes=sdk_hashes,
        source_hashes={p.relative_to(ROOT).as_posix(): sha(p.read_bytes()) for p in sorted(inputs) if p.is_file()},
        compiler_version=subprocess.check_output([builder.CC, '-dumpfullversion'], text=True).strip(),
        limitations='Build and static checks only. Camera timing, portrait layout and wake need hardware verification.')
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Firmware-unchanged L21 module sha256:', report['output_sha256'])


if __name__ == '__main__':
    main()
