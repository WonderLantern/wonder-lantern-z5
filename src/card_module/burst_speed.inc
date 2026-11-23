/* Burst speed-up ("连拍提速"): setup-menu row, its two switches and the background task.
 *
 * Camera-tested as separate readout builds (research L41-L51, 2026-11-14/15); this release combines them:
 *   - the live-view-restart capture holds 0x56 0x3c 0x3d 0x59 0x3f are taken out of the blocking set
 *     (list A at 0xC18A89E0, firmware remove 0x10062860 / add back 0x100628A6), so the next frame does not
 *     wait for live view to come back (L41);
 *   - burst interval words for the firmware entry at 0x10192F96: mechanical code 7 (125 ms), silent code 5
 *     (166 ms); the shutter and readout still limit the real rate (L43-L51 on camera: ~195 / ~300 ms);
 *   - flag word for the firmware entries at 0x10206E6C (low-light immediate go, L48) and 0x1020857A (no
 *     "back to live view" after each shot, L50): P + 0x14 = 0xBF5A1C00 | BURST_FLAG_BITS while the mode is on.
 *   The shutter lock seen with the first release (2026-11-16) was the start-up hang below, not these values;
 *   BURST_MECH_CODE / BURST_FLAG_BITS / BURST_DO_HOLD stay overridable (-D) for bisecting.
 *   - "View all in continuous mode" (setting 0x112C) is left alone (L56): any module write to it hung the
 *     camera / emulator; users switch it off themselves (with it on the screen can stay black after a burst).
 * Exposure and focus stay at the first frame (as in an "extended" burst): suited to AF-S, not to tracking.
 * Measured: mechanical 233 -> ~195 ms (also in low light, stock ~270), silent 400 -> ~300 ms.
 */
#define BURST_SLOT_ENTRY  254u             /* setup row "连拍提速" */
#define BURST_SLOT_MECH   255u             /* page row "机械快门提速：开/关" */
#define BURST_SLOT_SIL    256u             /* page row "电子快门提速：开/关" */
#define BURST_ID_ENTRY    0x2FCAu          /* ids not present in the stock ROM data */
#define BURST_ID_MECH     0x2FCBu
#define BURST_ID_SIL      0x2FCCu
/* MENU.DAT word 2 bits BURST_CFG_MECH_OFF / BURST_CFG_SIL_OFF are defined in main.c (default on). */

#define BURST_BLOCK_SET     0xC18A89E0u    /* list A: [0] bit count, [1] word pointer */
#define BURST_HOLD_REMOVE   0x10062861u    /* remove id from list A (locks, notifies) */
#define BURST_HOLD_ADD      0x100628A7u    /* add id to list A */
#define BURST_HOLD_IDS      0x3F593D3Cu    /* 0x3c 0x3d 0x59 0x3f; plus 0x56 */
#define BURST_FLAG_MAGIC    0xBF5A1C00u
#define BURST_RATE_MAGIC    0xB5000000u
#ifndef BURST_MECH_CODE
#define BURST_MECH_CODE     7u             /* 125 ms */
#endif
#ifndef BURST_SIL_CODE
#define BURST_SIL_CODE      5u             /* 166 ms */
#endif
#ifndef BURST_DO_HOLD
#define BURST_DO_HOLD       1u             /* take 0x56/0x3c/0x3d/0x59/0x3f out of the blocking set (L41) */
#endif
#ifndef BURST_EXTRA_BITS
#define BURST_EXTRA_BITS    0u             /* flag bits set whatever the mode (A7 test: 4 = M900, key 0x11A3 reads on) */
#endif
#ifndef BURST_FLAG_BITS
#define BURST_FLAG_BITS     3u             /* P + 0x14 = 0xBF5A1C00 | bits: bit0 immediate go, bit1 no live-view return */
#endif
#define BURST_SLEEP ((s32 (*)(u32))0x100363A8u)  /* same sleep the custom DISP task uses */
#define BURST_EXIT  ((void (*)(void))0x100360E4u)
#define BURST_TIMEOUT (-3276800)

IMG_FN static u32 burst_hold_id(u32 k) { return k ? (BURST_HOLD_IDS >> (8u * (k - 1u))) & 0xFFu : 0x56u; }

/* Firmware words in the SDK state block P: interval (P + 0x18 / ~ at 0x1C) and switch flags (P + 0x14). */
IMG_FN static void burst_words(cm_t *c, u32 flags)
{
    u32 p = *(volatile u32 *)Z5_STATE_SLOT;
    if (p < RAM_LO || p >= RAM_HI || *(volatile u32 *)Z5_STATE_CHECK != ~p) return;
    u32 v = BURST_RATE_MAGIC | ((c->burst_sil ? BURST_SIL_CODE : 0u) << 8) | (c->burst_mech ? BURST_MECH_CODE : 0u);
    *(volatile u32 *)(p + 0x1Cu) = ~v;
    *(volatile u32 *)(p + 0x18u) = v;
    *(volatile u32 *)(p + 0x14u) = flags ? (BURST_FLAG_MAGIC | flags) : 0u;
}

/* One pass: holds and flag words for the current mode. Never touches camera settings (see below). */
IMG_FN static void burst_step(cm_t *c)
{
    u32 silent = *(volatile u8 *)SILENT_BYTE == 2u;
    u32 active = silent ? c->burst_sil : c->burst_mech;
#if BURST_DO_HOLD
    u32 nbits = *(volatile u32 *)BURST_BLOCK_SET, words = *(volatile u32 *)(BURST_BLOCK_SET + 4u);
    if (nbits > 0x60u && nbits <= 0x400u && words >= RAM_LO && words < RAM_HI) {
        for (u32 k = 0u; k < 5u; k++) {
            u32 id = burst_hold_id(k), in = ((volatile u32 *)words)[id >> 5] & (1u << (id & 31u));
            if (active && in) {
                ((u32 (*)(u32))BURST_HOLD_REMOVE)(id);
                c->burst_removed |= 1u << k;
            } else if (!active && !in && (c->burst_removed & (1u << k))) {
                ((u32 (*)(u32))BURST_HOLD_ADD)(id);   /* back to the stock blocking set */
                c->burst_removed &= ~(1u << k);
            }
        }
    }
#endif
    burst_words(c, (active ? BURST_FLAG_BITS : 0u) | BURST_EXTRA_BITS);
    /* L56: no camera-setting access from this task. Writing 0x112C here (L53 every 0.5 s, L54 once)
     * hangs the camera at power-on (emulator: start never completes; camera: shutter locked). */
}


