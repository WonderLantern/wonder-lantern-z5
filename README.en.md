# Z5 Extensions

[中文](README.md)

Feature extensions for the Nikon Z 5, firmware C 1.50. **This repository contains no Nikon firmware.** You supply the official update file, and `patch.py` verifies it and builds the update on your own computer.

This project is not affiliated with, authorised by, or supported by Nikon. **Flashing modified firmware can damage your camera. Use it at your own risk.** So far it has been tested on only one camera, the author's.

## Features

| Feature | Description | Camera tested |
| --- | --- | --- |
| Custom shutter sound | Plays a WAV from the card during silent photography, with up to 5 sounds and on/off, sound and volume settings. The mechanical shutter is unchanged | Yes |
| Simplified Chinese menu | "Shutter sound" page in the Setup menu | Yes |
| Histogram with level | Shows the histogram on the virtual horizon page | Yes (photo mode, rear monitor) |
| Framing grid styles | Adds 3×3 and 2×2 to Custom Setting d8. The stock "On" remains 4×4 | Yes (photo mode, rear monitor) |
| Photo highlight display (zebra) | Brings the stock movie highlight display to photo live view, with its own switch in the Setup menu | Yes (photo mode, rear monitor) |
| Standby wake fix | Keeps the features when the camera wakes immediately after the display turns off | Yes |
| Portrait UI (test) | Shooting display, i menu and setting pages turn upright when the camera is held vertically | Author's camera, still in testing |
| Portrait touch (test) | Touch follows the portrait layout, including strip dragging; can be turned off | Author: "mostly usable" |
| Focal length | Shown for every CPU lens (stock: power-zoom lenses only); can be turned off | Yes |
| Battery percent | Remaining charge shown in the battery icon; can be turned off | Yes |
| Extensions menu | All switches in one row, "Extensions", at the end of the Setup menu | Yes |
| Custom DISP (L39) | One master; per-page level, histogram and details; separate LCD/EVF selected-page memory | L20 LCD/EVF reported working; L30 camera feedback: LCD-to-EVF hang; L32 visibility-only test failed to display; L36 reported a static stock level after restarting on custom DISP3; L37 fix awaits camera testing |
| Test records | One-press record of a problem (`Z5LOG` folder) for feedback | Yes |
| Burst speed-up (test) | Mechanical 233 → ~195 ms/frame (camera-tested with FX and DX lenses). **Silent: no clear gain yet** (~380 ms/frame vs 400 stock; under investigation). **AF-S only: in AF-C focus also stays at the first frame, not for tracking.** Setup menu row "连拍提速" below the extension row, two switches (mechanical / electronic, default on); please switch "view all in continuous mode" off yourself. See [docs/BURST_SPEED.md](docs/BURST_SPEED.md) (Chinese) | Older upstream build: mechanical tested, silent ineffective; this combination awaits camera testing |

The new features have not been checked separately in the viewfinder or in movie mode, and restoring stock behaviour by removing the card has not been rechecked on the latest build. See [PLAN.md](PLAN.md) for completed and future work and [docs/PROGRESS.md](docs/PROGRESS.md) for development notes (both in Chinese).

**This fork ships the L39 custom DISP test module**, merged with upstream `b8ffd1c`. One master switch now controls all four logical DISP0–DISP3 pages. Each page configures Off / Native / TypeA level, histogram and details; native drawing uses DISP0 throughout. Details off projects the untouched simple-page masks while retaining basics and battery. LCD and EVF share page preferences and remember their selected logical pages separately. LCD keeps native DISP4 display-off; EVF skips it as stock does. Turning the master off restores native page selection without clearing preferences. The shared 16/17 ms request scheduler (60 Hz), native update bit 4, tail painting/submission and independent histogram worker (at least 120 ms between acquisition starts) remain. Request rate is not measured display fps. No full-screen cache or additional firmware patches are introduced; replace the module and fully power-cycle on compatible firmware. Logical switching, cold-start memory, histogram flicker improvement and native-level EVF switching still require camera validation. See the [handoff notes](docs/CUSTOM_DISP_HANDOFF.md) and [build instructions](docs/CARD_MODULE_BUILD.md). Known issue: after waking from standby the battery and focal length only appear after one more half-press. In portrait, the i-menu sub pages, third-level pages and touch were **tuned only for the author's own 12 custom i-menu items** (Picture Control, ISO, flash mode, silent photography, HDR, AF-area mode, white balance, Active D-Lighting, metering, image area, vibration reduction, focus mode); other items may or may not work. The item list and the full development log are in the [portrait UI notes](docs/PORTRAIT_UI.md) (in Chinese, updated 2026-11-13). Please save a test record when something goes wrong.

