/* Burst speed-up: install-time code (runs once from the original module image). Image code is in
 * burst_speed.inc, which must stay before Z5_IMAGE_END. */

/* The switch page: two rows shaped like the battery page's. Returns 1 or a refusal >= 110. */
static u32 install_burst_page(cm_t *c, u32 *title, u32 first_map)
{
    const u32 *th = (const u32 *)TEMPLATE_HDR;
    if (((const u32 *)LAYOUTS_2ROW)[0] != 2u) return 110u;
    u32 *rows = (u32 *)z5_alloc(2u * ROW_SIZE, 0);
    u32 *disp = (u32 *)z5_alloc(2u * 4u, 0);
    u32 *hdr = (u32 *)z5_alloc(7u * 4u, 0);
    if (!rows || !disp || !hdr) return 111u;
    u32 kind = c->hans ? F_UTF16 : F_ASCII;
    copy_words(rows, (const u32 *)GRID_ROWS, ROW_WORDS);
    copy_words(rows + ROW_WORDS, rows, ROW_WORDS);
    rows[0] = BURST_SLOT_MECH;  rows[4] = PLAIN_ROW | kind; rows[6] = BURST_ID_MECH;
    rows[ROW_WORDS] = BURST_SLOT_SIL;  rows[ROW_WORDS + 4u] = PLAIN_ROW | kind; rows[ROW_WORDS + 6u] = BURST_ID_SIL;
    disp[0] = build_map(first_map, img_addr(c, burst_mech_ok), STAY);
    disp[1] = build_map(first_map, img_addr(c, burst_sil_ok), STAY);
    if (!disp[0] || !disp[1]) return 112u;
    hdr[0] = th[0]; hdr[1] = LAYOUTS_2ROW; hdr[2] = (u32)title;
    hdr[3] = (u32)rows; hdr[4] = (u32)disp; hdr[5] = (2u << 16) | 2u; hdr[6] = th[6];
    c->burst_rows = (u32)rows;
    c->burst_hdr = (u32)hdr;
    return 1u;
}

#ifdef BULB_MENU
/* Photo shooting menu (page 0x46; emulator trace 2026-11-16: descriptor 0xC18330E4, header 0xC1876878, 34 rows
 * 0x114EE2E8, key maps 0x1153DD90, count 0x00080022). Same scheme as the SETUP row: heap copy + one row. */
#define PHOTO_HDR      0xC1876878u
#define PHOTO_ROWS     0x114EE2E8u
#define PHOTO_DISP     0x1153DD90u
#define PHOTO_COUNT    0x00080022u
#define PHOTO_STOCK    34u

/* build_map, then left (0x249, 0x264; stock: back) calls dn_fn and stays. */
__attribute__((unused)) static u32 bt_map(u32 src, u32 up_fn, u32 dn_fn)
{
    u32 *m = (u32 *)build_map(src, up_fn, STAY);
    if (!m) return 0;
    for (u32 i = 0; m[i * 4u] != EV_END; i++)
        if (m[i * 4u] == 0x249u || m[i * 4u] == 0x264u) { m[i * 4u + 1u] = dn_fn; m[i * 4u + 2u] = 0u; m[i * 4u + 3u] = STAY; }
    return (u32)m;
}