#ifdef BULB_MENU
#define BT_RUN_MS(c) (BTP(c)->stop ? 0u : BTP(c)->secs * 1000u)
#else
#define BT_RUN_MS(c) BULB_TIMER_MS
#endif
#ifdef BULB_TIMER_MS
/* A12 (M900 search, 2026-11-16): timed Bulb through the camera's own remote-capture path (the USB 0x9207 / 0x920C
 * internals, camera-tested over USB: start -> ~10 s open -> end). Armed by "保存测试记录" in Bulb (setting 0x020E = 2);
 * BT_DELAY_MS later the task calls 0x10219DDE(0xC29C46D8, [0xC18CA1CC]) (remote release start, request 0x46) and
 * BULB_TIMER_MS after that 0x1021A32C(&0) (remote end). State block in the record buffer at 0xDC0 ('TB12'):
 * [1] phase 0 idle / 1 armed / 2 open / 3 done, [2] time (arm target, then start ms), [3] start result,
 * [4] end result, [5] / [6] USB capture state 0xC18A84E4 / exposure-open word 0xC18A7EE4 500 ms after the start
 * (0x100 | value), [7] end ms, [8] the same two 1 s after the end (0x100 | state | open << 4), [9] reason if skipped. */
#ifndef BT_DELAY_MS
#define BT_DELAY_MS    6000u
#endif
#define BT_START       0x10219DDFu
#define BT_END         0x1021A32Du
#define BT_SESSION     0xC29C46D8u
#define BT_PARAM       0xC18CA1CCu
#define BT_MAGIC       0x32314254u          /* 'TB12' */
IMG_FN static u32 bt_mode(void)
{
    u32 p = *(const volatile u32 *)(0x115C286Cu + 66u * 16u + 8u);   /* setting 0x020E: 0 normal, 2 Bulb, 3 Time */
    return (p >= RAM_LO && p < RAM_HI) ? *(volatile u32 *)p : 0xFFu;
}

IMG_FN static void bulb_arm(cm_t *c)
{
    if (!c->dg_buf || bt_mode() != 2u) return;
    volatile u32 *q = (volatile u32 *)(c->dg_buf + 0xDC0u);
    for (u32 i = 1u; i < 12u; i++) q[i] = 0u;
    q[2] = *(volatile u32 *)CM_MS_CLOCK + BT_DELAY_MS;
    q[0] = BT_MAGIC;
    q[1] = 1u;
}

#ifdef BULB_PCMODE
/* A13: the camera only accepts the remote start in computer-control mode (USB test: 0x90C2(1) first). This copies
 * the 0x90C2 executor 0x103564FA without the USB reply: property 0x2400 = current mode; if different: (mode 0:
 * 0x1031B874 ? 0x10331DF2, 0x1031B834 ? 0x10219D84), clear flag 0x20000 (0x103563A4), busy byte 0xC18CA1D4 = 1,
 * request 0x040F0007 {mode, 0, callback 0x103564C3} (0x1032FC26), wait for 0x20000 (0x10356412, no timeout), then
 * poll property 0x1B0C == 1 (300 x 10 ms). Returns 0x10000 | mode if already set, else busy byte | polls << 8. */
#define PCM_PROP       ((u32 (*)(u32))0x10361813u)
IMG_FN static u32 pc_mode(u32 mode)
{
    u32 s[3], i;
    if ((PCM_PROP(0x2400u) & 0xFFu) == mode) return 0x10000u | mode;
    if (!mode) {
        if (((u32 (*)(void))0x1031B875u)()) ((void (*)(void))0x10331DF3u)();
        if (((u32 (*)(void))0x1031B835u)()) ((void (*)(void))0x10219D85u)();
    }
    s[0] = mode; s[1] = 0u; s[2] = 0x103564C3u;
    ((void (*)(u32))0x103563A5u)(0x20000u);
    *(volatile u8 *)0xC18CA1D4u = 1u;
    ((void (*)(u32, u32 *))0x1032FC27u)(0x040F0007u, s);
    ((void (*)(u32, u32))0x10356413u)(0x20000u, 0u);
    for (i = 0u; i < 300u; i++) {
        if ((PCM_PROP(0x1B0Cu) & 0xFFu) == 1u) break;
        ((void (*)(u32))0x10232937u)(10u);
    }
    return *(volatile u8 *)0xC18CA1D4u | (i << 8);
}
#endif

#ifdef BULB_FULLEXEC
/* A14: run the USB 0x9207 executor 0x10317724 step by step without its USB reply (0x1044912E): a fake request
 * (opcode at +6 = 0x9207, +8 = 0, params +0xC = 0xFFFFFFFF, +0x10 = 0) -> 0x10317622(&req, 0xC29C46D8) fills the
 * capture struct, 0x103565C0(1), 0x1030872E(req +8), start 0x10219DDE(0xC29C46D8, [0xC18CA1CC]), then as the
 * executor: 0x1031752A(result, 2) == 0x2001 ? (0x1031B780(1), 0x1031B77A(struct +8 ? 2 : 1)) : 0x1030872E(0);
 * 0x10356602(). Returns the 0x10219DDE result | PTP code << 16. */
IMG_FN static u32 bulb_exec(void)
{
    u32 req[8], r, code;
    for (u32 i = 0u; i < 8u; i++) req[i] = 0u;
    ((volatile u16 *)req)[3] = 0x9207u;
    req[3] = 0xFFFFFFFFu;
    ((void (*)(u32 *, u32))0x10317623u)(req, BT_SESSION);
    ((void (*)(u32))0x103565C1u)(1u);
    ((void (*)(u32))0x1030872Fu)(req[2]);
    r = ((u32 (*)(u32, u32))BT_START)(BT_SESSION, *(volatile u32 *)BT_PARAM);
    code = ((u32 (*)(u32, u32))0x1031752Bu)(r, 2u) & 0xFFFFu;
    if (code == 0x2001u) {
        ((void (*)(u32))0x1031B781u)(1u);
        ((void (*)(u32))0x1031B77Bu)(((volatile u32 *)BT_SESSION)[2] ? 2u : 1u);
    } else {
        ((void (*)(u32))0x1030872Fu)(0u);
    }
    ((void (*)(void))0x10356603u)();
    return (r & 0xFFFFu) | (code << 16);
}
#endif

#ifdef BT_HALF_PRESS
/* Simulated half press through the camera's own switch input (as the emulator presses keys, vui-probe key_input.py):
 * 0x101ED79E returns the current switch state (0xC1F99450, 0x9C bytes); 0x101ED678 copies a 52-byte state
 * {group words at +4/+0x18/+0x2C/+0x40, 4 zero words, 6 encoder halves at +0x64 step 8, words at +0x94/+0x98}
 * to 0xC1F995C4 and signals the switch handler. Group 0 bit 31 = half press. */
