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

#ifdef BT_PICKER
/* The picker view: the stock date/time page header with elements 0, 1 (titles, now our text slots), 5-7 (时 分 秒),
 * 14-18 (hour box, ':', minute box, ':', second box), 19 (OK confirm), 20 (back icon) and the end marker 21; its key map with our
 * left / right / OK / MENU. Returns 1 or a refusal >= 130 (no picker; the time row then does nothing). */
#define PK_HDR         0xC1875210u
static u32 install_pk(cm_t *c)
{
    volatile u32 *sh = (volatile u32 *)PK_HDR;
    if (sh[2] != 0x11495858u || sh[3] != 0x114A4528u || sh[4] != 0x114DF6DCu || sh[5] != 0x00010001u) return 130u;
    const u32 *el = (const u32 *)sh[2];
    if (el[0 * 8u + 1u] != 1u || el[1 * 8u + 1u] != 1u || el[14 * 8u + 1u] != 8u || el[16 * 8u + 1u] != 8u ||
        el[18 * 8u + 1u] != 8u || el[19 * 8u + 1u] != 0xBu || el[20 * 8u + 1u] != 4u || el[21 * 8u + 1u] != 0u) return 131u;
    if ((el[0 * 8u + 7u] >> 16) != PK_W_TITLE || (el[1 * 8u + 7u] >> 16) != PK_W_SUB) return 132u;
    u32 keep[13];
    keep[0] = 0u; keep[1] = 1u; keep[2] = 5u; keep[3] = 6u; keep[4] = 7u; keep[5] = 14u;
    keep[6] = 15u; keep[7] = 16u; keep[8] = 17u; keep[9] = 18u; keep[10] = 19u; keep[11] = 20u; keep[12] = 21u;
    u32 *nel = (u32 *)z5_alloc(13u * 32u, 0);
    u32 *hdr = (u32 *)z5_alloc(7u * 4u, 0);
    u32 *disp = (u32 *)z5_alloc(8u * 4u, 0);   /* the route indexes it by the row the previous view left */
    if (!nel || !hdr || !disp) return 133u;
    for (u32 k = 0; k < 13u; k++) copy_words(nel + k * 8u, el + keep[k] * 8u, 8u);
    nel[0 * 8u + 1u] = 3u;                     /* titles: UTF-16 text slots bound in pk_cb0 */
    nel[1 * 8u + 1u] = 3u;
    const u32 *src = (const u32 *)((const u32 *)sh[4])[0];
    u32 n = 0;
    while (n < MAP_MAX && src[n * 4u] != EV_END) n++;
    if (n >= MAP_MAX) return 134u;
    u32 *m = (u32 *)z5_alloc((n + 1u) * 16u, 0);
    if (!m) return 135u;
    copy_words(m, src, (n + 1u) * 4u);
    for (u32 i = 0; i < n; i++) {
        u32 ev = m[i * 4u], fn = m[i * 4u + 1u];
        if (fn == 0x104CF859u) { m[i * 4u + 1u] = img_addr(c, pk_ok); m[i * 4u + 3u] = STAY; }      /* OK: never the clock */
        else if (fn == 0x104CF723u) m[i * 4u + 1u] = img_addr(c, pk_left);
        else if (fn == 0x104CF693u) m[i * 4u + 1u] = img_addr(c, pk_right);
        else if (fn == 0x104CF7B1u) { m[i * 4u + 1u] = 0u; m[i * 4u + 2u] = 0u; m[i * 4u + 3u] = STAY; }   /* touch */
        else if (ev == 0x2BFu) { m[i * 4u + 1u] = img_addr(c, pk_cancel); m[i * 4u + 2u] = 0u; m[i * 4u + 3u] = STAY; }
        else if (fn != 0u && fn != 0x104CF603u && fn != 0x104CF64Bu && !in_list(fn, navigation, sizeof(navigation) / 4u) &&
                 fn != 0x104D3E2Fu) return 136u;   /* an entry this copy does not know */
    }
    for (u32 k = 0; k < 8u; k++) disp[k] = (u32)m;
    for (u32 k = 0; k < 7u; k++) hdr[k] = sh[k];
    hdr[2] = (u32)nel;
    hdr[4] = (u32)disp;
    BTP(c)->pk_hdr = (u32)hdr;
    return 1u;
}
#endif

