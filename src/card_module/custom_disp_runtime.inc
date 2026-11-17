/* Page menus and native visibility, owned by the retained cm image. */
typedef struct {
    u16 hz[12][16]; char en[12][24];
    u32 page_hdr, level_hdr, editing, hist_page;
    u32 saved[64u * 6u], applied[64u * 6u], owned[12], pending, flags_cache, flags_valid;
    u32 cfg_ms, cfg_busy;
    u32 key_entry, route_owned, route_applied[2], route_pending, route_save;
} custom_disp_state;

IMG_FN static void custom_route_sync(cm_t *c);
IMG_FN static u32 custom_output(void)
{ return *(volatile u32 *)0xC181AD60u == 4u; }
IMG_FN static u32 custom_logical_page(cm_t *c)
{ return CUSTOM_LOGICAL(c->custom_control, custom_output()); }
IMG_FN static u32 custom_current_flags(cm_t *c)
{ return CUSTOM_ACTIVE_FLAGS(c->custom_flags, c->custom_control, custom_output()); }

IMG_FN static u32 custom_hide_native(cm_t *c)
{
    u32 flags = custom_current_flags(c);
    return (flags & 1u) && !(flags & 8u) &&
           *(volatile u8 *)0xC18119A0u == 3u && *(volatile u8 *)0xC18289DCu == 0u;
}

/* Read every source from the unmodified mask, so editing DISP0/DISP1 cannot
 * turn another page's baseline into its own already-projected result. */
IMG_FN static u32 custom_project_mask(u32 value, u32 flags, u32 group)
{
    u32 result = value;
    if (flags & 1u) {
        u32 source = (flags & 8u) ? 0u : 1u, bits = 0x111111u;
        u32 baseline = (value >> source) & 0x111111u;
        /* Group 32 is the native histogram; enabled pages use our worker and
         * CPU raster instead, regardless of the detailed/simple base. */
        if (group == 32u) baseline = 0u;
        /* White-balance group 38 is also permitted on stock simple DISP1.
         * Treat it as detailed information for our custom pages. */
        if (group == 38u && !(flags & 8u)) baseline = 0u;
        result = (result & ~bits) | baseline;
    }
    return result;
}

IMG_FN static void custom_apply(cm_t *c)
{
    custom_disp_state *s = (custom_disp_state *)c->custom_state;
    if (!s) return;
    custom_route_sync(c);
    u32 photo = *(volatile u8 *)0xC18119A0u == 3u && *(volatile u8 *)0xC18289DCu == 0u;
    u32 flags = photo ? custom_current_flags(c) : 0u, changed = 0u, retry = 0u;
    u32 same = s->flags_valid && s->flags_cache == flags;
    for (u32 group = 0; group < 64u; group++) {
        u32 descriptor = ((volatile const u32 *)0x10748504u)[group];
        if ((descriptor & 3u) || descriptor < RAM_LO || descriptor > RAM_HI - 48u) continue;
        for (u32 j = 0; j < 6u; j++) {
            u32 n = group * 6u + j, bit = 1u << (n & 31u);
            u32 index = 1u + (j / 2u) * 3u + (j & 1u) * 2u;
            volatile u32 *word = (volatile u32 *)descriptor + index;
            /* Portrait stock pages temporarily hide these nine masks. Work
             * on their saved logical masks until that older path restores. */
            if (group == 32u && c->vui_hm_saved) word = c->vui_hm + index - 1u;
            u32 value = *word, base = value;
            if (same && value == s->applied[n]) continue;
            if (s->owned[n >> 5] & bit) {
                u32 owned = s->saved[n] ^ s->applied[n];
                u32 restore = owned & ~(value ^ s->applied[n]);
                base = (value & ~restore) | (s->saved[n] & restore);
            }
            u32 projected = custom_project_mask(base, flags, group);
            if (value != projected && !__sync_bool_compare_and_swap(word, value, projected)) {
                retry = 1u;
                continue;
            }
            s->saved[n] = base; s->applied[n] = projected;
            if (base != projected) s->owned[n >> 5] |= bit;
            else s->owned[n >> 5] &= ~bit;
            if (value != projected) s->pending = changed = 1u;
        }
    }
    s->flags_cache = flags; s->flags_valid = !retry;
    if (photo && (custom_current_flags(c) & 1u) && c->vui_hm_saved) {
        volatile u32 *mask = (volatile u32 *)0xC1826118u;
        for (u32 i = 0u; i < 9u; i++) {
            u32 old = !i && (c->vui_hm[0] & 8u) ? 0x08000000u : 0u;
            if (__sync_bool_compare_and_swap(mask + i, old, c->vui_hm[i]) && old != c->vui_hm[i])
                s->pending = changed = 1u;
        }
        c->vui_hm_saved = 0u;
    }
    if (changed) {
        if (c->vui_ic) for (u32 i = 0; i < 64u; i++)
            ((u32 *)(c->vui_ic + i * 0x70u))[3] = 0u;
        c->vui_mtd_ok = c->vui_mtd_go = c->vui_bat_go = c->vui_strip_end = 0u;
        c->vui_bat_k = 0u;
    }
    if (s->pending && photo && !*(volatile u32 *)0xC18217C0u &&
        !FW(read_fn, 0x1042C00Cu)(12u) && !FW(read_fn, 0x1042DF92u)(0u)) {
        ((void (*)(void))0x103422AFu)();
        s->pending = 0u;
    }
    if (s->route_save && !c->cfg_state) {
        s->route_save = 0u;
        save_cfg(c);
    }
}