#ifndef BT_HALF_MS
#define BT_HALF_MS     300u
#endif
IMG_FN static void bt_half(u32 down)
{
    u32 b[13], k;
    u32 s = ((u32 (*)(void))0x101ED79Fu)();
    if (s < RAM_LO || s >= RAM_HI) return;
    b[0] = *(volatile u32 *)(s + 4u); b[1] = *(volatile u32 *)(s + 0x18u);
    b[2] = *(volatile u32 *)(s + 0x2Cu); b[3] = *(volatile u32 *)(s + 0x40u);
    b[4] = 0u; b[5] = 0u; b[6] = 0u; b[7] = 0u;
    for (k = 0; k < 6u; k++) ((u16 *)&b[8])[k] = *(volatile u16 *)(s + 0x64u + k * 8u);
    b[11] = *(volatile u32 *)(s + 0x94u); b[12] = *(volatile u32 *)(s + 0x98u);
    if (down) b[0] |= 0x80000000u; else b[0] &= ~0x80000000u;
    ((void (*)(u32 *))0x101ED679u)(b);
}
#endif


#ifdef BULB_MENU
/* A55: Bulb series ("拍摄张数" / "间隔"). Each shot is the camera-verified single run (computer-control switch on,
 * 0x9207 executor, end, switch off); between shots the task waits until the interval has passed and the camera is
 * ready: property 0x1B17 clear (the busy test the USB 0x9207 checker 0x103173F6 answers 0x2019 with) and the
 * exposure-open word 0xC18A7EE4 clear, seen on two passes in a row, and at least BT_GAP_MIN_MS after the end.
 * The getters the USB checker 0x103177D4 also asks (0x1021F528, 0x102203A2, 0x102203C6) are only recorded.
 * No ready camera within the exposure time + BT_READY_MS ends the series (reason 0xB5000000 | busy bits).
 * Record 'TBS1' at DIAG 0x36400: [1] shots, [2] done, [3] interval s, [4] exposure s, [5] last busy bits,
 * [6] last wait ms, [7] stop reason; per shot from +0x40, 64 x 16 bytes: start word, start ms, end ms, wait ms
 * before it. camera_verified: false. */
#define BT_SERIES_OFF  0x36400u
#define BT_SERIES_MAGIC 0x31534254u         /* 'TBS1' */
#ifndef BT_GAP_MIN_MS
#define BT_GAP_MIN_MS  2000u
#endif
#ifndef BT_READY_MS
#define BT_READY_MS    120000u
#endif
IMG_FN static u32 bt_count(u32 i)           /* 1..10, 15..50 by 5, 60..100 by 10, 200..900 by 100, 999 */
{
    i &= 31u;
    if (i < 10u) return i + 1u;
    if (i < 18u) return 15u + 5u * (i - 10u);
    if (i < 23u) return 60u + 10u * (i - 18u);
    if (i < 31u) return 200u + 100u * (i - 23u);
    return 999u;
}

IMG_FN static u32 bt_gap(u32 i)             /* seconds: 0..10, 15..60 by 5, 90..300 by 30, 600, 900, 1800 */
{
    i &= 31u;
    if (i < 11u) return i;
    if (i < 21u) return 15u + 5u * (i - 11u);
    if (i < 29u) return 90u + 30u * (i - 21u);
    return i == 29u ? 600u : i == 30u ? 900u : 1800u;
}

IMG_FN static u32 bt_busy(void)
{
    return (PCM_PROP(0x1B17u) ? 1u : 0u) | (*(volatile u32 *)0xC18A7EE4u ? 2u : 0u) |
           (((u32 (*)(void))0x1021F529u)() ? 4u : 0u) | (((u32 (*)(void))0x102203A3u)() ? 8u : 0u) |
           (((u32 (*)(void))0x102203C7u)() ? 16u : 0u);
}

IMG_FN static volatile u32 *bt_log(cm_t *c)
{
    return c->dg_buf ? (volatile u32 *)(c->dg_buf + BT_SERIES_OFF) : 0;
}

IMG_FN static void bt_end_series(cm_t *c, volatile u32 *q, u32 why)
{
    volatile u32 *g = bt_log(c);
    q[1] = 3u;
    if (why) q[9] = why;
    if (g) g[7] = why;
}
#endif