L39 menu revision: Custom DISP now uses separate text slots, fixing the Extensions setup label being replaced by DISP3 Level after leaving the menu with a shutter half-press. Chinese/English host regressions and the cross-build pass; the physical half-press exit still needs camera confirmation.

L39 white-balance fix: turning detailed information off additionally hides the stock white-balance widget (group 38), which stock simple-page permissions also allow. Turning details back on or disabling Custom DISP restores its permissions. Host logic is covered; camera validation is pending.

L38 protects settings reads and saves: only a complete, valid configuration is applied; failed startup reads retry from the GUI callback every 500ms until successful. Edits and saves are blocked until a valid configuration is loaded. Saves overwrite the existing file without truncating it first, then reopen and verify its size and contents. Cold-start persistence still needs camera validation.

On the first camera save, the existing `MENU.DAT` grows to 16 bytes: D8 preferences plus a CD50 master/page-memory word. Legacy 8-byte and D215/D216/D7 12-byte files remain readable. Existing page preferences are retained; any old page master enables the new global master. Legacy files start logical page memory at DISP0. Packages do not overwrite personal `MENU.DAT`; back it up because older modules cannot read the new 16-byte format.

The features live in a module on the memory card. The firmware gains only small entry points and checks. Without the module, the grid, highlight display and histogram-with-level paths behave as stock. Changes are limited to the MAIN module and do not touch the boot area.

**Language:** module menus currently use Simplified Chinese, following upstream; English strings remain in source.

L37 addresses the reported static stock-level residue after restarting on custom DISP3. Off/TypeA suppress all four native widgets; entry, level-mode changes and each native drawing bank receive a one-time local erase with native UI invalidation. Native mode now uses scoped pose/setup on all custom pages. This fix still needs camera validation.

## Windows tool (no installation)

