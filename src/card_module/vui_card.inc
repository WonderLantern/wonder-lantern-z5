/* Portrait UI: the card-module side of research/vertical-ui/z5_vui_hooks.py.
 * Same rules as vui_map.py, which is the emulator prototype. Included inside main.c's heap image. */

#define VUI_K_IMAGE   0u
#define VUI_K_RECT    1u
#define VUI_K_CLEAR   2u
#define VUI_K_FRAME   3u
#define VUI_K_SETTER  4u
#define VUI_K_TEXT    5u
#define VUI_K_IMAGE2  6u
#define VUI_K_IMAGE3  7u               /* no rotation in this renderer: redrawn through image 1 */
#define VUI_K_KEY     8u
#define VUI_K_PATH1   9u               /* 0x104A6064 */
#define VUI_K_PATH2   10u              /* 0x104A65BE */
#define VUI_K_PATH3   11u              /* 0x104A7F8E: a polyline */

#define VUI_IMAGE1    0x104A673Cu
#define VUI_TARGET    0xC18085D4u      /* u8, current render target; 10 is the 960x544 one */
#define VUI_A_RIGHT   2u               /* action codes seen at 0x1031239E */
#define VUI_A_LEFT    3u
#define VUI_A_UP      4u
#define VUI_A_DOWN    5u
#define VUI_A_OK      0x1Bu
#define VUI_A_MENU    0x1Cu
#define VUI_HIGHLIGHT 0xFCD723FFu
#define VUI_TARGET_OSD 2u

#define VUI_ORI_GET   0x101EA71Eu      /* camera-verified: 0 level, 1 grip up, 2 grip down, 3 inverted */
#define VUI_APP       0xC18119A0u      /* u8, 3 = shooting application */
#define VUI_UI        0xC18289DCu      /* u8, 0 = live view, 3 = i menu */
#define VUI_CURSOR    0xC182D2B0u
#define VUI_REFRESH   0x10467D3Eu
#define VUI_KEY_SITE  0x10477895u      /* setter's return address for direction keys */
#define VUI_K_RIGHT   0x103CCFBBu
#define VUI_K_UP      0x103CCFD9u
#define VUI_K_LEFT    0x103CCE97u
#define VUI_K_DOWN    0x103CD055u

#define VUI_PY_GRID   440
#define VUI_CELL_W    120
#define VUI_CELL_H    72
#define VUI_GRID_TOP  281
#define VUI_PY_BAR    670
#define VUI_BAR_SHIFT 341
#define VUI_PY_TITLE  420
#define VUI_PAGE_GRID    3u
#define VUI_PAGE_SETTING 2u

typedef signed int s32_v;
typedef void (*vui_void_fn)(void);

/* IEEE single <-> 24.8 fixed point, for the glyph matrix only. */
IMG_FN static s32_v vui_f2fix(u32 b)
{
    s32_v e = (s32_v)((b >> 23) & 255u) - 127;
    u32 m = (b & 0x7FFFFFu) | 0x800000u;
    s32_v v;
    if (e < -8) return 0;
    v = e >= 15 ? (s32_v)(m << (e - 15)) : (s32_v)(m >> (15 - e));
    return (b >> 31) ? -v : v;
}

IMG_FN static u32 vui_fix2f(s32_v v)
{
    u32 s = v < 0 ? 0x80000000u : 0u;
    u32 a = v < 0 ? (u32)-v : (u32)v;
    s32_v top;
    if (!a) return 0u;
    top = 31 - __builtin_clz(a);
    a = top >= 23 ? a >> (top - 23) : a << (23 - top);
    return s | ((u32)(top - 8 + 127) << 23) | (a & 0x7FFFFFu);
}

