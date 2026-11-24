# Z5 Extensions

[中文](README.md)

Feature extensions for the Nikon Z 5, firmware C 1.50. **This repository contains no Nikon firmware.** You supply the official update file, and `patch.py` verifies it and builds the update on your own computer.

This project is not affiliated with, authorised by, or supported by Nikon. **Flashing modified firmware can damage your camera. Use it at your own risk.** So far it has been tested on only one camera, the author's.

## Features

| Feature | Description | Camera tested |
| --- | --- | --- |
| Custom shutter sound | Plays a WAV from the card, up to 5 sounds, with sound and volume settings and separate switches for electronic and mechanical shutter (mechanical off by default) | **Tested on the author's camera** (A55) |
| English / Chinese menus | The card module's menus follow the camera language: English or Simplified Chinese | In use on the author's camera |
| Histogram with level | Shows the histogram on the virtual horizon page | Yes (photo mode, rear monitor) |
| Framing grid styles | Adds 3×3 and 2×2 to Custom Setting d8. The stock "On" remains 4×4 | Yes (photo mode, rear monitor) |
| Photo highlight display (zebra) | Brings the stock movie highlight display to photo live view, with its own switch in the Setup menu | Yes (photo mode, rear monitor) |
| Standby wake fix | Keeps the features when the camera wakes immediately after the display turns off | Yes |
| Portrait UI (test) | Shooting display, i menu and setting pages turn upright when the camera is held vertically | Author's camera, still in testing |
| Portrait touch (test) | Touch follows the portrait layout, including strip dragging; can be turned off | Author: "mostly usable" |
| Focal length | Shown for every CPU lens (stock: power-zoom lenses only); can be turned off | Yes |
| Battery percent | Remaining charge shown in the battery icon; can be turned off | Yes |
| Extensions menu | All switches in one row, "Extensions", at the end of the Setup menu | Yes |
| Custom DISP | Per-page level, histogram and details with separate LCD and EVF page memory | User reported A39 worked; A52 tested on the author's camera |
| Timed Bulb | Set hours, minutes, seconds, shot count and interval in the photo shooting menu. Start exits the menu and begins exposure about 1.5 seconds later. | **Tested on the author's camera** (series: A55) |
| Photo aspect | 4:3 or 2.33:1 in the photo shooting menu; JPG and NEF are recorded in that shape, with a mask on the monitor and in the viewfinder (needs Custom DISP on). **The mask can show minor glitches in some situations; this is a known issue.** See [docs/PHOTO_ASPECT.md](docs/PHOTO_ASPECT.md) (Chinese) | **Tested on the author's camera** (A81) |
| Film grain FILM | FUJI400 / HP5, strength 1–6, on the JPG and the NEF's embedded preview; about 1.1 s per image. **Avoid high-speed bursts with grain on** (low-speed 3 fps ran 20+ frames without waiting; high speed waits 1–2 s after 4 frames). Implementation and parameters: [docs/FILM_GRAIN.md](docs/FILM_GRAIN.md) (Chinese) | **Tested on the author's camera** (A52) |
| Lightroom grain inside the NEF | With "NEF写入: on" the camera writes matching Lightroom grain into the NEF's own XMP; with Lightroom's raw default set to "Camera Settings" it applies on import, no sidecar files | **Tested on the author's camera** (Lightroom Classic 13.2) |
| Faster photo listing over USB | Speeds up listing photos on a computer over USB (port of Z30 mtp-fast); on whenever the card module is installed | Ran on the author's camera, **effect not verified** |
| Test records | One-press record of a problem (`Z5LOG` folder) for feedback | Yes |
| Burst speed-up (test) | Mechanical 233 → ~195 ms/frame (camera-tested with FX and DX lenses). **Silent: no clear gain yet** (~380 ms/frame vs 400 stock; under investigation). **AF-S only: in AF-C focus also stays at the first frame, not for tracking.** Setup menu row "连拍提速" below the extension row, two switches (mechanical / electronic; from A50 off at every power-on, turning them on lasts until power-off). While on, "view all in continuous mode" cannot show live view during a burst. See [docs/BURST_SPEED.md](docs/BURST_SPEED.md) (Chinese) | Older upstream build: mechanical tested, silent ineffective; this combination awaits camera testing |