#include "custom_disp/route.inc"

IMG_FN static void custom_label(cm_t *c)
{
    custom_disp_state *s = (custom_disp_state *)c->custom_state;
    if (!s) return;
    if (c->hans) {
        FW(set_fn, UTF16_SET)(s->hz[0], 253u, HANS_STYLE);
        *(volatile u8 *)RUNTIME_STYLE = HANS_FONT;
    } else FW(set_fn, ASCII_SET)(s->en[0], 253u, 0u);
}

IMG_FN static void custom_choice_label(cm_t *c, custom_disp_state *s, u32 row,
    u32 label, u32 choice, u32 on)
{
    u32 n = 0u;
    if (c->hans) {
        u16 text[24];
        while (n < 15u && s->hz[label][n]) { text[n] = s->hz[label][n]; n++; }
        text[n++] = 0xFF1Au;
        if (choice) for (u32 i = 0u; i < 7u && s->hz[choice][i]; i++) text[n++] = s->hz[choice][i];
        else text[n++] = on ? c->hz_on[0] : c->hz_off[0];
        text[n] = 0u;
        FW(set_fn, UTF16_SET)(text, CUSTOM_MENU_FIRST_SLOT + row, HANS_STYLE);
        *(volatile u8 *)RUNTIME_STYLE = HANS_FONT;
    } else {
        char text[32]; const char *value = choice ? s->en[choice] : (on ? c->en_on : c->en_off);
        while (n < 22u && s->en[label][n]) { text[n] = s->en[label][n]; n++; }
        text[n++] = ':'; text[n++] = ' ';
        for (u32 i = 0u; i < 7u && value[i]; i++) text[n++] = value[i];
        text[n] = 0; FW(set_fn, ASCII_SET)(text, CUSTOM_MENU_FIRST_SLOT + row, 0u);
    }
}

/* One master switch, followed by the four logical DISP configurations. */
IMG_FN static u32 custom_entry_ok(u32 a0,u32 a1,u32 a2,u32 a3)
{
    (void)a0;(void)a1;(void)a2;(void)a3;
    cm_t *c = cm_get();
    if (!c || !c->custom_hdr || !c->custom_state) return 0u;
    custom_disp_state *s = (custom_disp_state *)c->custom_state;
    sub_title(c, s->hz[0], s->en[0]);
    custom_choice_label(c, s, 0u, 1u, 0u, !!(c->custom_control & CUSTOM_CONTROL_ON));
    for (u32 page = 0u; page < CUSTOM_PAGE_COUNT; page++) {
        if (c->hans) {
            FW(set_fn, UTF16_SET)(s->hz[8u + page], CUSTOM_MENU_FIRST_SLOT + page + 1u, HANS_STYLE);
            *(volatile u8 *)RUNTIME_STYLE = HANS_FONT;
        } else FW(set_fn, ASCII_SET)(s->en[8u + page], CUSTOM_MENU_FIRST_SLOT + page + 1u, 0u);
    }
    c->title_slot = SLOT_TITLE_V;
    ((volatile u32 *)PAGE0)[10] = c->custom_hdr;
    FW(page_fn, OPEN_PAGE)(2u, OWN_INDEX);
    return 0u;
}

IMG_FN static u32 custom_options_ok(u32 a0,u32 a1,u32 a2,u32 a3)
{
    (void)a0;(void)a1;(void)a2;(void)a3;
    cm_t *c = cm_get();
    if (!c || !c->custom_state) return 0u;
    custom_disp_state *s = (custom_disp_state *)c->custom_state;
    if (!s->page_hdr || s->editing >= CUSTOM_PAGE_COUNT) return 0u;
    sub_title(c, s->hz[8u + s->editing], s->en[8u + s->editing]);
    u32 flags = CUSTOM_PAGE_FLAGS(c->custom_flags, s->editing);
    for (u32 row = 0u; row < 3u; row++) {
        u32 on = !!(flags & (1u << (row + 1u)));
        u32 choice = row == 0u ? ((flags & 16u) ? 6u : (on ? 7u : 5u)) : 0u;
        custom_choice_label(c, s, row, row + 2u, choice, on);
    }
    c->title_slot = SLOT_TITLE_V;
    ((volatile u32 *)PAGE0)[10] = s->page_hdr;
    FW(page_fn, OPEN_PAGE)(2u, OWN_INDEX);
    return 0u;
}

