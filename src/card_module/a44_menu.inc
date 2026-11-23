#define A44_ENTRY_SLOT 271u
#define A44_FIRST_SLOT 272u
#define A44_LEVEL_SLOT 275u
#define A44_LEVEL_FIRST_SLOT 276u
#define A44_ENTRY_ID   0x2FC9u
#define A44_FIRST_ID   0x2FD5u
#define A44_LEVEL_FIRST_ID 0x2FD9u
#ifdef LIGHTROOM_GRAIN
#define A47_SWITCH_SLOT 282u
#define A47_SWITCH_ID 0x2FDFu
#endif

/* English pieces are packed immediates (en_w): these labels run from the hook image, which has no .rodata. */
/* Row text kind of one of the FILM pages for the current language (refresh_labels sets c->hans). */
IMG_FN static void a44_kind(cm_t *c, u32 hdr)
{
    if (!hdr) return;
    const volatile u32 *h = (const volatile u32 *)hdr;
    u32 *rows = (u32 *)h[3], count = h[5] & 0xFFFFu;
    for (u32 i = 0; rows && i < count; ++i) {
        volatile u32 *r = (volatile u32 *)(rows + i * ROW_WORDS);
        r[4] = (r[4] & ~(F_ASCII | F_UTF16)) | (c->hans ? F_UTF16 : F_ASCII);
    }
}

IMG_FN static void a44_set(cm_t *c, u16 *hz, u32 nh, char *en, u32 ne, u32 slot)
{
    hz[nh] = 0; en[ne] = 0;
    if (c->hans) FW(set_fn, UTF16_SET)(hz, slot, HANS_STYLE);
    else FW(set_fn, ASCII_SET)(en, slot, 0u);
}

IMG_FN static void a44_label(cm_t *c)
{
    if (!c->dg_buf) return;
    u16 hz[16];
    char en[24];
    u32 n = 0, m = 0, mode = A44_LOG(c)[A44_MODE], level = a44_strength(A44_LOG(c)[A44_LEVEL]);
    char *b = en;
    /* Entry and FILM page title: 胶片颗粒 (FILM without the added glyphs)：style / FILM: style */
#ifdef FONT_EXT
    if (fx_ready(c)) {
        hz_put(hz, &n, 0x80F6u); hz_put(hz, &n, 0x7247u); hz_put(hz, &n, 0x9897u); hz_put(hz, &n, 0x7C92u);
    } else
#endif
    {
        hz_put(hz, &n, 'F'); hz_put(hz, &n, 'I'); hz_put(hz, &n, 'L'); hz_put(hz, &n, 'M');
    }
    hz_put(hz, &n, 0xFF1Au);
    en_w(b, &m, 0x4D4C4946u); en_w(b, &m, 0x0000203Au);                 /* "FILM: " */
    if (mode == 1u) {
        hz_put(hz, &n, 'F'); hz_put(hz, &n, 'U'); hz_put(hz, &n, 'J'); hz_put(hz, &n, 'I');
        hz_put(hz, &n, '4'); hz_put(hz, &n, '0'); hz_put(hz, &n, '0');
        en_w(b, &m, 0x494A5546u); en_w(b, &m, 0x00303034u);             /* "FUJI400" */
    } else if (mode == 2u) {
        hz_put(hz, &n, 'H'); hz_put(hz, &n, 'P'); hz_put(hz, &n, '5');
        en_w(b, &m, 0x00355048u);                                       /* "HP5" */
    } else {
        hz_put(hz, &n, 0x5173u);                                        /* 关 */
        en_w(b, &m, 0x0066664Fu);                                       /* "Off" */
    }
    a44_set(c, hz, n, en, m, A44_ENTRY_SLOT);
    /* 强度：n / Strength: n */
    n = 0; m = 0;
    hz_put(hz, &n, 0x5F3Au); hz_put(hz, &n, 0x5EA6u); hz_put(hz, &n, 0xFF1Au); hz_put(hz, &n, '0' + level);
    en_w(b, &m, 0x65727453u); en_w(b, &m, 0x6874676Eu); en_w(b, &m, 0x0000203Au); en_put(b, &m, '0' + level);
    a44_set(c, hz, n, en, m, A44_LEVEL_SLOT);
#ifdef LIGHTROOM_GRAIN
    /* NEF写入：开/关 / NEF write: On/Off */
    n = 0; m = 0;
    hz_put(hz, &n, 'N'); hz_put(hz, &n, 'E'); hz_put(hz, &n, 'F');
    hz_put(hz, &n, 0x5199u); hz_put(hz, &n, 0x5165u); hz_put(hz, &n, 0xFF1Au);
    hz_put(hz, &n, A44_LOG(c)[A47_LIGHTROOM] ? 0x5F00u : 0x5173u);
    en_w(b, &m, 0x2046454Eu); en_w(b, &m, 0x74697277u); en_w(b, &m, 0x00203A65u);
    if (A44_LOG(c)[A47_LIGHTROOM]) en_w(b, &m, 0x00006E4Fu); else en_w(b, &m, 0x0066664Fu);
    a44_set(c, hz, n, en, m, A47_SWITCH_SLOT);
#endif
    /* Style rows: 关闭 / Off, FUJI400, HP5; strength rows 1-6 */
    n = 0; m = 0; hz_put(hz, &n, 0x5173u); hz_put(hz, &n, 0x95EDu); en_w(b, &m, 0x0066664Fu);
    a44_set(c, hz, n, en, m, A44_FIRST_SLOT);
    n = 0; m = 0; hz_put(hz, &n, 'F'); hz_put(hz, &n, 'U'); hz_put(hz, &n, 'J'); hz_put(hz, &n, 'I');
    hz_put(hz, &n, '4'); hz_put(hz, &n, '0'); hz_put(hz, &n, '0'); en_w(b, &m, 0x494A5546u); en_w(b, &m, 0x00303034u);
    a44_set(c, hz, n, en, m, A44_FIRST_SLOT + 1u);
    n = 0; m = 0; hz_put(hz, &n, 'H'); hz_put(hz, &n, 'P'); hz_put(hz, &n, '5'); en_w(b, &m, 0x00355048u);
    a44_set(c, hz, n, en, m, A44_FIRST_SLOT + 2u);
    for (u32 i = 0; i < A44_STRENGTHS; ++i) {
        n = 0; m = 0; hz_put(hz, &n, '1' + i); en_put(b, &m, '1' + i);
        a44_set(c, hz, n, en, m, A44_LEVEL_FIRST_SLOT + i);
    }
    a44_kind(c, A44_LOG(c)[8]);
    a44_kind(c, A44_LOG(c)[13]);
    if (c->hans) *(volatile u8 *)RUNTIME_STYLE = HANS_FONT;
}