/* Shooting page: landscape y-down box to portrait position. */
IMG_FN static u32 vui_locate_lv(s32_v x, s32_v y, s32_v w, s32_v h, s32_v *px, s32_v *py)
{
    {
        cm_t *z = cm_get();                    /* magnified view: the navigator, moved as one piece */
        if (z && z->vui_zoom && x >= 500 && y >= 360) { *px = x - 240; *py = y + 154; return 1u; }
    }
    if (x >= 50 && x < 120 && y < 50) { *px = x + 294; *py = y + 50; return 1u; } /* flash: top right, under the image-area icon */
    if (x >= 120 && (y + h <= 60 || (x < 540 && y < 50))) {
        /* Two rows. The zone follows the element's centre, so an icon, its outline and its clear move together. */
        s32_v c = x + w / 2;
        if (c >= 590 && c < 670) { *px = x - 322; *py = y + 44; return 1u; }   /* white balance */
        if (c < 200) { *px = x + 271; *py = y + 50; return 1u; }               /* flash icons (x = 127): with REAR, under the image-area icon */
        *px = x - (c >= 670 ? 238 : c >= 500 ? 138 : c >= 420 ? 125 : c >= 340 ? 105 : c >= 285 ? 119 : c >= 200 ? 124 : 79);
        *py = y;
        return 1u;
    }
    if (x >= 290 && x < 410 && y >= 372 && y < 425) { *px = x - 120; *py = y + 235; return 1u; }   /* OK: focus-point hint */
    if (x < 50 && y < 40) { *px = x + 4; *py = y; return 1u; }
    if (x >= 540 && y < 50) { *px = x - 572; *py = y + 8; return 1u; }           /* RAW+F* and size, under the mode */
    if (x >= 540 && y >= 50 && y < 110) { *px = x - 240; *py = y - 14; return 1u; }
    if (x >= 680 && y >= 225 && y < 440) { *px = x - 240; *py = y + 13; return 1u; }
    if (x >= 465 && x < 680 && y >= 270 && y < 375) { *px = x - 225; *py = y + 60; return 1u; }   /* histogram */
    if (x < 90 && y >= 90 && y < 120) { *px = x + 3; *py = y + 362; return 1u; }                  /* FLICKER, estimated */
    if (x < 90 && y >= 120 && y <= 185) { *px = x + 3; *py = y + 369; return 1u; }
    if (x >= 655 && y >= 120 && y <= 185) { *px = x - 243; *py = y + 366; return 1u; }
    if (x < 95 && y >= 186 && y < 232) { *px = x + 3; *py = y + 361; return 1u; }
    if (x < 45 && y >= 300 && y <= 355) { *px = x + 4; *py = y + 108; return 1u; }
    if (x < 80 && y >= 392 && y < 430) { *px = x + 380; *py = y + 191; return 1u; }
    if (x >= 110 && x < 180 && y >= 392 && y < 430) { *px = x + 10; *py = y + 242; return 1u; }
    if (x >= 620 && y >= 388 && y < 430) { *px = x - 240; *py = y + 154; return 1u; }
    if (x < 50 && y >= 430) { *px = x + 108; *py = y + 160; return 1u; }    /* metering, spread toward the middle */
    if (x >= 590 && y >= 425) { *px = x - 240; *py = y + 180; return 1u; }
    if (x >= 125 && x < 590 && y >= 425) { *px = x - 125; *py = y + 240; return 1u; }
    return 0u;
}

/* i menu: 6x2 landscape grid to 4x3 portrait, by slot number. */
IMG_FN static u32 vui_locate_menu(s32_v x, s32_v y, s32_v w, s32_v h, s32_v *px, s32_v *py)
{
    s32_v col = 0, row, index, v;
    if (y >= 430) { *px = x - VUI_BAR_SHIFT; *py = VUI_PY_BAR + y - 431; return 1u; }
    if (y >= 60 && y * 2 + h < 560) { *px = x - 120; *py = y + 150; return 1u; } /* dial strip above the grid */
    if (y * 2 + h < 560 || y * 2 + h >= 852) return 0u;
    if (w <= 2) {                              /* a vertical line is the right edge of its cell */
        for (v = x + 1; v >= VUI_CELL_W; v -= VUI_CELL_W) col++;
        col = x ? col - 1 : 0;
        if (col < 0) col = 0;
    } else {
        for (v = x * 2 + w; v >= 2 * VUI_CELL_W; v -= 2 * VUI_CELL_W) col++;
    }
    if (col > 5) col = 5;
    row = y * 2 + h < 2 * (VUI_GRID_TOP + VUI_CELL_H) ? 0 : 1;
    if (h <= 2) row = y <= VUI_GRID_TOP + VUI_CELL_H - 1 ? 0 : 1;
    index = row * 6 + col;
    *px = (index & 3) * VUI_CELL_W + x - col * VUI_CELL_W;
    *py = VUI_PY_GRID + (index >> 2) * VUI_CELL_H + y - (VUI_GRID_TOP + row * VUI_CELL_H);
    return 1u;
}

#define VUI_FIT(v) (((s32_v)(v) * 683) >> 10)   /* two thirds, without a division */

IMG_FN static u32 vui_is_fit(cm_t *c, s32_v y)
{
    return c && c->vui_fit && c->vui_bd_top && y >= 530 - (s32_v)c->vui_bd_top && y < 430;
}

IMG_FN static s32_v vui_bar_shift(cm_t *c)
{
    if (c && c->vui_bar_max > c->vui_bar_min) return (((s32_v)c->vui_bar_min + (s32_v)c->vui_bar_max) >> 1) - 240;
    return VUI_BAR_SHIFT;
}

/* How far a page wider than the portrait screen is shifted. Only the option rows move, not the title. */
IMG_FN static s32_v vui_slide(cm_t *c, s32_v y)
{
    s32_v s, lo, hi;
    if (!c || c->vui_fit || !c->vui_bd_top || y < 530 - (s32_v)c->vui_bd_top || c->vui_max <= c->vui_min) return 0;
    lo = (s32_v)c->vui_min - 120;
    hi = (s32_v)c->vui_max - 600;
    if (lo >= hi) return 0;                    /* fits the portrait screen: the page is already centred */
    s = c->vui_hl_c - 360;
    if (c->vui_bd_h < 200u) return s;          /* one strip: the selected value stays in the middle */
    return s < lo ? lo : s > hi ? hi : s;
}

IMG_FN static u32 vui_locate(u32 ui, s32_v x, s32_v y, s32_v w, s32_v h, s32_v *px, s32_v *py)
{
    if (ui == VUI_PAGE_SETTING) {              /* a setting page: centred and kept upright as a whole */
        if (y >= 430) { *px = x - vui_bar_shift(cm_get()); *py = VUI_PY_BAR + y - 431; }
        else if (vui_is_fit(cm_get(), y)) { *px = VUI_FIT(x); *py = y + 240; }
        else { *px = x - 120 - vui_slide(cm_get(), y); *py = y + 240; }
        return 1u;
    }
    return ui ? vui_locate_menu(x, y, w, h, px, py) : vui_locate_lv(x, y, w, h, px, py);
}

