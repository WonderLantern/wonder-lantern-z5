"""Decode the B1 readout (stock live-view window / display-fit RAM watch) from a DIAG record.

    python -B research/aspect/decode_b1.py <DIAGnn.BIN> [--all]

Record (release worktree src/card_module/b1_lvwin.inc, layout 2) at DIAG 0x39840: head, baseline, shadow, change
counts, then the log: 0xFFFFFFFF + ms once per tick with changes, then index | value << 16 per changed halfword.
Shots come from the A90T ring at 0x38040 (kind 19 configurations, same ms clock).

Prints the named fields (stock window 0xC23802C8 + 0x5A.., fit outputs, image area, layers, the module's aspect
state) at power-on and at every tick where one of them changed, with the shots in between. --all also lists every
other changed halfword. camera_verified: false.
"""
import argparse
import struct
from pathlib import Path

B1_OFF, B1_END, MAGIC = 0x39840, 0x3B000, 0x574C3142
LOG_END = {2: 0x3B000, 3: 0x3AFF0}                  # layout 3 (B2): counters 'B2HS' at 0x3AFF0
B2_OFF, B2_MAGIC = 0x3AFF0, 0x53483242
R3_LEN = 0x10
A83_OFF, A83_MAGIC = 0x38000, 0x54303941

# name: (address, halfwords, signed); rectangles are left, top, right, bottom unless named otherwise
FIELDS = {
    'full size (w,h)      C23802C8+5A': (0xC2380322, 2),
    'source window        C23802C8+5E': (0xC2380326, 4),
    'window 2             C23802C8+6C': (0xC2380334, 6),
    'fit out 1            C23802F4': (0xC23802F4, 4),
    'fit out 2            C2380314': (0xC2380314, 4),
    'fit src 1            C23802EC': (0xC23802EC, 4),
    'fit src 2            C238030C': (0xC238030C, 4),
    'fit size 2 (w,h)     C2380674': (0xC2380674, 2),
    'fit out 3            C2380678': (0xC2380678, 4),
    'fit size 4 (w,h)     C2380680': (0xC2380680, 2),
    'fit out 4            C2380684': (0xC2380684, 4),
    'fit params +16/+18   C2380BC2': (0xC2380BC2, 2),
    'fit params +36/+38   C2380BE2': (0xC2380BE2, 2),
    'image area 0x169E    C25043A4': (0xC25043A4, 2),
    'GUI layers           C1D61388': (0xC1D61388, 8),
}


def parse(diag):
    h = struct.unpack_from('<16I', diag, B1_OFF)
    if h[0] != MAGIC:
        raise ValueError('no B1 record at 0x%x' % B1_OFF)
    if h[8] not in LOG_END:
        raise ValueError('layout %d, expected 2 or 3' % h[8])
    ranges = [(h[9], h[10]), (h[11], h[12]), (h[13], h[14]), (h[15], R3_LEN)]
    nhw = h[7]
    pad = (nhw + 3) & ~3
    base = list(struct.unpack_from('<%dH' % nhw, diag, B1_OFF + 0x40))
    shadow = list(struct.unpack_from('<%dH' % nhw, diag, B1_OFF + 0x40 + 2 * pad))
    count = list(diag[B1_OFF + 0x40 + 4 * pad:B1_OFF + 0x40 + 4 * pad + nhw])
    ev_off = B1_OFF + 0x40 + 5 * pad
    words = struct.unpack_from('<%dI' % h[2], diag, ev_off)
    events, ms, k = [], None, 0
    while k < len(words):
        if words[k] == 0xFFFFFFFF:
            ms = words[k + 1]; k += 2; continue
        events.append((ms, words[k] & 0xFFFF, words[k] >> 16)); k += 1
    return dict(head=h, ranges=ranges, nhw=nhw, base=base, shadow=shadow, count=count, events=events,
                cap=(LOG_END[h[8]] - ev_off) // 4,
                b2=struct.unpack_from('<4I', diag, B2_OFF) if h[8] == 3 else None)


