/* B1 readout (2026-11-26, -DB1_LVWIN, needs -DA83_TRACE): the stock live-view window and display-fit results.
 * Why (research/aspect/native-liveview-window-2026-11-26.md in the development tree): the stock window updater
 * 0x10197FDC writes the source window at 0xC23802C8 + 0x5E (left, top, right, bottom on the FX frame), and the fit
 * function 0x1018B326 (size 0x1018B388, class 0x101DC978, fit size 0x10209304) writes its rectangles into the same
 * view block (0xC23802F4, 0xC2380314, 0xC2380674.., 0xC2380684) and next to 0xC2380BAC. Before the window can be
 * handed to the stock path for 4:3 / 2.33:1, we need the values the camera really holds for stock FX, 16:9, 1:1,
 * after a shot, LCD and EVF.
 *
 * Read-only: the module task (burst_task, every 50 ms) compares the watched RAM with a shadow copy and logs every
 * changed halfword. No firmware hook, no call into the camera, nothing in the display / overlay code (A89-A92: any
 * change there stopped the extended display).
 *
 * Record at DIAG 0x39840 .. 0x3B000 (A91: the A89 snapshot area, unused; the VUI_DIAG_FOCUS rings start at 0x3B000):
 *   head [0] 'B1LW' [1] ticks [2] log words used [3] changes dropped (log full) [4] halfwords that went quiet
 *        (changed B1_NOISY times, no longer logged) [5] ms of the first tick [6] ms of the last tick
 *        [7] halfwords watched [8] layout 3 (B2: log ends at 0x3AFF0) [9..15] ranges: address, length (bytes) pairs; R3's length is 16
 *   +0x40          baseline: every watched halfword at the first tick
 *   +0x40 + 2n     shadow: the value last seen
 *   +0x40 + 4n     change count per halfword (u8, saturates at 255)
 *   B1_EV_OFF      log words: 0xFFFFFFFF then the ms of the tick, once per tick with changes; then one word per
 *                  change: halfword index | new value << 16
 * Halfword index i < B1_NHW_RAM is RAM halfword i of the ranges in order; B1_NHW_RAM is the module's own aspect
 * state: a43_is_on() | a43_sel << 4 | a43_status << 8.
 * Only .text here (hook image): no string literals or const tables. camera_verified: false. */
#ifndef A83_TRACE
#error B1_LVWIN keeps its record inside the A83 trace area (it calls a83_head first): build with -DA83_TRACE
#endif
#define B1_OFF        0x39840u
#define B1_END        0x3B000u
#define B1_MAGIC      0x574C3142u     /* 'B1LW' */
#define B1_R0         0xC2380280u     /* view block: window +0x5E of 0xC23802C8, fit outputs */
#define B1_R0_LEN     0x480u
#define B1_R1         0xC2380BA0u     /* fit parameter block 0xC2380BAC */
#define B1_R1_LEN     0x60u
#define B1_R2         0xC25043A4u     /* actual image area (setting 0x169E) */
#define B1_R2_LEN     4u
#define B1_R3         0xC1D61388u     /* GUI layer enables (LCD) */
#define B1_R3_LEN     0x10u
#define B1_NHW_RAM    ((B1_R0_LEN + B1_R1_LEN + B1_R2_LEN + B1_R3_LEN) / 2u)
#define B1_NHW        (B1_NHW_RAM + 1u)
#define B1_NHW_PAD    ((B1_NHW + 3u) & ~3u)
#define B1_EV_OFF     (0x40u + B1_NHW_PAD * 5u)
#define B1_LOG_END    0x3AFF0u        /* B2 counters 0x3AFF0 .. 0x3B000 (aspect_fw.inc AF_B2_OFF) */
#define B1_EV_CAP     ((B1_LOG_END - B1_OFF - B1_EV_OFF) / 4u)     /* words */
#define B1_NOISY      8u

IMG_FN static u32 b1_value(cm_t *c, u32 i)
{
    u32 a;
    if (i >= B1_NHW_RAM) {
        bt_t *t = BTP(c);
        return t ? (a43_is_on() ? 1u : 0u) | (t->a43_sel & 0xFu) << 4 | (t->a43_status & 0xFFu) << 8 : 0xFFFFu;
    }
    a = i * 2u;
    if (a < B1_R0_LEN) return *(volatile const u16 *)(B1_R0 + a);
    a -= B1_R0_LEN;
    if (a < B1_R1_LEN) return *(volatile const u16 *)(B1_R1 + a);
    a -= B1_R1_LEN;
    if (a < B1_R2_LEN) return *(volatile const u16 *)(B1_R2 + a);
    a -= B1_R2_LEN;
    return *(volatile const u16 *)(B1_R3 + a);
}

IMG_FN static void b1_watch(cm_t *c)       /* module task, every 50 ms */
{
    volatile u32 *h;
    volatile u16 *base, *shadow;
    volatile u8 *count;
    u32 i, ms, mark;
    if (!c->dg_buf) return;
    (void)a83_head(c);                     /* sets up 0x38000 .. 0x3B000 once, so it never clears this record later */
    h = (volatile u32 *)(c->dg_buf + B1_OFF);
    base = (volatile u16 *)(c->dg_buf + B1_OFF + 0x40u);
    shadow = base + B1_NHW_PAD;
    count = (volatile u8 *)(shadow + B1_NHW_PAD);
    ms = *(volatile u32 *)CM_MS_CLOCK;
    if (h[0] != B1_MAGIC) {
        for (i = 0; i < (B1_LOG_END - B1_OFF) / 4u; i++) h[i] = 0u;
        h[7] = B1_NHW; h[8] = 3u;
        h[9] = B1_R0; h[10] = B1_R0_LEN; h[11] = B1_R1; h[12] = B1_R1_LEN; h[13] = B1_R2; h[14] = B1_R2_LEN;
        h[15] = B1_R3;                     /* B1_R3_LEN: the decoder knows it (16) */
        for (i = 0; i < B1_NHW; i++) { u32 v = b1_value(c, i); base[i] = (u16)v; shadow[i] = (u16)v; }
        h[5] = ms; h[6] = ms; h[1] = 1u;
        h[0] = B1_MAGIC;
        return;
    }
    for (i = 0, mark = 0u; i < B1_NHW; i++) {
        u32 v = b1_value(c, i) & 0xFFFFu, n;
        if (v == shadow[i]) continue;
        shadow[i] = (u16)v;
        n = count[i];
        if (n < 255u) count[i] = (u8)(n + 1u);
        if (n >= B1_NOISY) continue;
        if (n + 1u == B1_NOISY) h[4] += 1u;
        if (h[2] + (mark ? 1u : 3u) > B1_EV_CAP) { h[3] += 1u; continue; }
        {
            volatile u32 *e = (volatile u32 *)(c->dg_buf + B1_OFF + B1_EV_OFF) + h[2];
            if (!mark) { *e++ = 0xFFFFFFFFu; *e++ = ms; h[2] += 2u; mark = 1u; }
            *e = i | v << 16;
            h[2] += 1u;
        }
    }
    h[1] += 1u; h[6] = ms;
}
