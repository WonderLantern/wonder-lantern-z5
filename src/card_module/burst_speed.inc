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

#ifdef TIME_TIMER_MS
/* A11 (M900 search, 2026-11-16): timed Time exposure. In shutter mode Time (setting 0x020E = 3, entry 66 of the
 * setting table 0x115C286C), when the capture state byte 0xC18A84E4 (read by 0x1021B478) is 2, start a timer; after
 * TIME_TIMER_MS, if it is still 2, call 0x1021A32C(&0) - the end-capture call of the USB TerminateCapture handler
 * (opcode 0x920C, 0x1031799C), which itself only acts when the state is 2. burst_viewall_off (unused since L56) holds
 * 0 = idle, ms | 1 = timing, 1 = ended (waits for the state to leave 2). */
#define TT_MODE_ENTRY  (0x115C286Cu + 66u * 16u)
#define TT_STATE       0xC18A84E4u
#define TT_END         0x1021A32Du
IMG_FN static u32 tt_mode(void)
{
    u32 p = *(const volatile u32 *)(TT_MODE_ENTRY + 8u);
    return (p >= RAM_LO && p < RAM_HI) ? *(volatile u32 *)p : 0xFFu;
}

IMG_FN static void time_step(cm_t *c)
{
    u32 st = *(volatile u8 *)TT_STATE, t = c->burst_viewall_off, now = *(volatile u32 *)CM_MS_CLOCK;
    if (tt_mode() != 3u || st != 2u) { c->burst_viewall_off = 0u; return; }
    if (t == 0u) { c->burst_viewall_off = now | 1u; return; }
    if (t == 1u) return;
    if (now - t >= TIME_TIMER_MS) {
        u32 z = 0u;
        ((u32 (*)(u32 *))TT_END)(&z);
        c->burst_viewall_off = 1u;
    }
}
#endif

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

#ifdef DIAG_RAM_DUMP
IMG_FN static void dg_snap(cm_t *c)            /* A22: RAM snapshot into the record buffer */
{
    volatile u32 *h = (volatile u32 *)(c->dg_buf + DG_RAM_HDR);
    volatile u32 *o = (volatile u32 *)(c->dg_buf + DG_RAM_OFF);
    for (u32 i = 0u; i < 0x4000u; i++) o[i] = ((const volatile u32 *)DG_RAM_A)[i];
    for (u32 i = 0u; i < 0x4000u; i++) o[0x4000u + i] = ((const volatile u32 *)DG_RAM_B)[i];
    h[0] = 0x444D4152u;                        /* 'RAMD' */
    h[1] = *(volatile u32 *)CM_MS_CLOCK;
    h[2] += 1u;
    h[3] = bt_mode();
}
#endif

IMG_FN static void bulb_step(cm_t *c)
{
#ifdef BULB_MENU
    if (!BTP(c)) return;
    volatile u32 *q = BTP(c)->q;               /* armed from the menu, mirrored into the record by burst_task */
#ifdef DIAG_RAM_DUMP
    if (BTP(c)->snap_at && c->dg_buf && (s32)(*(volatile u32 *)CM_MS_CLOCK - BTP(c)->snap_at) >= 0) {
        BTP(c)->snap_at = 0u;
        dg_snap(c);
    }
#endif
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
    if (q[1] == 1u && (s32)(now - q[2]) >= 0) {
        if (bt_mode() != 2u) { q[9] = 0xBAD00000u | bt_mode(); q[1] = 3u; return; }
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
            BTP(c)->stop = 0u;
#endif
            q[4] = ((u32 (*)(u32 *))BT_END)(&z);
            q[7] = now;
            q[1] = 3u;
#ifdef BULB_PCMODE
            q[11] = pc_mode(0u);               /* 0xDEC: back from computer control */
#endif
        }
    } else if (q[1] == 3u && q[7] && !q[8] && now - q[7] >= 1000u) {
        q[8] = 0x100u | *(volatile u8 *)0xC18A84E4u | (*(volatile u32 *)0xC18A7EE4u << 4);
    }
}
#endif

#ifdef BULB_MENU
/* "B门定时" page (photo shooting menu row -> 4 rows): start / cancel, hours, minutes, seconds.
 * Right or OK adds one, left takes one (wraps 0..9 h, 0..59 min / s). Start arms the run BT_DELAY_MS later
 * (time to leave the menu with a half press); pressed again it cancels (before the start) or ends now.
 * Only in Bulb (0x020E = 2); otherwise the row says so. Labels always Simplified Chinese (AGENTS.md rule 8). */
#define BT_SLOT_ENTRY  257u             /* photo shooting menu row "B门定时：h:mm:ss" */
#define BT_SLOT_ROW    258u             /* 258..261: start, hours, minutes, seconds */
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