/* The B门定时 page and its photo shooting menu row. Returns 1 or a refusal >= 120 (menu left stock). */
static u32 install_bt(cm_t *c, u32 *title, u32 first_map)
{
    volatile u32 *ph = (volatile u32 *)PHOTO_HDR;
    const u32 *th = (const u32 *)TEMPLATE_HDR;
    const u32 *layout = 0;
    u32 k, up[5], dn[5];
    if (!BTP(c)) return 125u;
    if (ph[3] != PHOTO_ROWS || ph[4] != PHOTO_DISP || ph[5] != PHOTO_COUNT) return 120u;
    for (k = 0; k < 8u; k++)
        if (((const u32 *)(th[1] + k * 0x10u))[0] == 4u) { layout = (const u32 *)(th[1] + k * 0x10u); break; }
    if (!layout) return 121u;
#ifdef BT_PICKER
    const u32 nr = 2u;                         /* start, time (the picker) */
    if (((const u32 *)LAYOUTS_2ROW)[0] != 2u) return 121u;
    layout = (const u32 *)LAYOUTS_2ROW;
    if (install_pk(c) != 1u) return 126u;
#elif defined(BT_SUGGEST)
    const u32 nr = 5u;                         /* start, hours, minutes, seconds, metering suggestion */
    layout = 0;
    for (k = 0; k < 8u; k++)
        if (((const u32 *)(th[1] + k * 0x10u))[0] == 5u) { layout = (const u32 *)(th[1] + k * 0x10u); break; }
    if (!layout) return 127u;
#else
    const u32 nr = 4u;
#endif
    u32 *rows = (u32 *)z5_alloc(5u * ROW_SIZE, 0);
    u32 *disp = (u32 *)z5_alloc(5u * 4u, 0);
    u32 *hdr = (u32 *)z5_alloc(7u * 4u, 0);
#ifdef PC_IMPORT
    const u32 extra = 2u;                      /* B门定时, 一键导入配方 */
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
#ifdef BT_SUGGEST
    up[4] = img_addr(c, bt_sug_ok); dn[4] = 0u;
#else
    up[4] = 0u; dn[4] = 0u;
#endif
    for (k = 0; k < nr; k++) {
        u32 *r = rows + k * ROW_WORDS;
#ifdef BT_PICKER
        copy_words(r, (const u32 *)GRID_ROWS, ROW_WORDS);   /* the 2-row shape of the stock On/Off pages */
        r[4] = PLAIN_ROW | F_UTF16;
#else
        copy_words(r, (const u32 *)SETUP_ROWS + (STOCK_ROWS - 1u) * ROW_WORDS, ROW_WORDS);
        r[4] = ENTRY_FLAGS | F_UTF16;
#endif
        r[0] = BT_SLOT_ROW + k;
        r[6] = BT_ID_ROW + k;
#ifdef BT_PICKER
        disp[k] = build_map(first_map, k ? img_addr(c, pk_open) : up[0], STAY);
#else
        disp[k] = (k && k < 4u) ? bt_map(first_map, up[k], dn[k]) : build_map(first_map, up[k], STAY);
#endif
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
#ifdef PC_IMPORT
    {   /* "导入配方" page (rows 开始导入 / result, shaped like the B门定时 rows) and its photo menu row */
        const u32 *lay2 = 0;
        for (k = 0; k < 9u; k++)               /* template layouts: 3, 4, 5, 6, 6, 7, 8, 1, 2 rows (0x11547838) */
            if (((const u32 *)(th[1] + k * 0x10u))[0] == 2u) { lay2 = (const u32 *)(th[1] + k * 0x10u); break; }
        if (!lay2) return 129u;
        u32 *irows = (u32 *)z5_alloc(2u * ROW_SIZE, 0);
        u32 *idisp = (u32 *)z5_alloc(2u * 4u, 0);
        u32 *ihdr = (u32 *)z5_alloc(7u * 4u, 0);
        if (!irows || !idisp || !ihdr) return 130u;
        for (k = 0; k < 2u; k++) {
            u32 *r = irows + k * ROW_WORDS;
            copy_words(r, (const u32 *)SETUP_ROWS + (STOCK_ROWS - 1u) * ROW_WORDS, ROW_WORDS);
            r[0] = PI_SLOT + 1u + k;
            r[4] = ENTRY_FLAGS | F_UTF16;
            r[6] = PI_ID + 1u + k;
            idisp[k] = build_map(first_map, img_addr(c, k ? pi_none : pi_ok), STAY);
            if (!idisp[k]) return 131u;
        }
        ihdr[0] = th[0]; ihdr[1] = (u32)lay2; ihdr[2] = (u32)title;
        ihdr[3] = (u32)irows; ihdr[4] = (u32)idisp; ihdr[5] = (2u << 16) | 2u; ihdr[6] = th[6];
        e = prow + (PHOTO_STOCK + 1u) * ROW_WORDS;
        copy_words(e, (const u32 *)SETUP_ROWS + (STOCK_ROWS - 1u) * ROW_WORDS, ROW_WORDS);
        e[0] = PI_SLOT;
        e[4] = ENTRY_FLAGS | F_UTF16;
        e[6] = PI_ID;
        pdisp[PHOTO_STOCK + 1u] = build_map(((const u32 *)PHOTO_DISP)[PHOTO_STOCK - 1u], img_addr(c, pi_entry_ok), STAY);
        if (!pdisp[PHOTO_STOCK + 1u]) return 128u;
        BTP(c)->im_hdr = (u32)ihdr;
    }
#endif
    BTP(c)->hdr = (u32)hdr;
    BTP(c)->map0 = disp[0];
    bt_labels(c);
#ifdef PC_IMPORT
    pi_label(c);
#endif
    ph[3] = (u32)prow;
    ph[4] = (u32)pdisp;
    ph[5] = PHOTO_COUNT + extra;               /* count last: tables are complete */
    return 1u;
}
#endif

#if defined(PC_TEST_OPEN) && !defined(PC_HIDE_CREATIVE)
/* Emulator test of the stock list: only callback 3 replaced (see pcl_usable). */
static u32 install_pc_hide(cm_t *c)
{
    volatile u32 *d = (volatile u32 *)0xC18339A8u;
    u32 *cb = (u32 *)z5_alloc(5u * 4u, 0);
    if (!cb || d[4] != 0x107625DCu) return 147u;
    copy_words(cb, (const u32 *)0x107625DCu, 5u);
    cb[3] = img_addr(c, pcl_usable);
    d[4] = (u32)cb;
    return 1u;
}
#endif
#ifdef PC_HIDE_CREATIVE
/* Page 0x79 rows: 0-7 Auto .. Flat (ids 0x17DC, 0x169A, 0x16A6-0x16A8, 0x16AF-0x16B1), 8 separator 0x1872,
 * 9-28 Creative 01-20 (0x1873-0x1886, one shared OK handler that works by row), 29-37 C-1..C-9 (0x16B2-0x16BA).
 * Kept: 0-7 and 29-37, each with its own OK handler; the cursor is set by id (0x1044E5FA, -1 -> row 0).
 * Up / down 0x105113E9 / 0x10511427 jump 2 rows at 0x1873 / 0x16B1 (around the separator): the copies use the plain
 * moves pcl_up / pcl_down (0x10450908(page, +-1), as the stock ones without the jump). Returns 1 or a refusal >= 140 (page left stock). */
#define PCL_DESC       0xC18339A8u
#define PCL_HDR        0xC1876D40u
#define PCL_ROWS       0x114F0738u
#define PCL_DISP       0x1153E2C0u
#define PCL_COUNT      0x00070026u
#define PCL_CB         0x107625DCu
static u32 install_pc_hide(cm_t *c)
{
    volatile u32 *h = (volatile u32 *)PCL_HDR, *d = (volatile u32 *)PCL_DESC;
    const u32 *rows = (const u32 *)PCL_ROWS, *disp = (const u32 *)PCL_DISP;
    u32 k, n = 0u;
    if (h[3] != PCL_ROWS || h[4] != PCL_DISP || h[5] != PCL_COUNT || d[4] != PCL_CB || d[10] != PCL_HDR) return 140u;
    if (((const u16 *)PCL_CB)[0] != 0x3067u || ((const u32 *)PCL_CB)[0] != 0x104E3067u) return 141u;
    if (rows[7 * ROW_WORDS + 6u] != 0x16B1u || rows[8 * ROW_WORDS + 6u] != 0x1872u ||
        rows[9 * ROW_WORDS + 6u] != 0x1873u || rows[28 * ROW_WORDS + 6u] != 0x1886u ||
        rows[29 * ROW_WORDS + 6u] != 0x16B2u || rows[37 * ROW_WORDS + 6u] != 0x16BAu) return 142u;
    /* A29: all 38 rows stay (rows 8-28 are hidden by pcl_init); only the key maps are copied. */
    u32 *nd = (u32 *)z5_alloc(38u * 4u, 0);
    u32 *cb = (u32 *)z5_alloc(5u * 4u, 0);
    if (!nd || !cb) return 143u;
    for (k = 0; k < 38u; k++) {
        const u32 *src = (const u32 *)disp[k];
        u32 m = 0;
        while (m < MAP_MAX && src[m * 4u] != EV_END) m++;
        if (m >= MAP_MAX) return 144u;
        u32 *o = (u32 *)z5_alloc((m + 1u) * 16u, 0);
        if (!o) return 145u;
        copy_words(o, src, (m + 1u) * 4u);
        for (u32 i = 0; i < m; i++) {
            if (o[i * 4u + 1u] == 0x105113E9u) o[i * 4u + 1u] = img_addr(c, pcl_up);
            else if (o[i * 4u + 1u] == 0x10511427u) o[i * 4u + 1u] = img_addr(c, pcl_down);
        }
        nd[n] = (u32)o;
        n++;
    }
    if (n != 38u) return 146u;
    copy_words(cb, (const u32 *)PCL_CB, 5u);
    cb[0] = img_addr(c, pcl_init);
    if (cb[1] != 0x104E308Du || cb[4] != 0x104E320Fu) return 148u;
    cb[1] = img_addr(c, pcl_cb1);
    cb[4] = img_addr(c, pcl_cb4);
#ifdef PC_TEST_OPEN
    cb[3] = img_addr(c, pcl_usable);           /* emulator test only */
#endif
    h[4] = (u32)nd;
    d[4] = (u32)cb;
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
