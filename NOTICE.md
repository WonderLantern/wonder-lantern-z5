# NOTICE

## No affiliation

This is an independent, experimental project. It is not affiliated with, authorised by, endorsed by, or supported by Nikon Corporation. "Nikon" and "Z 5" are trademarks of their owners and are used only to describe compatibility.

## No firmware

This repository does not contain Nikon firmware, in original or modified form, decryption keys, extracted modules, or memory dumps. `patch_data/patches.json` holds addresses, short original-byte fragments used to verify the input, and this project's own code. You must obtain the stock firmware from Nikon yourself and comply with Nikon's terms. Do not redistribute the generated `EG165050.BIN`.

Running `patch.py` downloads z5mod's decoder files, including its XOR mask, into `third_party/`. They are not part of this repository and are covered by z5mod's own LICENSE and NOTICE.

## Third-party work

- **z5mod** (https://github.com/youyli03/z5mod, MIT, z5mod contributors): the module loader, the SDK linked into `card/Z5APP.BIN`, and the firmware decoder used at build time. `src/card_module/z5mod-sdk.patch` holds this project's changes to its SDK.
- **wonder-lantern-z30** (https://github.com/WonderLantern/wonder-lantern-z30): the approach for histogram on the level page, for the grid parameters, and for photo zebra was studied from this project. Its code is not copied here; the Z 5 hooks were written and located separately.

## z5mod licence

MIT License, Copyright (c) 2026 z5mod contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