IMG_FN static void bulb_step(cm_t *c)
{
#ifdef BULB_MENU
    if (!BTP(c)) return;
    volatile u32 *q = BTP(c)->q;               /* armed from the menu, mirrored into the record by burst_task */
    if (BTP(c)->close_at && (s32)(*(volatile u32 *)CM_MS_CLOCK - BTP(c)->close_at) >= 0) {
        BTP(c)->close_at = 0u;                 /* "开始" pressed: leave the menu */
#ifdef BT_HALF_PRESS
        bt_half(1u);                           /* a simulated half press, released BT_HALF_MS later */
        BTP(c)->half_off = (*(volatile u32 *)CM_MS_CLOCK + BT_HALF_MS) | 1u;
#else
        ((void (*)(u32, u32, u32))0x104B544Fu)(0xC182DC14u, 1u, 0u);
#endif
    }
#ifdef BT_HALF_PRESS
    if (BTP(c)->half_off && (s32)(*(volatile u32 *)CM_MS_CLOCK - BTP(c)->half_off) >= 0) {
        BTP(c)->half_off = 0u;
        bt_half(0u);
    }
#endif
#else
    if (!c->dg_buf) return;
    volatile u32 *q = (volatile u32 *)(c->dg_buf + 0xDC0u);
#endif
    u32 now = *(volatile u32 *)CM_MS_CLOCK;
    if (q[0] != BT_MAGIC) return;
#ifdef BULB_MENU
    if (q[1] == 1u && BTP(c)->stop && BTP(c)->done) {   /* A55: ended between shots */
        BTP(c)->stop = 0u;
        bt_end_series(c, q, 0x5E000000u | BTP(c)->done);
        return;
    }
#endif
    if (q[1] == 1u && (s32)(now - q[2]) >= 0) {
        if (bt_mode() != 2u) { q[9] = 0xBAD00000u | bt_mode(); q[1] = 3u; return; }
#ifdef BULB_MENU
        for (u32 i = 3u; i <= 8u; i++) q[i] = 0u;   /* per-shot words (A55: a series reuses the block) */
#endif
#ifdef BULB_PCMODE
        q[10] = pc_mode(1u);                   /* 0xDE8: switch to computer control first */
        now = *(volatile u32 *)CM_MS_CLOCK;
#endif
        q[2] = now;
        q[1] = 2u;
#ifdef BULB_FULLEXEC
        q[3] = bulb_exec();
#else
        q[3] = ((u32 (*)(u32, u32))BT_START)(BT_SESSION, *(volatile u32 *)BT_PARAM);
#endif
#ifdef BULB_MENU
        {
            volatile u32 *g = bt_log(c);
            if (g) {
                volatile u32 *e = g + 16u + (BTP(c)->done & 63u) * 4u;
                e[0] = q[3]; e[1] = q[2]; e[2] = 0u; e[3] = BTP(c)->done ? now - BTP(c)->wait_from : 0u;
            }
        }
#endif
    } else if (q[1] == 2u) {
        if (!q[5] && now - q[2] >= 500u) {
            q[5] = 0x100u | *(volatile u8 *)0xC18A84E4u;
            q[6] = 0x100u | *(volatile u32 *)0xC18A7EE4u;
        }
        if (!q[9] && now - q[2] >= 35000u)     /* A15: past the old 30 s limit, still open? 0x35000000 | state | open << 4 */
            q[9] = 0x35000000u | *(volatile u8 *)0xC18A84E4u | (*(volatile u32 *)0xC18A7EE4u << 4);
        if (now - q[2] >= BT_RUN_MS(c)) {
            u32 z = 0u;
#ifdef BULB_MENU
            u32 stopped = BTP(c)->stop;
            BTP(c)->stop = 0u;
#endif
            q[4] = ((u32 (*)(u32 *))BT_END)(&z);
            q[7] = now;
            q[1] = 3u;
#ifdef BULB_PCMODE
            q[11] = pc_mode(0u);               /* 0xDEC: back from computer control */
#endif
#ifdef BULB_MENU
            {
                volatile u32 *g = bt_log(c);
                if (g) { g[16u + (BTP(c)->done & 63u) * 4u + 2u] = now; g[2] = BTP(c)->done + 1u; }
            }
            BTP(c)->done += 1u;
            BTP(c)->wait_from = *(volatile u32 *)CM_MS_CLOCK;
            BTP(c)->ready = 0u;
            if (stopped) bt_end_series(c, q, 0x5E000000u | BTP(c)->done);
            else if (BTP(c)->done < BTP(c)->total) q[1] = 4u;   /* A55: next shot after the interval */
#endif
        }
    } else if (q[1] == 3u && q[7] && !q[8] && now - q[7] >= 1000u) {
        q[8] = 0x100u | *(volatile u8 *)0xC18A84E4u | (*(volatile u32 *)0xC18A7EE4u << 4);
    }
#ifdef BULB_MENU
    else if (q[1] == 4u) {                     /* A55: between the shots of a series */
        bt_t *t = BTP(c);
        u32 waited = now - t->wait_from, busy = bt_busy();
        volatile u32 *g = bt_log(c);
        if (!q[8] && waited >= 1000u)
            q[8] = 0x100u | *(volatile u8 *)0xC18A84E4u | (*(volatile u32 *)0xC18A7EE4u << 4);
        if (g) { g[5] = busy; g[6] = waited; }
        if (t->stop) {
            t->stop = 0u;
            bt_end_series(c, q, 0x5E000000u | t->done);
        } else if (bt_mode() != 2u) {
            bt_end_series(c, q, 0xBAD00000u | bt_mode());
        } else if (waited >= BT_GAP_MIN_MS && waited >= bt_gap(t->gap_i) * 1000u) {
            if (busy & 3u) {
                t->ready = 0u;
                if (waited >= t->secs * 1000u + bt_gap(t->gap_i) * 1000u + BT_READY_MS)
                    bt_end_series(c, q, 0xB5000000u | busy);
            } else if (++t->ready >= 2u) {
                t->ready = 0u;
                q[2] = now;
                q[1] = 1u;                     /* the next pass starts the shot */
            }
        }
    }
#endif
}
#endif

#ifdef BULB_MENU
/* "B门定时" page (photo shooting menu row -> 6 rows): start / cancel, hours, minutes, seconds; A55: shots, interval.
 * Right or OK adds one, left takes one (wraps 0..9 h, 0..59 min / s). Start arms the run BT_DELAY_MS later
 * (time to leave the menu with a half press); pressed again it cancels (before the start) or ends now.
 * Only in Bulb (0x020E = 2); otherwise the row says so. Labels always Simplified Chinese (AGENTS.md rule 8). */
#define BT_SLOT_ENTRY  257u             /* photo shooting menu row "B门定时：h:mm:ss" */
#define BT_SLOT_ROW    258u             /* 258..263: start, hours, minutes, seconds, shots, interval (A55) */
#define BT_ROWS        6u
#define BT_ID_ENTRY    0x2FC0u          /* ids not present in the stock ROM row tables */
#define BT_ID_ROW      0x2FC1u
#ifndef BT_CLOSE_MS
#define BT_CLOSE_MS    200u
#endif
#ifndef BT_EXIT_DEST
#define BT_EXIT_DEST   STAY             /* start row after a successful start: STAY or "back N" (0x406 + N) out of the menu */
#endif

/* The menu route (0x1047897A) reads the entry's destination after the handler ran, so the handler picks it:
 * leave the menu when the run was armed, stay (and show why) otherwise. */
IMG_FN static void bt_dest(cm_t *c, u32 dest)
{
    u32 *m = (u32 *)BTP(c)->map0;
    if (!m) return;
    for (u32 i = 0; i < 40u && m[i * 4u] != EV_END; i++) {
        u32 ev = m[i * 4u];
        if (ev == 0x24Au || ev == 0x24Fu || ev == 0x263u || ev == 0x270u) m[i * 4u + 3u] = dest;
    }
}

IMG_FN static void bt_two(u16 *b, u32 *n, u32 v)
{
    u32 t = 0u;
    while (v >= 10u) { v -= 10u; t++; }
    hz_put(b, n, '0' + t); hz_put(b, n, '0' + v);
}

IMG_FN static void bt_time(cm_t *c, u16 *b, u32 *n)
{
    if (BTP(c)->h >= 10u) bt_two(b, n, BTP(c)->h); else hz_put(b, n, '0' + BTP(c)->h);
    hz_put(b, n, ':'); bt_two(b, n, BTP(c)->m); hz_put(b, n, ':'); bt_two(b, n, BTP(c)->s);
}

IMG_FN static void bt_name(u16 *b, u32 *n)      /* B门定时 */
{
    hz_put(b, n, 'B'); hz_put(b, n, 0x95E8u); hz_put(b, n, 0x5B9Au); hz_put(b, n, 0x65F6u);
}

