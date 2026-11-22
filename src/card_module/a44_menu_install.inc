static u32 a44_build_page(u32 *title, u32 first_map, const u32 *handlers, u32 count,
                          u32 first_slot, u32 first_id, u32 back)
{
    const u32 *th = (const u32 *)TEMPLATE_HDR;
    /* C 1.50's second six-row layout has six equally spaced selectable rows. */
    const u32 *layout = (const u32 *)(th[1] + (count == A44_STRENGTHS ? 0x40u : (count - 3u) * 0x10u));
    if (layout[0] != count) return 0u;
    u32 *rows = z5_alloc(count * ROW_SIZE, 0);
    u32 *disp = z5_alloc(count * sizeof(u32), 0);
    u32 *hdr = z5_alloc(7u * sizeof(u32), 0);
    if (!rows || !disp || !hdr) return 0u;
    for (u32 i = 0; i < count; ++i) {
        u32 *r = rows + i * ROW_WORDS;
        copy_words(r, (const u32 *)SETUP_ROWS + (STOCK_ROWS - 1u) * ROW_WORDS, ROW_WORDS);
        r[0] = first_slot + i;
        r[4] = ENTRY_FLAGS | F_UTF16;
        r[6] = first_id + i;
#ifdef LIGHTROOM_GRAIN
        if (count == 5u && i == 4u) {
            r[0] = A47_SWITCH_SLOT;
            r[6] = A47_SWITCH_ID;
        }
#endif
        disp[i] = build_map(first_map, handlers[i], STAY);
        if (!disp[i]) return 0u;
        for (u32 *e = (u32 *)disp[i]; e[0] != EV_END; e += 4u) {
            if (e[3] != BACK) continue;
            e[1] = back;
            e[2] = 0u;
            e[3] = STAY;
        }
    }
    hdr[0] = th[0]; hdr[1] = (u32)layout; hdr[2] = (u32)title;
    hdr[3] = (u32)rows; hdr[4] = (u32)disp; hdr[5] = (count << 16) | count; hdr[6] = th[6];
    return (u32)hdr;
}

static void a44_install_menu(cm_t *c, u32 *title, u32 first_map)
{
    if (!c->dg_buf) return;
#ifdef LIGHTROOM_GRAIN
    u32 film[5];
#else
    u32 film[4];
#endif
    u32 levels[A44_STRENGTHS];
    film[0] = img_addr(c, a44_off);
    film[1] = img_addr(c, a44_fuji);
    film[2] = img_addr(c, a44_mono);
    film[3] = img_addr(c, a44_level_entry);
#ifdef LIGHTROOM_GRAIN
    film[4] = img_addr(c, a47_develop_toggle);
#endif
    levels[0] = img_addr(c, a44_choose_1);
    levels[1] = img_addr(c, a44_choose_2);
    levels[2] = img_addr(c, a44_choose_3);
    levels[3] = img_addr(c, a44_choose_4);
    levels[4] = img_addr(c, a44_choose_5);
    levels[5] = img_addr(c, a44_choose_6);
    u32 level_page = a44_build_page(title, first_map, levels, A44_STRENGTHS,
                                   A44_LEVEL_FIRST_SLOT, A44_LEVEL_FIRST_ID, img_addr(c, a44_entry));
    if (!level_page) return;
    u32 film_page = a44_build_page(title, first_map, film, sizeof(film) / sizeof(film[0]),
                                  A44_FIRST_SLOT, A44_FIRST_ID, img_addr(c, a44_return));
    if (!film_page) return;
    FW(set_fn, UTF16_SET)((const u16 *)u"关闭", A44_FIRST_SLOT, HANS_STYLE);
    FW(set_fn, UTF16_SET)((const u16 *)u"FUJI400", A44_FIRST_SLOT + 1u, HANS_STYLE);
    FW(set_fn, UTF16_SET)((const u16 *)u"HP5", A44_FIRST_SLOT + 2u, HANS_STYLE);
    for (u32 i = 0; i < A44_STRENGTHS; ++i) {
        u16 text[2];
        text[0] = (u16)('1' + i);
        text[1] = 0;
        FW(set_fn, UTF16_SET)(text, A44_LEVEL_FIRST_SLOT + i, HANS_STYLE);
    }
    A44_LOG(c)[8] = film_page;
    A44_LOG(c)[13] = level_page;
    a44_label(c);
}
