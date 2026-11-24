/* A69 (2026-11-24): "照片画幅" in the photo shooting menu: off / 4:3. With the A68 firmware the geometry
 * configuration takes area 8's tables for FX photos once area 8's group-1 pointer of the RAM geometry pointer table
 * points into RAM. The 4:3 copies are switched in only while the choice is 4:3 AND the camera's actual image area
 * (setting 0x169E, RAM 0xC25043A4) is FX; in every other area (the stock 1:1 included) area 8 has the stock ROM
 * tables, so the stock 1:1 is unchanged. The module task calls a43_watch every 50 ms; a43_apply switches group 1
 * last when enabling and first when disabling. Each shot configures its geometry anew, so a change applies from the
 * next shot. The live view still shows the full FX frame (a mask is the next step).
 * Image code: immediates only, no const tables or string literals. camera_verified: false. */
#define A43_AREA_NOW   (*(volatile const u32 *)0xC25043A4u)   /* actual image area (0x169E), as A62's readout */
#define A43_SLOT_ENTRY 265u            /* photo shooting menu row "照片画幅：关 / 4:3" (free since A60's row is off) */
#define A43_SLOT_ROW   283u            /* 283 "关", 284 "4:3" */
#define A43_ID_ENTRY   0x2FC7u
#define A43_ID_ROW     0x2FC8u         /* 0x2FC8, 0x2FC9 is A44's: second row 0x2FCD */
#define A43_ID_ROW1    0x2FCDu

IMG_FN static u32 a43_want(cm_t *c)
{
    bt_t *t = BTP(c);
    return t && t->a43_status == 1u && t->a43_copy && t->a43_sel && A43_AREA_NOW == 0u;
}

IMG_FN static u32 a43_is_on(void)
{
    u32 g1 = ((volatile u32 *)A43_PTRS)[1u * 9u + 8u];
    return g1 >= RAM_LO && g1 < RAM_HI;
}

IMG_FN static void a43_apply(cm_t *c)
{
    bt_t *t = BTP(c);
    if (!t || t->a43_status != 1u || !t->a43_copy) return;
    volatile u32 *p = (volatile u32 *)A43_PTRS;
    u32 b = t->a43_copy;
    if (a43_want(c)) {
        p[0u * 9u + 8u] = b + 0u * A43_GROUP_BYTES;
        p[2u * 9u + 8u] = b + 2u * A43_GROUP_BYTES;
        p[3u * 9u + 8u] = b + 3u * A43_GROUP_BYTES;
        p[4u * 9u + 8u] = b + 4u * A43_GROUP_BYTES;
        p[5u * 9u + 8u] = b + 5u * A43_GROUP_BYTES;
        p[6u * 9u + 8u] = b + 6u * A43_GROUP_BYTES;
        __asm__ volatile ("dsb sy" ::: "memory");
        p[1u * 9u + 8u] = b + 1u * A43_GROUP_BYTES;    /* the firmware's switch: last */
    } else {
        p[1u * 9u + 8u] = 0x1167B8F0u;                  /* the firmware's switch: first */
        __asm__ volatile ("dsb sy" ::: "memory");
        p[0u * 9u + 8u] = 0x1167B884u;
        p[2u * 9u + 8u] = 0x1167B95Cu;
        p[3u * 9u + 8u] = 0x1167BA28u;
        p[4u * 9u + 8u] = 0x1167BB00u;
        p[5u * 9u + 8u] = 0x1167BB00u;
        p[6u * 9u + 8u] = 0x1167BBF0u;
    }
    __asm__ volatile ("dsb sy" ::: "memory");
}