IMG_FN static void vui_panel_rect(u32 ori, s32_v px, s32_v py, s32_v w, s32_v h, s32_v *o)
{
    if (ori == 2u) { o[0] = py; o[1] = px; }
    else { o[0] = 720 - py - h; o[1] = 480 - px - w; }
    o[2] = h; o[3] = w;
}

/* Focus frames are red or green and belong to the picture, not to the layout. */
IMG_FN static u32 vui_is_focus_color(u32 rgba)
{
    u32 r = rgba >> 24, g = (rgba >> 16) & 255u, b = (rgba >> 8) & 255u;
    return (r >= 0xC0u && g < 0x80u && b < 0x60u) || (g >= 0xC0u && r < 0x40u && b < 0x40u);
}

IMG_FN static void vui_learn(cm_t *c, s32_v x, s32_v y, s32_v w, u32 color)
{
    if (!c || !c->vui_bd_top || y < 530 - (s32_v)c->vui_bd_top || y >= 430 || w > 200) return;
    if (color == VUI_HIGHLIGHT) {
        if (c->vui_hl_c != x + (w >> 1)) c->vui_band_dirty = 1u;
        c->vui_hl_c = x + (w >> 1);
        c->vui_hl_w = w;
    }
}

IMG_FN static void vui_extent(cm_t *c, s32_v x, s32_v y, s32_v w)
{
    if (!c || !c->vui_bd_top || y < 530 - (s32_v)c->vui_bd_top || y >= 430) return;
    if (!c->vui_max || x < (s32_v)c->vui_min) c->vui_min = (u32)x;
    if (x + w > (s32_v)c->vui_max) c->vui_max = (u32)(x + w);
}

IMG_FN static void vui_bar_learn(cm_t *c, s32_v x, s32_v w)
{
    if (!c) return;
    if (!c->vui_bar_max || x < (s32_v)c->vui_bar_min) { c->vui_bar_min = (u32)x; c->vui_bar_dirty = 1u; }
    if (x + w > (s32_v)c->vui_bar_max) { c->vui_bar_max = (u32)(x + w); c->vui_bar_dirty = 1u; }
}

/* A box given as x, y-up, w, h: rectangles, clears, rounded outlines. */
IMG_FN static void vui_rect(u32 ori, u32 ui, u32 kind, s32_v *b)
{
    s32_v x = b[0], yu = b[1], w = b[2], h = b[3], px, py;
    if (!ui && kind == VUI_K_RECT && vui_is_focus_color((u32)b[4])) return;
    if (!ui && w >= 600 && h >= 400) return;   /* full-screen clears and the black rectangle before playback stay full-screen */
    if (!ui && kind != VUI_K_CLEAR && x >= 500 && 480 - yu - h >= 360) {
        cm_t *c = cm_get();                    /* the white outline of the magnified-view navigator (camera photo) */
        if (c && ((w >= 150 && w <= 210 && (h <= 4 || (h >= 70 && h <= 110))) || (w <= 4 && h >= 70 && h <= 110))) c->vui_zoom = 1u;
    }
    if (ui == VUI_PAGE_GRID) {
        if (x == 0 && w == 720 && yu < 260) { vui_panel_rect(ori, 0, VUI_PY_GRID, 480, VUI_CELL_H * 3, b); return; }
        if (yu >= 200 && yu <= 260 && h <= 60 && w > 300) { vui_panel_rect(ori, 0, 384, 480, 44, b); return; }
    } else if (!ui && kind == VUI_K_CLEAR && x >= 90 && x < 150 && 480 - yu - h >= 42 && 480 - yu - h < 90) {
        b[2] = 0; b[3] = 0;                    /* a periodic clear with no element: would punch a hole */
        return;
    }
    if (ui == VUI_PAGE_SETTING) {
        cm_t *c = cm_get();
        s32_v y = 480 - yu - h;
        if (kind == VUI_K_RECT) vui_learn(c, x, y, w, (u32)b[4]);
        if (kind == VUI_K_FRAME && y >= 430 && w <= 300) vui_bar_learn(c, x, w);
        if (kind == VUI_K_CLEAR && c && y >= 430 && c->vui_bar_dirty) {   /* the bar moved: wipe its old position */
            c->vui_bar_dirty = 0u;
            vui_panel_rect(ori, 0, VUI_PY_BAR + y - 431, 480, h, b);
            return;
        }
        if (kind == VUI_K_CLEAR && c && !c->vui_fit && c->vui_band_dirty && c->vui_bd_top && y >= 530 - (s32_v)c->vui_bd_top && y < 430 && h >= 20) {
            c->vui_band_dirty = 0u;                /* the row slid: wipe the whole row, or the old cells stay */
            vui_panel_rect(ori, 0, y + 240, 480, h, b);
            return;
        }
    }
    if (vui_locate(ui, x, 480 - yu - h, w, h, &px, &py)) {
        if (ui == VUI_PAGE_SETTING && vui_is_fit(cm_get(), 480 - yu - h)) w = VUI_FIT(w);
        vui_panel_rect(ori, px, py, w, h, b);
    } else if (ui == VUI_PAGE_GRID && kind == VUI_K_CLEAR) {
        b[2] = 0; b[3] = 0;                    /* shooting-layer widgets must not punch holes in the menu */
    }
}

