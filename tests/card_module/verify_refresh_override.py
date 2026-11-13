#!/usr/bin/env python3
"""Host-only checks of the real RAM override helper; no camera/ARM execution."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'src/card_module/custom_disp/refresh_override.h'
HARNESS = r'''
#include <assert.h>
#include <stdint.h>
typedef uint32_t u32;
#define Z5_IMAGE(name)
#include "refresh_override.h"

int main(void) {
    refresh_override s = {0};
    volatile u32 table[32];
    u32 original[16];
    for (u32 i = 0; i < 16; i++) {
        original[i] = i % 4 + 1;
        table[i * 2] = original[i];
        table[i * 2 + 1] = 0xace00000u + i;
    }
    refresh_override_update(&s, table, 0);
    assert(s.owned == 0);
    refresh_override_update(&s, table, 1);
    for (u32 i = 0; i < 16; i++) {
        assert(table[i * 2] == 1);
        assert(!!(s.owned & (1u << i)) == (original[i] != 1));
    }
    /* Repeated entry must not replace saved native values with our own 1. */
    for (u32 n = 0; n < 100; n++) refresh_override_update(&s, table, 1);
    refresh_override_update(&s, table, 0);
    assert(s.owned == 0);
    for (u32 i = 0; i < 16; i++) assert(table[i * 2] == original[i]);

    refresh_override_update(&s, table, 1);
    table[2] = 9; /* Native change after the override must win on exit. */
    refresh_override_update(&s, table, 0);
    assert(table[2] == 9 && s.owned == 0);

    /* Native changes observed on another update become the restoration value. */
    refresh_override_update(&s, table, 1);
    table[2] = 7;
    refresh_override_update(&s, table, 1);
    assert(table[2] == 1);
    refresh_override_update(&s, table, 0);
    assert(table[2] == 7);

    /* Initially-unmodified entries can be acquired later, including slot 15. */
    table[0] = 11;
    table[30] = 12;
    refresh_override_update(&s, table, 1);
    assert(table[0] == 1 && table[30] == 1);
    refresh_override_update(&s, table, 0);
    assert(table[0] == 11 && table[30] == 12);

    /* Even an original zero must produce 1 while borrowed, then restore exactly. */
    table[4] = 0;
    refresh_override_update(&s, table, 1);
    assert(table[4] == 1);
    refresh_override_update(&s, table, 0);
    assert(table[4] == 0);
    refresh_override_update(&s, table, 0);
    assert(s.owned == 0);
    for (u32 i = 0; i < 16; i++) assert(table[i * 2 + 1] == 0xace00000u + i);
    return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix='z5-k1-host-') as scratch:
        root = Path(scratch)
        source, binary = root / 'check.c', root / 'check'
        source.write_text(HARNESS)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-I', str(HEADER.parent), str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print('PASS: all 16 slots, paired words, repeated updates, restoration, native changes, reentry and zero k')


if __name__ == '__main__':
    main()
