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
#define A43_ID_ROW2    0x2FCEu         /* A81: third row "2.33:1" */
/* A81: 2.33:1 needs the A81 firmware (parameter block; preview kind 2 changed to 2.33 sizes: 640 x 272 at the
 * kind-2 entry of the selector table 0x1167B7A4). On A78 or older a 2.33 choice stays off. */
#define A43_HAS_233    (*(volatile const u32 *)0x1167B7ACu == ((272u << 16) | 640u))
#define A43_PRM        0x200u          /* parameter block in the copy, read by the A81 firmware */

IMG_FN static u32 a43_want(cm_t *c)
{
    bt_t *t = BTP(c);
    return t && t->a43_status == 1u && t->a43_copy && t->a43_sel && A43_AREA_NOW == 0u &&
           (t->a43_sel != 2u || A43_HAS_233);
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
        u32 r = t->a43_sel, i;
        volatile u32 *pb = (volatile u32 *)(b + A43_PRM);
        if (t->a43_loaded != r || pb[0] != 0x50544152u) {
            if (a43_is_on()) {                         /* switch the firmware off before touching the tables */
                p[1u * 9u + 8u] = 0x1167B8F0u;
                __asm__ volatile ("dsb sy" ::: "memory");
            }
            const volatile u32 *src = (const volatile u32 *)(b + (r == 2u ? A43_STASH233 : A43_STASH43));
            for (i = 0; i < A43_GROUPS * A43_GROUP_BYTES / 4u; i++) ((volatile u32 *)b)[i] = src[i];
            pb[1] = r == 2u ? (720u << 16) : 332u;     /* area offset (x, y) */
            pb[2] = pb[3] = pb[4] = r == 2u ? 0u : 332u;   /* trim row (x, y) x 3 */
            pb[5] = r == 2u ? 0x0202u : 0x0103u;       /* kind | ratio << 8 */
            __asm__ volatile ("dsb sy" ::: "memory");
            pb[0] = 0x50544152u;                       /* 'RATP' */
            t->a43_loaded = r;
        }
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

IMG_FN static void a43_watch(cm_t *c)      /* module task, every 50 ms: follow the area and the choice */
{
    if (BTP(c) && BTP(c)->a43_status == 1u && a43_want(c) != a43_is_on()) a43_apply(c);
}

IMG_FN static void a43_row_kind(volatile u32 *r, u32 hans)
{
    if (r) r[4] = (r[4] & ~(F_ASCII | F_UTF16)) | (hans ? F_UTF16 : F_ASCII);
}

IMG_FN static void a43_hz_name(u16 *b, u32 *n)  /* 照片画幅 */
{
    hz_put(b, n, 0x7167u); hz_put(b, n, 0x7247u); hz_put(b, n, 0x753Bu); hz_put(b, n, 0x5E45u);
}

/* Entry row "照片画幅：关 / 4:3 / 2.33:1", page rows "关" / "4:3" / "2.33:1" with "*" before the current one. */
IMG_FN static void a43_hz_ratio(u16 *b, u32 *n, u32 r)
{
    if (r == 1u) { hz_put(b, n, '4'); hz_put(b, n, ':'); hz_put(b, n, '3'); }
    else if (r == 2u) { hz_put(b, n, '2'); hz_put(b, n, '.'); hz_put(b, n, '3'); hz_put(b, n, '3');
                        hz_put(b, n, ':'); hz_put(b, n, '1'); }
    else hz_put(b, n, 0x5173u);                        /* 关 */
}

IMG_FN static void a43_en_ratio(char *b, u32 *n, u32 r)
{
    if (r == 1u) en_w(b, n, 0x00333A34u);                         /* "4:3" */
    else if (r == 2u) { en_w(b, n, 0x33332E32u); en_w(b, n, 0x0000313Au); }   /* "2.33:1" */
    else en_w(b, n, 0x0066664Fu);                                 /* "Off" */
}

IMG_FN static void a43_labels(cm_t *c)
{
    bt_t *t = BTP(c);
    u32 hans = c->hans, n, sel, r;
    if (!t || t->a43_status != 1u) return;
    sel = t->a43_sel;
    a43_row_kind((volatile u32 *)t->a43_entry_row, hans);
    if (t->a43_hdr) {
        const volatile u32 *h = (const volatile u32 *)t->a43_hdr;
        u32 *rows = (u32 *)h[3];
        for (r = 0; r < (h[5] & 0xFFFFu); r++) a43_row_kind((volatile u32 *)(rows + r * ROW_WORDS), hans);
    }
    if (hans) {
        u16 b[16];
        n = 0; a43_hz_name(b, &n); hz_put(b, &n, 0xFF1Au); a43_hz_ratio(b, &n, sel);
        b[n] = 0; FW(set_fn, UTF16_SET)(b, A43_SLOT_ENTRY, HANS_STYLE);
        for (r = 0; r < 3u; r++) {
            n = 0; if (sel == r) { hz_put(b, &n, '*'); hz_put(b, &n, ' '); }
            a43_hz_ratio(b, &n, r);
            b[n] = 0; FW(set_fn, UTF16_SET)(b, A43_SLOT_ROW + r, HANS_STYLE);
        }
        *(volatile u8 *)RUNTIME_STYLE = HANS_FONT;
    } else {
        char b[28];
        n = 0; en_w(b, &n, 0x746F6850u); en_w(b, &n, 0x7361206Fu); en_w(b, &n, 0x74636570u); en_w(b, &n, 0x0000203Au);
        a43_en_ratio(b, &n, sel);                                  /* "Photo aspect: Off / 4:3 / 2.33:1" */
        b[n] = 0; FW(set_fn, ASCII_SET)(b, A43_SLOT_ENTRY, 0u);
        for (r = 0; r < 3u; r++) {
            n = 0; if (sel == r) en_w(b, &n, 0x0000202Au);
            a43_en_ratio(b, &n, r);
            b[n] = 0; FW(set_fn, ASCII_SET)(b, A43_SLOT_ROW + r, 0u);
        }
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
IMG_FN static u32 a43_233_ok(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return a43_pick(2u); }
