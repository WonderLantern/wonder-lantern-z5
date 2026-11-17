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
    burst_words(c, active ? BURST_FLAG_BITS : 0u);
    /* L56: no camera-setting access from this task. Writing 0x112C here (L53 every 0.5 s, L54 once)
     * hangs the camera at power-on (emulator: start never completes; camera: shutter locked). */
}

IMG_FN static void burst_task(u32 unused)
{
    (void)unused;
    for (;;) {
        cm_t *c = cm_get();
        if (c) { burst_step(c); c->burst_tick += 1u; }
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