/* A70 (2026-11-24): A69 on camera (DIAG18/19, DSC_1708-1712): the first 4:3 shot after power-on (with 4:3 saved)
 * shows a pixel mess in the image review right after the shot; the file is fine and so is the review of every later
 * shot. The A68 firmware hook gives the capture info area 8 / kind 3 at the shot's first geometry call and leaves
 * them there. A70 put the info into that "after a 4:3 shot" state whenever 4:3 was on (pointer 0xC18C68CC).
 * A71 (2026-11-24): A70 on camera (DIAG20/DIAG01, DSC_1713-1720): power-on with 4:3, FX shot, back to 4:3 -> review
 * mess and a freeze; after a restart everything fine. The record shows 0xC18C68CC moves to another object around
 * shots (A70's cross-check stopped the priming after the first 4:3 shot), so after a shot A70 could write into the
 * wrong object. The JPEG log (q[13] = capture info of each encode) shows one capture info 0xC3B34558 for every FX,
 * DX and 4:3 shot of A69/A70. A71 locks the pointer's first valid value and writes only there, and only before the
 * first JPEG encode of the power-up (JPEG log count at record 0x33004; RAW shots encode their preview too) and the
 * first 4:3 configuration; it stops for good when the pointer leaves the locked value. The mess was always the first
 * 4:3 shot of a power-up (A69 DSC_1708/1711; A70 after the restart the primed first shot and the first 4:3 shot after
 * FX / DX were fine), so later shots need no priming.
 * Record 0x36A00 words 3-6: info, primed, restored, stop value; 7: pointer changes, 8-15: first 4 (ms, value).
 * camera_verified: false. */
#define A43_INFO_PTR   (*(volatile const u32 *)0xC18C68CCu)
#define A43_KIND_FN    ((u32 (*)(u32))0x10190577u)          /* aspect kind of an area (stock, A59 notes) */

IMG_FN static void a43_prime(cm_t *c)
{
    bt_t *t = BTP(c);
    volatile u32 *log = c->dg_buf ? (volatile u32 *)(c->dg_buf + A43_LOG_OFF) : 0;
    const volatile u32 *fw = (const volatile u32 *)(t->a43_copy + 224u);
    u32 info = A43_INFO_PTR, area, n;
    volatile u8 *a, *k;
    if (info != t->a43_last_ptr) {                                 /* trace the pointer's first changes */
        t->a43_last_ptr = info;
        if (log) {
            n = log[7];
            if (n < 4u) { log[8u + 2u * n] = *(volatile const u32 *)0xC18EB388u; log[9u + 2u * n] = info; }
            log[7] = n + 1u;
        }
    }
    if (t->a43_prime_off || !log) return;                          /* no record buffer: no shot detector */
    if (!t->a43_lock) {
        if (info < RAM_LO || info >= RAM_HI - 0x400u) return;
        t->a43_lock = info;
    }
    if (info != t->a43_lock || fw[0] != 0u || *(volatile const u32 *)(c->dg_buf + 0x33004u) != 0u) {
        t->a43_prime_off = 1u;                                     /* a shot happened: never write again */
        log[6] = info;
        return;
    }
    info = t->a43_lock;
    if (*(volatile u32 *)(info + 0xFCu) != 0u) return;             /* photo branch only */
    a = (volatile u8 *)(info + 0x200u);
    k = (volatile u8 *)(info + 0x21Eu);
    if (a43_is_on()) {
        if (*a == 0u && *k == 0u) {
            *a = 8u; *k = 3u;
            log[3] = info; log[4]++;
        }
    } else if (*k == 3u) {                                         /* kind 3 is ours: stock maps no area to it */
        area = A43_AREA_NOW;
        if (area > 8u) return;
        *a = (u8)area; *k = (u8)A43_KIND_FN(area);
        log[3] = info; log[5]++;
    }
}

IMG_FN static void a43_watch(cm_t *c)      /* module task, every 50 ms: follow the area and the choice */
{
    if (!BTP(c) || BTP(c)->a43_status != 1u) return;
    if (a43_want(c) != a43_is_on()) a43_apply(c);
    a43_prime(c);
}

IMG_FN static void a43_row_kind(volatile u32 *r, u32 hans)
{
    if (r) r[4] = (r[4] & ~(F_ASCII | F_UTF16)) | (hans ? F_UTF16 : F_ASCII);
}

IMG_FN static void a43_hz_name(u16 *b, u32 *n)  /* 照片画幅 */
{
    hz_put(b, n, 0x7167u); hz_put(b, n, 0x7247u); hz_put(b, n, 0x753Bu); hz_put(b, n, 0x5E45u);
}