IMG_FN static void vui_image(u32 ori, u32 ui, u8 *d)
{
    s32_v x = *(signed short *)(d + 0x18), y = *(signed short *)(d + 0x1A);
    s32_v sw = *(u16 *)(d + 0x14), sh = *(u16 *)(d + 0x16), px, py;
    signed short *box = (signed short *)(d + 0x18);
    if (sw <= 0 || sh <= 0 || (ui == VUI_PAGE_GRID && sw == 10 && sh == 36)) return;
    if (!ui && vui_is_focus_color(*(u32 *)(d + 0x20))) return;
    if (ui == VUI_PAGE_SETTING) vui_extent(cm_get(), x, y, sw);
    if (!ui && x >= 120 && y + sh <= 60) {     /* the top row is back: the magnified view has ended */
        cm_t *c = cm_get();
        if (c) c->vui_zoom = 0u;
    }
    if (!vui_locate(ui, x, y, sw, sh, &px, &py)) return;
    if (ui == VUI_PAGE_SETTING && vui_is_fit(cm_get(), y)) {
        s32_v nh = VUI_FIT(sh);
        py += (sh - nh) >> 1;
        sw = VUI_FIT(sw);
        sh = nh;
    }
    *(u32 *)d = 0u;                            /* mode 1 ignores the box and mis-scales at 90/270 */
    if (ori == 2u) {
        box[0] = (signed short)py; box[1] = (signed short)(480 - px - sw);
        box[2] = (signed short)(py + sh); box[3] = (signed short)(480 - px);
        *(u16 *)(d + 0x40) = 90u;
    } else {
        box[0] = (signed short)(720 - py - sh); box[1] = (signed short)px;
        box[2] = (signed short)(720 - py); box[3] = (signed short)(px + sw);
        *(u16 *)(d + 0x40) = 270u;
    }
}

IMG_FN static void vui_text(u32 ori, u32 ui, u32 *m)
{
    u32 a = m[0], d = m[4];
    s32_v e = vui_f2fix(m[6]), f = vui_f2fix(m[7]), px, py;
    if (ui && f < (50 << 8)) {
        px = e - ((ui == VUI_PAGE_SETTING ? vui_bar_shift(cm_get()) : VUI_BAR_SHIFT) << 8);
        py = ((VUI_PY_BAR + 480 - 431) << 8) - f;
    }
    else if (ui == VUI_PAGE_GRID && f >= (205 << 8) && f < (262 << 8)) { px = e - (120 << 8); py = VUI_PY_TITLE << 8; }
    else {                                     /* values inside a cell or on the shooting page */
        s32_v gw = vui_f2fix(a) >> 8, gh = vui_f2fix(d) >> 8, bx, by;
        if (ui == VUI_PAGE_SETTING) vui_extent(cm_get(), e >> 8, 480 - (f >> 8) - gh, 0);
        if (!vui_locate(ui, e >> 8, 480 - (f >> 8) - gh, gw, gh, &bx, &by)) return;
        if (ui == VUI_PAGE_SETTING && vui_is_fit(cm_get(), 480 - (f >> 8) - gh)) {
            a = vui_fix2f(VUI_FIT(vui_f2fix(a)));
            d = vui_fix2f(VUI_FIT(vui_f2fix(d)));
        }
        px = (bx << 8) + (e & 255);
        py = (by + gh) << 8;
    }
    m[0] = 0u; m[2] = 0u; m[4] = 0u; m[5] = 0u; m[8] = 0x3F800000u;
    if (ori == 2u) {
        m[1] = a; m[3] = d ^ 0x80000000u; m[6] = vui_fix2f(py); m[7] = vui_fix2f(px);
    } else {
        m[1] = a ^ 0x80000000u; m[3] = d; m[6] = vui_fix2f((720 << 8) - py); m[7] = vui_fix2f((480 << 8) - px);
    }
}

/* Direction keys: follow the rotated screen, moving in 4x3 with wrap-around. */
IMG_FN static void vui_setter(u32 ori, u32 *r)
{
    const u32 *sp = r + 6;
    s32_v dx = 0, dy = 0;
    u32 slot, col, row, i;
    if (r[5] != VUI_KEY_SITE) return;
    for (i = 0; i < 24u; i++) {
        u32 v = sp[i];
        if (v == VUI_K_RIGHT) { if (ori == 2u) dy = 1; else dy = -1; break; }
        if (v == VUI_K_LEFT) { if (ori == 2u) dy = -1; else dy = 1; break; }
        if (v == VUI_K_UP) { if (ori == 2u) dx = 1; else dx = -1; break; }
        if (v == VUI_K_DOWN) { if (ori == 2u) dx = -1; else dx = 1; break; }
    }
    if (!dx && !dy) return;
    slot = *(volatile u32 *)VUI_CURSOR;
    if (slot > 11u) return;
    col = ((slot & 3u) + (u32)(dx + 4)) & 3u;
    row = (slot >> 2) + (u32)(dy + 3);
    while (row >= 3u) row -= 3u;
    r[0] = row * 4u + col;
}

/* Press or release the shutter half way through the firmware's switch API.
 * Nothing else found repaints every shooting widget: 0x10467D3E only wakes the GUI task. */
#define VUI_SW_SNAPSHOT 0x101ED79Eu
#define VUI_SW_SUBMIT   0x101ED678u
#define VUI_HALF_MASK   0x80000000u
#define VUI_HALF_MS     120u