IMG_FN static void bt_set(u16 *b, u32 n, u32 slot)
{
    b[n] = 0;
    FW(set_fn, UTF16_SET)(b, slot, HANS_STYLE);
    *(volatile u8 *)RUNTIME_STYLE = HANS_FONT;
}

/* English (A54): same rows in ASCII; packed immediates, no .rodata (hook image). */
IMG_FN static void bt_two_en(char *b, u32 *n, u32 v)
{
    u32 t = 0u;
    while (v >= 10u) { v -= 10u; t++; }
    en_put(b, n, '0' + t); en_put(b, n, '0' + v);
}

IMG_FN static void bt_time_en(cm_t *c, char *b, u32 *n)
{
    if (BTP(c)->h >= 10u) bt_two_en(b, n, BTP(c)->h); else en_put(b, n, '0' + BTP(c)->h);
    en_put(b, n, ':'); bt_two_en(b, n, BTP(c)->m); en_put(b, n, ':'); bt_two_en(b, n, BTP(c)->s);
}

IMG_FN static void bt_set_en(char *b, u32 n, u32 slot)
{
    b[n] = 0;
    FW(set_fn, ASCII_SET)(b, slot, 0u);
}

/* A55: decimal (no division helper in the module) into a UTF-16 (hb) or ASCII (eb) buffer. */
IMG_FN static void bt_num(u16 *hb, char *eb, u32 *n, u32 v)
{
    u32 started = 0u, p = 1000u;
    while (p) {
        u32 d = 0u;
        while (v >= p) { v -= p; d++; }
        if (d || started || p == 1u) {
            started = 1u;
            if (hb) hz_put(hb, n, '0' + d); else en_put(eb, n, '0' + d);
        }
        p = p == 1000u ? 100u : p == 100u ? 10u : p == 10u ? 1u : 0u;
    }
}

IMG_FN static void bt_gap_text(u16 *hb, char *eb, u32 *n, u32 v)   /* "1分30秒" / "1 min 30 s" */
{
    u32 m = 0u;
    while (v >= 60u) { v -= 60u; m++; }
    if (m) { bt_num(hb, eb, n, m); if (hb) hz_put(hb, n, 0x5206u); else en_w(eb, n, 0x6E696D20u); }
    if (v || !m) {
        if (!hb && m) en_put(eb, n, ' ');
        bt_num(hb, eb, n, v);
        if (hb) hz_put(hb, n, 0x79D2u); else en_w(eb, n, 0x00007320u);
    }
}

/* " 3/10" while a series runs (shot i of the total); "×10" after the time when more than one shot is set. */
IMG_FN static void bt_prog(cm_t *c, u16 *hb, char *eb, u32 *n, u32 i)
{
    if (BTP(c)->total < 2u) return;
    if (hb) hz_put(hb, n, ' '); else en_put(eb, n, ' ');
    bt_num(hb, eb, n, i);
    if (hb) hz_put(hb, n, '/'); else en_put(eb, n, '/');
    bt_num(hb, eb, n, BTP(c)->total);
}

IMG_FN static void bt_times(cm_t *c, u16 *hb, char *eb, u32 *n)
{
    u32 k = bt_count(BTP(c)->cnt_i);
    if (k < 2u) return;
    if (hb) hz_put(hb, n, 0x00D7u); else en_w(eb, n, 0x00007820u);   /* "×" / " x" */
    bt_num(hb, eb, n, k);
}

IMG_FN static void bt_kind(cm_t *c)
{
    u32 kind = c->hans ? F_UTF16 : F_ASCII;
    if (BTP(c)->entry_row) {
        volatile u32 *r = (volatile u32 *)BTP(c)->entry_row;
        r[4] = (r[4] & ~(F_ASCII | F_UTF16)) | kind;
    }
    if (BTP(c)->hdr) {
        const volatile u32 *h = (const volatile u32 *)BTP(c)->hdr;
        u32 *rows = (u32 *)h[3], count = h[5] & 0xFFFFu;
        for (u32 k = 0; rows && k < count; k++) {
            volatile u32 *r = (volatile u32 *)(rows + k * ROW_WORDS);
            r[4] = (r[4] & ~(F_ASCII | F_UTF16)) | kind;
        }
    }
}

IMG_FN static void bt_labels_en(cm_t *c)
{
    char b[40];
    u32 n = 0, ph = BTP(c)->q[1];
    en_w(b, &n, 0x656D6954u); en_w(b, &n, 0x75422064u); en_w(b, &n, 0x203A626Cu); bt_time_en(c, b, &n);
    bt_times(c, 0, b, &n);
    bt_set_en(b, n, BT_SLOT_ENTRY);                                   /* "Timed Bulb: h:mm:ss x10" */
    n = 0;
    if (ph == 4u || (ph == 1u && BTP(c)->done)) {                      /* A55: "Interval 3/10 (OK ends)" */
        en_w(b, &n, 0x65746E49u); en_w(b, &n, 0x6C617672u); bt_prog(c, 0, b, &n, BTP(c)->done);
        en_w(b, &n, 0x4B4F2820u); en_w(b, &n, 0x646E6520u); en_w(b, &n, 0x00002973u);
    } else if (ph == 2u) {                                             /* "Exposing 3/10 (OK ends)" */
        en_w(b, &n, 0x6F707845u); en_w(b, &n, 0x676E6973u); bt_prog(c, 0, b, &n, BTP(c)->done + 1u);
        en_w(b, &n, 0x4B4F2820u); en_w(b, &n, 0x646E6520u); en_w(b, &n, 0x00002973u);
    } else if (ph == 1u) {                                             /* "Waiting (OK cancels)" */
        en_w(b, &n, 0x74696157u); en_w(b, &n, 0x20676E69u); en_w(b, &n, 0x204B4F28u);
        en_w(b, &n, 0x636E6163u); en_w(b, &n, 0x29736C65u);
    } else if (BTP(c)->msg == 1u) {                                    /* "Set Bulb mode first" */
        en_w(b, &n, 0x20746553u); en_w(b, &n, 0x626C7542u); en_w(b, &n, 0x646F6D20u);
        en_w(b, &n, 0x69662065u); en_w(b, &n, 0x00747372u);
    } else if (BTP(c)->msg == 2u) {                                    /* "Set a time first" */
        en_w(b, &n, 0x20746553u); en_w(b, &n, 0x69742061u); en_w(b, &n, 0x6620656Du); en_w(b, &n, 0x74737269u);
    } else {                                                           /* "Start: h:mm:ss x10" */
        en_w(b, &n, 0x72617453u); en_w(b, &n, 0x00203A74u); bt_time_en(c, b, &n); bt_times(c, 0, b, &n);
    }
    bt_set_en(b, n, BT_SLOT_ROW);
    n = 0; en_w(b, &n, 0x72756F48u); en_w(b, &n, 0x00203A73u); en_put(b, &n, '0' + BTP(c)->h);
    bt_set_en(b, n, BT_SLOT_ROW + 1u);                                 /* "Hours: h" */
    n = 0; en_w(b, &n, 0x756E694Du); en_w(b, &n, 0x3A736574u); en_w(b, &n, 0x00000020u); bt_two_en(b, &n, BTP(c)->m);
    bt_set_en(b, n, BT_SLOT_ROW + 2u);                                 /* "Minutes: mm" */
    n = 0; en_w(b, &n, 0x6F636553u); en_w(b, &n, 0x3A73646Eu); en_w(b, &n, 0x00000020u); bt_two_en(b, &n, BTP(c)->s);
    bt_set_en(b, n, BT_SLOT_ROW + 3u);                                 /* "Seconds: ss" */
    n = 0; en_w(b, &n, 0x746F6853u); en_w(b, &n, 0x00203A73u); bt_num(0, b, &n, bt_count(BTP(c)->cnt_i));
    bt_set_en(b, n, BT_SLOT_ROW + 4u);                                 /* A55 "Shots: n" */
    n = 0; en_w(b, &n, 0x65746E49u); en_w(b, &n, 0x6C617672u); en_w(b, &n, 0x0000203Au);
    bt_gap_text(0, b, &n, bt_gap(BTP(c)->gap_i));
    bt_set_en(b, n, BT_SLOT_ROW + 5u);                                 /* A55 "Interval: 1 min 30 s" */
}