The new features have not been checked separately in the viewfinder or in movie mode, and restoring stock behaviour by removing the card has not been rechecked on the latest build. See [PLAN.md](PLAN.md) for completed and future work and [docs/PROGRESS.md](docs/PROGRESS.md) for development notes (both in Chinese).

**Current version A81** adds photo aspect 4:3 / 2.33:1, Bulb series with shot count and interval, separate electronic/mechanical shutter-sound switches and menus that follow the camera language (English or Simplified Chinese), and fixes the A50–A52 freeze on the FILM strength page after a shot. Tested on the author's camera (the USB listing speed-up's effect is still not verified).

**Known issue: the aspect mask can show minor glitches in some situations** (for example a brief flicker near icons); the recorded photo is not affected. Playback zoom works only for photos in the currently selected aspect. See [docs/PHOTO_ASPECT.md](docs/PHOTO_ASPECT.md) (Chinese). Please avoid high-speed bursts with film grain on.

**A39** combines the A19 timed Bulb menu with Custom DISP from PR #6. Enable Custom DISP under Setup Menu → Extensions → Custom DISP. Each of the four pages has its own level, histogram and detail settings. The LCD keeps a display-off position, and the EVF cycles the four visible pages.

The user reported that A39 worked in camera testing. Individual display scenarios and video were not reported separately. See the [portrait notes](docs/PORTRAIT_UI.md) and [Custom DISP handoff](docs/CUSTOM_DISP_HANDOFF.md).

## Windows tool (no installation)

Download `Z5Extensions.exe` from the [A81 release](https://github.com/WonderLantern/wonder-lantern-z5/releases/tag/a81-20261125). It generates the A81 firmware and card files from the official C 1.50 file. It is the only download: separate module files and update bundles are no longer provided, and bundle import has been removed. To update, download the new exe. The output includes Chinese instructions and the shutter sound converter.

## Usage

**A81 needs new firmware** (photo aspect entries). Coming from any older version, including the A52 firmware (SHA-256 starting `a3704c6f`), flash it once as below and replace Z5APP.BIN on the card as well. Back up Z5APP.BIN and SHUTTER/MENU.DAT first; keep your MENU.DAT to keep your settings.

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

The SHA-256 of `EG165050.BIN` should be `13525d9ba3d461833e816ba838cf86369181445f0598378352069546118de6c5` (A52 firmware + photo aspect entries). Do not redistribute this file.

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
| Shutter sound | Separate electronic / mechanical switches, sound type, volume, reload |
| Photo zebra | Also turn on the stock highlight display pattern in Custom Settings group g |
| Portrait UI | Portrait layout master switch (on in the generated settings file) |
| Portrait touch | Touch in portrait; when off, touches in portrait are ignored, landscape is unaffected |
| Focal length | Focal length for every CPU lens |
| Battery % | Charge in percent inside the battery icon, red at 20 % and below |
| Custom DISP | One master for DISP0–DISP3; configure level, histogram and details per page; LCD/EVF remember their pages separately |
| Timed Bulb | Set hours, minutes and seconds in the photo shooting menu. Start exits the menu and begins exposure about 1.5 seconds later. | A19 tested on camera. User reported A39 worked. |
| FILM | Film grain: off / FUJI400 / HP5, strength 1–6, NEF write (on: Lightroom grain in the NEF). See [docs/FILM_GRAIN.md](docs/FILM_GRAIN.md) |

**Save log** is now the last row of the Setup menu: it saves a test record; the row shows the file number ("01"), cycling over the oldest after all 20 fill.


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
| `docs/` | Film grain implementation, development notes, custom DISP handoff and module build instructions (Chinese) |
| `tests/card_module/` | Host tests for visibility, settings (including FILM), refresh intervals and the shared histogram raster |

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