IMG_FN static u32 vui_half_set(cm_t *c, u32 press)
{
    u32 s = FW(read_fn, VUI_SW_SNAPSHOT)(0u);
    u32 *p = c->vui_sw;
    u32 i;
    if (s < RAM_LO || s >= RAM_HI - 0x9Cu || (s & 3u)) return 0u;
    p[0] = *(volatile u32 *)(s + 0x04u);
    p[1] = *(volatile u32 *)(s + 0x18u);
    p[2] = *(volatile u32 *)(s + 0x2Cu);
    p[3] = *(volatile u32 *)(s + 0x40u);
    p[4] = 0u; p[5] = 0u; p[6] = 0u; p[7] = 0u;
    for (i = 0; i < 6u; i++) ((u16 *)(p + 8))[i] = *(volatile u16 *)(s + 0x64u + i * 8u);
    p[11] = *(volatile u32 *)(s + 0x94u);
    p[12] = *(volatile u32 *)(s + 0x98u);
    if (press) {
        if (p[0] & VUI_HALF_MASK) return 0u;   /* the user is already half-pressing */
        p[0] |= VUI_HALF_MASK;
    } else {
        p[0] &= ~VUI_HALF_MASK;
    }
    FW(read_fn, VUI_SW_SUBMIT)((u32)p);
    return 1u;
}

/* Redraw the shooting page after the camera has been turned. */
IMG_FN static void vui_repaint(cm_t *c)
{
    FW(vui_void_fn, VUI_REFRESH)();
    if (!c->vui_half && vui_half_set(c, 1u)) {
        c->vui_half = 1u;
        c->vui_half_ms = *(volatile u32 *)CM_MS_CLOCK;
    }
}

/* Renderer 3 has no angle. Its descriptor is rewritten as a renderer 1 descriptor; the firmware's own call is then skipped. */
IMG_FN static void vui_image3(u32 *r)
{
    const u8 *s = (const u8 *)r[0];
    u32 d[0x5Cu / 4u];
    u32 i;
    if (!s) return;
    for (i = 0; i < sizeof d / 4u; i++) d[i] = 0u;
    d[0] = *(const u32 *)s;                    /* mode */
    d[1] = *(const u32 *)(s + 4);              /* pixel format */
    d[2] = *(const u32 *)(s + 8);              /* texture */
    d[3] = *(const u32 *)(s + 12);
    d[4] = *(const u32 *)(s + 16);             /* source origin */
    d[5] = *(const u32 *)(s + 20);             /* source size */
    d[8] = 0xFFFFFFFFu;                        /* renderer 1 multiplies by this; renderer 3 has no tint */
    ((u16 *)d)[12] = (u16)(*(const s32_v *)(s + 0x18) >> 16);
    ((u16 *)d)[13] = (u16)(*(const s32_v *)(s + 0x1C) >> 16);
    ((u16 *)d)[14] = (u16)(*(const s32_v *)(s + 0x20) >> 16);
    ((u16 *)d)[15] = (u16)(*(const s32_v *)(s + 0x24) >> 16);
    d[0x24u / 4u] = *(const u32 *)(s + 0x2C);  /* flip */
    d[0x2Cu / 4u] = *(const u32 *)(s + 0x28);  /* blending */
    ((u8 *)d)[0x44] = s[0x30];
    ((u8 *)d)[0x45] = s[0x31];
    d[0x48u / 4u] = *(const u32 *)(s + 0x34);  /* filter */
    d[0x4Cu / 4u] = *(const u32 *)(s + 0x38);  /* stride */
    d[0x50u / 4u] = *(const u32 *)(s + 0x3C);  /* lines */
    FW(read_fn, VUI_IMAGE1)((u32)d);
    r[0] = 0u;
}

/* Direction keys on a setting page, turned with the screen. Same table as the 4x3 grid. */
IMG_FN static void vui_key(cm_t *c, u32 ori, u32 *r)
{
    u32 a = r[0];
    if (a == VUI_A_MENU) { c->vui_ok_open = 0u; return; }
    if (a == VUI_A_OK) { c->vui_ok_open = 1u; return; }
    if (!c->vui_sub || a < VUI_A_RIGHT || a > VUI_A_DOWN) return;
    /* One nibble per action, right first. Not a conditional chain: GCC turns that into a table in .rodata,
       which is not copied with the code image. */
    r[0] = ((ori == 2u ? 0x3245u : 0x2354u) >> ((a - VUI_A_RIGHT) * 4u)) & 15u;
}