#ifdef BT_SUGGEST
/* Metering suggestion from the last photo (A24, 2026-11-17; f/4 ISO 100 photos at 1/250, 1/80, 1/3):
 * 0xC20EA478 = the photo's Tv x 192 (1508 / 1225 / 309), 0xC20EA484 = Tv + Av - Sv = Bv x 192 (1316 / 1033 / 117),
 * both 0 until a photo was taken this power-up. Current exposure: Av x 128 at P + 2 (0xC18A75A2: f/4 512, f/8 768),
 * Sv x 192 at 0xC18A91B4 (ISO 100: 960; only ISO 100 seen so far). Needed Tv = Bv + Sv - Av, time = 2^-Tv s. */
#define BT_SLOT_SUG    (BT_SLOT_ROW + 4u)  /* 262: "测光建议：h:mm:ss" */
#define BT_M_TV        0xC20EA478u
#define BT_M_BV        0xC20EA484u
#define BT_M_AV        0xC18A75A2u
#define BT_M_SV        0xC18A91B4u

/* 0 no photo yet, 1 ok (*secs), 2 shorter than 1 s, 3 longer than 23:59:59, 4 exposure values look wrong. */
IMG_FN static u32 bt_suggest(u32 *secs)
{
    s32 tvp = *(volatile s32 *)BT_M_TV, bv = *(volatile s32 *)BT_M_BV;
    s32 av = *(volatile s16 *)BT_M_AV, sv = *(volatile s16 *)BT_M_SV;
    if (!tvp && !bv) return 0u;
    if (av <= 0 || av > 16 * 128 || sv < 2 * 192 || sv > 14 * 192 || bv < -12 * 192 || bv > 24 * 192) return 4u;
    s32 tv = bv + sv - ((av * 3) >> 1);        /* Av x 128 -> x 192 */
    if (tv > 0) return 2u;
    u32 e = (u32)(-tv), q = 0u;
    while (e >= 192u) { e -= 192u; q++; }
    if (q >= 17u) return 3u;
    u32 t[13];                                 /* 1024 x 2^(k / 12) */
    t[0] = 1024u; t[1] = 1085u; t[2] = 1149u; t[3] = 1218u; t[4] = 1290u; t[5] = 1367u; t[6] = 1448u;
    t[7] = 1534u; t[8] = 1625u; t[9] = 1722u; t[10] = 1825u; t[11] = 1933u; t[12] = 2048u;
    u32 i = e >> 4, f = e & 15u;
    u32 m = t[i] + (((t[i + 1u] - t[i]) * f) >> 4);
    u32 s = ((m << q) + 512u) >> 10;
    if (s > 86399u) return 3u;
    *secs = s;
    return 1u;
}

#ifdef BT_SUG_RAW
/* A26 debug: the row shows the raw words: T<photo Tv> B<Bv> A<Av x128> S<Sv>. */
IMG_FN static void bt_dec(u16 *b, u32 *n, s32 v)
{
    u16 d[12];
    u32 k = 0u, u = v < 0 ? (u32)(-v) : (u32)v;
    if (v < 0) hz_put(b, n, '-');
    do { u32 q = 0u; while (u >= 10u) { u -= 10u; q++; } d[k++] = (u16)('0' + u); u = q; } while (u && k < 11u);
    while (k) hz_put(b, n, d[--k]);
}
#endif

IMG_FN static void bt_sug_label(cm_t *c)
{
    u16 b[40];
    u32 n = 0, s = 0, r = bt_suggest(&s);
    (void)c;
#ifdef BT_SUG_RAW
    /* text slots hold 25 UTF-16 units: no spaces, at most 4 x (letter + 5 digits + sign) = 28 -> cut at 24 */
    hz_put(b, &n, 'B'); bt_dec(b, &n, *(volatile s32 *)BT_M_BV);
    hz_put(b, &n, 'A'); bt_dec(b, &n, *(volatile s16 *)BT_M_AV);
    hz_put(b, &n, 'S'); bt_dec(b, &n, *(volatile s16 *)BT_M_SV);
    hz_put(b, &n, 'T'); bt_dec(b, &n, *(volatile s32 *)BT_M_TV);
    (void)r;
    if (n > 24u) n = 24u;
    bt_set(b, n, BT_SLOT_SUG);
    return;
#endif
    hz_put(b, &n, 0x6D4Bu); hz_put(b, &n, 0x5149u); hz_put(b, &n, 0x5EFAu); hz_put(b, &n, 0x8BAEu); hz_put(b, &n, 0xFF1Au);
    if (r == 1u) {
        u32 h = 0u, m = 0u;
        while (s >= 3600u) { s -= 3600u; h++; }
        while (s >= 60u) { s -= 60u; m++; }
        if (h >= 10u) bt_two(b, &n, h); else hz_put(b, &n, '0' + h);
        hz_put(b, &n, ':'); bt_two(b, &n, m); hz_put(b, &n, ':'); bt_two(b, &n, s);
    } else if (r == 0u) {                      /* 先拍一张 */
        hz_put(b, &n, 0x5148u); hz_put(b, &n, 0x62CDu); hz_put(b, &n, 0x4E00u); hz_put(b, &n, 0x5F20u);
    } else if (r == 2u) {                      /* 不到1秒 */
        hz_put(b, &n, 0x4E0Du); hz_put(b, &n, 0x5230u); hz_put(b, &n, '1'); hz_put(b, &n, 0x79D2u);
    } else if (r == 3u) {                      /* 超过24小时 */
        hz_put(b, &n, 0x8D85u); hz_put(b, &n, 0x8FC7u); hz_put(b, &n, '2'); hz_put(b, &n, '4');
        hz_put(b, &n, 0x5C0Fu); hz_put(b, &n, 0x65F6u);
    } else {                                   /* -- */
        hz_put(b, &n, '-'); hz_put(b, &n, '-');
    }
    bt_set(b, n, BT_SLOT_SUG);
}
#endif