IMG_FN static u32 custom_page_open(u32 page)
{
    cm_t *c = cm_get();
    if (!c || !c->custom_state || page >= CUSTOM_PAGE_COUNT) return 0u;
    ((custom_disp_state *)c->custom_state)->editing = page;
    return custom_options_ok(0,0,0,0);
}
IMG_FN static u32 custom_page0_ok(u32 a,u32 b,u32 d,u32 e) { (void)a;(void)b;(void)d;(void)e;return custom_page_open(0u); }
IMG_FN static u32 custom_page1_ok(u32 a,u32 b,u32 d,u32 e) { (void)a;(void)b;(void)d;(void)e;return custom_page_open(1u); }
IMG_FN static u32 custom_page2_ok(u32 a,u32 b,u32 d,u32 e) { (void)a;(void)b;(void)d;(void)e;return custom_page_open(2u); }
IMG_FN static u32 custom_page3_ok(u32 a,u32 b,u32 d,u32 e) { (void)a;(void)b;(void)d;(void)e;return custom_page_open(3u); }

IMG_FN static u32 custom_toggle(u32 bit)
{
    cm_t *c = cm_get();
    if (!c || !c->custom_state || !cfg_ready(c)) return 0u;
    custom_disp_state *s = (custom_disp_state *)c->custom_state;
    if (s->editing >= CUSTOM_PAGE_COUNT) return 0u;
    c->custom_flags ^= bit << (s->editing * 5u);
    s->pending = 1u; custom_apply(c); save_cfg(c);
    return custom_options_ok(0,0,0,0);
}
IMG_FN static u32 custom_enable_ok(u32 a,u32 b,u32 d,u32 e) { (void)a;(void)b;(void)d;(void)e;cm_t *c = cm_get();
    if (c && cfg_ready(c)) {
        c->custom_control ^= CUSTOM_CONTROL_ON;
        custom_apply(c); save_cfg(c);
    }
    return custom_entry_ok(0,0,0,0); }
IMG_FN static u32 custom_level_ok(u32 a,u32 b,u32 d,u32 e)
{
    (void)a;(void)b;(void)d;(void)e;
    cm_t *c = cm_get();
    if (!c || !c->custom_state) return 0u;
    custom_disp_state *s = (custom_disp_state *)c->custom_state;
    if (!s->level_hdr) return 0u;
    sub_title(c, s->hz[2], s->en[2]);
    for (u32 row = 0u; row < 3u; row++) {
        if (c->hans) {
            FW(set_fn, UTF16_SET)(s->hz[5u + row], CUSTOM_MENU_FIRST_SLOT + row, HANS_STYLE);
            *(volatile u8 *)RUNTIME_STYLE = HANS_FONT;
        } else FW(set_fn, ASCII_SET)(s->en[5u + row], CUSTOM_MENU_FIRST_SLOT + row, 0u);
    }
    c->title_slot = SLOT_TITLE_V;
    ((volatile u32 *)PAGE0)[10] = s->level_hdr;
    FW(page_fn, OPEN_PAGE)(2u, OWN_INDEX);
    return 0u;
}
IMG_FN static u32 custom_level_choose(u32 mode)
{
    cm_t *c = cm_get();
    if (!c || !c->custom_state || !cfg_ready(c)) return 0u;
    custom_disp_state *s = (custom_disp_state *)c->custom_state;
    if (s->editing >= CUSTOM_PAGE_COUNT) return 0u;
    u32 shift = s->editing * 5u;
    c->custom_flags = (c->custom_flags & ~(18u << shift)) | (mode << shift);
    s->pending = 1u; custom_apply(c); save_cfg(c);
    return custom_options_ok(0,0,0,0);
}
IMG_FN static u32 custom_level_off(u32 a,u32 b,u32 d,u32 e) { (void)a;(void)b;(void)d;(void)e;return custom_level_choose(0u); }
IMG_FN static u32 custom_level_native(u32 a,u32 b,u32 d,u32 e) { (void)a;(void)b;(void)d;(void)e;return custom_level_choose(16u); }
IMG_FN static u32 custom_level_typea(u32 a,u32 b,u32 d,u32 e) { (void)a;(void)b;(void)d;(void)e;return custom_level_choose(2u); }
IMG_FN static u32 custom_hist_ok(u32 a,u32 b,u32 d,u32 e) { (void)a;(void)b;(void)d;(void)e;return custom_toggle(4u); }
IMG_FN static u32 custom_info_ok(u32 a,u32 b,u32 d,u32 e) { (void)a;(void)b;(void)d;(void)e;return custom_toggle(8u); }