IMG_FN static u32 a44_entry(u32 a0, u32 a1, u32 a2, u32 a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    cm_t *c = cm_get();
    if (!c || !c->dg_buf || !A44_LOG(c)[8]) return 0u;
    a44_label(c);
    c->title_slot = A44_ENTRY_SLOT;
    ((volatile u32 *)PAGE0)[10] = A44_LOG(c)[8];
    FW(page_fn, OPEN_PAGE)(2u, OWN_INDEX);
    return 0u;
}

IMG_FN static u32 a44_return(u32 a0, u32 a1, u32 a2, u32 a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    cm_t *c = cm_get();
    if (c) hub_open(c);
    return 0u;
}

#ifdef LIGHTROOM_GRAIN
IMG_FN static u32 a47_develop_toggle(u32 a0, u32 a1, u32 a2, u32 a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    cm_t *c = cm_get();
    if (!c || !c->dg_buf) return 0u;
    A44_LOG(c)[A47_LIGHTROOM] ^= 1u;
    save_cfg(c);
    return a44_entry(0u, 0u, 0u, 0u);
}
#endif

IMG_FN static u32 a44_select(u32 mode)
{
    cm_t *c = cm_get();
    if (c && c->dg_buf) {
        A44_LOG(c)[A44_MODE] = mode;
        a44_label(c);
        save_cfg(c);
        hub_open(c);
    }
    return 0u;
}

IMG_FN static u32 a44_off(u32 a, u32 b, u32 c, u32 d)
{ (void)a; (void)b; (void)c; (void)d; return a44_select(0u); }
IMG_FN static u32 a44_fuji(u32 a, u32 b, u32 c, u32 d)
{ (void)a; (void)b; (void)c; (void)d; return a44_select(1u); }
IMG_FN static u32 a44_mono(u32 a, u32 b, u32 c, u32 d)
{ (void)a; (void)b; (void)c; (void)d; return a44_select(2u); }

IMG_FN static u32 a44_level_entry(u32 a, u32 b, u32 c, u32 d)
{
    (void)a; (void)b; (void)c; (void)d;
    cm_t *state = cm_get();
    if (!state || !state->dg_buf || !A44_LOG(state)[13]) return 0u;
    a44_label(state);
    state->title_slot = A44_LEVEL_SLOT;
    ((volatile u32 *)PAGE0)[10] = A44_LOG(state)[13];
    FW(page_fn, OPEN_PAGE)(2u, OWN_INDEX);
    return 0u;
}

IMG_FN static u32 a44_choose_level(u32 level)
{
    cm_t *c = cm_get();
    if (!c || !c->dg_buf) return 0u;
    A44_LOG(c)[A44_LEVEL] = level;
    save_cfg(c);
    return a44_entry(0u, 0u, 0u, 0u);
}

#define A44_CHOOSE(level) \
    IMG_FN static u32 a44_choose_##level(u32 a, u32 b, u32 c, u32 d) \
    { (void)a; (void)b; (void)c; (void)d; return a44_choose_level(level); }
A44_CHOOSE(1)
A44_CHOOSE(2)
A44_CHOOSE(3)
A44_CHOOSE(4)
A44_CHOOSE(5)
A44_CHOOSE(6)
#undef A44_CHOOSE