IMG_FN static void bt_labels(cm_t *c)
{
    u16 b[24];
    u32 n = 0, ph = BTP(c)->q[1];
    bt_name(b, &n); hz_put(b, &n, 0xFF1Au); bt_time(c, b, &n);
    bt_set(b, n, BT_SLOT_ENTRY);
    n = 0;
    if (ph == 1u) {                            /* 等待开始（OK取消） */
        hz_put(b, &n, 0x7B49u); hz_put(b, &n, 0x5F85u); hz_put(b, &n, 0x5F00u); hz_put(b, &n, 0x59CBu);
        hz_put(b, &n, 0xFF08u); hz_put(b, &n, 'O'); hz_put(b, &n, 'K'); hz_put(b, &n, 0x53D6u); hz_put(b, &n, 0x6D88u);
        hz_put(b, &n, 0xFF09u);
    } else if (ph == 2u) {                     /* 曝光中（OK结束） */
        hz_put(b, &n, 0x66DDu); hz_put(b, &n, 0x5149u); hz_put(b, &n, 0x4E2Du);
        hz_put(b, &n, 0xFF08u); hz_put(b, &n, 'O'); hz_put(b, &n, 'K'); hz_put(b, &n, 0x7ED3u); hz_put(b, &n, 0x675Fu);
        hz_put(b, &n, 0xFF09u);
    } else if (BTP(c)->msg == 1u) {              /* 请先设为B门 */
        hz_put(b, &n, 0x8BF7u); hz_put(b, &n, 0x5148u); hz_put(b, &n, 0x8BBEu); hz_put(b, &n, 0x4E3Au);
        hz_put(b, &n, 'B'); hz_put(b, &n, 0x95E8u);
    } else if (BTP(c)->msg == 2u) {              /* 请先设定时间 */
        hz_put(b, &n, 0x8BF7u); hz_put(b, &n, 0x5148u); hz_put(b, &n, 0x8BBEu); hz_put(b, &n, 0x5B9Au);
        hz_put(b, &n, 0x65F6u); hz_put(b, &n, 0x95F4u);
    } else {                                   /* 开始：h:mm:ss */
        hz_put(b, &n, 0x5F00u); hz_put(b, &n, 0x59CBu); hz_put(b, &n, 0xFF1Au); bt_time(c, b, &n);
    }
    bt_set(b, n, BT_SLOT_ROW);
#ifdef BT_PICKER
    n = 0; hz_put(b, &n, 0x65F6u); hz_put(b, &n, 0x95F4u); hz_put(b, &n, 0xFF1Au); bt_time(c, b, &n);
    bt_set(b, n, BT_SLOT_ROW + 1u);            /* 时间：h:mm:ss */
    return;
#endif
    n = 0; hz_put(b, &n, 0x5C0Fu); hz_put(b, &n, 0x65F6u); hz_put(b, &n, 0xFF1Au); hz_put(b, &n, '0' + BTP(c)->h);
    bt_set(b, n, BT_SLOT_ROW + 1u);            /* 小时：h */
    n = 0; hz_put(b, &n, 0x5206u); hz_put(b, &n, 0x949Fu); hz_put(b, &n, 0xFF1Au); bt_two(b, &n, BTP(c)->m);
    bt_set(b, n, BT_SLOT_ROW + 2u);            /* 分钟：mm */
    n = 0; hz_put(b, &n, 0x79D2u); hz_put(b, &n, 0xFF1Au); bt_two(b, &n, BTP(c)->s);
    bt_set(b, n, BT_SLOT_ROW + 3u);            /* 秒：ss */
#ifdef BT_SUGGEST
    bt_sug_label(c);
#endif
}

IMG_FN static void bt_show(cm_t *c, u32 refresh)
{
    u16 b[8];
    u32 n = 0;
    bt_labels(c);
    bt_name(b, &n);
    bt_set(b, n, SLOT_TITLE_V);
    c->title_slot = SLOT_TITLE_V;
#ifdef BT_REFRESH
    if (refresh) { FW(refresh_fn, REFRESH)(REFRESH_ARG, 3u, 7u); return; }
#else
    (void)refresh;
#endif
    ((volatile u32 *)PAGE0)[10] = BTP(c)->hdr;
    FW(page_fn, OPEN_PAGE)(2u, OWN_INDEX);
}