IMG_FN static void bt_labels(cm_t *c)
{
    c->hans = CM_HANS;                         /* the photo menu does not pass through refresh_labels */
    bt_kind(c);
    if (!c->hans) { bt_labels_en(c); return; }
    u16 b[24];
    u32 n = 0, ph = BTP(c)->q[1];
    bt_name(b, &n); hz_put(b, &n, 0xFF1Au); bt_time(c, b, &n); bt_times(c, b, 0, &n);
    bt_set(b, n, BT_SLOT_ENTRY);
    n = 0;
    if (ph == 4u || (ph == 1u && BTP(c)->done)) {   /* A55: 间隔中 3/10（OK结束） */
        hz_put(b, &n, 0x95F4u); hz_put(b, &n, 0x9694u); hz_put(b, &n, 0x4E2Du); bt_prog(c, b, 0, &n, BTP(c)->done);
        hz_put(b, &n, 0xFF08u); hz_put(b, &n, 'O'); hz_put(b, &n, 'K'); hz_put(b, &n, 0x7ED3u); hz_put(b, &n, 0x675Fu);
        hz_put(b, &n, 0xFF09u);
    } else if (ph == 2u) {                     /* 曝光中 3/10（OK结束） */
        hz_put(b, &n, 0x66DDu); hz_put(b, &n, 0x5149u); hz_put(b, &n, 0x4E2Du); bt_prog(c, b, 0, &n, BTP(c)->done + 1u);
        hz_put(b, &n, 0xFF08u); hz_put(b, &n, 'O'); hz_put(b, &n, 'K'); hz_put(b, &n, 0x7ED3u); hz_put(b, &n, 0x675Fu);
        hz_put(b, &n, 0xFF09u);
    } else if (ph == 1u) {                     /* 等待开始（OK取消） */
        hz_put(b, &n, 0x7B49u); hz_put(b, &n, 0x5F85u); hz_put(b, &n, 0x5F00u); hz_put(b, &n, 0x59CBu);
        hz_put(b, &n, 0xFF08u); hz_put(b, &n, 'O'); hz_put(b, &n, 'K'); hz_put(b, &n, 0x53D6u); hz_put(b, &n, 0x6D88u);
        hz_put(b, &n, 0xFF09u);
    } else if (BTP(c)->msg == 1u) {              /* 请先设为B门 */
        hz_put(b, &n, 0x8BF7u); hz_put(b, &n, 0x5148u); hz_put(b, &n, 0x8BBEu); hz_put(b, &n, 0x4E3Au);
        hz_put(b, &n, 'B'); hz_put(b, &n, 0x95E8u);
    } else if (BTP(c)->msg == 2u) {              /* 请先设定时间 */
        hz_put(b, &n, 0x8BF7u); hz_put(b, &n, 0x5148u); hz_put(b, &n, 0x8BBEu); hz_put(b, &n, 0x5B9Au);
        hz_put(b, &n, 0x65F6u); hz_put(b, &n, 0x95F4u);
    } else {                                   /* 开始：h:mm:ss */
        hz_put(b, &n, 0x5F00u); hz_put(b, &n, 0x59CBu); hz_put(b, &n, 0xFF1Au); bt_time(c, b, &n); bt_times(c, b, 0, &n);
    }
    bt_set(b, n, BT_SLOT_ROW);
    n = 0; hz_put(b, &n, 0x5C0Fu); hz_put(b, &n, 0x65F6u); hz_put(b, &n, 0xFF1Au); hz_put(b, &n, '0' + BTP(c)->h);
    bt_set(b, n, BT_SLOT_ROW + 1u);            /* 小时：h */
    n = 0; hz_put(b, &n, 0x5206u); hz_put(b, &n, 0x949Fu); hz_put(b, &n, 0xFF1Au); bt_two(b, &n, BTP(c)->m);
    bt_set(b, n, BT_SLOT_ROW + 2u);            /* 分钟：mm */
    n = 0; hz_put(b, &n, 0x79D2u); hz_put(b, &n, 0xFF1Au); bt_two(b, &n, BTP(c)->s);
    bt_set(b, n, BT_SLOT_ROW + 3u);            /* 秒：ss */
    n = 0; hz_put(b, &n, 0x5F20u); hz_put(b, &n, 0x6570u); hz_put(b, &n, 0xFF1Au); bt_num(b, 0, &n, bt_count(BTP(c)->cnt_i));
    bt_set(b, n, BT_SLOT_ROW + 4u);            /* A55 张数：n */
    n = 0; hz_put(b, &n, 0x95F4u); hz_put(b, &n, 0x9694u); hz_put(b, &n, 0xFF1Au); bt_gap_text(b, 0, &n, bt_gap(BTP(c)->gap_i));
    bt_set(b, n, BT_SLOT_ROW + 5u);            /* A55 间隔：1分30秒 */
}