#ifdef VUI_DIAG
IMG_FN static void vui_diag(cm_t *c, u32 kind, u32 *r, u32 app, u32 ui)
{
    u32 t = (app << 8) | ui, target = *(volatile u8 *)VUI_TARGET;
    s32_v x = -1, y = 0, w = 0, h = 0;
    if (app == 1u && ui == 0u) return;         /* the menu page showing this readout */
    if (kind >= VUI_K_PATH1) {                 /* which of these is drawing, and the longest polyline */
        if (ui == 0u) c->vui_d_hs[*(volatile u8 *)0xC2028442u & 3u] |= 1u << kind;
        if (kind == VUI_K_PATH3 && ui == 0u && *(volatile u8 *)0xC2028442u >= 2u) {
            const u32 *o = (const u32 *)r[0];
            if (o[1] > c->vui_d_i2 && o[0] >= RAM_LO && o[0] < RAM_HI - 8u) {
                c->vui_d_i2 = o[1];
                c->vui_d_i3 = *(const u32 *)o[0];
                c->vui_d_k = ((const u32 *)o[0])[1];
            }
        }
        return;
    }
    if (t != c->vui_d_tup[c->vui_d_n & 3u]) {
        c->vui_d_n++;
        c->vui_d_tup[c->vui_d_n & 3u] = t;
    }
    if (target < 32u) c->vui_d_t |= 1u << target;
    if (kind == VUI_K_IMAGE || kind == VUI_K_IMAGE2) {
        const u8 *d = (const u8 *)r[0];
        w = *(const u16 *)(d + 0x14); h = *(const u16 *)(d + 0x16);
        if (kind == VUI_K_IMAGE2 && w >= 700 && h >= 400) return;
        x = *(const short *)(d + 0x18); y = *(const short *)(d + 0x1A);
    } else if (kind == VUI_K_IMAGE3) {
        const u8 *d = (const u8 *)r[0];
        w = *(const u16 *)(d + 0x14); h = *(const u16 *)(d + 0x16);
        x = *(const s32_v *)(d + 0x18) >> 16; y = *(const s32_v *)(d + 0x1C) >> 16;
    } else if (kind == VUI_K_RECT || kind == VUI_K_CLEAR || kind == VUI_K_FRAME) {
        const s32_v *b = kind == VUI_K_FRAME ? (const s32_v *)r : (const s32_v *)r[0];
        x = b[0]; w = b[2]; h = b[3]; y = 480 - b[1] - h;
    } else if (kind == VUI_K_TEXT) {
        const u32 *m = (const u32 *)r[0];
        x = vui_f2fix(m[6]) >> 8; y = 480 - (vui_f2fix(m[7]) >> 8);
    }
    if (ui == 0u && x + (w >> 1) >= 465 && x + (w >> 1) < 680 && y + (h >> 1) >= 250 && y + (h >> 1) < 380)
        c->vui_d_hs[*(volatile u8 *)0xC2028442u & 3u] |= 1u << kind;   /* the histogram's area */
}
#endif

/* Own portrait histogram. The stock one is not drawn through OpenVG (camera readout, group 50), so it cannot be
   turned; it is hidden in portrait and redrawn here from the stock counts.
   0x1034B914: 256 u32 counts copied from 0xC202A55C to 0xC2028DE8, display peak stored at 0xC2028094 + 0x34. */
#define VUI_HIST_BINS  0xC2028DE8u
#define VUI_HIST_PEAK  0xC20280C8u
#define VUI_HIST_MASKS 0xC1826118u     /* 9 visibility masks of the stock histogram widget */
#define VUI_DISP_PAGE  0xC2028442u
#define VUI_RECT_FN    0x104A5EDCu
#define VUI_CLEAR_FN   0x104A641Eu
#define VUI_HX 236                     /* portrait box: 64 bars of 3 pixels */
#define VUI_HY 330
#define VUI_HX3 280                    /* on the level page: top right, clear of the level dial */
#define VUI_HY3 120
#define VUI_HW 192
#define VUI_HH 96

IMG_FN static u32 vui_udiv(u32 n, u32 d)       /* no libgcc in the module */
{
    u32 q = 0u, r = 0u;
    s32_v i;
    if (!d) return 0u;
    for (i = 31; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) { r -= d; q |= 1u << i; }
    }
    return q;
}

IMG_FN static void vui_prim(cm_t *c, u32 fn, u32 ori, s32_v px, s32_v py, s32_v w, s32_v h, u32 color)
{
    s32_v d[7];
    vui_panel_rect(ori, px, py, w, h, d);
    d[4] = (s32_v)color; d[5] = 0; d[6] = 0;
    c->vui_drawing = 1u;                       /* our own calls pass the hooks untouched */
    FW(read_fn, fn)((u32)d);
    c->vui_drawing = 0u;
}