IMG_FN static u32 bt_entry_ok(u32 a0, u32 a1, u32 a2, u32 a3)
{
    cm_t *c = cm_get();
    (void)a0; (void)a1; (void)a2; (void)a3;
    if (!c || !BTP(c) || !BTP(c)->hdr) return 0;
#ifdef PC_TEST_OPEN
    FW(page_fn, OPEN_PAGE)(2u, 0x79u);          /* emulator test: the Picture Control list (greyed in Auto) */
    return 0;
#endif
    BTP(c)->msg = 0u;
    bt_show(c, 0u);
    return 0;
}

IMG_FN static void bt_adjust(u32 field, u32 up)
{
    cm_t *c = cm_get();
    if (!c || !BTP(c)) return;
    if (BTP(c)->q[1] != 1u && BTP(c)->q[1] != 2u) {   /* fixed while armed or open */
        u32 *v = field == 0u ? &BTP(c)->h : field == 1u ? &BTP(c)->m : &BTP(c)->s;
        u32 max = field == 0u ? 23u : 59u;
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

#ifdef BT_SUGGEST
/* "测光建议" row: OK copies the suggestion into hours / minutes / seconds. */
IMG_FN static u32 bt_sug_ok(u32 a0, u32 a1, u32 a2, u32 a3)
{
    cm_t *c = cm_get();
    (void)a0; (void)a1; (void)a2; (void)a3;
    if (!c || !BTP(c)) return 0;
    u32 s = 0u;
    if (BTP(c)->q[1] != 1u && BTP(c)->q[1] != 2u && bt_suggest(&s) == 1u) {
        u32 h = 0u, m = 0u;
        while (s >= 3600u) { s -= 3600u; h++; }
        while (s >= 60u) { s -= 60u; m++; }
        if (!(h | m | s)) s = 1u;
        BTP(c)->h = h; BTP(c)->m = m; BTP(c)->s = s;
        BTP(c)->msg = 0u;
        save_cfg(c);
    }
    bt_show(c, 0u);
    return 0;
}
#endif

IMG_FN static u32 bt_start_ok(u32 a0, u32 a1, u32 a2, u32 a3)
{
    cm_t *c = cm_get();
    (void)a0; (void)a1; (void)a2; (void)a3;
    if (!c || !BTP(c)) return 0;
    volatile u32 *q = BTP(c)->q;
    if (q[1] == 1u) {
        q[1] = 0u;                             /* cancel before the start */
    } else if (q[1] == 2u) {
        BTP(c)->stop = 1u;                       /* end now (next task pass) */
    } else if (bt_mode() != 2u) {
        BTP(c)->msg = 1u;
    } else if (!(BTP(c)->h | BTP(c)->m | BTP(c)->s)) {
        BTP(c)->msg = 2u;
    } else {
        for (u32 i = 1u; i < 12u; i++) q[i] = 0u;
        BTP(c)->secs = BTP(c)->h * 3600u + BTP(c)->m * 60u + BTP(c)->s;
        BTP(c)->stop = 0u;
        BTP(c)->msg = 0u;
        q[2] = *(volatile u32 *)CM_MS_CLOCK + BT_DELAY_MS;
        q[0] = BT_MAGIC;
        q[1] = 1u;
#ifndef BT_NO_CLOSE
        /* Close the menu as a half press does (emulator trace 2026-11-16: event 0x240 -> 0x104787E0 posts message 1
         * to the menu state machine 0xC182DC14 through 0x104B544E). The page is not reopened. */
        bt_labels(c);
        bt_dest(c, STAY);
#if defined(BT_CLOSE_NOW)
        /* A17 on camera: black screen, no exposure, restart needed. Test option only. */
        ((void (*)(u32, u32, u32))0x104B544Fu)(0xC182DC14u, 1u, 0u);   /* message 1 to the menu, as 0x104787E0 */
#elif defined(BT_CLOSE_TASK)
        BTP(c)->close_at = (*(volatile u32 *)CM_MS_CLOCK + BT_CLOSE_MS) | 1u;   /* test: the task closes it */
        bt_show(c, 1u);
#else
        bt_show(c, 1u);                        /* stay: the computer-control switch itself leaves the menu (A16) */
#endif
        return 0;
#else
        if (BT_EXIT_DEST != STAY) { bt_labels(c); bt_dest(c, BT_EXIT_DEST); return 0; }   /* leave the menu */
#endif
    }
    bt_dest(c, STAY);
    bt_show(c, 1u);
    return 0;
}
#endif

#if defined(PC_IMPORT) && defined(BULB_MENU)
/* One-click import: photo shooting menu row "导入配方" (under B门定时, 2026-11-17) opens a page whose row "开始导入"
 * does the import and whose second row shows the result. Every Picture Control file on the card goes
 * into C-1, C-2 ... in card order (at most 9; slots already used are overwritten, the rest stay as they are).
 * Same steps as the stock "copy to camera" executor 0x10510FC2 (mode 8, after the name keyboard), per file:
 *   0x10456716()                card scan (0x1020C756: lists NIKON/CUSTOMPC, parses every file), once
 *   0x10456540(card id, buf)    entry -> 0x60-byte UI struct (card ids 200..328, 0 if the id has no file)
 *   0x104566F6(card id, slot)   reads the file again (0x1020C92A, sets the busy word 0xC18C9BD8 + 0x10) and stores
 *                               its data for the slot (0x101D95A8 -> 0x10209D86, which clears the busy word)
 *   buf + 0x2C <- buf + 0x1A    0x12 bytes, as the executor
 *   0x10456622(slot, buf)       store the entry (0x10235832 -> 0x101D8D56), slot ids 0x1C..0x24 = C-1..C-9
 *   0x1043F590(slot, 0)         as the executor after the store
 * The name stays the one in the file (the stock flow asks for one). If the busy word is still set after a file it
 * is cleared, so the stock card functions are not left blocked. Log: 'PCIM' block in the test record (0x32A00).
 * camera_verified: false. */
#define PI_SLOT        263u             /* 263 photo menu row "导入配方", 264 "开始导入", 265 result */
#define PI_ID          0x2FC6u          /* 0x2FC6 .. 0x2FC8 */
#define PI_SCAN        0x10456717u
#define PI_GET         0x10456541u
#define PI_EXTRA       0x104566F7u
#define PI_SET         0x10456623u
#define PI_AFTER       0x1043F591u
#define PI_BUSY        (0xC18C9BD8u + 0x10u)
#define PI_FIRST_CARD  200u
#define PI_LAST_CARD   328u
#define PI_FIRST_SLOT  0x1Cu
#define PI_SLOTS       9u

/* Emulator A36 (2026-11-17): an action on the photo menu row itself (no page opened) left the menu stuck in its
 * "entering" state, so the row opens a page like B门定时: row 0 "开始导入" runs the import, row 1 shows the result. */
IMG_FN static void pi_name(u16 *b, u32 *n)      /* 导入配方 */
{
    hz_put(b, n, 0x5BFCu); hz_put(b, n, 0x5165u); hz_put(b, n, 0x914Du); hz_put(b, n, 0x65B9u);
}

IMG_FN static void pi_result(cm_t *c, u16 *b, u32 *n)
{
    u32 k = BTP(c)->im_n;
    if (!BTP(c)->im_runs) {                                            /* 未导入 */
        hz_put(b, n, 0x672Au); hz_put(b, n, 0x5BFCu); hz_put(b, n, 0x5165u);
    } else if (k) {                                                    /* 已导入N个 */
        hz_put(b, n, 0x5DF2u); hz_put(b, n, 0x5BFCu); hz_put(b, n, 0x5165u);
        hz_put(b, n, '0' + k); hz_put(b, n, 0x4E2Au);
    } else {                                                           /* 卡上没有配方 */
        hz_put(b, n, 0x5361u); hz_put(b, n, 0x4E0Au); hz_put(b, n, 0x6CA1u); hz_put(b, n, 0x6709u);
        hz_put(b, n, 0x914Du); hz_put(b, n, 0x65B9u);
    }
}

IMG_FN static void pi_label(cm_t *c)
{
    u16 b[24];
    u32 n = 0;
    pi_name(b, &n);                                                    /* 导入配方[：result] */
    if (BTP(c)->im_runs) { hz_put(b, &n, 0xFF1Au); pi_result(c, b, &n); }
    bt_set(b, n, PI_SLOT);
    n = 0;
    hz_put(b, &n, 0x5F00u); hz_put(b, &n, 0x59CBu); hz_put(b, &n, 0x5BFCu); hz_put(b, &n, 0x5165u);
    bt_set(b, n, PI_SLOT + 1u);                                        /* 开始导入 */
    n = 0;
#ifdef PI_GLYPHS
    /* emulator glyph check: every character the result row can show */
    hz_put(b, &n, 0x672Au); hz_put(b, &n, 0x5DF2u); hz_put(b, &n, 0x5BFCu); hz_put(b, &n, 0x5165u); hz_put(b, &n, '9');
    hz_put(b, &n, 0x4E2Au); hz_put(b, &n, 0x5361u); hz_put(b, &n, 0x4E0Au); hz_put(b, &n, 0x6CA1u); hz_put(b, &n, 0x6709u);
    hz_put(b, &n, 0x914Du); hz_put(b, &n, 0x65B9u);
#else
    pi_result(c, b, &n);
#endif
    bt_set(b, n, PI_SLOT + 2u);                                        /* result */
}

IMG_FN static void pi_show(cm_t *c)
{
    u16 b[8];
    u32 n = 0;
    pi_label(c);
    pi_name(b, &n);
    bt_set(b, n, SLOT_TITLE_V);
    c->title_slot = SLOT_TITLE_V;
    ((volatile u32 *)PAGE0)[10] = BTP(c)->im_hdr;
    FW(page_fn, OPEN_PAGE)(2u, OWN_INDEX);
}

IMG_FN static u32 pi_entry_ok(u32 a0, u32 a1, u32 a2, u32 a3)
{
    cm_t *c = cm_get();
    (void)a0; (void)a1; (void)a2; (void)a3;
    if (c && BTP(c) && BTP(c)->im_hdr) pi_show(c);
    return 0;
}

IMG_FN static u32 pi_none(u32 a0, u32 a1, u32 a2, u32 a3)
{
    cm_t *c = cm_get();
    (void)a0; (void)a1; (void)a2; (void)a3;
    if (c && BTP(c) && BTP(c)->im_hdr) pi_show(c);
    return 0;
}

IMG_FN static u32 pi_ok(u32 a0, u32 a1, u32 a2, u32 a3)
{
    cm_t *c = cm_get();
    (void)a0; (void)a1; (void)a2; (void)a3;
    if (!c || !BTP(c) || !BTP(c)->im_hdr) return 0;
    u32 *lg = BTP(c)->im_log, n = 0u;
    u8 buf[0x60];
    for (u32 i = 0; i < 1u + 4u * PI_SLOTS; i++) lg[i] = 0u;
    ((void (*)(void))PI_SCAN)();
    for (u32 id = PI_FIRST_CARD; id <= PI_LAST_CARD && n < PI_SLOTS; id++) {
        for (u32 i = 0; i < sizeof(buf); i++) buf[i] = 0u;
        if (!((u32 (*)(u32, u8 *))PI_GET)(id, buf)) continue;
        u32 slot = PI_FIRST_SLOT + n, *e = lg + 1u + 4u * n;
        e[0] = id | (slot << 16);
        e[1] = ((u32 (*)(u32, u32))PI_EXTRA)(id, slot);
        for (u32 i = 0; i < 0x12u; i++) buf[0x2Cu + i] = buf[0x1Au + i];
        e[2] = ((u32 (*)(u32, u8 *))PI_SET)(slot, buf);
        ((void (*)(u32, u32))PI_AFTER)(slot, 0u);
        e[3] = *(volatile u32 *)PI_BUSY;
        if (e[3]) *(volatile u32 *)PI_BUSY = 0u;
        n++;
    }
    lg[0] = 0x4D494350u;                       /* 'PCIM' */
    BTP(c)->im_n = n;
    BTP(c)->im_runs += 1u;
    pi_show(c);                                /* reopen the page with the result, as bt_start_ok does */
    return 0;
}
#endif

#ifdef BT_PICKER
/* h:m:s picker in the look of the stock "日期和时间" page (page 0x1D9, descriptor 0xC1837628, header 0xC1875210,
 * callbacks 0x10764238, emulator trace 2026-11-17). The view is a copy of that header with the year / month / day
 * elements left out; it runs on the module's own page slot (PAGE0), whose callback words pick the picker's or
 * the template's per view. Stock working storage: 0x10549814(1) = 10-byte date {u16 year, month, day, hour +4,
 * minute +5, second +6}, 0x10549814(4) = u16 box index 0..5. Up / down are the stock handlers (0x104CF603 /
 * 0x104CF64B: step the field of the box index, redraw the boxes). Left / right are the stock handlers kept to
 * boxes 3..5. OK and MENU are ours: OK keeps the time (never the stock 0x104CF859, which sets the camera clock
 * through 0x10549940), MENU drops it; both go back to the B门定时 view. */
#define PK_STORE(n)    (((u32 (*)(u32))0x10549815u)(n))
#define PK_FMT         ((void (*)(u32, u32, u32, u32, u32))0x104CF2EDu)   /* (widget, date, field, box, slot) */
#define PK_IDS         0x1075B720u      /* u16 widget ids of the six boxes: 0xA32 .. 0xA3B */
#define PK_W_TITLE     0x0A1Du
#define PK_W_SUB       0x0A2Au
#define BT_SLOT_PK     262u             /* "曝光时间" under the title */
#define PK_CALL(fn, a0, a1, a2, a3) (((u32 (*)(u32, u32, u32, u32))(fn))((a0), (a1), (a2), (a3)))

IMG_FN static u32 pk_active(void)
{
    cm_t *c = cm_get();
    return c && BTP(c) && BTP(c)->pk_hdr && ((volatile u32 *)PAGE0)[10] == BTP(c)->pk_hdr;
}

/* Page callback 0: the stock init (labels, separators, boxes from the clock), then our time, box 3, titles. */
IMG_FN static u32 pk_cb0(u32 page, u32 a1, u32 a2, u32 a3)
{
    if (!pk_active()) return title_cb(page, a1, a2, a3);
    cm_t *c = cm_get();
    u32 r = PK_CALL(0x104CF389u, page, a1, a2, a3);
    u8 *d = (u8 *)PK_STORE(1u);
    volatile u16 *cur = (volatile u16 *)PK_STORE(4u);
    if (!d || !cur) return r;
    d[4] = (u8)BTP(c)->h; d[5] = (u8)BTP(c)->m; d[6] = (u8)BTP(c)->s;
    *cur = 3u;
    u32 list = *(volatile u32 *)(page + 0x0Cu);
    for (u32 f = 3u; f < 6u; f++) {
        u32 w = FW(find_fn, WIDGET_FIND)(((const u16 *)PK_IDS)[f], list);
        if (w) PK_FMT(w, (u32)d, f, 3u, *(volatile u32 *)(w + 8u));
    }
    /* A20 on camera: left / right froze the camera. No widget-list moves (0x10446EBC / 0x10446EB4) any more. */
    u32 w = FW(find_fn, WIDGET_FIND)(PK_W_TITLE, list);
    if (w) { FW(widget_fn, WIDGET_SLOT)(w, SLOT_TITLE_V); FW(widget_fn, WIDGET_SHOW)(w, 1u); }
    w = FW(find_fn, WIDGET_FIND)(PK_W_SUB, list);
    if (w) { FW(widget_fn, WIDGET_SLOT)(w, BT_SLOT_PK); FW(widget_fn, WIDGET_SHOW)(w, 1u); }
    return r;
}

IMG_FN static u32 pk_cb1(u32 a0, u32 a1, u32 a2, u32 a3)
{
    return PK_CALL(pk_active() ? 0x104CF45Du : ((const u32 *)PAGE_CB)[1], a0, a1, a2, a3);
}
IMG_FN static u32 pk_cb3(u32 a0, u32 a1, u32 a2, u32 a3)
{
    return PK_CALL(pk_active() ? 0x104CF49Bu : ((const u32 *)PAGE_CB)[3], a0, a1, a2, a3);
}
IMG_FN static u32 pk_cb4(u32 a0, u32 a1, u32 a2, u32 a3)
{
    return PK_CALL(pk_active() ? 0x104CF4AFu : ((const u32 *)PAGE_CB)[4], a0, a1, a2, a3);
}

/* Move the selected box: redraw the old and the new box (0x104CF2EC with the new index), store the index, redraw
 * the page as the stock up / down handlers end (0x10446EC0, refresh). Unlike the stock left / right handlers
 * (0x104CF723 / 0x104CF693) nothing is moved in the page's widget list. Boxes 3..5 only, no wrap. */
IMG_FN static void pk_move(s32 step)
{
    volatile u16 *cur = (volatile u16 *)PK_STORE(4u);
    u32 d = PK_STORE(1u);
    u32 page = ((u32 (*)(void))0x10451F3Fu)();
    if (!cur || !d || !page || *cur < 3u || *cur > 5u) return;
    u32 old = *cur, nw = (u32)((s32)old + step);
    if (nw < 3u || nw > 5u) return;
    u32 list = *(volatile u32 *)(page + 0x0Cu);
    u32 wo = FW(find_fn, WIDGET_FIND)(((const u16 *)PK_IDS)[old], list);
    u32 wn = FW(find_fn, WIDGET_FIND)(((const u16 *)PK_IDS)[nw], list);
    if (!wo || !wn) return;
    *cur = (u16)nw;
    PK_FMT(wo, d, old, nw, *(volatile u32 *)(wo + 8u));
    PK_FMT(wn, d, nw, nw, *(volatile u32 *)(wn + 8u));
    ((void (*)(u32, u32))0x10446EC1u)(list, 0x104467A5u);
    FW(refresh_fn, REFRESH)(REFRESH_ARG, 3u, 7u);
}
IMG_FN static u32 pk_left(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; pk_move(-1); return 0; }
IMG_FN static u32 pk_right(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; pk_move(1); return 0; }

IMG_FN static u32 pk_ok(u32 a0, u32 a1, u32 a2, u32 a3)
{
    cm_t *c = cm_get();
    (void)a0; (void)a1; (void)a2; (void)a3;
    if (!c || !BTP(c)) return 0;
    const u8 *d = (const u8 *)PK_STORE(1u);
    if (d && BTP(c)->q[1] != 1u && BTP(c)->q[1] != 2u && d[4] <= 23u && d[5] <= 59u && d[6] <= 59u) {
        BTP(c)->h = d[4]; BTP(c)->m = d[5]; BTP(c)->s = d[6];
        BTP(c)->msg = 0u;
        save_cfg(c);
    }
    bt_show(c, 0u);
    return 0;
}
IMG_FN static u32 pk_cancel(u32 a0, u32 a1, u32 a2, u32 a3)
{
    cm_t *c = cm_get();
    (void)a0; (void)a1; (void)a2; (void)a3;
    if (c && BTP(c)) bt_show(c, 0u);
    return 0;
}

/* B门定时 row "时间": open the picker. */
IMG_FN static u32 pk_open(u32 a0, u32 a1, u32 a2, u32 a3)
{
    cm_t *c = cm_get();
    (void)a0; (void)a1; (void)a2; (void)a3;
    if (!c || !BTP(c) || !BTP(c)->pk_hdr) return 0;
    if (BTP(c)->q[1] == 1u || BTP(c)->q[1] == 2u) return 0;   /* fixed while armed or open */
    u16 b[8];
    u32 n = 0;
    bt_labels(c);
    bt_name(b, &n);
    bt_set(b, n, SLOT_TITLE_V);
    n = 0; hz_put(b, &n, 0x66DDu); hz_put(b, &n, 0x5149u); hz_put(b, &n, 0x65F6u); hz_put(b, &n, 0x95F4u);
    bt_set(b, n, BT_SLOT_PK);                  /* 曝光时间 */
    c->title_slot = SLOT_TITLE_V;
    ((volatile u32 *)PAGE0)[10] = BTP(c)->pk_hdr;
    FW(page_fn, OPEN_PAGE)(2u, OWN_INDEX);
    return 0;
}
#endif

#if defined(PC_HIDE_CREATIVE) || defined(PC_TEST_OPEN)
/* Emulator test only: page 0x79's callback 3 (0x104E31FB: item 0x21 available?) answers "usable", so the page does
 * not back out in the emulator's Auto mode (the rows stay greyed there). */
IMG_FN static u32 pcl_usable(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 1u; }
#endif
#ifdef PC_HIDE_CREATIVE
/* "设定优化校准" without the 20 Creative Picture Controls (2026-11-17). Page 0x79 (descriptor 0xC18339A8, header
 * 0xC1876D40, 38 rows 0x114F0738, key maps 0x1153E2C0, callbacks 0x107625DC). Its init 0x104E3066 ends with
 * 0x105114A8(page, 0x1872), which draws the creative section's caption at the row of the separator 0x1872 - that row
 * is gone, so this init does the same steps without the caption. */
IMG_FN static void pcl_count(u32 k)
{
#ifdef BULB_MENU
    cm_t *c = cm_get();
    if (c && BTP(c)) BTP(c)->pcl_n[k] += 1u;
#else
    (void)k;
#endif
}

IMG_FN static void pcl_hide(u32 page)           /* rows 8-28 (separator, Creative 01-20) not shown */
{
    u32 list = page ? *(volatile u32 *)(page + 0x24u) : 0u;
    if (!list) return;
    for (u32 k = 8u; k <= 28u; k++) {
        u32 w = ((u32 (*)(u32, u32, u32))0x10448E0Bu)(list, 1u, k);
        if (w) FW(widget_fn, WIDGET_SHOW)(w, 0u);
    }
}

IMG_FN static u32 pcl_init(u32 page, u32 a1, u32 a2, u32 a3)
{
    (void)a1; (void)a2; (void)a3;
    ((void (*)(u32, u32))0x10510873u)(page, 1u);
    ((void (*)(u32, u32))0x1051011Du)(page, 1u);
    ((void (*)(u32))0x1051162Fu)(1u);
    /* A29: keep the stock 38 rows (the init steps place C-1..C-9 by fixed row numbers, table 0x1075F5FC; A27's
     * 17-row copy broke that on a camera with custom controls) and hide rows 8-28 (separator, Creative 01-20) the
     * way the stock hides empty custom slots: widget 0x10448E0A(page + 0x24, 1, row), 0x10447358(widget, 0). */
    pcl_count(0u);
    pcl_hide(page);
    u32 at = (u32)*(volatile u16 *)(page + 0x16u) + (u32)*(volatile u16 *)(page + 0x18u);
    if (at >= 8u && at <= 28u) ((void (*)(u32, u32))0x1044E627u)(page, 0x17DCu);   /* on a hidden row: Auto */
    return 0;
}

/* A30: the page's refresh (callback 4 0x104E320F -> 0x1051035A(page, 1), same table) and its callback 1
 * (0x104E308D) can show the rows again on the camera: run them, then hide again. */
IMG_FN static u32 pcl_cb1(u32 page, u32 a1, u32 a2, u32 a3)
{
    u32 r = ((u32 (*)(u32, u32, u32, u32))0x104E308Du)(page, a1, a2, a3);
    pcl_count(1u);
    pcl_hide(page);
    return r;
}
IMG_FN static u32 pcl_cb4(u32 page, u32 a1, u32 a2, u32 a3)
{
    u32 r = ((u32 (*)(u32, u32, u32, u32))0x104E320Fu)(page, a1, a2, a3);
    pcl_count(2u);
    pcl_hide(page);
    return r;
}

/* Up / down: the stock 0x105113E9 / 0x10511427 without the 2-row jumps around the separator:
 * 0x10450908(page, step), then the stock redraw. */
IMG_FN static void pcl_move(s32 step)
{
    u32 page = ((u32 (*)(void))0x10451F3Fu)();
    ((void (*)(u32, s32))0x10450909u)(page, step);
    FW(refresh_fn, REFRESH)(REFRESH_ARG, 3u, 7u);
}
IMG_FN static u32 pcl_up(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; pcl_move(-1); return 0; }
IMG_FN static u32 pcl_down(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; pcl_move(1); return 0; }
#endif

IMG_FN static void burst_task(u32 unused)
{
    (void)unused;
    for (;;) {
        cm_t *c = cm_get();
        if (c) { burst_step(c); c->burst_tick += 1u; }
#ifdef TIME_TIMER_MS
        if (c) time_step(c);
#endif
#ifdef BULB_TIMER_MS
        if (c) bulb_step(c);
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
    if (c) {
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