/* Entry row "照片画幅：关 / 4:3", page rows "关" / "4:3" with "*" before the current one. */
IMG_FN static void a43_labels(cm_t *c)
{
    bt_t *t = BTP(c);
    u32 hans = c->hans, n, sel;
    if (!t || t->a43_status != 1u) return;
    sel = t->a43_sel;
    a43_row_kind((volatile u32 *)t->a43_entry_row, hans);
    if (t->a43_hdr) {
        const volatile u32 *h = (const volatile u32 *)t->a43_hdr;
        u32 *rows = (u32 *)h[3];
        a43_row_kind((volatile u32 *)rows, hans);
        a43_row_kind((volatile u32 *)(rows + ROW_WORDS), hans);
    }
    if (hans) {
        u16 b[16];
        n = 0; a43_hz_name(b, &n); hz_put(b, &n, 0xFF1Au);
        if (sel) { hz_put(b, &n, '4'); hz_put(b, &n, ':'); hz_put(b, &n, '3'); } else hz_put(b, &n, 0x5173u);
        b[n] = 0; FW(set_fn, UTF16_SET)(b, A43_SLOT_ENTRY, HANS_STYLE);
        n = 0; if (!sel) { hz_put(b, &n, '*'); hz_put(b, &n, ' '); } hz_put(b, &n, 0x5173u);
        b[n] = 0; FW(set_fn, UTF16_SET)(b, A43_SLOT_ROW, HANS_STYLE);
        n = 0; if (sel == 1u) { hz_put(b, &n, '*'); hz_put(b, &n, ' '); }
        hz_put(b, &n, '4'); hz_put(b, &n, ':'); hz_put(b, &n, '3');
        b[n] = 0; FW(set_fn, UTF16_SET)(b, A43_SLOT_ROW + 1u, HANS_STYLE);
        *(volatile u8 *)RUNTIME_STYLE = HANS_FONT;
    } else {
        char b[24];
        n = 0; en_w(b, &n, 0x746F6850u); en_w(b, &n, 0x7361206Fu); en_w(b, &n, 0x74636570u); en_w(b, &n, 0x0000203Au);
        if (sel) en_w(b, &n, 0x00333A34u); else en_w(b, &n, 0x0066664Fu);   /* "Photo aspect: 4:3 / Off" */
        b[n] = 0; FW(set_fn, ASCII_SET)(b, A43_SLOT_ENTRY, 0u);
        n = 0; if (!sel) en_w(b, &n, 0x0000202Au); en_w(b, &n, 0x0066664Fu);
        b[n] = 0; FW(set_fn, ASCII_SET)(b, A43_SLOT_ROW, 0u);
        n = 0; if (sel == 1u) en_w(b, &n, 0x0000202Au); en_w(b, &n, 0x00333A34u);
        b[n] = 0; FW(set_fn, ASCII_SET)(b, A43_SLOT_ROW + 1u, 0u);
    }
}

IMG_FN static u32 a43_entry_ok(u32 a0, u32 a1, u32 a2, u32 a3)
{
    cm_t *c = cm_get();
    (void)a0; (void)a1; (void)a2; (void)a3;
    if (!c || !BTP(c) || !BTP(c)->a43_hdr) return 0;
    c->hans = CM_HANS;                         /* the photo menu does not pass through refresh_labels */
    a43_labels(c);
    if (c->title) ((volatile u32 *)c->title)[1] = c->hans ? 3u : 2u;
    if (c->hans) {
        u16 b[8];
        u32 n = 0;
        a43_hz_name(b, &n); b[n] = 0;
        FW(set_fn, UTF16_SET)(b, SLOT_TITLE_V, HANS_STYLE);
        *(volatile u8 *)RUNTIME_STYLE = HANS_FONT;
    } else {
        char e[16];
        u32 n = 0;
        en_w(e, &n, 0x746F6850u); en_w(e, &n, 0x7361206Fu); en_w(e, &n, 0x74636570u);   /* "Photo aspect" */
        e[n] = 0;
        FW(set_fn, ASCII_SET)(e, SLOT_TITLE_V, 0u);
    }
    c->title_slot = SLOT_TITLE_V;
    ((volatile u32 *)PAGE0)[10] = BTP(c)->a43_hdr;
    FW(page_fn, OPEN_PAGE)(2u, OWN_INDEX);
    return 0;
}

IMG_FN static u32 a43_pick(u32 sel)
{
    cm_t *c = cm_get();
    if (c && BTP(c) && BTP(c)->a43_status == 1u && cfg_ready(c) && sel <= A43_PROFILES) {
        BTP(c)->a43_sel = sel;
        a43_apply(c);
        save_cfg(c);
        a43_entry_ok(0u, 0u, 0u, 0u);          /* show the page again with the new mark */
    }
    return 0;
}

IMG_FN static u32 a43_off_ok(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return a43_pick(0u); }
IMG_FN static u32 a43_43_ok(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return a43_pick(1u); }