Upstream Windows executables do not automatically include this fork's L39 module. Use this checkout's `patch.py`, import a data package made from this fork, or rebuild the executable; see the [build notes](docs/CARD_MODULE_BUILD.md). The original tool is available from [Releases](https://github.com/WonderLantern/wonder-lantern-z5/releases). Run it and choose the official firmware you downloaded yourself. It builds the files on your computer, and the output folder includes usage notes (in Chinese) and the shutter sound converter. The first run needs an internet connection to download and verify the decoder. The program is unsigned, so Windows may show a security warning. The usage and recovery steps below still apply. Do not redistribute the generated firmware.

## Usage

### 1. Prepare

- Python 3.10 or newer.
- The Z 5 C 1.50 update `Z_5_0150.bin` from Nikon's website, with SHA-256
  `efc0ce70dcafdd96c64e95d8d50f6b7f0509fc0c2840b28d12b6f9e4877ee776`.

### 2. Build

Download this repository and run this in its folder:

```bash
python patch.py --source Z_5_0150.bin --output output
```

The first run needs an internet connection: the tool downloads a fixed version of the decoder files from [z5mod](https://github.com/youyli03/z5mod) and checks their hashes. Nothing is written unless the result matches the release manifest byte for byte. Output:

```text
output/EG165050.BIN              firmware update
output/card/Z5APP.BIN            card module
output/card/SHUTTER/MENU.DAT     settings file, must stay on the card
output/card/SHUTTER/1-5.WAV      default test sounds (five)
output/card/Z5LOG/DIAG01-20.BIN  20 empty test-record files
```

The SHA-256 of `EG165050.BIN` should be `bf821031e6d0b7d5f8762eba7178db94adda90f91c8d078985039f4f1cad558e` (group 108 + burst speed-up entries). Do not redistribute this file.

### 3. Flash (once)

1. With a well-charged battery, put only `EG165050.BIN` in the root of the card.
2. Go to Setup menu → Firmware version → Update and follow the on-screen prompts.
3. Turn the camera off, remove the battery for a moment, and put it back.
4. Delete `EG165050.BIN` from the card.

### 4. Memory card

Copy the contents of `output/card/` to the root of the card. Five test sounds are included, and you can replace them with your own:

```text
A:\Z5APP.BIN
A:\SHUTTER\MENU.DAT
A:\SHUTTER\1.WAV ... 5.WAV
A:\Z5LOG\DIAG01.BIN ... DIAG20.BIN
```

Files created by the camera do not persist, so `MENU.DAT` and the 20 record files in `Z5LOG` must already be on the card.

### 5. Sound format

Custom sounds must be named `1.WAV` to `5.WAV`, and you do not need all five. Use a WAV with a standard 44-byte header: uncompressed PCM, 48 kHz, 16-bit, stereo, at least 2000 bytes. Up to about 1.28 seconds is read. The sound should start at the very beginning of the file, because leading silence makes the shutter sound late.

To convert a file (requires Python 3):

```bash
python tools/make_snd_wav.py your_sound.wav --out converted
```

The result is `SND.WAV` in that folder. Rename it and put it in `SHUTTER`.

### 6. Menus

The last row of the Setup menu, "Extensions" (扩展功能 in Chinese), holds:

| Item | What it does |
| --- | --- |
| Shutter sound | With silent photography on: on/off, sound type, volume, reload |
| Photo zebra | Also turn on the stock highlight display pattern in Custom Settings group g |
| Portrait UI | Portrait layout master switch (on in the generated settings file) |
| Portrait touch | Touch in portrait; when off, touches in portrait are ignored, landscape is unaffected |
| Focal length | Focal length for every CPU lens |
| Battery % | Charge in percent inside the battery icon, red at 20 % and below |
| Custom DISP | One master for DISP0–DISP3; configure level, histogram and details per page; LCD/EVF remember their pages separately |
| Save log | Saves a test record; the row shows the file number ("01"), cycling over the oldest after all 20 fill |

- **Framing grid:** Custom Setting d8, with On (4×4), Off, 3×3, or 2×2 (centre cross).
- **Histogram with level:** press DISP to reach the virtual horizon page. It appears about 1 second after power-on or wake, and "Apply settings to live view" must be on.
- **Status:** the last row of MY MENU shows the module status. `OK ...` means it is working (`正常 ...` in Chinese).

Select **Extensions → Custom DISP → DISP0..DISP3 → Level → Native** for the L37 experiment. Native pose/setup is active again; visiting the stock level page first is no longer required. L37 addresses a suspected stale-dirty callback during LCD/EVF switching, but the L30 hard hang is not confirmed fixed on hardware. TypeA remains available.

With the master on, DISP cycles logical pages while the native page stays DISP0. Details on uses original DISP0 masks; details off projects DISP1 masks onto DISP0, preserving basics and battery. Accepted page changes save logical LCD/EVF identities to the existing settings file, and startup restores them after a valid read. Turning the master off restores native page selection. All logical pages use the existing scoped native-level or TypeA path and private cached histogram. The complete combination still needs camera validation.

### 7. Reporting a problem

Stay on the screen where the problem shows, take a photo of it, then use Extensions → Save log and note the number with a sentence on what you did. Send the written DIAG files from `Z5LOG`, the photos and the notes. The records hold only UI drawing, touch and battery state, no image content.

## Recovery

If the camera freezes, the power switch and removing the card may have no effect:

1. Remove the battery.
2. Remove the card, or delete `Z5APP.BIN` from it on a computer.
3. Put the battery back and turn the camera on. Without the module, it runs the stock path.

To restore the camera completely, download the Z 5 C 1.50 firmware from Nikon and install it following the official steps.

## Repository contents

| Path | Contents |
| --- | --- |
| `patch.py`, `patch_data/` | Build tool and patch data: addresses, short original-byte fragments for verification, and this project's code |
| `card/Z5APP.BIN` | Prebuilt card module, containing only code from this project and the z5mod SDK |
| `card/SHUTTER/` | Default test sounds |
| `src/` | Source for the card module and firmware hooks |
| `tools/` | Audio converter |
| `PLAN.md` | Completed and future work (Chinese) |
| `docs/` | Development notes, custom DISP handoff and module build instructions (Chinese) |
| `tests/card_module/` | Host tests for visibility, settings, refresh intervals and the shared histogram raster |

The [module build instructions](docs/CARD_MODULE_BUILD.md) describe the pinned SDK and private MAIN inputs. Most users only need `patch.py` or the prebuilt module.

## Credits and licence

This project's code is released under the MIT licence. See [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md).

This project builds on the following work. Thanks to their authors and contributors:

| Project | How it was used |
| --- | --- |
| [z5mod](https://github.com/youyli03/z5mod) | Used directly: card module loader, SDK and firmware decoder |
| [wonder-lantern-z30](https://github.com/WonderLantern/wonder-lantern-z30) | Approach studied: histogram with level, grid parameters, photo zebra, and the bring-your-own-firmware release model. Its code is not copied; the Z 5 addresses and hooks were located and written separately |
| [WonderLantern-z6](https://github.com/WonderLantern/WonderLantern-z6) | Feature design studied: custom shutter sounds and framing guides |
| [z7-qemu](https://github.com/WonderLantern/z7-qemu) (by [R3tr0ooo](https://github.com/R3tr0ooo)), [z6-qemu](https://github.com/WonderLantern/z6-qemu) | Our emulator, [z5-qemu](https://github.com/WonderLantern/z5-qemu), derives from these and was used to check menus and buttons |
| [z7ii-qemu](https://github.com/nightglasses/z7ii-qemu) | Reference for the emulator's live view and autofocus plans |

The default test sounds are provided by the maintainer. See [NOTICE.md](NOTICE.md).