IMG_FN static void bt_show(cm_t *c, u32 refresh)
{
    u16 b[8];
    u32 n = 0;
    bt_labels(c);                              /* also takes the camera language into c->hans */
    if (c->title) ((volatile u32 *)c->title)[1] = c->hans ? 3u : 2u;   /* shared title widget's text kind */
    if (c->hans) {
        bt_name(b, &n);
        bt_set(b, n, SLOT_TITLE_V);
    } else {
        char e[12];
        en_w(e, &n, 0x656D6954u); en_w(e, &n, 0x75422064u); en_w(e, &n, 0x0000626Cu);   /* "Timed Bulb" */
        bt_set_en(e, n, SLOT_TITLE_V);
    }
    c->title_slot = SLOT_TITLE_V;
    (void)refresh;
    ((volatile u32 *)PAGE0)[10] = BTP(c)->hdr;
    FW(page_fn, OPEN_PAGE)(2u, OWN_INDEX);
}

IMG_FN static u32 bt_entry_ok(u32 a0, u32 a1, u32 a2, u32 a3)
{
    cm_t *c = cm_get();
    (void)a0; (void)a1; (void)a2; (void)a3;
    if (!c || !BTP(c) || !BTP(c)->hdr) return 0;
    BTP(c)->msg = 0u;
    bt_show(c, 0u);
    return 0;
}

IMG_FN static void bt_adjust(u32 field, u32 up)
{
    cm_t *c = cm_get();
    if (!c || !BTP(c) || !cfg_ready(c)) return;
    if (BTP(c)->q[1] != 1u && BTP(c)->q[1] != 2u && BTP(c)->q[1] != 4u) {   /* fixed while a run is on */
        u32 *v = field == 0u ? &BTP(c)->h : field == 1u ? &BTP(c)->m : field == 2u ? &BTP(c)->s :
                 field == 3u ? &BTP(c)->cnt_i : &BTP(c)->gap_i;
        u32 max = field == 0u ? 23u : field <= 2u ? 59u : 31u;   /* A55: shot / interval steps 0..31 */
        *v = up ? (*v >= max ? 0u : *v + 1u) : (*v ? *v - 1u : max);
        BTP(c)->msg = 0u;
        save_cfg(c);
    }
    bt_show(c, 1u);
}

IMG_FN static u32 bt_h_up(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; bt_adjust(0u, 1u); return 0; }
IMG_FN static u32 bt_h_dn(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; bt_adjust(0u, 0u); return 0; }
IMG_FN static u32 bt_m_up(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; bt_adjust(1u, 1u); return 0; }
IMG_FN static u32 bt_m_dn(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; bt_adjust(1u, 0u); return 0; }
IMG_FN static u32 bt_s_up(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; bt_adjust(2u, 1u); return 0; }
IMG_FN static u32 bt_s_dn(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; bt_adjust(2u, 0u); return 0; }
IMG_FN static u32 bt_n_up(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; bt_adjust(3u, 1u); return 0; }
IMG_FN static u32 bt_n_dn(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; bt_adjust(3u, 0u); return 0; }
IMG_FN static u32 bt_g_up(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; bt_adjust(4u, 1u); return 0; }
IMG_FN static u32 bt_g_dn(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; bt_adjust(4u, 0u); return 0; }


IMG_FN static u32 bt_start_ok(u32 a0, u32 a1, u32 a2, u32 a3)
{
    cm_t *c = cm_get();
    (void)a0; (void)a1; (void)a2; (void)a3;
    if (!c || !BTP(c)) return 0;
    volatile u32 *q = BTP(c)->q;
    if (q[1] == 1u && !BTP(c)->done) {
        q[1] = 0u;                             /* cancel before the start */
    } else if (q[1] == 1u || q[1] == 2u || q[1] == 4u) {
        BTP(c)->stop = 1u;                       /* end now (next task pass); A55: no further shots */
    } else if (!cfg_ready(c)) {
        return 0;
    } else if (bt_mode() != 2u) {
        BTP(c)->msg = 1u;
    } else if (!(BTP(c)->h | BTP(c)->m | BTP(c)->s)) {
        BTP(c)->msg = 2u;
    } else {
        for (u32 i = 1u; i < 12u; i++) q[i] = 0u;
        BTP(c)->secs = BTP(c)->h * 3600u + BTP(c)->m * 60u + BTP(c)->s;
        BTP(c)->total = bt_count(BTP(c)->cnt_i);   /* A55 */
        BTP(c)->done = 0u;
        BTP(c)->ready = 0u;
        {
            volatile u32 *g = bt_log(c);
            if (g) {
                for (u32 i = 0u; i < 16u + 64u * 4u; i++) g[i] = 0u;
                g[0] = BT_SERIES_MAGIC; g[1] = BTP(c)->total; g[3] = bt_gap(BTP(c)->gap_i); g[4] = BTP(c)->secs;
            }
        }
        BTP(c)->stop = 0u;
        BTP(c)->msg = 0u;
        q[2] = *(volatile u32 *)CM_MS_CLOCK + BT_DELAY_MS;
        q[0] = BT_MAGIC;
        q[1] = 1u;
        /* Close the menu as a half press does (emulator trace 2026-11-16: event 0x240 -> 0x104787E0 posts message 1
         * to the menu state machine 0xC182DC14 through 0x104B544E). The page is not reopened. */
        bt_labels(c);
        bt_dest(c, STAY);
#if   defined(BT_CLOSE_TASK)
        BTP(c)->close_at = (*(volatile u32 *)CM_MS_CLOCK + BT_CLOSE_MS) | 1u;   /* test: the task closes it */
        bt_show(c, 1u);
#else
        bt_show(c, 1u);                        /* stay: the computer-control switch itself leaves the menu (A16) */
#endif
        return 0;
    }
    bt_dest(c, STAY);
    bt_show(c, 1u);
    return 0;
}
#endif




IMG_FN static void burst_task(u32 unused)
{
    (void)unused;
    for (;;) {
        cm_t *c = cm_get();
        if (c) { burst_step(c); c->burst_tick += 1u; }
#ifdef BULB_TIMER_MS
        if (c) bulb_step(c);
#endif
#ifdef ASPECT43_FX
        if (c) a43_watch(c);                   /* A69: 4:3 tables only while FX and chosen */
#endif
#ifdef BULB_MENU
        if (c && c->dg_buf && BTP(c)) for (u32 i = 0u; i < 12u; i++) ((volatile u32 *)(c->dg_buf + 0xDC0u))[i] = BTP(c)->q[i];
#endif
        s32 rc = BURST_SLEEP(50u);
        if (rc != 0 && rc != BURST_TIMEOUT) {
            if (c) c->burst_task_rc = 0xE0000000u | ((u32)rc & 0xFFFFFFu);
            BURST_EXIT();
            return;
        }
    }
}