IMG_FN static void vui_hist(cm_t *c, u32 kind, u32 ori, u32 active, u32 ui)
{
    volatile u32 *mask = (volatile u32 *)VUI_HIST_MASKS;
    u32 i, ms, peak, page = *(volatile u8 *)VUI_DISP_PAGE, want;
    s32_v hx, hy;
    if (active && !c->vui_hm_saved) {
        /* Bit 3 of the first mask is what the histogram-on-level-page gate (firmware, group 28 on) tests before
           sampling on DISP page 3. Keeping only that bit keeps the counts fresh there. */
        for (i = 0; i < 9u; i++) { c->vui_hm[i] = mask[i]; mask[i] = 0u; }
        if (c->vui_hm[0] & 8u) mask[0] = 0x08000000u;   /* bit 27: the gate only (firmware group 54) */
        c->vui_hm_saved = 1u;
    } else if (!active && c->vui_hm_saved) {
        for (i = 0; i < 9u; i++) mask[i] = c->vui_hm[i];
        c->vui_hm_saved = 0u;
    }
    if (kind != VUI_K_CLEAR) return;           /* drawn alongside the page's periodic clears */
#ifdef VUI_TEST_HIST
    want = active && ui == 0u && (page == 2u || page == 3u);
#else
    want = active && ui == 0u && (page == 2u || (page == 3u && c->vui_hm_saved && (c->vui_hm[0] & 8u)));
#endif
    hx = page == 3u ? VUI_HX3 : VUI_HX;
    hy = page == 3u ? VUI_HY3 : VUI_HY;
    if (c->vui_hist_on && (!want || hx != c->vui_hist_x || hy != c->vui_hist_y)) {
        vui_prim(c, VUI_CLEAR_FN, c->vui_hist_ori, c->vui_hist_x, c->vui_hist_y, VUI_HW, VUI_HH, 0u);
        c->vui_hist_on = 0u;
    }
    if (!want) return;
    ms = *(volatile u32 *)CM_MS_CLOCK;
    if (c->vui_hist_on && ms - c->vui_hist_ms < 80u) return;
    c->vui_hist_ms = ms;
    c->vui_hist_on = 1u;
    c->vui_hist_ori = ori;
    c->vui_hist_x = hx;
    c->vui_hist_y = hy;
#ifdef VUI_TEST_HIST
    peak = 128u;
#else
    peak = *(volatile u32 *)VUI_HIST_PEAK;
#endif
    vui_prim(c, VUI_CLEAR_FN, ori, hx, hy, VUI_HW, VUI_HH, 0u);
    vui_prim(c, VUI_RECT_FN, ori, hx, hy, VUI_HW, VUI_HH, 0x80u);
    if (!peak) return;
    for (i = 0; i < 64u; i++) {
        u32 b = i * 4u, sum = 0u, j, h;
        for (j = 0; j < 4u; j++) {
#ifdef VUI_TEST_HIST
            u32 k = b + j;
            sum += k < 128u ? k : 255u - k;    /* emulator only: a triangle */
#else
            sum += ((const volatile u32 *)VUI_HIST_BINS)[b + j];
#endif
        }
        h = vui_udiv(sum * VUI_HH, peak * 4u);
        if (h > VUI_HH) h = VUI_HH;
        if (h) vui_prim(c, VUI_RECT_FN, ori, hx + (s32_v)i * 3, hy + VUI_HH - (s32_v)h, 3, (s32_v)h, 0xE6E6E6FFu);
    }
}

