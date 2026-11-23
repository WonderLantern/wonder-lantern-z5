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

IMG_FN static void a44_label(cm_t *c)
{
    if (!c->dg_buf) return;
    u16 text[16];
    u32 n = 0, mode = A44_LOG(c)[A44_MODE];
#ifdef FONT_EXT
    if (fx_ready(c)) {                         /* 胶片颗粒 with the glyphs added by font_ext.inc */
        hz_put(text, &n, 0x80F6u); hz_put(text, &n, 0x7247u); hz_put(text, &n, 0x9897u); hz_put(text, &n, 0x7C92u);
    } else
#endif
    {
        hz_put(text, &n, 'F'); hz_put(text, &n, 'I'); hz_put(text, &n, 'L'); hz_put(text, &n, 'M');
    }
    hz_put(text, &n, 0xFF1Au);
    if (mode == 1u) {
        hz_put(text, &n, 'F'); hz_put(text, &n, 'U');
        hz_put(text, &n, 'J'); hz_put(text, &n, 'I');
        hz_put(text, &n, '4'); hz_put(text, &n, '0'); hz_put(text, &n, '0');
    } else if (mode == 2u) {
        hz_put(text, &n, 'H'); hz_put(text, &n, 'P'); hz_put(text, &n, '5');
    } else {
        hz_put(text, &n, 0x5173u);
    }
    hz_put(text, &n, 0u);
    FW(set_fn, UTF16_SET)(text, A44_ENTRY_SLOT, HANS_STYLE);
    n = 0;
    hz_put(text, &n, 0x5F3Au); hz_put(text, &n, 0x5EA6u);
    hz_put(text, &n, 0xFF1Au);
    hz_put(text, &n, '0' + a44_strength(A44_LOG(c)[A44_LEVEL]));
    hz_put(text, &n, 0u);
    FW(set_fn, UTF16_SET)(text, A44_LEVEL_SLOT, HANS_STYLE);
#ifdef LIGHTROOM_GRAIN
    n = 0;
    hz_put(text, &n, 'N'); hz_put(text, &n, 'E'); hz_put(text, &n, 'F');
    hz_put(text, &n, 0x5199u); hz_put(text, &n, 0x5165u); /* 写入 */
    hz_put(text, &n, 0xFF1Au);
    hz_put(text, &n, A44_LOG(c)[A47_LIGHTROOM] ? 0x5F00u : 0x5173u);
    hz_put(text, &n, 0u);
    FW(set_fn, UTF16_SET)(text, A47_SWITCH_SLOT, HANS_STYLE);
#endif
    *(volatile u8 *)RUNTIME_STYLE = HANS_FONT;
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