/* Labels, built from character constants (string literals are not in the copied image). */
IMG_FN static void burst_put_name(u16 *b, u32 *n)              /* 连拍提速 */
{
    hz_put(b, n, 0x8FDEu); hz_put(b, n, 0x62CDu); hz_put(b, n, 0x63D0u); hz_put(b, n, 0x901Fu);
}

IMG_FN static void burst_label(cm_t *c)                         /* setup row */
{
    u32 n = 0;
    if (c->burst_row) {
        volatile u32 *r = (volatile u32 *)c->burst_row;
        r[4] = (r[4] & ~(F_ASCII | F_UTF16)) | (c->hans ? F_UTF16 : F_ASCII);
    }
    if (c->hans) {
        u16 b[6];
        burst_put_name(b, &n);
        b[n] = 0;
        FW(set_fn, UTF16_SET)(b, BURST_SLOT_ENTRY, HANS_STYLE);
        *(volatile u8 *)RUNTIME_STYLE = HANS_FONT;
    } else {
        char b[16];
        en_put(b, &n, 'B'); en_put(b, &n, 'u'); en_put(b, &n, 'r'); en_put(b, &n, 's'); en_put(b, &n, 't');
        en_put(b, &n, ' '); en_put(b, &n, 's'); en_put(b, &n, 'p'); en_put(b, &n, 'e'); en_put(b, &n, 'e');
        en_put(b, &n, 'd'); en_put(b, &n, '-'); en_put(b, &n, 'u'); en_put(b, &n, 'p');
        b[n] = 0;
        FW(set_fn, ASCII_SET)(b, BURST_SLOT_ENTRY, 0u);
    }
}

/* "机械快门提速：开/关" (silent = 0) or "电子快门提速：开/关" (silent = 1). */
IMG_FN static void burst_row_label(cm_t *c, u32 silent)
{
    u32 n = 0, on = silent ? c->burst_sil : c->burst_mech, slot = silent ? BURST_SLOT_SIL : BURST_SLOT_MECH;
    if (c->hans) {
        u16 b[12];
        if (silent) { hz_put(b, &n, 0x7535u); hz_put(b, &n, 0x5B50u); }
        else { hz_put(b, &n, 0x673Au); hz_put(b, &n, 0x68B0u); }
        hz_put(b, &n, 0x5FEBu); hz_put(b, &n, 0x95E8u); hz_put(b, &n, 0x63D0u); hz_put(b, &n, 0x901Fu);
        hz_put(b, &n, 0xFF1Au); hz_put(b, &n, on ? 0x5F00u : 0x5173u);
        b[n] = 0;
        FW(set_fn, UTF16_SET)(b, slot, HANS_STYLE);
        *(volatile u8 *)RUNTIME_STYLE = HANS_FONT;
    } else {
        char b[20];
        if (silent) { en_put(b, &n, 'E'); en_put(b, &n, 'l'); en_put(b, &n, 'e'); en_put(b, &n, 'c'); en_put(b, &n, 't');
                      en_put(b, &n, 'r'); en_put(b, &n, 'o'); en_put(b, &n, 'n'); en_put(b, &n, 'i'); en_put(b, &n, 'c'); }
        else { en_put(b, &n, 'M'); en_put(b, &n, 'e'); en_put(b, &n, 'c'); en_put(b, &n, 'h'); en_put(b, &n, 'a');
               en_put(b, &n, 'n'); en_put(b, &n, 'i'); en_put(b, &n, 'c'); en_put(b, &n, 'a'); en_put(b, &n, 'l'); }
        en_put(b, &n, ':'); en_put(b, &n, ' ');
        if (on) { en_put(b, &n, 'O'); en_put(b, &n, 'n'); }
        else { en_put(b, &n, 'O'); en_put(b, &n, 'f'); en_put(b, &n, 'f'); }
        b[n] = 0;
        FW(set_fn, ASCII_SET)(b, slot, 0u);
    }
    if (c->burst_rows) {
        volatile u32 *r = (volatile u32 *)(c->burst_rows + (silent ? ROW_SIZE : 0u));
        r[4] = (r[4] & ~(F_ASCII | F_UTF16)) | (c->hans ? F_UTF16 : F_ASCII);
    }
}

/* "View all in continuous mode" (0x112C) is NOT changed by the module (L56): writing it from the module's
 * task hung the camera at power-on, and writing it from the menu handler hung the emulator as well.
 * Users switch it off themselves (docs/BURST_SPEED.md); with it on the screen can stay black after a burst. */

/* Setup row OK: the switch page, titled "连拍提速". */
IMG_FN static u32 burst_entry_ok(u32 a0, u32 a1, u32 a2, u32 a3)
{
    cm_t *c = cm_get();
    (void)a0; (void)a1; (void)a2; (void)a3;
    if (!c || !c->burst_hdr) return 0;
    burst_row_label(c, 0u);
    burst_row_label(c, 1u);
    if (c->hans) {
        u16 b[6];
        u32 n = 0;
        burst_put_name(b, &n);
        b[n] = 0;
        FW(set_fn, UTF16_SET)(b, SLOT_TITLE_V, HANS_STYLE);
        *(volatile u8 *)RUNTIME_STYLE = HANS_FONT;
    } else {
        char b[16];
        u32 n = 0;
        en_put(b, &n, 'B'); en_put(b, &n, 'u'); en_put(b, &n, 'r'); en_put(b, &n, 's'); en_put(b, &n, 't');
        b[n] = 0;
        FW(set_fn, ASCII_SET)(b, SLOT_TITLE_V, 0u);
    }
    c->title_slot = SLOT_TITLE_V;
    ((volatile u32 *)PAGE0)[10] = c->burst_hdr;
    FW(page_fn, OPEN_PAGE)(2u, OWN_INDEX);
    return 0;
}

IMG_FN static u32 burst_toggle(u32 silent)
{
    cm_t *c = cm_get();
    if (c && cfg_ready(c)) {
        if (silent) c->burst_sil = !c->burst_sil;
        else c->burst_mech = !c->burst_mech;
        save_cfg(c);
        burst_step(c);
        burst_entry_ok(0u, 0u, 0u, 0u);        /* show the page again with the new value */
    }
    return 0;
}

IMG_FN static u32 burst_mech_ok(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return burst_toggle(0u); }
IMG_FN static u32 burst_sil_ok(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return burst_toggle(1u); }

