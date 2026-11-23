#!/usr/bin/env python3
"""Host run of the real timed-Bulb task step (burst_speed.inc bulb_step) with the A55 series.

The firmware calls are replaced by host stubs (start, end, computer-control switch, busy getters, clock), and the
task is stepped every 50 ms as burst_task does. Checks: one shot behaves as before; a series takes exactly the set
number of shots, each with the set exposure, the computer-control switch on and off around every shot, never
started while the camera is busy and never before the interval or BT_GAP_MIN_MS; OK during the interval or an
exposure ends the series; leaving Bulb ends it; a camera that never gets ready ends it after the timeout without a
start; the step tables and the number / interval texts.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
inc = (ROOT / 'src/card_module/burst_speed.inc').read_text(encoding='utf-8')
main = (ROOT / 'src/card_module/main.c').read_text(encoding='utf-8')


def function(name):
    m = re.search(r'IMG_FN static (?:void |u32 |volatile u32 \*)' + name + r'\([^;{]*\)[^\n;]*\n\{', inc)
    cursor, depth = inc.index('{', m.start()) + 1, 1
    while depth:
        depth += (inc[cursor] == '{') - (inc[cursor] == '}')
        cursor += 1
    return inc[m.start():cursor]


bt_t = main[main.index('typedef struct {\n    u32 q[12];'):main.index('} bt_t;') + len('} bt_t;')]
defs = inc[inc.index('#define BT_SERIES_OFF'):inc.index('IMG_FN static u32 bt_count')]
body = '\n'.join(function(n) for n in ('bt_count', 'bt_gap', 'bt_busy', 'bt_log', 'bt_end_series', 'bulb_step',
                                       'bt_num', 'bt_gap_text'))
# firmware addresses -> host stubs
body = re.sub(r'\(\(u32 \(\*\)\(void\)\)0x([0-9A-F]+)u\)\(\)', r'fw_\1()', body)
body = body.replace('0xC18A84E4u', '((uintptr_t)&usb_state)').replace('0xC18A7EE4u', '((uintptr_t)&open_word)')

HARNESS = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t u32; typedef int32_t s32; typedef uint16_t u16; typedef uint8_t u8;
#define IMG_FN
#define BULB_MENU
#define BULB_PCMODE
#define BULB_FULLEXEC
#define BT_HALF_PRESS
#define BT_HALF_MS 300u
static u32 clock_ms, usb_state, open_word;
#define CM_MS_CLOCK ((uintptr_t)&clock_ms)
''' + bt_t + r'''
typedef struct { uintptr_t dg_buf; bt_t *bt; } cm_t;
#define BTP(c) ((c)->bt)
#define BT_MAGIC 0x32314254u
#define BT_RUN_MS(c) (BTP(c)->stop ? 0u : BTP(c)->secs * 1000u)
''' + defs + r'''
/* camera model: Bulb mode, busy for busy_ms after each end, exposure state */
static u32 mode = 2u, pc = 0u, busy_ms = 0u, busy_until = 0u, never_ready = 0u;
static u32 starts, ends, pc_on, pc_off, start_ms[1200], end_ms[1200], busy_starts, pc_bad;
static u32 PCM_PROP(u32 key) { assert(key == 0x1B17u); return never_ready || (s32)(clock_ms - busy_until) < 0; }
static u32 fw_1021F529(void) { return 0u; }
static u32 fw_102203A3(void) { return 0u; }
static u32 fw_102203C7(void) { return 0u; }
static u32 bt_mode(void) { return mode; }
static u32 pc_mode(u32 m) { if (m) { if (pc) pc_bad++; pc_on++; } else { if (!pc) pc_bad++; pc_off++; } pc = m; return 0u; }
static u32 bulb_exec(void) {
    if (!pc || PCM_PROP(0x1B17u) || open_word) busy_starts++;
    start_ms[starts++] = clock_ms; open_word = 1u; usb_state = 2u; return 0x20010000u;
}
static u32 fw_end(u32 *z) { (void)z; end_ms[ends++] = clock_ms; open_word = 0u; usb_state = 0u;
    busy_until = clock_ms + busy_ms; return 0u; }
static void bt_half(u32 d) { (void)d; }
#define BT_END ((uintptr_t)fw_end)
static void hz_put(u16 *b, u32 *n, u32 ch) { b[(*n)++] = (u16)ch; }
static void en_put(char *b, u32 *n, u32 ch) { b[(*n)++] = (char)ch; }
static void en_w(char *b, u32 *n, u32 w) { while (w & 255u) { b[(*n)++] = (char)w; w >>= 8; } }
''' + body + r'''
static u32 diag[0x40000 / 4];
static bt_t bt;
static cm_t cm;
static void reset(void) {
    memset(&bt, 0, sizeof bt); memset(diag, 0, sizeof diag); cm.dg_buf = (uintptr_t)diag; cm.bt = &bt;
    mode = 2u; pc = 0u; busy_ms = 0u; busy_until = 0u; never_ready = 0u; starts = ends = pc_on = pc_off = 0u;
    busy_starts = pc_bad = 0u; usb_state = open_word = 0u; clock_ms = 100000u;
}
/* as bt_start_ok after a successful check */
static void start(u32 h, u32 m, u32 s, u32 ci, u32 gi) {
    volatile u32 *q = bt.q;
    bt.h = h; bt.m = m; bt.s = s; bt.cnt_i = ci; bt.gap_i = gi;
    for (u32 i = 1u; i < 12u; i++) q[i] = 0u;
    bt.secs = h * 3600u + m * 60u + s; bt.total = bt_count(ci); bt.done = 0u; bt.ready = 0u;
    volatile u32 *g = bt_log(&cm);
    for (u32 i = 0u; i < 16u + 64u * 4u; i++) g[i] = 0u;
    g[0] = BT_SERIES_MAGIC; g[1] = bt.total; g[3] = bt_gap(gi); g[4] = bt.secs;
    bt.stop = 0u; q[2] = clock_ms + 1500u; q[0] = BT_MAGIC; q[1] = 1u;
}
static void run(u32 ms) { for (u32 t = 0; t < ms; t += 50u) { bulb_step(&cm); clock_ms += 50u; } }
static u32 run_until_idle(u32 limit) {
    for (u32 t = 0; t < limit; t += 50u) { bulb_step(&cm); clock_ms += 50u; if (bt.q[1] == 3u || bt.q[1] == 0u) return t; }
    return ~0u;
}
static void check_series(u32 shots, u32 secs, u32 gap_s, u32 busy) {
    for (u32 i = 0; i < shots; i++) {
        u32 len = end_ms[i] - start_ms[i];
        assert(len >= secs * 1000u && len <= secs * 1000u + 50u);
        if (i) {
            u32 wait = start_ms[i] - end_ms[i - 1];
            u32 need = gap_s * 1000u > BT_GAP_MIN_MS ? gap_s * 1000u : BT_GAP_MIN_MS;
            if (busy > need) need = busy;
            assert(wait >= need && wait <= need + 250u);
        }
    }
}
int main(void) {
    /* step tables: 32 distinct increasing values */
    for (u32 i = 1; i < 32; i++) { assert(bt_count(i) > bt_count(i - 1)); assert(bt_gap(i) > bt_gap(i - 1)); }
    assert(bt_count(0) == 1 && bt_count(9) == 10 && bt_count(10) == 15 && bt_count(17) == 50 && bt_count(18) == 60 &&
           bt_count(22) == 100 && bt_count(23) == 200 && bt_count(30) == 900 && bt_count(31) == 999);
    assert(bt_gap(0) == 0 && bt_gap(10) == 10 && bt_gap(11) == 15 && bt_gap(20) == 60 && bt_gap(21) == 90 &&
           bt_gap(28) == 300 && bt_gap(29) == 600 && bt_gap(30) == 900 && bt_gap(31) == 1800);
    /* texts */
    { char b[32]; u32 n = 0; bt_num(0, b, &n, 0); bt_num(0, b, &n, 7); b[n++] = ','; bt_num(0, b, &n, 999);
      b[n++] = ','; bt_num(0, b, &n, 1800); b[n] = 0; assert(!strcmp(b, "07,999,1800"));
      n = 0; bt_gap_text(0, b, &n, 0); b[n] = 0; assert(!strcmp(b, "0 s"));
      n = 0; bt_gap_text(0, b, &n, 45); b[n] = 0; assert(!strcmp(b, "45 s"));
      n = 0; bt_gap_text(0, b, &n, 90); b[n] = 0; assert(!strcmp(b, "1 min 30 s"));
      n = 0; bt_gap_text(0, b, &n, 1800); b[n] = 0; assert(!strcmp(b, "30 min"));
      u16 h[16]; n = 0; bt_gap_text(h, 0, &n, 150);
      assert(n == 5 && h[0] == '2' && h[1] == 0x5206u && h[2] == '3' && h[3] == '0' && h[4] == 0x79D2u); }
    /* one shot: as before (start 1.5 s after OK, 5 s open, switch on/off once, phase 3) */
    reset(); start(0, 0, 5, 0, 0); assert(run_until_idle(60000u) != ~0u);
    assert(starts == 1 && ends == 1 && pc_on == 1 && pc_off == 1 && !pc_bad && bt.q[1] == 3u && bt.done == 1);
    check_series(1, 5, 0, 0); run(5000); assert(starts == 1);
    /* series: 4 x 3 s, interval 5 s, camera busy 1 s after each shot */
    reset(); busy_ms = 1000u; start(0, 0, 3, 3, 5); assert(run_until_idle(120000u) != ~0u);
    assert(starts == 4 && ends == 4 && pc_on == 4 && pc_off == 4 && !pc_bad && !busy_starts && bt.done == 4);
    check_series(4, 3, 5, 1000u);
    assert(diag[0x36400 / 4] == BT_SERIES_MAGIC && diag[0x36400 / 4 + 1] == 4 && diag[0x36400 / 4 + 2] == 4);
    for (u32 i = 0; i < 4; i++) { const u32 *e = diag + 0x36440 / 4 + i * 4;
        assert(e[0] == 0x20010000u && e[1] == start_ms[i] && e[2] == end_ms[i] && e[3] == (i ? start_ms[i] - end_ms[i - 1] : 0)); }
    /* no interval: BT_GAP_MIN_MS; long processing (noise reduction 20 s) is waited for */
    reset(); busy_ms = 20000u; start(0, 0, 20, 2, 0); assert(run_until_idle(400000u) != ~0u);
    assert(starts == 3 && !busy_starts && !pc_bad); check_series(3, 20, 0, 20000u);
    reset(); start(0, 0, 1, 4, 0); assert(run_until_idle(60000u) != ~0u); assert(starts == 5); check_series(5, 1, 0, 0);
    /* OK during the interval: no further shot */
    reset(); start(0, 0, 2, 9, 10); run(1500u + 2000u + 3000u); assert(starts == 1 && bt.q[1] == 4u);
    bt.stop = 1u; run(100u); assert(bt.q[1] == 3u && bt.q[9] == (0x5E000000u | 1u)); run(60000u);
    assert(starts == 1 && pc_on == 1 && pc_off == 1 && !pc_bad);
    /* OK during the second exposure: it ends now, no third */
    reset(); start(0, 1, 0, 9, 0); run(1500u + 60000u + 2500u + 5000u); assert(starts == 2 && bt.q[1] == 2u);
    bt.stop = 1u; run(100u); assert(ends == 2 && bt.q[1] == 3u && bt.q[9] == (0x5E000000u | 2u)); run(200000u);
    assert(starts == 2 && pc_on == 2 && pc_off == 2 && !pc_bad);
    /* OK in the instant between the wait and the start (phase 1 of a later shot) */
    reset(); start(0, 0, 1, 2, 0); run(1500u + 1000u + 2000u); assert(starts == 1);
    while (bt.q[1] != 1u) { bulb_step(&cm); clock_ms += 50u; }
    bt.stop = 1u; bulb_step(&cm); assert(bt.q[1] == 3u && starts == 1 && pc_on == 1 && pc_off == 1);
    /* Bulb left during the interval */
    reset(); start(0, 0, 2, 5, 30); run(1500u + 2000u + 5000u); mode = 0u; run(100u);
    assert(bt.q[1] == 3u && bt.q[9] == 0xBAD00000u && starts == 1);
    /* camera never ready: ends after exposure + interval + BT_READY_MS, never started busy */
    reset(); start(0, 0, 10, 3, 5); run(1500u + 10000u + 100u); never_ready = 1u;
    u32 t = run_until_idle(400000u); assert(t != ~0u);
    assert(starts == 1 && bt.q[9] == (0xB5000000u | 1u) && pc_on == 1 && pc_off == 1);
    assert(t + 100u >= 10000u + 5000u + BT_READY_MS - 200u && t <= 10000u + 5000u + BT_READY_MS + 200u);
    /* 999 shots fit the per-shot ring (64 kept) */
    reset(); start(0, 0, 1, 31, 0); assert(run_until_idle(4000000u) != ~0u);
    assert(starts == 999 && ends == 999 && pc_on == 999 && pc_off == 999 && !pc_bad && diag[0x36400 / 4 + 2] == 999);
    puts("ok");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='z5-bulb-series-') as t:
    p = Path(t)
    (p / 'series.c').write_text(HARNESS)
    subprocess.run(['cc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', str(p / 'series.c'),
                    '-o', str(p / 'series')], check=True)
    subprocess.run([str(p / 'series')], check=True)
print('PASS: real bulb_step with the A55 series: one shot unchanged; set shot count and exposure; switch on/off '
      'around every shot; interval, 2 s minimum and busy camera waited for; OK in the interval, an exposure or the '
      'start instant ends it; Bulb left ends it; never-ready timeout; 999 shots; step tables and texts')
