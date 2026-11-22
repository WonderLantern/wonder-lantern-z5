#!/usr/bin/env python3
"""Build the A39 timed Bulb and custom DISP card module with checked native drawing entry points.

Fixed target: four native photo DISP pages, LCD/EVF, upstream 60 Hz refresh requests (16/17ms).
No firmware changes, flashing, dynamic tests, or full-screen output buffers.
"""
from __future__ import annotations
import argparse
import binascii
import hashlib
import importlib.util
import json
import os
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
    histogram_descriptor = struct.unpack('<I', read(0x10748504 + 32 * 4, 4))[0]
    if histogram_descriptor != 0xC1826114:
        raise ValueError('native histogram visibility descriptor differs')
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
    # Native level footprint erase and snapshot invalidation for cold DISP3
    # takeover. Check the complete erase path rather than only its veneer.
    windows += [(0x1032C2C2, 40), (0x104B9880, 84), (0x104B90DC, 70),
                (0x104B9712, 366), (0x103422AE, 48),
                (0x1032C344, 4), (0x104B9A88, 20), (0x103426A8, 16)]
    # Exact-transfer settings reads and existing-file overwrite/seek dependencies.
    # FW_READAT returns 1 only after seek, full read and copy; include its literals.
    windows += [(0x1020B0DE, 104), (0x1020B4E4, 8),
                (0x102CB190, 46), (0x1033A9AE, 28), (0x1033A9A0, 14),
                (0x1033A82A, 4), (0x1033A82E, 72), (0x1033A8BA, 60),
                (0x102CB162, 46), (0x1033A796, 34), (0x102CB15E, 4)]
    # Logical-page takeover: RAM normal-key callback, exact native outer/inner
    # dispatch, DISP eligibility, page setter and display-off transition.
    windows += [(0x10304108, 152), (0x10303E4C, 460),
                (0x10341A60, 238), (0x1034B872, 122), (0x1042DE0A, 10),
                (0x10311F40, 18), (0x1031239E + 4, 12), (0x10388280, 16),
                (0x103882C8, 24), (0x104560A4, 4),
                (0x10368156, 4), (0x10307184, 4), (0x10307236, 4),
                (0x10307250, 4), (0x102F5F56, 4), (0x102F5F46, 4),
                (0x103489E8, 4), (0x1042D5FA, 4), (0x1042C7A4, 4),
                (0x104555AA, 4), (0x10455712, 4), (0x10455570, 4)]
    ram = profile['ram_init']
    wb_descriptor = struct.unpack('<I', read(0x10748504 + 38 * 4, 4))[0]
    wb_init = ram['source'] + wb_descriptor - ram['destination']
    if wb_descriptor != 0xC1826534 or struct.unpack('<I', read(wb_init + 44, 4))[0] != 0xC1826474:
        raise ValueError('native white-balance descriptor differs')
    # Group 38 uses the mode/fine-adjust icon table in this native updater.
    windows += [(0x10748504 + 38 * 4, 4), (wb_init, 48), (0x1032D3BE, 68)]
    key_init = ram['source'] + 0xC18289E0 - ram['destination']
    if int.from_bytes(read(key_init + 0x14, 4), 'little') != 0x10304109:
        raise ValueError('native normal-key RAM callback initialization differs')
    windows += [(key_init, 44)]
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
    if manifest['output_sha256'] not in (compatibility['firmware_sha256'], *compatibility.get('release_firmware_sha256', {})):
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
    # Fixed A39 profile: A19 list-mode Bulb, with the group-119 recorder.
    definitions = ['-DVUI_DIAG_FOCUS', '-DBULB_MENU'] + os.environ.get('Z5_EXTRA', '').split()   # Z5_EXTRA: test builds (FILM)
    builder.build(HERE / 'main.c', args.output, defs=definitions)
    elf = out / 'obj/main.elf'
    subprocess.run([sys.executable, str(sdk / 'tools/imgcheck.py'), str(elf)], check=True)
    branches = retained_branch_checks(elf)
    subprocess.run([sys.executable, str(sdk / 'tools/z5pack.py'), 'check', str(args.output)], check=True)

    # Compile-time layout checks: the firmware reads vui_fn at +4, and the
    # SDK's persistent block must also fit the unchanged audio allocations.
    layout_source = out / 'layout.c'
    layout_source.write_text('#include <stddef.h>\n#include "' + str(HERE / 'main.c') + '"\n'
        '_Static_assert(offsetof(cm_t,custom_control) == offsetof(cm_t,custom_flags)+4, "custom settings pair");\n'
        '_Static_assert(offsetof(cm_t, vui_fn) == 4, "portrait entry ABI");\n'
        '_Static_assert(((sizeof(cm_t)+7)&~7) + Z5_STATE_RESERVED + '
        '0x60 + 0x40 + 2*6*0x60 <= Z5_STATE_SIZE, "SDK state capacity");\n'
        'const u32 port_layout[] __attribute__((used,section(".port_layout"))) = '
        '{sizeof(cm_t), offsetof(cm_t,vui_fn), offsetof(cm_t,custom_disp), Z5_STATE_SIZE, sizeof(custom_disp_state)};\n')
    subprocess.run([builder.CC, *builder.CFLAGS, *definitions, '-c', str(layout_source),
                    '-o', str(out / 'layout.o')], check=True)
    subprocess.run([builder.OBJCOPY, '-O', 'binary', '-j', '.port_layout',
                    str(out / 'layout.o'), str(out / 'layout.bin')], check=True)
    layout = dict(zip(('cm_size', 'vui_fn_offset', 'custom_disp_offset', 'sdk_state_size', 'custom_menu_state_size'),
                      struct.unpack('<5I', (out / 'layout.bin').read_bytes())))
    inputs = [HERE / 'main.c', HERE / 'vui_card.inc', HERE / 'custom_disp.h', Path(__file__).resolve(),
              *(HERE / 'custom_disp').glob('*'), HERE / 'custom_disp_runtime.inc', HERE / 'custom_disp_install.inc', HERE / 'burst_speed.inc', HERE / 'burst_speed_install.inc', HERE / 'z5mod-sdk.patch']
    report = dict(camera_verified=False, dynamic_tests_run=False,
        firmware_modified=False, upstream_commit='b8ffd1c', release_firmware_sha256=manifest['output_sha256'], variant="A39-A19-list-bulb-L39-custom-disp", compiler_definitions=definitions, compatibility=compatibility, firmware_sha256=sha(image),
        source_cleanup=dict(module="custom_disp", single_rendering_path=True,
            removed_paths=["layer_cache", "lcd_clock", "lcd_async", "native_overlay", "rectangle_level", "source_rebinding", "independent_evf_surface_fallback", "legacy_force3_restore", "unread_evf_window_metrics"],
            removed_unused_stencil_bytes=124944, removed_evf_metadata_words=6, metadata_words=32, state_revision="0x112D0018"),
        compatible_firmware_sha256=accepted_hashes,
        lcd_walk_policy='skip checked first walk; paint second and conditional later walks',
        lcd_walk_identity='callback-entry SP +28 wrapper LR, +44 controller LR; exact native code checks',
        lcd_final_submission_hook=False,
        lcd_diagnostics='L39: same R/U/C/F W/D/K/N metrics on last photo output (L39L/L39E); Q also reports page/activation refusal; H maximum independent-worker statistics-read elapsed ms since boot (hex, saturated FFF); S worker result (4 acquiring,5 failure,6 published)',
        output_sha256=sha(args.output.read_bytes()), output_size=args.output.stat().st_size,
        custom_disp_settings=dict(menu='Extensions > Custom DISP > DISP0..DISP3', menu_text_slots=[266,267,268,269,270], setup_entry_text_slot=250, flags=['reserved_enable','typea_level','histogram','detailed_info','native_level'], master_switch=True, logical_pages=True, remembered_pages_per_output=True, native_base=0, default_flags=562223, page_count=4, bits_per_page=5, page_names=["detailed", "simple", "histogram", "level"], level_choices=['off','native','TypeA'], native_level=dict(enabled=True, outputs=['LCD','EVF'], policy='All logical pages use native DISP0; scoped native pose/setup once per APPLV callback; Off/TypeA disarms all four dirty-only children; both leading guards installed outside traversal', pose='0x104BA04C', show='0x1032C290', parent_rebinding=True, mode_writes=True, native_callback_replacement=False, bitmap_cache=False, native_disp_identity_unchanged=False), persistence='20-byte MENU.DAT: D8 preferences and burst bits, B7 A19 timer, CD50 switch and page memory; accepts 8/12-byte legacy and 16-byte A19/L39', visibility_groups=list(range(64)), native_base_when_info_off="DISP1 masks projected to native DISP0, additionally hide white-balance group38; DISP0 masks when details on", projected_disp_mask="native DISP0 bit 0x111111 only; choose preferences by logical page", outputs=["LCD","EVF"]),
        logical_disp_routing=dict(key_slot='0xC18289F4', original_key='0x10304109',
            entry='retained cm custom_key_dispatch', native_input_prefix_and_suffix=True,
            native_disp_eligibility='0x10341AAA once; transient native simple selection remapped before GUI',
            native_base=0, lcd_cycle=[0,1,2,3,4], evf_cycle=[0,1,2,3],
            disp4='native display-off 0x1042DE0A on LCD; EVF skips it',
            persistence='verified MENU.DAT write on accepted logical change; no per-frame or shutdown-only save',
            native_non_disp_page_change='adopt into logical memory; native wake is not forced back off',
            disabled='compare owned values and transfer logical page back; restore RAM key callback'),
        settings_io=dict(format='8/12-byte legacy, 16-byte A19 or L39, or 20-byte A39; validate exact size, tags, timer and routing values',
            transactional_read=True, failed_read_preserves_ram=True, gui_retry_interval_ms=500,
            retry_scope='APPLV GUI before callback after boot setup; stop IO on successful load; menu entry/mutations also retry',
            unread_settings_mutations_blocked=True, boot_writes=False,
            write='existing-file 0x0A, seek 0, write 20 bytes, close, reopen and compare all words; no create/truncate',
            save_success='full write plus matching reread; physical power-loss durability requires camera validation',
            seek_entry='0x102CB190', readat_entry='0x1020B0DE', hardware_verified=False),
        target_disp=[0,1,2,3], lcd_native_ui='per-page detailed/simple projection',
        native_refresh_override=dict(k=1, minimum_interval_ms=0, table='0xC18E74B0', entries=16, stride=8, scope='enabled native photo DISP page, LCD or EVF, normal UI', owner='APPLV page wrapper; override for native callback only, restore immediately on return', restore='compare-and-restore owned entries; preserve paired word and changed native values', saved_state_bytes=68),
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
        own_histogram=dict(landscape_disp_pages=[0,1,2,3], portrait_disp_pages=[0,1,2,3],
            landscape_requires_portrait_switch=False,
            portrait_position_follows_portrait_switch=True,
            detailed_page_callback='retained cm callback: prepare before GPU sync; cached CPU raster after GPU sync; all supported orientations',
            landscape_box_top_down=[476, 278, 192, 96], landscape_throttle_ms=120,
            detailed_portrait_box_top_down=[144, 480, 192, 96],
            sampling='shared disp_worker/disp_collect, 120ms attempt-start gate and 20ms poll; custom native DISP0 avoids native DISP2 sampling branch',
            sampling_failure='keep last published private statistics packet',
            evf_histogram_box_size=[192, 96], evf_histogram_origin='checked native EVF top-left',
            shared_raster='custom_disp/histogram_raster.h', statistics_packets=3, statistics_packet_bytes=1056, statistics_total_bytes=3168,
            gui_statistics_acquisition=False, raster='CPU batch, native EVF palette and bin aggregation, scaled into 192x96',
            raster_publication='merged into the existing level sparse cache ranges',
            lcd_histogram_uses_native_bins_cache=False,
            gui_updates='on new completed packet or dirty/bank/geometry repair; unchanged packet safety repair at 150ms',
            dirty_repair_bypasses_sampling_throttle=False, dirty_repair_bypasses_draw_throttle=True,
            stock_pages_untouched_when_custom_disabled=True, portrait_throttle_ms=120,
            full_screen_cache=False, full_repaint_on_bank_change=True),
        build_defines=['VUI_DIAG_FOCUS'], layout=layout,
        native_function_checks=checks, legacy_native_function_checks=legacy_checks,
        retained_branch_checks=branches, sdk_hashes=sdk_hashes,
        source_hashes={p.relative_to(ROOT).as_posix(): sha(p.read_bytes()) for p in sorted(inputs) if p.is_file()},
        compiler_version=subprocess.check_output([builder.CC, '-dumpfullversion'], text=True).strip(),
        limitations='Build and static checks only. Camera timing, portrait layout and wake need hardware verification.')
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('A39 module sha256:', report['output_sha256'])


if __name__ == '__main__':
    main()
