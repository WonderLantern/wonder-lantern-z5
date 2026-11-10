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
| Portrait UI | Held upright, the shooting information, the i menu (as 4×3), its setting pages and the histogram turn with the camera, and the direction keys follow the screen. Rear monitor only | Yes (photo mode, rear monitor) |

The new features have not been checked separately in the viewfinder or in movie mode, and restoring stock behaviour by removing the card has not been rechecked on the latest build. See [PLAN.md](PLAN.md) for completed and future work and [docs/PROGRESS.md](docs/PROGRESS.md) for development notes, and [docs/PORTRAIT_UI.md](docs/PORTRAIT_UI.md) for how the portrait UI works and how to port it (all in Chinese).

The features live in a module on the memory card. The firmware gains only small entry points and checks. Without the module, the grid, highlight display, histogram-with-level and portrait UI paths behave as stock. Changes are limited to the MAIN module and do not touch the boot area.

**Language:** the menus were tested on camera in Simplified Chinese. English labels are built in and appear when the camera language is English, but they have not been tested on a camera.

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

The first run needs an internet connection: the tool downloads a fixed version of the decoder files from [z5mod](https://github.com/youyli03/z5mod) and checks their hashes. Nothing is written unless the result matches the tested build byte for byte. Output:

```text
output/EG165050.BIN              firmware update
output/card/Z5APP.BIN            card module
output/card/SHUTTER/MENU.DAT     settings file, must stay on the card
output/card/SHUTTER/1-3.WAV      default test sounds
```

The SHA-256 of `EG165050.BIN` should be `f8ffd4ad51bf045c523cc22f3bdc17d845fe46e8ae06659f2480dd70a72a7643`. Do not redistribute this file.

### 3. Flash (once)

1. With a well-charged battery, put only `EG165050.BIN` in the root of the card.
2. Go to Setup menu → Firmware version → Update and follow the on-screen prompts.
3. Turn the camera off, remove the battery for a moment, and put it back.
4. Delete `EG165050.BIN` from the card.

### 4. Memory card

Copy the contents of `output/card/` to the root of the card. Three test sounds are included, and you can replace them with your own:

```text
A:\Z5APP.BIN
A:\SHUTTER\MENU.DAT
A:\SHUTTER\1.WAV ... 5.WAV
```

A `MENU.DAT` created by the camera does not persist, so this file must already be on the card.

### 5. Sound format

Custom sounds must be named `1.WAV` to `5.WAV`, and you do not need all five. Use a WAV with a standard 44-byte header: uncompressed PCM, 48 kHz, 16-bit, stereo, at least 2000 bytes. Up to about 1.28 seconds is read. The sound should start at the very beginning of the file, because leading silence makes the shutter sound late.

To convert a file (requires Python 3):

```bash
python tools/make_snd_wav.py your_sound.wav --out converted
```

The result is `SND.WAV` in that folder. Rename it and put it in `SHUTTER`.

### 6. Menus

- **Shutter sound:** with silent photography on, open the second-to-last row of the Setup menu for on/off, sound type, volume and reload.
- **Photo highlight display:** the last row of the Setup menu, labelled "Photo zebra" in English. You also need to turn on the stock highlight display pattern in Custom Settings group g.
- **Framing grid:** Custom Setting d8, with On (4×4), Off, 3×3, or 2×2 (centre cross).
- **Histogram with level:** press DISP to reach the virtual horizon page. It appears about 1 second after power-on or wake, and "Apply settings to live view" must be on.
- **Portrait UI:** "Portrait UI" in the Setup menu, off by default. For the rear monitor in photo mode; the viewfinder and HDMI keep the stock landscape layout. Turn it off before recording movies. Touch input is not rotated, so use the buttons when holding the camera upright.
- **Status:** the last row of MY MENU shows the module status. `OK ...` means it is working (`正常 ...` in Chinese).

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
| `docs/` | Development notes and the portrait UI write-up (Chinese) |

The build scripts in `src/` depend on the author's development setup and are provided for reference and maintenance. Most users only need `patch.py`.

## Credits and licence

This project's code is released under the MIT licence. See [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md).

This project builds on the following work. Thanks to their authors and contributors:

| Project | How it was used |
| --- | --- |
| [z5mod](https://github.com/youyli03/z5mod) | Used directly: card module loader, SDK and firmware decoder |
| [wonder-lantern-z30](https://github.com/WonderLantern/wonder-lantern-z30) | Approach studied: histogram with level, grid parameters, photo zebra, and the bring-your-own-firmware release model. Its code is not copied; the Z 5 addresses and hooks were located and written separately |
| [WonderLantern-z6](https://github.com/WonderLantern/WonderLantern-z6) | Feature design studied: custom shutter sounds and framing guides |
| [z7-qemu](https://github.com/WonderLantern/z7-qemu) (by [R3tr0ooo](https://github.com/R3tr0ooo)), [z6-qemu](https://github.com/WonderLantern/z6-qemu) | Our emulator, [z5-qemu](https://github.com/honokaoshi83-max/z5-qemu), derives from these and was used to check menus and buttons |
| [z7ii-qemu](https://github.com/nightglasses/z7ii-qemu) | Reference for the emulator's live view and autofocus plans |

The default test sounds are provided by the maintainer. See [NOTICE.md](NOTICE.md).