/* Called from the firmware stubs: r = saved r0-r3, r12, lr, then the caller's stack. */
IMG_FN static void vui_hook(u32 kind, u32 *r)
{
    cm_t *c = cm_get();
    u32 ori, ui, app, active, now, sub, setting;
    if (!c || c->vui_drawing) return;
    if (c->vui_half && *(volatile u32 *)CM_MS_CLOCK - c->vui_half_ms >= VUI_HALF_MS) {
        c->vui_half = 0u;                      /* before the switch check: never leave it pressed */
        vui_half_set(c, 0u);
    }
    if (!c->vui) return;
    ori = FW(read_fn, VUI_ORI_GET)(0u);
    ui = *(volatile u8 *)VUI_UI;
    app = *(volatile u8 *)VUI_APP;
#ifdef VUI_DIAG
    if (kind != VUI_K_KEY && kind != VUI_K_SETTER) vui_diag(c, kind, r, app, ui);
#endif
    if (kind >= VUI_K_PATH1) return;           /* entries only observed so far */
    if (app == 3u && ui == 0u) c->vui_ok_open = 0u;
    /* White balance is ui 10 and Picture Control ui 1 (camera readout); opened from the i menu they are setting pages. */
    setting = app == 3u && (ui == 1u || ui == 10u) && c->vui_ok_open;
    /* The viewfinder has its own layout. Targets 15 and up were only drawn to in the viewfinder (camera readout). */
    {
        u32 target = *(volatile u8 *)VUI_TARGET, ms = *(volatile u32 *)CM_MS_CLOCK;
        if (target >= 15u && target < 32u) { c->vui_evf = 1u; c->vui_evf_ms = ms; }
        else if (c->vui_evf && ms - c->vui_evf_ms >= 3000u) c->vui_evf = 0u;
    }
    active = (ori == 1u || ori == 2u) && *(volatile u8 *)VUI_TARGET != 10u && !c->vui_evf &&
             ((app == 3u && (ui == 0u || ui == 3u)) || setting);
    if (kind == VUI_K_KEY) {
        if (app == 3u && active) vui_key(c, ori, r);
        return;
    }
    /* Only the on-screen layer. Anything else is an off-screen buffer that is later pasted as one image;
       turning its contents as well left white bars and noise in the dial strip's value row. */
    if (kind != VUI_K_SETTER && *(volatile u8 *)VUI_TARGET != VUI_TARGET_OSD) return;
    if (kind != VUI_K_SETTER) vui_hist(c, kind, ori, active && app == 3u, ui);
    if (ui != 3u && !setting) c->vui_sub = 0u;
    else if (!setting && (kind == VUI_K_RECT || kind == VUI_K_CLEAR)) {
        /* The grid's backdrop is 0,54,720,146; setting pages start with a different one. */
        const s32_v *b = (const s32_v *)r[0];
        /* The grid's own backdrop is the only thing that ends a setting page: HDR has lines like the grid's. */
        if (b[0] == 0 && b[1] == 54 && b[2] == 720 && b[3] == 146) c->vui_sub = 0u;
        else if ((b[0] == 0 && b[2] == 720 && b[3] >= 100 && b[3] < 480) ||
                 (b[0] >= 40 && b[0] + b[2] <= 680 && b[2] >= 400 && b[3] >= 100 && b[1] < 200)) {
            if (!c->vui_sub || (kind == VUI_K_RECT && (u32)b[4] == 0x80u && (c->vui_bd_top != (u32)(b[1] + b[3]) || c->vui_bd_h != (u32)b[3]))) {
                c->vui_min = 0u; c->vui_max = 0u; c->vui_hl_c = 360; c->vui_hl_w = 0; c->vui_idx = -1;
                c->vui_bar_min = 0u; c->vui_bar_max = 0u;
                c->vui_bd_top = (u32)(b[1] + b[3]);
                c->vui_bd_h = (u32)b[3];
            }
            c->vui_sub = 1u;
        }
    }
    if (setting && (kind == VUI_K_RECT || kind == VUI_K_CLEAR)) {
        /* In the menu application the page has no grid to come back to. */
        const s32_v *b = (const s32_v *)r[0];
        if (b[2] >= 400 && b[3] >= 60 && b[3] < 480 && !c->vui_sub) {
            c->vui_min = 0u; c->vui_max = 0u; c->vui_hl_c = 360; c->vui_hl_w = 0; c->vui_idx = -1;
            c->vui_bar_min = 0u; c->vui_bar_max = 0u;
            c->vui_bd_top = (u32)(b[1] + b[3]);
            c->vui_bd_h = (u32)b[3];
            c->vui_sub = 1u;
        }
    }
    sub = c->vui_sub || setting;
#ifdef VUI_TEST_FIT
    c->vui_fit = sub;
#else
    c->vui_fit = setting;
#endif
    now = active ? 0x10000u | (ori << 8) | ui | (sub << 4) | (c->vui_zoom << 20) : 0u;
    if (now != c->vui_state) {
        u32 turned = (now ^ c->vui_state) & 0x1FF00u;  /* orientation or portrait on/off, not page changes */
        c->vui_state = now;
        c->vui_pending = 1u;
        if (turned && ui == 0u && *(volatile u8 *)VUI_APP == 3u) vui_repaint(c);
    }
    if (app != 3u) c->vui_pending = 0u;
    if (kind == VUI_K_CLEAR && c->vui_pending) {
        s32_v *b = (s32_v *)r[0];
        c->vui_pending = 0u;                   /* drop everything drawn for the previous layout */
        b[0] = 0; b[1] = 0; b[2] = 720; b[3] = 480;
        return;
    }
#ifdef VUI_TEST_IMG3
    /* Emulator only: send small icons through renderer 3, whose descriptor format is otherwise unexercised there. */
    if (kind == VUI_K_IMAGE && !c->vui_test && *(u16 *)(r[0] + 0x14u) < 160u) {
        const u8 *s = (const u8 *)r[0];
        u32 d[0x48u / 4u];
        u32 i;
        for (i = 0; i < sizeof d / 4u; i++) d[i] = 0u;
        for (i = 0; i < 6u; i++) d[i] = ((const u32 *)s)[i];
        d[6] = (u32)*(const short *)(s + 0x18) << 16; d[7] = (u32)*(const short *)(s + 0x1A) << 16;
        d[8] = (u32)*(const short *)(s + 0x1C) << 16; d[9] = (u32)*(const short *)(s + 0x1E) << 16;
        d[10] = *(const u32 *)(s + 0x2C); d[11] = *(const u32 *)(s + 0x24);
        ((u8 *)d)[0x30] = s[0x44]; ((u8 *)d)[0x31] = s[0x45];
        d[13] = *(const u32 *)(s + 0x48); d[14] = *(const u32 *)(s + 0x4C); d[15] = *(const u32 *)(s + 0x50);
        c->vui_test = 1u;
        FW(read_fn, 0x104A7AC2u)((u32)d);
        c->vui_test = 0u;
        r[0] = 0u;
        return;
    }
#endif
    if (!active) return;
    if (sub) ui = VUI_PAGE_SETTING;
    switch (kind) {
    case VUI_K_IMAGE:  vui_image(ori, ui, (u8 *)r[0]); break;
    case VUI_K_IMAGE2:                         /* full-layer copies must stay as they are */
        if (*(u16 *)(r[0] + 0x14u) < 700u || *(u16 *)(r[0] + 0x16u) < 400u) vui_image(ori, ui, (u8 *)r[0]);
        break;
    case VUI_K_IMAGE3: vui_image3(r); break;
    case VUI_K_RECT:
    case VUI_K_CLEAR:  vui_rect(ori, ui, kind, (s32_v *)r[0]); break;
    case VUI_K_FRAME:  vui_rect(ori, ui, kind, (s32_v *)r); break;
    case VUI_K_SETTER:
        if (ui == VUI_PAGE_GRID) vui_setter(ori, r);
        else if (ui == VUI_PAGE_SETTING) {
            s32_v d = (s32_v)r[0] - c->vui_idx;
            if (c->vui_idx >= 0 && (d == 1 || d == -1) && c->vui_hl_w) {
                s32_v n = c->vui_hl_c + d * (c->vui_hl_w + 6), half = c->vui_hl_w >> 1;
                if (n >= (s32_v)c->vui_min + half && n <= (s32_v)c->vui_max - half) { c->vui_hl_c = n; c->vui_band_dirty = 1u; }
            }
            c->vui_idx = (s32_v)r[0];
        }
        break;
    case VUI_K_TEXT:   vui_text(ori, ui, (u32 *)r[0]); break;
    default: break;
    }
}
