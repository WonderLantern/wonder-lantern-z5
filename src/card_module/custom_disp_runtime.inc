/* Settings view and native detailed-information visibility, owned by the cm image. */
typedef struct {
    u16 hz[5][16]; char en[5][24];
    u32 saved[64u * 6u], owned[12], pending;
} custom_disp_state;

IMG_FN static u32 custom_hide_native(cm_t *c)
{
    return (c->custom_flags & 1u) && !(c->custom_flags & 8u) &&
               *(volatile u8 *)0xC18119A0u == 3u && *(volatile u8 *)0xC18289DCu == 0u &&
               *(volatile u8 *)0xC2028442u == 0u;
}

/* Project the simple-page bit onto the detailed-page bit in every mode nibble. */
IMG_FN static u32 custom_simple_mask(u32 value)
{
    return (value & ~0x111111u) | ((value >> 1u) & 0x111111u);
}

IMG_FN static void custom_apply(cm_t *c)
{
    custom_disp_state *s = (custom_disp_state *)c->custom_state;
    if (!s) return;
    u32 hide = custom_hide_native(c), changed = 0u;
    for (u32 group = 0; group < 64u; group++) {
        /* Match DISP1 native visibility on DISP0, for LCD/EVF only. */
        u32 descriptor = ((volatile const u32 *)0x10748504u)[group];
        if ((descriptor & 3u) || descriptor < RAM_LO || descriptor > RAM_HI - 48u) continue;
        for (u32 j = 0; j < 6u; j++) {
            u32 n = group * 6u + j, bit = 1u << (n & 31u);
            volatile u32 *word = (volatile u32 *)descriptor + 1u + (j / 2u) * 3u + (j & 1u) * 2u;
            if (hide) {
                u32 value = *word;
                u32 simple = custom_simple_mask(value);
                if (value != simple && __sync_bool_compare_and_swap(word, value, simple)) {
                    s->saved[n] = value; s->owned[n >> 5] |= bit; s->pending = changed = 1u;
                }
            } else if (s->owned[n >> 5] & bit) {
                if (__sync_bool_compare_and_swap(word, custom_simple_mask(s->saved[n]), s->saved[n])) s->pending = changed = 1u;
                s->owned[n >> 5] &= ~bit;
            }
        }
    }
    if (changed) {
        /* Drop old draw-call metadata, so portrait repairs learn only the new layout. */
        if (c->vui_ic) for (u32 i = 0; i < 64u; i++)
            ((u32 *)(c->vui_ic + i * 0x70u))[3] = 0u;
        c->vui_mtd_ok = c->vui_mtd_go = c->vui_bat_go = c->vui_strip_end = 0u;
        c->vui_bat_k = 0u;
    }
    if (s->pending && *(volatile u8 *)0xC18119A0u == 3u &&
        *(volatile u8 *)0xC18289DCu == 0u && !*(volatile u32 *)0xC18217C0u &&
        !FW(read_fn, 0x1042C00Cu)(12u) && !FW(read_fn, 0x1042DF92u)(0u)) {
        ((void (*)(void))0x103422AFu)();
        s->pending = 0u;
    }
}

IMG_FN static void custom_label(cm_t *c)
{
    custom_disp_state *s = (custom_disp_state *)c->custom_state;
    if (!s) return;
    if (c->hans) {
        FW(set_fn, UTF16_SET)(s->hz[0], 253u, HANS_STYLE);
        *(volatile u8 *)RUNTIME_STYLE = HANS_FONT;
    }
    else FW(set_fn, ASCII_SET)(s->en[0], 253u, 0u);
}

IMG_FN static u32 custom_entry_ok(u32 a0,u32 a1,u32 a2,u32 a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    cm_t *c = cm_get();
    if (!c || !c->custom_hdr || !c->custom_state) return 0;
    custom_disp_state *s = (custom_disp_state *)c->custom_state;
    sub_title(c, s->hz[0], s->en[0]);
    for (u32 row = 0; row < 4u; row++) {
        u32 n = 0, on = !!(c->custom_flags & (1u << row));
        if (c->hans) {
            u16 text[24];
            while (n < 15u && s->hz[row + 1u][n]) { text[n] = s->hz[row + 1u][n]; n++; }
            text[n++] = 0xFF1Au; text[n++] = on ? c->hz_on[0] : c->hz_off[0]; text[n] = 0;
            FW(set_fn, UTF16_SET)(text, ROW_FIRST_SLOT + row, HANS_STYLE);
            *(volatile u8 *)RUNTIME_STYLE = HANS_FONT;
        } else {
            char text[32]; const char *value = on ? c->en_on : c->en_off;
            while (n < 23u && s->en[row + 1u][n]) { text[n] = s->en[row + 1u][n]; n++; }
            text[n++] = ':'; text[n++] = ' ';
            for (u32 i = 0; i < 4u && value[i]; i++) text[n++] = value[i];
            text[n] = 0; FW(set_fn, ASCII_SET)(text, ROW_FIRST_SLOT + row, 0u);
        }
    }
    c->title_slot = SLOT_TITLE_V;
    ((volatile u32 *)PAGE0)[10] = c->custom_hdr;
    FW(page_fn, OPEN_PAGE)(2u, OWN_INDEX);
    return 0;
}
IMG_FN static u32 custom_toggle(u32 bit)
{
    cm_t *c = cm_get();
    if (c) { c->custom_flags ^= bit; ((custom_disp_state *)c->custom_state)->pending = 1u; custom_apply(c); save_cfg(c); custom_entry_ok(0,0,0,0); }
    return 0;
}
IMG_FN static u32 custom_enable_ok(u32 a,u32 b,u32 d,u32 e) { (void)a;(void)b;(void)d;(void)e;return custom_toggle(1u); }
IMG_FN static u32 custom_level_ok(u32 a,u32 b,u32 d,u32 e) { (void)a;(void)b;(void)d;(void)e;return custom_toggle(2u); }
IMG_FN static u32 custom_hist_ok(u32 a,u32 b,u32 d,u32 e) { (void)a;(void)b;(void)d;(void)e;return custom_toggle(4u); }
IMG_FN static u32 custom_info_ok(u32 a,u32 b,u32 d,u32 e) { (void)a;(void)b;(void)d;(void)e;return custom_toggle(8u); }