/* The B门定时 page and its photo shooting menu row. Returns 1 or a refusal >= 120 (menu left stock). */
static u32 install_bt(cm_t *c, u32 *title, u32 first_map)
{
    volatile u32 *ph = (volatile u32 *)PHOTO_HDR;
    const u32 *th = (const u32 *)TEMPLATE_HDR;
    const u32 *layout = 0;
    u32 k, up[BT_ROWS], dn[BT_ROWS], six = 0u;
    if (!BTP(c)) return 125u;
    if (ph[3] != PHOTO_ROWS || ph[4] != PHOTO_DISP || ph[5] != PHOTO_COUNT) return 120u;
    /* A55: six rows; the second six-row layout (equally spaced selectable rows, as the FILM strength page) */
    for (k = 0; k < 8u; k++)
        if (((const u32 *)(th[1] + k * 0x10u))[0] == BT_ROWS && ++six == 2u) { layout = (const u32 *)(th[1] + k * 0x10u); break; }
    if (!layout) return 121u;
    const u32 nr = BT_ROWS;
    u32 *rows = (u32 *)z5_alloc(BT_ROWS * ROW_SIZE, 0);
    u32 *disp = (u32 *)z5_alloc(BT_ROWS * 4u, 0);
    u32 *hdr = (u32 *)z5_alloc(7u * 4u, 0);
#ifdef ASPECT43_FX
    const u32 extra = BTP(c)->a43_status == 1u ? 2u : 1u;   /* A69: "照片画幅" after "B门定时" (A68 firmware only) */
#else
    const u32 extra = 1u;
#endif
    u32 *prow = (u32 *)z5_alloc((PHOTO_STOCK + extra) * ROW_SIZE, 0);
    u32 *pdisp = (u32 *)z5_alloc((PHOTO_STOCK + extra) * 4u, 0);
    if (!rows || !disp || !hdr || !prow || !pdisp) return 122u;
    up[0] = img_addr(c, bt_start_ok); dn[0] = 0u;
    up[1] = img_addr(c, bt_h_up); dn[1] = img_addr(c, bt_h_dn);
    up[2] = img_addr(c, bt_m_up); dn[2] = img_addr(c, bt_m_dn);
    up[3] = img_addr(c, bt_s_up); dn[3] = img_addr(c, bt_s_dn);
    up[4] = img_addr(c, bt_n_up); dn[4] = img_addr(c, bt_n_dn);
    up[5] = img_addr(c, bt_g_up); dn[5] = img_addr(c, bt_g_dn);
    for (k = 0; k < nr; k++) {
        u32 *r = rows + k * ROW_WORDS;
        copy_words(r, (const u32 *)SETUP_ROWS + (STOCK_ROWS - 1u) * ROW_WORDS, ROW_WORDS);
        r[4] = ENTRY_FLAGS | F_UTF16;
        r[0] = BT_SLOT_ROW + k;
        r[6] = BT_ID_ROW + k;
        disp[k] = k ? bt_map(first_map, up[k], dn[k]) : build_map(first_map, up[k], STAY);
        if (!disp[k]) return 123u;
    }
    hdr[0] = th[0]; hdr[1] = (u32)layout; hdr[2] = (u32)title;
    hdr[3] = (u32)rows; hdr[4] = (u32)disp; hdr[5] = (nr << 16) | nr; hdr[6] = th[6];
    copy_words(prow, (const u32 *)PHOTO_ROWS, PHOTO_STOCK * ROW_WORDS);
    u32 *e = prow + PHOTO_STOCK * ROW_WORDS;
    copy_words(e, (const u32 *)SETUP_ROWS + (STOCK_ROWS - 1u) * ROW_WORDS, ROW_WORDS);
    e[0] = BT_SLOT_ENTRY;
    e[4] = ENTRY_FLAGS | F_UTF16;
    e[6] = BT_ID_ENTRY;
    copy_words(pdisp, (const u32 *)PHOTO_DISP, PHOTO_STOCK);
    pdisp[PHOTO_STOCK] = build_map(((const u32 *)PHOTO_DISP)[PHOTO_STOCK - 1u], img_addr(c, bt_entry_ok), STAY);
    if (!pdisp[PHOTO_STOCK]) return 124u;
#ifdef ASPECT43_FX
    if (extra == 2u) {                         /* A69: "照片画幅" row; A81: three-row page (off / 4:3 / 2.33:1) */
        u32 *e2 = e + ROW_WORDS;
        u32 *arows = (u32 *)z5_alloc(3u * ROW_SIZE, 0);
        u32 *adisp = (u32 *)z5_alloc(3u * 4u, 0);
        u32 *ahdr = (u32 *)z5_alloc(7u * 4u, 0);
        const u32 *lay3 = (const u32 *)th[1];      /* the template's first layout: three rows (as a44_build_page) */
        if (!arows || !adisp || !ahdr || lay3[0] != 3u) return 126u;
        copy_words(e2, e, ROW_WORDS);
        e2[0] = A43_SLOT_ENTRY;
        e2[6] = A43_ID_ENTRY;
        pdisp[PHOTO_STOCK + 1u] = build_map(((const u32 *)PHOTO_DISP)[PHOTO_STOCK - 1u], img_addr(c, a43_entry_ok), STAY);
        copy_words(arows, (const u32 *)GRID_ROWS, ROW_WORDS);
        copy_words(arows + ROW_WORDS, arows, ROW_WORDS);
        copy_words(arows + 2u * ROW_WORDS, arows, ROW_WORDS);
        arows[0] = A43_SLOT_ROW;      arows[4] = PLAIN_ROW | F_UTF16; arows[6] = A43_ID_ROW;
        arows[ROW_WORDS] = A43_SLOT_ROW + 1u; arows[ROW_WORDS + 4u] = PLAIN_ROW | F_UTF16; arows[ROW_WORDS + 6u] = A43_ID_ROW1;
        arows[2u * ROW_WORDS] = A43_SLOT_ROW + 2u; arows[2u * ROW_WORDS + 4u] = PLAIN_ROW | F_UTF16;
        arows[2u * ROW_WORDS + 6u] = A43_ID_ROW2;
        adisp[0] = build_map(first_map, img_addr(c, a43_off_ok), STAY);
        adisp[1] = build_map(first_map, img_addr(c, a43_43_ok), STAY);
        adisp[2] = build_map(first_map, img_addr(c, a43_233_ok), STAY);
        if (!pdisp[PHOTO_STOCK + 1u] || !adisp[0] || !adisp[1] || !adisp[2]) return 127u;
        ahdr[0] = th[0]; ahdr[1] = (u32)lay3; ahdr[2] = (u32)title;
        ahdr[3] = (u32)arows; ahdr[4] = (u32)adisp; ahdr[5] = (3u << 16) | 3u; ahdr[6] = th[6];
        BTP(c)->a43_hdr = (u32)ahdr;
        BTP(c)->a43_entry_row = (u32)e2;
        a43_labels(c);
    }
#endif
    BTP(c)->hdr = (u32)hdr;
    BTP(c)->map0 = disp[0];
    BTP(c)->entry_row = (u32)e;
    bt_labels(c);
    ph[3] = (u32)prow;
    ph[4] = (u32)pdisp;
    ph[5] = PHOTO_COUNT + extra;               /* count last: tables are complete */
    return 1u;
}
#endif


/* Module code, once per power-up after the menu: words, then the task. */
static void burst_start(cm_t *c)
{
    burst_words(c, 0u);
    z5_task *t = (z5_task *)z5_alloc(sizeof(z5_task), 0);
    if (!t) { c->burst_task_rc = 0xE1000000u; return; }
    char name[8] = {'B', 'U', 'R', 'S', 'T', 'S', 'P', 0};
    t->start_rc = -1;
    c->burst_task_rc = (u32)z5_task_create_raw(t, name, 0x30u, (void *)img_addr(c, burst_task));
    if (!c->burst_task_rc && t->start_rc) c->burst_task_rc = 0xE2000000u | ((u32)t->start_rc & 0xFFFFFFu);
}