def address_of(ranges, i):
    for a, n in ranges:
        if i < n // 2:
            return a + 2 * i
        i -= n // 2
    return None                                         # the module's own aspect state


def index_of(ranges, addr):
    base = 0
    for a, n in ranges:
        if a <= addr < a + n:
            return base + (addr - a) // 2
        base += n // 2
    raise ValueError(hex(addr))


def shots(diag):
    if struct.unpack_from('<I', diag, A83_OFF)[0] != A83_MAGIC:
        return []
    n = struct.unpack_from('<I', diag, A83_OFF + 4)[0]
    out = []
    for i in range(min(n, 64)):
        e = struct.unpack_from('<16I', diag, A83_OFF + 0x40 + 64 * i)
        if e[0] & 0xFF == 19 and e[2] not in (0x1014421B,):
            out.append((e[1], e[0] >> 8 & 0xFF, e[2]))
    return sorted(out)


def fmt(vals):
    return ' '.join('%5d' % (v - 0x10000 if v >= 0x8000 else v) for v in vals)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('diag', type=Path)
    ap.add_argument('--all', action='store_true')
    a = ap.parse_args()
    diag = a.diag.read_bytes()
    r = parse(diag)
    h, rg = r['head'], r['ranges']
    print('B1: ticks %d, ms %d .. %d, log %d of %d words, %d changes dropped, %d halfwords quiet (>= 8 changes)'
          % (h[1], h[5], h[6], h[2], r['cap'], h[3], h[4]))
    if r['b2'] and r['b2'][0] == B2_MAGIC:
        print('B2 kind 28: %d calls, answered with 4:3: window %d, fit %d, LV crop (B4) %d, class 3 (B7) %d'
              % (r['b2'][3], r['b2'][1] & 0xFFFF, r['b2'][2] & 0xFFFF, r['b2'][1] >> 16, r['b2'][2] >> 16))
    elif r['b2'] is not None:
        print('B2 kind 28: no call recorded')
    idx = {name: [index_of(rg, addr + 2 * k) for k in range(n)] for name, (addr, n) in FIELDS.items()}
    idx['module aspect (on | sel<<4 | status<<8)'] = [r['nhw'] - 1]
    cur = list(r['base'])
    print('\npower-on (ms %d):' % h[5])
    for name, ii in idx.items():
        print('  %-40s %s' % (name, fmt(cur[i] for i in ii)))
    named = {i for ii in idx.values() for i in ii}
    shot_list = shots(diag)
    by_tick = {}
    for ms, i, v in r['events']:
        by_tick.setdefault(ms, []).append((i, v))
    for ms in sorted(by_tick):
        while shot_list and shot_list[0][0] <= ms:
            s = shot_list.pop(0)
            print('\n  -- shot (configuration) ms %d, area %d, caller %08x' % s)
        changes = by_tick[ms]
        hit = {i for i, _ in changes} & named
        for i, v in changes:
            cur[i] = v
        other = [(i, v) for i, v in changes if i not in named]
        if hit:
            print('\nms %d:' % ms)
            for name, ii in idx.items():
                if set(ii) & hit:
                    print('  %-40s %s' % (name, fmt(cur[i] for i in ii)))
            if other:
                print('  (+%d other halfwords)' % len(other))
        if a.all and other:
            print('ms %d other: %s' % (ms, ' '.join('%s=%d' % (('%08x' % address_of(rg, i)) if address_of(rg, i)
                                                                else 'module', v) for i, v in other)))
    noisy = [i for i, n in enumerate(r['count']) if n >= 8]
    if noisy:
        print('\nquiet after 8 changes: ' + ' '.join('%08x' % address_of(rg, i) if address_of(rg, i) else 'module'
                                                     for i in noisy))


if __name__ == '__main__':
    main()
