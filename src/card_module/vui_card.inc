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
#ifndef VUI_NO_G97SLOT
#define VUI_NO_G97SLOT 0
#endif
#define VUI_K_TOUCH   13u              /* 0x102BFAFC: stock touch handler, r0 = event (group 92 firmware) */
#define VUI_K_TOUCH2  14u              /* 0x1049BD44: the other 0x4F receiver, group 105 firmware */
#define VUI_K_TOUCH3  15u              /* 0x103B4F4E: shooting application, screen id < 15, group 108 firmware */
#define VUI_K_TOUCH4  16u              /* 0x1036C7E8: shooting application, screen id >= 15, group 108 firmware */
#define VUI_IS_TOUCH(k) ((k) == VUI_K_TOUCH || ((k) >= VUI_K_TOUCH2 && (k) <= VUI_K_TOUCH4))
#define VUI_K_FILL    12u              /* 0x104A5E66: filled rectangle; flag clear = direct OpenVG fill (AF frame) */

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
#define VUI_AF_WINDOW 0                    /* groups 58-64: extra corner clears after an AF-area change (off) */
#define VUI_HINT_DY 244                    /* the hint row: right above the aperture box (group 99, user; was 190) */
#define VUI_HINT_PX1 410                   /* hint row right edge: clear of the right column (group 99) */

/* Group 100: the quick-adjust strip block (ui_mode 9), native landscape coordinates, top-down. */
IMG_FN static u32 vui_strip_hit(const cm_t *c, s32_v x, s32_v y, s32_v w, s32_v h)
{
    return c && ((c->vui_strip && x >= 66 && x + w <= 680 && y >= 88 && y + h <= 280) ||
                 (c->vui_adj && x >= 88 && x + w <= 634 && y >= 352 && y + h <= 434));   /* group 111: adjust page row */
}

IMG_FN static u32 vui_locate_lv(s32_v x, s32_v y, s32_v w, s32_v h, s32_v *px, s32_v *py)
{
    {
        cm_t *s = cm_get();                    /* group 100: the strip, 7/10 around its centre, clear of the exposure scale */
        if (vui_strip_hit(s, x, y, w, h)) {
            if (s->vui_adj) { *px = 240 + ((x - 360) * 7) / 10; *py = 552 + ((y - 355) * 7) / 10; return 1u; }   /* group 111 */
            *px = 224 + ((x - 360) * 7) / 10; *py = 270 + ((y - 90) * 7) / 10; return 1u;
        }
        if (s && s->vui_adj && x >= 630 && y < 60) { *px = x - 237; *py = y + 115; return 1u; }   /* group 111: back button */
    }
    {
        cm_t *z = cm_get();                    /* magnified view: the navigator, moved as one piece */
        if (z && z->vui_zoom && x >= 500 && y >= 360) { *px = x - 240; *py = y + 154; return 1u; }
    }
    if (x >= 280 && x < 440 && y >= 40 && y < 84) { *px = x - 226; *py = y + 42; return 1u; }   /* low-light AF text and its clear: third row, left (group 98) */
    if (x >= 50 && x < 120 && y < 50) { *px = x + 57; *py = y + 40; return 1u; } /* flash mode: second row, under AF-S (user) */
    if (x >= 120 && (y + h <= 60 || (x < 540 && y < 50))) {
        /* Two rows. The zone follows the element's centre, so an icon, its outline and its clear move together. */
        s32_v c = x + w / 2;
        if (c >= 590 && c < 670) { *px = x - 323; *py = y; return 1u; }        /* white balance: top row, after the area (group 73) */
        if (c >= 670) { *px = x - 291; *py = y + 82; return 1u; }              /* FX: third row, left of the bracket line */
        if (c < 200) { *px = x + 108; *py = y + 40; return 1u; }               /* REAR: second row, right of the Fn-press clears (px 188-228) */
        *px = x - (c >= 500 ? 98 : c >= 420 ? 76 : c >= 340 ? 134 : c >= 285 ? 119 : c >= 200 ? 121 : 79);   /* group 97: 121, the slot at 198 clear of the release-mode icon */
        *py = y;
        return 1u;
    }
    if (x >= 190 && x < 520 && y >= 368 && y < 425) { *px = x - 130; *py = y + VUI_HINT_DY; return 1u; }   /* px <= 393: clear of the i button */   /* hint row: "OK:" for the focus point, "OK 确定 / zoom-out 退出" for subject tracking (camera photo) */
    if (x < 50 && y < 40) { *px = x + 4; *py = y; return 1u; }
    if (x >= 540 && y < 50) { *px = x - 247; *py = y - 3; return 1u; }           /* RAW+F* and size: second row, above the bracket line */
    if (x >= 540 && y >= 50 && y < 110) { *px = x - 240; *py = y - 14; return 1u; }
    if (x >= 680 && y >= 225 && y < 440) { *px = x - 240; *py = y + 13; return 1u; }
    if (x >= 465 && x < 680 && y >= 270 && y < 375) { *px = x - 225; *py = y + 60; return 1u; }   /* histogram */
    if (x < 90 && y >= 84 && y < 120) { *px = x + 3; *py = y + 275; return 1u; }                  /* top of the column; group 117: from y 84, the live-view-preview-off icon 2,86 (capture 46) */
    if (x < 90 && y >= 120 && y <= 185) { *px = x + 3; *py = y + 367; return 1u; }                /* touch shutter */
    if (x >= 655 && y >= 120 && y <= 185) { *px = x - 243; *py = y + 366; return 1u; }
    if (x >= 40 && x < 95 && y >= 186 && y < 232) { *px = x + 42; *py = y + 359; return 1u; }   /* group 103: focal text and its clear, inside the AF frame lines */
    if (x < 95 && y >= 186 && y < 232) { *px = x - 1; *py = y + 359; return 1u; }   /* VR/SPT: above metering */
    if (x < 45 && y >= 300 && y < 335) { *px = x + 4; *py = y + 105; return 1u; }    /* release-mode/Fn icon: py ~430 */
    if (x < 90 && y >= 335 && y < 392) { *px = x; *py = 465; return 1u; }            /* FLICKER: its reported y varies; fixed row, clear of the touch button */
    if (x < 80 && y >= 392 && y < 430 && w >= 20) { *px = x + 416; *py = y + 60; return 1u; }   /* battery (x 2, 60 wide): against the right edge (group 87) */
    if (x >= 80 && x < 127 && y >= 392 && y < 430) { *px = x - 26; *py = y + 242; return 1u; }   /* MF focus indicator: before the distance bar's end icon (group 104) */
    if (x >= 127 && x < 180 && y >= 392 && y < 430) { *px = x + 10; *py = y + 242; return 1u; }
    if (x >= 620 && y >= 388 && y < 430) { *px = x - 346; *py = y - 360; return 1u; }   /* Bluetooth: second row, after REAR */
    if (x < 50 && y >= 430) { *px = x + 8; *py = y + 160; return 1u; }      /* metering: left column under VR (Z50II); readout names its eraser */
    if (x >= 695 && y >= 425) { *px = x - 259; *py = y + 240; return 1u; }   /* flash ready (yellow): bottom row, right of ISO (user) */
    if (x >= 677 && y >= 425) { *px = x - 227; *py = y + 126; return 1u; }   /* "k": into the closing bracket's place, "[ 1.7k]" (under i, group 99) */
    if (x >= 666 && y >= 425) { *px = x - 201; *py = y + 126; return 1u; }   /* closing bracket (x 666-676): after the "k", px 465-475 under i */
    if (x >= 590 && y >= 425) { *px = x - 215; *py = y + 126; return 1u; }   /* shots remaining: "[" and the digits, under the i button (group 99) */
    if (x >= 400 && x < 420 && y >= 438) { *px = x - 151; *py = y + 240; return 1u; }   /* EV icon (406,442): before ISO (group 96) */
    if (x >= 420 && x < 590 && y >= 425) { *px = x - 145; *py = y + 240; return 1u; }   /* ISO: bottom row, a little left (user) */
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
#define VUI_DET_PY 400                         /* group 116: third-level page, 480 x 320 at the bottom */
IMG_FN static u32 vui_det_on(void)
{
    cm_t *z = cm_get();
    return z && z->vui_det;
}

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
        cm_t *z = cm_get();
        if (z && z->vui_det) { *px = VUI_FIT(x); *py = VUI_FIT(y) + VUI_DET_PY; return 1u; }   /* group 116 */
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
    return (r >= 0xC0u && g < 0x80u && b < 0x60u) || (g >= 0xC0u && r < 0x40u && b < 0x40u) ||
           (r >= 0xC0u && g < 0xA0u && b < 0x60u && r - g >= 0x40u);
}

/* Focus frames stay where the firmware draws them. Remember them, so a clear that erases one also runs there. */
IMG_FN static void vui_focus_note(cm_t *c, s32_v x, s32_v y, s32_v w, s32_v h)
{
    u32 i;
    if (!c || w <= 0 || h <= 0) return;
    for (i = 0; i < 32u; i += 2u)
        if (c->vui_fbox[i] == (((u32)x & 0xFFFFu) | ((u32)y << 16)) && c->vui_fbox[i + 1] == (((u32)w & 0xFFFFu) | ((u32)h << 16)))
            return;
    i = (c->vui_fpos & 15u) * 2u;
    c->vui_fbox[i] = ((u32)x & 0xFFFFu) | ((u32)y << 16);
    c->vui_fbox[i + 1] = ((u32)w & 0xFFFFu) | ((u32)h << 16);
    c->vui_fpos++;
}

IMG_FN static u32 vui_focus_hit(cm_t *c, s32_v x, s32_v y, s32_v w, s32_v h)
{
    u32 i;
    if (!c) return 0u;
    for (i = 0; i < 32u; i += 2u) {
        s32_v fx = (s32_v)(signed short)(c->vui_fbox[i] & 0xFFFFu), fy = (s32_v)(signed short)(c->vui_fbox[i] >> 16);
        s32_v fw = (s32_v)(c->vui_fbox[i + 1] & 0xFFFFu), fh = (s32_v)(c->vui_fbox[i + 1] >> 16);
        if (fw && x < fx + fw && fx < x + w && y < fy + fh && fy < y + h) return 1u;
    }
    return 0u;
}

/* Where the module put moved items (native, top-down). A clear repeated at its own place must not cut them. */
IMG_FN static void vui_occ_note(cm_t *c, const s32_v *o)
{
    u32 i;
    s32_v y;
    if (!c || o[2] <= 0 || o[3] <= 0) return;
    y = 480 - o[1] - o[3];
    for (i = 0; i < 128u; i += 2u)
        if (c->vui_occ[i] == (((u32)o[0] & 0xFFFFu) | ((u32)y << 16)) && c->vui_occ[i + 1] == (((u32)o[2] & 0xFFFFu) | ((u32)o[3] << 16)))
            return;
    i = c->vui_occ_pos * 2u;
    c->vui_occ[i] = ((u32)o[0] & 0xFFFFu) | ((u32)y << 16);
    c->vui_occ[i + 1] = ((u32)o[2] & 0xFFFFu) | ((u32)o[3] << 16);
    c->vui_occ_pos = c->vui_occ_pos >= 63u ? 0u : c->vui_occ_pos + 1u;
}

IMG_FN static u32 vui_occ_hit(cm_t *c, s32_v x, s32_v y, s32_v w, s32_v h)
{
    u32 i;
    for (i = 0; i < 128u; i += 2u) {
        s32_v ox = (s32_v)(signed short)(c->vui_occ[i] & 0xFFFFu), oy = (s32_v)(signed short)(c->vui_occ[i] >> 16);
        s32_v ow = (s32_v)(c->vui_occ[i + 1] & 0xFFFFu), oh = (s32_v)(c->vui_occ[i + 1] >> 16);
        if (ow && x < ox + ow && ox < x + w && y < oy + oh && oy < y + h) return 1u;
    }
    return 0u;
}

/* The auto-area frame is four corner brackets. Only boxes reaching into a corner get an extra clear. */
IMG_FN static u32 vui_af_corner(s32_v x, s32_v y, s32_v w, s32_v h)
{
    return (x < 160 || x + w > 560) && (y < 110 || y + h > 370);
}

/* Polylines (the AF frame corners) are drawn in place; the clears that remove them must run in place too. */
IMG_FN static void vui_pl_note(cm_t *c, s32_v x, s32_v y, s32_v w, s32_v h)
{
    u32 i, a, b;
    if (!c || w <= 0 || h <= 0 || w > 720 || h > 480) return;
    x -= 4; y -= 4; w += 8; h += 8;            /* line width */
    a = ((u32)x & 0xFFFFu) | ((u32)y << 16);
    b = ((u32)w & 0xFFFFu) | ((u32)h << 16);
    for (i = 0; i < 32u; i += 2u) if (c->vui_pl[i] == a && c->vui_pl[i + 1] == b) return;
    i = c->vui_pl_pos * 2u;
    c->vui_pl[i] = a; c->vui_pl[i + 1] = b;
    c->vui_pl_pos = c->vui_pl_pos >= 15u ? 0u : c->vui_pl_pos + 1u;
    if (c->vui_pl_n < 16u) c->vui_pl_n++;
}

IMG_FN static u32 vui_pl_hit(cm_t *c, s32_v x, s32_v y, s32_v w, s32_v h)
{
    u32 i;
    for (i = 0; i < 32u; i += 2u) {
        s32_v px = (s32_v)(signed short)(c->vui_pl[i] & 0xFFFFu), py = (s32_v)(signed short)(c->vui_pl[i] >> 16);
        s32_v pw = (s32_v)(c->vui_pl[i + 1] & 0xFFFFu), ph = (s32_v)(c->vui_pl[i + 1] >> 16);
        if (pw && x < px + pw && px < x + w && y < py + ph && py < y + h) return 1u;
    }
    return 0u;
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
    if (!c || c->vui_det || !c->vui_bd_top || y < 530 - (s32_v)c->vui_bd_top || y >= 430) return;   /* group 116 */
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
/* Group 96: the last image call of each small shooting-page icon, kept as the firmware made it (heap).
 * Entry: +0 x | y << 16, +4 w | h << 16, +8 tint, +0x0C draw sequence (0 free; group 102), +0x10 the 0x5C-byte call. */
#define VUI_IC_N    64u                /* group 102: 32 churned (about 30 icon pairs on the portrait page) */
#define VUI_IC_SIZE 0x70u
IMG_FN static void vui_ic_note(cm_t *c, const u8 *d)
{
    u32 key0, key1, tint, i, old, *t;
    s32_v x = *(const signed short *)(d + 0x18), y = *(const signed short *)(d + 0x1A);
    u32 sw = *(const u16 *)(d + 0x14), sh = *(const u16 *)(d + 0x16);
    if (!c->vui_ic || !sw || !sh || sw > 120u || sh > 64u || x < 0 || y < 0) return;
    key0 = (u32)x | (u32)y << 16; key1 = sw | sh << 16; tint = *(const u32 *)(d + 0x20);
    for (i = 0; i < VUI_IC_N; i++) {
        t = (u32 *)(c->vui_ic + i * VUI_IC_SIZE);
        if (t[3] && t[0] == key0 && t[1] == key1 && t[2] == tint) break;
    }
    if (i == VUI_IC_N) {                       /* group 102: a free slot, else the oldest call */
        u32 k;
        old = 0xFFFFFFFFu;
        for (k = 0; k < VUI_IC_N; k++) {
            u32 s = ((const u32 *)(c->vui_ic + k * VUI_IC_SIZE))[3];
            if (s < old) { old = s; i = k; }
            if (!s) break;
        }
    }
    t = (u32 *)(c->vui_ic + i * VUI_IC_SIZE);
    if (++c->vui_ic_pos == 0u) c->vui_ic_pos = 1u;
    t[0] = key0; t[1] = key1; t[2] = tint; t[3] = c->vui_ic_pos;
    for (i = 0; i < 0x5Cu / 4u; i++) t[4 + i] = ((const u32 *)d)[i];
}

/* A stock clear that covers an icon's own box: the icon is gone on purpose, its kept call is dropped. */
IMG_FN static void vui_ic_drop(cm_t *c, s32_v x, s32_v y, s32_v w, s32_v h)
{
    u32 i;
    if (!c->vui_ic) return;
    for (i = 0; i < VUI_IC_N; i++) {
        u32 *t = (u32 *)(c->vui_ic + i * VUI_IC_SIZE);
        s32_v ex = (s32_v)(t[0] & 0xFFFFu), ey = (s32_v)(t[0] >> 16), ew = (s32_v)(t[1] & 0xFFFFu), eh = (s32_v)(t[1] >> 16);
        if (t[3] && ex >= x && ey >= y && ex + ew <= x + w && ey + eh <= y + h) t[3] = 0u;
    }
}

/* A bracket line crosses placed icons: the line first, here, then the icons it crossed, then no stock call. */
IMG_FN static void vui_ic_redo(cm_t *c, u32 ori, s32_v *b)
{
    s32_v e[7], px, py, o[4];
    u32 i, k, n = 0u, d[0x5Cu / 4u];
    u8 hit[VUI_IC_N];
    for (i = 0; i < 7u; i++) e[i] = b[i];
    c->vui_drawing = 1u;
    FW(read_fn, 0x104A641Eu)((u32)e);
    c->vui_drawing = 0u;
    b[2] = 0; b[3] = 0;
    if (!c->vui_ic || c->vui_zoom) return;     /* group 101: no icon on the magnified view */
    for (i = 0; i < VUI_IC_N; i++) {
        const u32 *t = (const u32 *)(c->vui_ic + i * VUI_IC_SIZE);
        s32_v x = (s32_v)(t[0] & 0xFFFFu), y = (s32_v)(t[0] >> 16), w = (s32_v)(t[1] & 0xFFFFu), h = (s32_v)(t[1] >> 16);
        if (!t[3] || !vui_locate(0u, x, y, w, h, &px, &py)) continue;
        vui_panel_rect(ori, px, py, w, h, o);  /* placed box, x, y-up, w, h like the clear */
        if (!(e[0] < o[0] + o[2] && o[0] < e[0] + e[2] && e[1] < o[1] + o[3] && o[1] < e[1] + e[3])) continue;
        hit[n++] = (u8)i;
    }
    if (n) {                                   /* group 103: what was drawn on top of a crossed icon (its glyph) comes too */
        u32 j, pass, more = 1u;
        u8 in[VUI_IC_N];
        for (j = 0; j < VUI_IC_N; j++) in[j] = 0u;
        for (j = 0; j < n; j++) in[hit[j]] = 1u;
        for (pass = 0; more && pass < 4u; pass++) {
            more = 0u;
            for (j = 0; j < VUI_IC_N; j++) {
                const u32 *t = (const u32 *)(c->vui_ic + j * VUI_IC_SIZE);
                s32_v x = (s32_v)(t[0] & 0xFFFFu), y = (s32_v)(t[0] >> 16), w = (s32_v)(t[1] & 0xFFFFu), h = (s32_v)(t[1] >> 16);
                if (in[j] || !t[3] || !vui_locate(0u, x, y, w, h, &px, &py)) continue;
                for (k = 0; k < n; k++) {
                    const u32 *g = (const u32 *)(c->vui_ic + hit[k] * VUI_IC_SIZE);
                    s32_v gx = (s32_v)(g[0] & 0xFFFFu), gy = (s32_v)(g[0] >> 16), gw = (s32_v)(g[1] & 0xFFFFu), gh = (s32_v)(g[1] >> 16);
                    if (g[3] < t[3] && x < gx + gw && gx < x + w && y < gy + gh && gy < y + h) {
                        hit[n++] = (u8)j; in[j] = 1u; more = 1u;
                        break;
                    }
                }
            }
        }
    }
    for (i = 1; i < n; i++) {                  /* group 102: oldest first, the firmware's order (backing, then glyph) */
        u8 v = hit[i];
        u32 s = ((const u32 *)(c->vui_ic + v * VUI_IC_SIZE))[3];
        for (k = i; k && ((const u32 *)(c->vui_ic + hit[k - 1u] * VUI_IC_SIZE))[3] > s; k--) hit[k] = hit[k - 1u];
        hit[k] = v;
    }
    for (i = 0; i < n; i++) {
        const u32 *t = (const u32 *)(c->vui_ic + hit[i] * VUI_IC_SIZE);
        for (k = 0; k < 0x5Cu / 4u; k++) d[k] = t[4 + k];
        c->vui_ic_n++;
        FW(read_fn, VUI_IMAGE1)((u32)d);       /* through the hook: placed again */
    }
}

IMG_FN static void vui_rect(u32 ori, u32 ui, u32 kind, s32_v *b)
{
    s32_v x = b[0], yu = b[1], w = b[2], h = b[3], px, py;
    if (kind == VUI_K_CLEAR) {                 /* group 112: the stock clear's other arguments, for our own clears */
        cm_t *t = cm_get();
        if (t) { t->vui_ct[0] = (u32)b[4]; t->vui_ct[1] = (u32)b[5]; t->vui_ct[2] = (u32)b[6]; t->vui_ct_ok = 1u; }
    }
    if (!ui && kind != VUI_K_CLEAR && vui_is_focus_color(kind == VUI_K_FRAME ? (u32)b[6] : (u32)b[4])) {
        vui_focus_note(cm_get(), x, 480 - yu - h, w, h);
        return;
    }
    if (!ui && kind == VUI_K_CLEAR && (w <= 3 || h <= 3) && !vui_strip_hit(cm_get(), x, 480 - yu - h, w, h)) {   /* group 93: the bracket lines, erased where they are */
        cm_t *c = cm_get();
        if (c && !c->vui_drawing && vui_occ_hit(c, x, 480 - yu - h, w, h))
            vui_ic_redo(c, ori, b);            /* group 96: a placed icon was crossed: the line, then the icon again */
        return;
    }
    if (!ui && kind == VUI_K_CLEAR) {          /* group 96: an icon erased by the firmware is not drawn again by us */
        cm_t *c = cm_get();
        if (c && !c->vui_drawing) vui_ic_drop(c, x, 480 - yu - h, w, h);
    }
    if (!ui && w >= 600 && h >= 400) return;   /* full-screen clears and the black rectangle before playback stay full-screen */
    if (!ui && kind == VUI_K_CLEAR) {
        s32_v yd = 480 - yu - h;
        s32_v x0 = x > 190 ? x : 190, x1 = x + w < VUI_HINT_PX1 + 130 ? x + w : VUI_HINT_PX1 + 130;   /* group 99: clear of the shots count */
        s32_v y0 = yd > 368 ? yd : 368, y1 = yd + h < 425 ? yd + h : 425;
        cm_t *c = cm_get();
        if (c && !c->vui_drawing && x0 < x1 && y0 < y1 && !(x >= 190 && x < 520 && yd >= 368 && yd < 425)) {
            s32_v d[7];                        /* the part over the hint row, where the row was moved */
            u32 i;
            for (i = 0; i < 7u; i++) d[i] = b[i];
            vui_panel_rect(ori, x0 - 130, y0 + VUI_HINT_DY, x1 - x0, y1 - y0, d);
            c->vui_drawing = 1u;
            FW(read_fn, 0x104A641Eu)((u32)d);
            c->vui_drawing = 0u;
        }
    }
    if (!ui && kind == VUI_K_CLEAR) {
        cm_t *c = cm_get();
        if (c && !c->vui_drawing && vui_focus_hit(c, x, 480 - yu - h, w, h)) {
            c->vui_fhit_n++;
            s32_v d[7];                        /* the same clear at its own place: erases the old focus frame */
            u32 i;
            for (i = 0; i < 7u; i++) d[i] = b[i];
            c->vui_drawing = 1u;
            FW(read_fn, 0x104A641Eu)((u32)d);
            c->vui_drawing = 0u;
        }
    }
    if (!ui && kind != VUI_K_CLEAR && x >= 500 && 480 - yu - h >= 360) {
        cm_t *c = cm_get();                    /* the white outline of the magnified-view navigator (camera photo) */
        if (c && ((w >= 150 && w <= 210 && (h <= 4 || (h >= 70 && h <= 110))) || (w <= 4 && h >= 70 && h <= 110))) {
            c->vui_zoom = 1u;
            c->vui_zoom_ms = *(volatile u32 *)CM_MS_CLOCK;
            c->vui_zoom_n++;
        }
    }
    if (ui == VUI_PAGE_GRID) {
        if (x == 0 && w == 720 && yu < 260) { vui_panel_rect(ori, 0, VUI_PY_GRID, 480, VUI_CELL_H * 3, b); return; }
        if (yu >= 200 && yu <= 260 && h <= 60 && w > 300) { vui_panel_rect(ori, 0, 384, 480, 44, b); return; }
    } else if (!ui && kind == VUI_K_CLEAR && x >= 90 && x < 150 && 480 - yu - h >= 42 && 480 - yu - h < 90) {
        b[2] = 0; b[3] = 0;                    /* an empty icon slot's clear (group 86: the arms are cleared by group 81) */
        return;
    } else if (!ui && kind == VUI_K_CLEAR && x < 50 && 480 - yu - h >= 270 && 480 - yu <= 305 && !VUI_NO_G97SLOT) {
        b[2] = 0; b[3] = 0;                    /* group 97: an empty left-column slot; in place it cuts the EV icon */
        return;
    } else if (!ui && kind == VUI_K_CLEAR && x >= 610 && 480 - yu - h >= 186 && 480 - yu <= 230) {
        b[2] = 0; b[3] = 0;                    /* group 89: empty right-column slots, in place across REAR (px 188-228) */
        return;
    }
    if (ui == VUI_PAGE_SETTING && !vui_det_on()) {   /* group 116: no strip learning on a third-level page */
        cm_t *c = cm_get();
        s32_v y = 480 - yu - h;
        if (kind == VUI_K_RECT) vui_learn(c, x, y, w, (u32)b[4]);
        if (kind == VUI_K_FRAME && y >= 430 && w <= 300) {
            vui_bar_learn(c, x, w);
            if (c && c->vui_bar_dirty && c->vui_ct_ok && !c->vui_drawing) {   /* group 112: the bar moved: wipe its row */
                s32_v d[7];
                c->vui_bar_dirty = 0u;
                d[4] = (s32_v)c->vui_ct[0]; d[5] = (s32_v)c->vui_ct[1]; d[6] = (s32_v)c->vui_ct[2];
                vui_panel_rect(ori, 0, VUI_PY_BAR, 480, 50, d);
                c->vui_drawing = 1u;
                FW(read_fn, 0x104A641Eu)((u32)d);
                c->vui_drawing = 0u;
            }
        }
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
        s32_v own[7];
        u32 i;
        for (i = 0; i < 7u; i++) own[i] = b[i];
        if (ui == VUI_PAGE_SETTING && vui_det_on()) { w = VUI_FIT(w); h = VUI_FIT(h); }   /* group 116 */
        else if (ui == VUI_PAGE_SETTING && vui_is_fit(cm_get(), 480 - yu - h)) w = VUI_FIT(w);
        if (!ui && vui_strip_hit(cm_get(), x, 480 - yu - h, w, h)) {
            w = (w * 7) / 10; h = (h * 7) / 10;   /* group 100: the strip at 7/10 */
            if (w < 1) w = 1;
            if (h < 1) h = 1;
        }
        if (!ui && x >= 190 && x < 520 && 480 - yu - h >= 368 && 480 - yu - h < 425 && px + w > VUI_HINT_PX1) {
            w = VUI_HINT_PX1 - px;             /* group 99: the lowered hint row stops short of the shots count */
            if (w <= 0) { b[2] = 0; b[3] = 0; return; }
        }
        vui_panel_rect(ori, px, py, w, h, b);
        if (!ui) {
            cm_t *c = cm_get();
            if (c && kind == VUI_K_CLEAR && !c->vui_drawing && vui_pl_hit(c, x, 480 - yu - h, own[2], own[3])) {
                c->vui_pl_rep++;               /* as in landscape: the stock clear removes the polyline where it is */
                c->vui_drawing = 1u;
                FW(read_fn, 0x104A641Eu)((u32)own);
                c->vui_drawing = 0u;
            }
            if (c && kind != VUI_K_CLEAR) vui_occ_note(c, b);
            else if (c && !c->vui_drawing && (s32_v)(*(volatile u32 *)CM_MS_CLOCK - c->vui_af_win) >= 0) {
                u32 *e = c->vui_mv + c->vui_mv_pos * 5u;   /* kept, in case an AF-area change is noticed late */
                e[0] = (u32)own[0]; e[1] = (u32)own[1]; e[2] = (u32)own[2]; e[3] = (u32)own[3];
                e[4] = *(volatile u32 *)CM_MS_CLOCK;
                c->vui_mv_pos = c->vui_mv_pos >= 15u ? 0u : c->vui_mv_pos + 1u;
            } else if (c && !c->vui_drawing && vui_af_corner(x, 480 - yu - h, own[2], own[3])) {
                if (vui_occ_hit(c, x, 480 - yu - h, own[2], own[3])) { c->vui_af_s++; c->vui_af_cut = 1u; }
                c->vui_af_n++;                 /* erase what the stock clear meant to erase, where it was */
                c->vui_drawing = 1u;
                FW(read_fn, 0x104A641Eu)((u32)own);
                c->vui_drawing = 0u;
            }
        }
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
    if (ui == VUI_PAGE_SETTING && !vui_det_on() && sw <= 60 && sh <= 72 && (x < 90 || x >= 670) && y >= 355 && y < 445 && sw < sh) {
        *(u32 *)d = 0u;                        /* group 112: the strip-end arrows: not shown in portrait */
        box[0] = -300; box[1] = -300; box[2] = -300; box[3] = -300;
        return;
    }
    if (!ui && vui_is_focus_color(*(u32 *)(d + 0x20))) {
        vui_focus_note(cm_get(), x, y, sw, sh);
        return;
    }
    if (ui == VUI_PAGE_SETTING) vui_extent(cm_get(), x, y, sw);
    if (!ui && x >= 120 && y + sh <= 60) {     /* the top row is back: the magnified view has ended */
        cm_t *c = cm_get();
        if (c) c->vui_zoom = 0u;
    }
    {
        u32 st = !ui && vui_strip_hit(cm_get(), x, y, sw, sh);
        if (!vui_locate(ui, x, y, sw, sh, &px, &py)) return;
        if (st) {                              /* group 100: the strip at 3/4 */
            sw = (sw * 7) / 10; sh = (sh * 7) / 10;
            if (sw < 1) sw = 1;
            if (sh < 1) sh = 1;
        }
    }
    if (ui == VUI_PAGE_SETTING && vui_det_on()) { sw = VUI_FIT(sw); sh = VUI_FIT(sh); }   /* group 116 */
    else if (ui == VUI_PAGE_SETTING && vui_is_fit(cm_get(), y)) {
        s32_v nh = VUI_FIT(sh);
        py += (sh - nh) >> 1;
        sw = VUI_FIT(sw);
        sh = nh;
    }
    if (!ui) {
        s32_v o[4];
        vui_panel_rect(ori, px, py, sw, sh, o);
        vui_occ_note(cm_get(), o);
#ifdef VUI_DIAG_FOCUS
        if (x < 50 && y >= 430) {              /* the metering icon (group 77 readout) */
            cm_t *m = cm_get();
            if (m) { m->vui_mt[0] = (u32)o[0]; m->vui_mt[1] = (u32)o[1]; m->vui_mt[2] = (u32)o[2]; m->vui_mt[3] = (u32)o[3]; m->vui_mt_n++; }
        }
#endif
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
    if (ui && f < (50 << 8) && !(ui == VUI_PAGE_SETTING && vui_det_on())) {
        px = e - ((ui == VUI_PAGE_SETTING ? vui_bar_shift(cm_get()) : VUI_BAR_SHIFT) << 8);
        py = ((VUI_PY_BAR + 480 - 431) << 8) - f;
    }
    else if (ui == VUI_PAGE_GRID && f >= (205 << 8) && f < (262 << 8)) { px = e - (120 << 8); py = VUI_PY_TITLE << 8; }
    else {                                     /* values inside a cell or on the shooting page */
        s32_v gw = vui_f2fix(a) >> 8, gh = vui_f2fix(d) >> 8, bx, by;
        if (ui == VUI_PAGE_SETTING) vui_extent(cm_get(), e >> 8, 480 - (f >> 8) - gh, 0);
        if (!ui && vui_strip_hit(cm_get(), e >> 8, 480 - (f >> 8) - gh, gw, gh)) {   /* group 100: the strip title */
            cm_t *z = cm_get();
            if (!vui_locate(ui, e >> 8, 480 - (f >> 8) - gh, gw, gh, &bx, &by)) return;
            a = vui_fix2f((vui_f2fix(a) * 7) / 10);
            d = vui_fix2f((vui_f2fix(d) * 7) / 10);
            gh = (gh * 7) / 10;
            if (z) z->vui_tr_f = -1;
        } else if (!ui) {                      /* group 96: a glyph that continues the previous one keeps its offset */
            cm_t *z = cm_get();
            s32_v nx = e >> 8, ny = 480 - (f >> 8) - gh;
            if (z && z->vui_tr_f == f && nx >= z->vui_tr_x1 - 2 && nx <= z->vui_tr_x1 + 14) {
                bx = nx + z->vui_tr_dx; by = ny + z->vui_tr_dy;
            } else if (!vui_locate(ui, nx, ny, gw, gh, &bx, &by)) {
                if (z) z->vui_tr_f = -1;
                return;
            } else if (z) {
                z->vui_tr_dx = bx - nx; z->vui_tr_dy = by - ny;
            }
            if (z) { z->vui_tr_f = f; z->vui_tr_x1 = nx + gw; }
        } else if (!vui_locate(ui, e >> 8, 480 - (f >> 8) - gh, gw, gh, &bx, &by)) return;
        if (ui == VUI_PAGE_SETTING && vui_det_on()) {   /* group 116: glyphs at 2/3 too */
            a = vui_fix2f(VUI_FIT(vui_f2fix(a)));
            d = vui_fix2f(VUI_FIT(vui_f2fix(d)));
            gh = VUI_FIT(gh);
        } else if (ui == VUI_PAGE_SETTING && vui_is_fit(cm_get(), 480 - (f >> 8) - gh)) {
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

/* Group 95: the photo/video switch (camera, thirteenth DIAG.BIN): snapshot +0x7C is 0x00010001 in video mode. */
#define VUI_SW_VIDEO 0x7Cu
IMG_FN static u32 vui_video(cm_t *c)
{
    u32 ms = *(volatile u32 *)CM_MS_CLOCK;
    if (ms - c->vui_vid_ms >= 100u) {
        u32 s = FW(read_fn, VUI_SW_SNAPSHOT)(0u);
        c->vui_vid_ms = ms;
        if (s >= RAM_LO && s < RAM_HI - 0x9Cu && !(s & 3u)) c->vui_vid = *(volatile u32 *)(s + VUI_SW_VIDEO) & 1u;
    }
    return c->vui_vid;
}

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
#ifdef VUI_NO_REPAINT                          /* emulator diagnostic: the host tries other repaint calls */
    (void)c;
    return;
#endif
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

/* Own LCD histogram: the detailed page in landscape/portrait, plus the original portrait pages.
   The stock histogram is not OpenVG (camera readout, group 50); draw its counts with rectangle primitives.
   0x1034B914: 256 u32 counts copied from 0xC202A55C to 0xC2028DE8, display peak stored at 0xC2028094 + 0x34. */
#define VUI_HIST_BINS  0xC2028DE8u
#define VUI_HIST_PEAK  0xC20280C8u
#define VUI_HIST_MASKS 0xC1826118u     /* 9 visibility masks of the stock histogram widget */
#define VUI_DISP_PAGE  0xC2028442u
#define VUI_RECT_FN    0x104A5EDCu
#define VUI_CLEAR_FN   0x104A641Eu
#define VUI_HX 236                     /* portrait box: 64 bars of 3 pixels */
#define VUI_HY 330
#define VUI_HLX 476                    /* landscape, top-down coordinates, native histogram area */
#define VUI_HLY 278
#define VUI_HIST_INTERVAL 80u
#define VUI_HX3 144                    /* on detailed/level pages: under the level, centred (group 97; landscape size, group 101) */
#ifndef VUI_HY3
#define VUI_HY3 480
#endif
#define VUI_HHY(y) 96                   /* group 101: 192 x 96, 3 px columns everywhere, as in landscape */
#define VUI_HCW(y) 3
#define VUI_HWY(y) (64 * VUI_HCW(y))
#define VUI_HW 192
#define VUI_HH 96
#define VUI_ZOOM_HOLD_MS 600u          /* the stock histogram is hidden in the magnified view; so is ours */

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

IMG_FN static void vui_hist_rect(u32 ori, s32_v px, s32_v py, s32_v w, s32_v h, s32_v *o)
{
    if (ori) { vui_panel_rect(ori, px, py, w, h, o); return; }
    o[0] = px; o[1] = 480 - py - h; o[2] = w; o[3] = h;
}

IMG_FN static void vui_prim(cm_t *c, u32 fn, u32 ori, s32_v px, s32_v py, s32_v w, s32_v h, u32 color)
{
    s32_v d[7];
    vui_hist_rect(ori, px, py, w, h, d);
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
    if (active && ori && !c->vui_hm_saved) {
        /* Bit 3 of the first mask is what the histogram-on-level-page gate (firmware, group 28 on) tests before
           sampling on DISP page 3. Keeping only that bit keeps the counts fresh there. */
        for (i = 0; i < 9u; i++) { c->vui_hm[i] = mask[i]; mask[i] = 0u; }
        if (c->vui_hm[0] & 8u) mask[0] = 0x08000000u;   /* bit 27: the gate only (firmware group 54) */
        c->vui_hm_saved = 1u;
    } else if ((!active || !ori) && c->vui_hm_saved) {
        for (i = 0; i < 9u; i++) mask[i] = c->vui_hm[i];
        c->vui_hm_saved = 0u;
    }
    if (kind != VUI_K_CLEAR) return;           /* drawn alongside the page's periodic clears */
#ifdef VUI_TEST_HIST
    want = active && ui == 0u && !c->vui_strip && (page == 0u || (ori && (page == 2u || page == 3u)));
#else
    want = active && ui == 0u && !c->vui_strip && (page == 0u || (ori &&
           (page == 2u || (page == 3u && c->vui_hm_saved && (c->vui_hm[0] & 8u)))));
#endif
    if (page == 0u && (c->custom_flags & 5u) != 5u) want = 0u;
    if (c->vui_zoom || *(volatile u32 *)CM_MS_CLOCK - c->vui_zoom_ms < VUI_ZOOM_HOLD_MS) want = 0u;
    hx = !ori ? VUI_HLX : (page == 0u || page == 3u) ? VUI_HX3 : VUI_HX;
    hy = !ori ? VUI_HLY : (page == 0u || page == 3u) ? VUI_HY3 : VUI_HY;
    if (c->vui_hist_on && (!want || ori != c->vui_hist_ori || hx != c->vui_hist_x || hy != c->vui_hist_y)) {
        vui_prim(c, VUI_CLEAR_FN, c->vui_hist_ori, c->vui_hist_x, c->vui_hist_y, VUI_HWY(c->vui_hist_y), VUI_HHY(c->vui_hist_y), 0u);
        c->vui_hist_on = 0u;
    }
    if (!want) { c->vui_hist_due = 0u; return; }
    c->vui_hist_due = 1u;                      /* drawn at the end of this hook, after the layout */
    c->vui_hist_nx = hx; c->vui_hist_ny = hy; c->vui_hist_nori = ori;
    (void)ms; (void)peak;
}

/* The hook runs before the stock clear: a clear over the box would cut what is drawn here. Wait for the next one. */
IMG_FN static void vui_hist_draw(cm_t *c, s32_v *b)
{
    u32 i, ms, peak, ori = c->vui_hist_nori;
    s32_v hx = c->vui_hist_nx, hy = c->vui_hist_ny, d[4], hh = VUI_HHY(c->vui_hist_ny);
    s32_v cw = VUI_HCW(c->vui_hist_ny), hw = VUI_HWY(c->vui_hist_ny);
    c->vui_hist_due = 0u;
    vui_hist_rect(ori, hx, hy, hw, hh, d);
    if (b && b[2] > 0 && b[3] > 0 &&
        b[0] < d[0] + d[2] && d[0] < b[0] + b[2] && b[1] < d[1] + d[3] && d[1] < b[1] + b[3]) {
        s32_v e[7];                            /* group 94: the clear first, here, then the histogram over it */
        for (i = 0; i < 7u; i++) e[i] = b[i];
        c->vui_drawing = 1u;
        FW(read_fn, VUI_CLEAR_FN)((u32)e);
        c->vui_drawing = 0u;
        b[2] = 0; b[3] = 0;                    /* the stock call has nothing left to do */
        c->vui_hist_dirty = 1u;
    }
    ms = *(volatile u32 *)CM_MS_CLOCK;
    /* Dirty/bank/rotation repairs reuse cached data immediately; ordinary updates retain the 80ms gate. */
    if (c->vui_hist_on && !c->vui_hist_dirty && ms - c->vui_hist_ms < VUI_HIST_INTERVAL) return;
    {
        u8 hg[64];
        u8 *prev = c->vui_mtd ? (u8 *)(c->vui_mtd + 4u * 0x5Cu)   /* after the 4 kept image calls */ : (u8 *)0;
        u32 full = !c->vui_hist_on || c->vui_hist_dirty || !prev || c->vui_hist_ori != ori ||
                   c->vui_hist_x != hx || c->vui_hist_y != hy, changed = 0u;
#ifdef VUI_TEST_HIST
        peak = 128u;
#else
        peak = *(volatile u32 *)VUI_HIST_PEAK;
#endif
        for (i = 0; i < 64u; i++) {
            u32 bb = i * 4u, sum = 0u, j, h = 0u;
            for (j = 0; peak && j < 4u; j++) {
#ifdef VUI_TEST_HIST
                u32 k = bb + j;
                sum += k < 128u ? k : 255u - k;    /* emulator only: a triangle */
#else
                sum += ((const volatile u32 *)VUI_HIST_BINS)[bb + j];
#endif
            }
            if (peak) h = vui_udiv(sum * (u32)hh, peak * 4u);
            hg[i] = (u8)(h > (u32)hh ? (u32)hh : h);
        }
#ifndef VUI_NO_ZEROHOLD
        if (ori && *(volatile u8 *)VUI_DISP_PAGE != 0u && prev && !full) { /* original pages' zero hold */
            u32 now = 0u, before = 0u;
            for (i = 0; i < 64u; i++) { now |= hg[i]; before |= prev[i]; }
            if (!now && before && c->vui_hist_zero < 3u) { c->vui_hist_zero++; c->vui_hist_ms = ms; return; }
            if (now) c->vui_hist_zero = 0u;
        }
#endif
        c->vui_hist_dirty = 0u;
        c->vui_hist_ms = ms;
        c->vui_hist_on = 1u;
        c->vui_hist_ori = ori;
        c->vui_hist_x = hx;
        c->vui_hist_y = hy;
        if (full) {
            vui_prim(c, VUI_CLEAR_FN, ori, hx, hy, hw, hh, 0u);
            vui_prim(c, VUI_RECT_FN, ori, hx, hy, hw, hh, 0x80u);
            changed = 1u;
        }
        for (i = 0; i < 64u; i++) {
            s32_v cx = hx + (s32_v)i * cw, h = hg[i];
            if (!full) {
                if (hg[i] == prev[i]) continue;   /* group 96: an unchanged column is left alone */
                vui_prim(c, VUI_CLEAR_FN, ori, cx, hy, cw, hh, 0u);
                vui_prim(c, VUI_RECT_FN, ori, cx, hy, cw, hh, 0x80u);
                changed = 1u;
            }
            if (h) vui_prim(c, VUI_RECT_FN, ori, cx, hy + hh - h, cw, h, 0xE6E6E6FFu);
        }
        if (prev) for (i = 0; i < 64u; i++) prev[i] = hg[i];
        if (!changed) return;                  /* the same bars: nothing is drawn, nothing can flicker */
    }
    for (i = 1; i < 4u; i++)                   /* group 91: three dividers, four parts, as on the stock histogram */
        vui_prim(c, VUI_RECT_FN, ori, hx + (s32_v)i * (hw / 4), hy, 1, hh, 0xFFFFFFFFu);
    vui_prim(c, VUI_RECT_FN, ori, hx, hy, hw, 2, 0xFFFFFFFFu);   /* and the white frame */
    vui_prim(c, VUI_RECT_FN, ori, hx, hy + hh - 2, hw, 2, 0xFFFFFFFFu);
    vui_prim(c, VUI_RECT_FN, ori, hx, hy, 2, hh, 0xFFFFFFFFu);
    vui_prim(c, VUI_RECT_FN, ori, hx + hw - 2, hy, 2, hh, 0xFFFFFFFFu);
}

/* Same packed-pixel conversion as the EVF batch painter. */
IMG_FN static u32 vui_hist_pixel(u32 index)
{
    u32 rgba = FW(read_fn, 0x10526478u)(index);
    return (rgba << 24) | ((rgba & 0xff00u) << 8) | ((rgba >> 8) & 0xff00u) | (rgba >> 24);
}

IMG_FN static void vui_hist_cpu_fill(u32 surface, u32 ori, s32_v x, s32_v y, s32_v w, s32_v h, u32 pixel)
{
    s32_v d[4];
    vui_hist_rect(ori, x, y, w, h, d);
    s32_v top = 480 - d[1] - d[3];
    for (s32_v row = top; row < top + d[3]; row++) {
        volatile u32 *out = (volatile u32 *)surface + row * 720 + d[0];
        for (s32_v col = 0; col < d[2]; col++) out[col] = pixel;
    }
}

/* CPU batch painter from the EVF design, fitted into the existing 192x96 landscape/portrait boxes.
   Read only the pinned packet, never the ISP or its shared native-bin cache. */
IMG_FN static void vui_hist_cpu_draw(cm_t *c, u32 surface, const u32 *packet, u16 *dirty_rows)
{
    u32 ori = c->vui_hist_nori, scale = packet[256], sequence = packet[257];
    u32 ms = *(volatile u32 *)CM_MS_CLOCK;
    if (!scale || !dirty_rows) return;
    if (c->vui_hist_on && !c->vui_hist_dirty && c->vui_hist_last_publication == sequence &&
        ms - c->vui_hist_ms < 150u) return;
    s32_v hx = c->vui_hist_nx, hy = c->vui_hist_ny, d[4];
    vui_hist_rect(ori, hx, hy, VUI_HW, VUI_HH, d);
    s32_v top = 480 - d[1] - d[3];
    if (d[0] < 0 || d[0] + d[2] > 720 || top < 0 || top + d[3] > 480) return;
#define HIST_FILL(x,y,w,h,p) vui_hist_cpu_fill(surface, ori, hx + (x), hy + (y), (w), (h), (p))
#define HIST_COLOR(i) vui_hist_pixel(i)
#define HIST_BIN(i) packet[i]
#define HIST_DIV(n,d) vui_udiv((n),(d))
#include "custom_disp/histogram_raster.h"
#undef HIST_FILL
#undef HIST_COLOR
#undef HIST_BIN
#undef HIST_DIV
    /* Merge CPU histogram damage into the level's existing sparse cache publication. */
    for (s32_v y = top; y < top + d[3]; y++) {
        if (dirty_rows[y * 2] > (u32)d[0]) dirty_rows[y * 2] = (u16)d[0];
        if (dirty_rows[y * 2 + 1] < (u32)(d[0] + d[2])) dirty_rows[y * 2 + 1] = (u16)(d[0] + d[2]);
    }
    c->vui_hist_on = 1u; c->vui_hist_dirty = 0u;
    c->vui_hist_ori = ori; c->vui_hist_x = hx; c->vui_hist_y = hy;
    c->vui_hist_last_publication = sequence; c->vui_hist_ms = ms;
}

/* Two phases on the checked LCD tail: GPU cleanup before sync, cached CPU paint after sync.
   This callback never calls a statistics-read service and does not skip painting while the worker waits. */
IMG_FN static void vui_hist_before_level(u32 native_bank, u32 surface, const u32 *packet, u16 *dirty_rows)
{
    cm_t *c = cm_get();
    if (!c || c->vui_drawing || c->custom_disp_rc) return;
    if (!native_bank && !surface) { custom_apply(c); return; }
    if ((c->custom_flags & 5u) != 5u) {
        if (!surface && c->vui_hist_on) vui_hist(c, VUI_K_CLEAR, c->vui_hist_ori, 0u, 0u);
        return;
    }
    u32 raw_ori = FW(read_fn, VUI_ORI_GET)(0u);
    if (raw_ori > 2u) {
        if (!surface) {
            if (c->vui_hist_on && c->vui_hist_bank && *(volatile u8 *)VUI_TARGET == VUI_TARGET_OSD)
                vui_hist(c, VUI_K_CLEAR, 0u, 0u, *(volatile u8 *)VUI_UI);
            c->vui_hist_bank = 0u;
        }
        return;
    }
    u32 ori = c->vui ? raw_ori : 0u;
    if (*(volatile u8 *)VUI_APP != 3u || *(volatile u8 *)VUI_UI != 0u ||
        *(volatile u8 *)VUI_DISP_PAGE != 0u || *(volatile u8 *)VUI_TARGET != VUI_TARGET_OSD ||
        vui_video(c) || c->vui_evf) return;
    if (!surface) {
        vui_hist(c, VUI_K_CLEAR, ori, 1u, 0u);
        if (c->vui_hist_bank != native_bank) { c->vui_hist_dirty = 1u; c->vui_hist_bank = native_bank; }
        return;
    }
    if (!packet || !c->vui_hist_due || ori != c->vui_hist_nori || c->vui_zoom ||
        c->vui_hist_bank != native_bank) return;
    c->vui_hist_due = 0u;
    vui_hist_cpu_draw(c, surface, packet, dirty_rows);
}

#ifdef VUI_DIAG_FOCUS
/* Group 78: one record per drawing call while the shooting application runs. */
IMG_FN static void vui_dg_box(u32 kind, u32 *r, signed short *o, u32 *col, u32 *x)
{
    u32 i;
    for (i = 0; i < 4u; i++) o[i] = 0;
    x[0] = x[1] = x[2] = 0u; *col = 0u;
    if (kind == VUI_K_KEY || kind == VUI_K_SETTER) { x[0] = r[0]; x[1] = r[1]; return; }
    if (kind == VUI_K_TOUCH) {                 /* type (+8, +9), point (+0x12, +0x14) */
        const u8 *ev = (const u8 *)r[0];
        if (ev) { x[0] = ev[8] | (u32)ev[9] << 8; x[1] = *(const u16 *)(ev + 0x12) | (u32)*(const u16 *)(ev + 0x14) << 16; }
        return;
    }
    if (kind == VUI_K_FRAME) { for (i = 0; i < 4u; i++) o[i] = (signed short)r[i]; *col = r[6]; return; }
    if (!r[0]) return;
    if (kind == VUI_K_RECT || kind == VUI_K_CLEAR || kind == VUI_K_FILL) {
        const s32_v *q = (const s32_v *)r[0];
        for (i = 0; i < 4u; i++) o[i] = (signed short)q[i];
        *col = (u32)q[4]; x[0] = (u32)q[5];
    } else if (kind == VUI_K_IMAGE || kind == VUI_K_IMAGE2) {
        const u8 *d = (const u8 *)r[0];
        for (i = 0; i < 4u; i++) o[i] = *(const signed short *)(d + 0x18 + 2u * i);
        *col = *(const u32 *)(d + 0x20); x[0] = *(const u32 *)(d + 0x14); x[1] = *(const u16 *)(d + 0x40);
        x[2] = *(const u32 *)d;
    } else if (kind == VUI_K_IMAGE3) {
        const u8 *d = (const u8 *)r[0];
        for (i = 0; i < 4u; i++) o[i] = (signed short)(*(const s32_v *)(d + 0x18 + 4u * i) >> 16);
        x[0] = *(const u32 *)(d + 0x14);
    } else if (kind == VUI_K_TEXT) {
        const u32 *m = (const u32 *)r[0];
        x[0] = m[6]; x[1] = m[7]; x[2] = m[0]; *col = m[4];
    }
}

IMG_FN static u8 *vui_dg_in(cm_t *c, u32 kind, u32 *r)
{
    u8 *e;
    u32 app = *(volatile u8 *)VUI_APP, i;
    if (c->dg_buf && app == 3u && c->dg_frozen && *(volatile u8 *)VUI_UI == 9u) {
        c->dg_frozen = 0u; c->dg_left = 0u;    /* group 99: a quick-adjust strip wins over an earlier capture */
    }
    if (!c->dg_buf || app != 3u || c->dg_frozen) return 0;
#ifndef VUI_DIAG_NOFREEZE                      /* group 102: that build keeps the last calls before the write */
    if (c->dg_left && *(volatile u8 *)VUI_UI == 0u && !--c->dg_left) c->dg_frozen = 1u;   /* kept; paused off the shooting page (group 98) */
#endif
    e = (u8 *)c->dg_buf + DG_EV_OFF + c->dg_i * DG_EV_SIZE;
    c->dg_i = c->dg_i + 1u >= DG_EV_CAP ? 0u : c->dg_i + 1u;
    c->dg_n++;
    *(u32 *)e = *(volatile u32 *)CM_MS_CLOCK;
    e[4] = (u8)kind; e[5] = *(volatile u8 *)VUI_TARGET; e[6] = (u8)c->dg_ori; e[7] = (u8)c->vui_af;
    e[8] = *(volatile u8 *)VUI_UI; e[9] = (u8)app; e[10] = c->vui_drawing ? 2u : 0u;
    e[11] = *(volatile u8 *)0xC2028442u;      /* DISP page (group 87) */
    vui_dg_box(kind, r, (signed short *)(e + 12), (u32 *)(e + 32), (u32 *)(e + 36));
    for (i = 0; i < 4u; i++) ((signed short *)(e + 20))[i] = ((signed short *)(e + 12))[i];
    *(u32 *)(e + 28) = r[5];                   /* LR */
    if (kind == VUI_K_KEY && r[0] && r[0] != 0x29u && r[0] != 0x46u && r[0] != 0x47u && !c->dg_left && e[8] == 0u)
        c->dg_left = DG_POST;                  /* a key on the shooting page (not DISP; not Fn/dial 0x46/0x47, group 98) */
    {   /* group 87: state markers */
        u32 now[14], k, ms = *(volatile u32 *)CM_MS_CLOCK;
        now[0] = e[8]; now[1] = e[9]; now[2] = e[11]; now[3] = c->dg_ori; now[4] = c->vui_zoom;
        if (ms - c->dg_sw_ms >= 50u) {         /* group 95: the switch snapshot, to find the photo/video switch */
            u32 s = FW(read_fn, VUI_SW_SNAPSHOT)(0u);
            c->dg_sw_ms = ms;
            if (s >= RAM_LO && s < RAM_HI - 0x9Cu && !(s & 3u)) {
                c->dg_sw[0] = *(volatile u32 *)(s + 0x04u); c->dg_sw[1] = *(volatile u32 *)(s + 0x18u);
                c->dg_sw[2] = *(volatile u32 *)(s + 0x2Cu); c->dg_sw[3] = *(volatile u32 *)(s + 0x40u);
                c->dg_sw[4] = *(volatile u32 *)(s + 0x94u); c->dg_sw[5] = *(volatile u32 *)(s + 0x98u);
                for (k = 0; k < 3u; k++)
                    c->dg_sw[6 + k] = *(volatile u16 *)(s + 0x64u + k * 16u) | (u32)*(volatile u16 *)(s + 0x6Cu + k * 16u) << 16;
            }
        }
        for (k = 0; k < 9u; k++) now[5 + k] = c->dg_sw[k];
        for (k = 0; k < 14u; k++) {
            if (now[k] != c->dg_st[k]) {
                u8 *m;
                u32 j;
                m = (u8 *)c->dg_buf + DG_EV_OFF + c->dg_i * DG_EV_SIZE;
                c->dg_i = c->dg_i + 1u >= DG_EV_CAP ? 0u : c->dg_i + 1u;
                c->dg_n++;
                for (j = 0; j < DG_EV_SIZE / 4u; j++) ((u32 *)m)[j] = 0u;
                *(u32 *)m = *(u32 *)e;
                m[4] = 0x42u; m[6] = e[6]; m[7] = e[7]; m[8] = e[8]; m[9] = e[9]; m[11] = e[11];
                *(u32 *)(m + 36) = c->dg_st[k]; *(u32 *)(m + 40) = now[k]; *(u32 *)(m + 44) = k + 1u;
                if (k == 0u && c->dg_st[0] != 0u && now[0] == 0u && (!c->dg_left || c->dg_left > 1500u))
                    c->dg_left = 1500u;        /* group 98: back on the shooting page from a strip or overlay */
                c->dg_st[k] = now[k];
            }
        }
    }
    return e;
}

IMG_FN static void vui_dg_out(u8 *e, u32 kind, u32 *r)
{
    u32 col, x[3];
    vui_dg_box(kind, r, (signed short *)(e + 20), &col, x);
    e[10] |= 1u;                               /* reached the end of the hook */
}

IMG_FN static void vui_dg_mark(cm_t *c, u32 old, u32 now)
{
    u8 *e;
    u32 i;
    if (!c->dg_buf || c->dg_frozen) return;
    c->dg_left = DG_POST;                      /* group 89: every change restarts the countdown, so it is kept */
    e = (u8 *)c->dg_buf + DG_EV_OFF + c->dg_i * DG_EV_SIZE;
    c->dg_i = c->dg_i + 1u >= DG_EV_CAP ? 0u : c->dg_i + 1u;
    c->dg_n++;
    for (i = 0; i < DG_EV_SIZE / 4u; i++) ((u32 *)e)[i] = 0u;
    *(u32 *)e = *(volatile u32 *)CM_MS_CLOCK;
    e[4] = 0x40u; e[6] = (u8)c->dg_ori; e[7] = (u8)now; e[8] = *(volatile u8 *)VUI_UI; e[9] = *(volatile u8 *)VUI_APP;
    *(u32 *)(e + 36) = old; *(u32 *)(e + 40) = now;
    for (i = 0; i < 0x80u / 4u; i++) ((u32 *)(c->dg_buf + 0xD00u))[i] = ((const volatile u32 *)DG_AFAREA)[i];
    for (i = 0; i < 0x14u / 4u; i++) ((u32 *)(c->dg_buf + 0xD80u))[i] = ((const volatile u32 *)DG_LAYERS)[i];
}
#endif

/* Group 81: one AF-area bracket arm, cleared at its own (landscape) place. x, y top-down. */
IMG_FN static void vui_arm_one(cm_t *c, s32_v x, s32_v y, s32_v w, s32_v h)
{
    s32_v d[7];
    d[0] = x - 4; d[1] = 480 - (y - 4) - (h + 8); d[2] = w + 8; d[3] = h + 8; d[4] = 0; d[5] = 0; d[6] = 0;
    c->vui_drawing = 1u;
    FW(read_fn, 0x104A641Eu)((u32)d);
    c->vui_drawing = 0u;
}

IMG_FN static void vui_arm_clear(cm_t *c)
{
    vui_arm_one(c, 72, 42, 69, 8);   vui_arm_one(c, 72, 42, 8, 48);    /* top left */
    vui_arm_one(c, 579, 42, 69, 8);  vui_arm_one(c, 640, 42, 8, 48);   /* top right: mirror of top left */
    vui_arm_one(c, 87, 401, 8, 42);  vui_arm_one(c, 87, 435, 56, 8);   /* bottom left */
    vui_arm_one(c, 633, 401, 8, 42); vui_arm_one(c, 577, 435, 64, 8);  /* bottom right: mirror of bottom left */
}

/* Called from the firmware stubs: r = saved r0-r3, r12, lr, then the caller's stack. */
/* Group 88: the metering icon is drawn only on a full redraw (power-on, Fn2). Its two image calls are kept as the
 * firmware made them and made again after the module's full-screen clear and after the bracket-arm clears. */
#define VUI_MTD_SIZE  0x5Cu
#define VUI_DISP_PAGE 0xC2028442u
IMG_FN static void vui_mtd_draw(cm_t *c, u32 k0)
{
    u32 d[VUI_MTD_SIZE / 4u], k, i;
    for (k = k0; k < k0 + 2u; k++) {
        const u32 *s = (const u32 *)(c->vui_mtd + k * VUI_MTD_SIZE);
        for (i = 0; i < VUI_MTD_SIZE / 4u; i++) d[i] = s[i];
        FW(read_fn, VUI_IMAGE1)((u32)d);       /* through the hook: placed for the current orientation */
    }
}

IMG_FN static void vui_mtd(cm_t *c, u32 kind, u32 *r)
{
    if (!c->vui_mtd) return;
    if (kind == VUI_K_IMAGE && r[0]) {
        const u8 *d = (const u8 *)r[0];
        s32_v x = *(const signed short *)(d + 0x18), y = *(const signed short *)(d + 0x1A);
        u32 sw = *(const u16 *)(d + 0x14), sh = *(const u16 *)(d + 0x16), k = 4u, i;
        if (x < 50 && y >= 430 && sw == 40u && sh == 36u) k = 0u;        /* metering */
        else if (x < 80 && y >= 392 && y < 430 && sw == 60u && sh == 32u) k = 2u;   /* battery (group 90) */
        if (k < 4u) {
            u32 *t;
            if (*(const u32 *)(d + 0x20) != 0x000000FFu) k++;   /* black outline first, then the white icon */
            t = (u32 *)(c->vui_mtd + k * VUI_MTD_SIZE);
            for (i = 0; i < VUI_MTD_SIZE / 4u; i++) t[i] = ((const u32 *)d)[i];
            c->vui_mtd_ok |= 1u << k;
            if (k < 2u) c->vui_mtd_pg = *(volatile u8 *)VUI_DISP_PAGE;
        }
        return;
    }
    if (kind != VUI_K_CLEAR || !r[0]) return;
    if (c->vui_bat_go) {                       /* group 90: the battery the full-screen clear took in the magnified view */
        const s32_v *b = (const s32_v *)r[0];
        if (b[0] < 80 && 480 - b[1] - b[3] >= 392 && 480 - b[1] - b[3] < 430) return;   /* its own clear: redrawn now */
        c->vui_bat_go = 0u;
        if ((c->vui_mtd_ok & 12u) == 12u && c->vui_zoom) vui_mtd_draw(c, 2u);
    }
    if (c->vui_mtd_go && (c->vui_mtd_ok & 3u) == 3u &&
        (s32_v)(*(volatile u32 *)CM_MS_CLOCK - c->vui_mtd_due) >= 0) {
        const s32_v *b = (const s32_v *)r[0];
        if (b[0] < 50 && 480 - b[1] - b[3] >= 430) return;   /* the icon's own clear: the firmware redraws it now */
        c->vui_mtd_go--;
        c->vui_mtd_due = *(volatile u32 *)CM_MS_CLOCK + 300u;
        if (*(volatile u8 *)VUI_DISP_PAGE != c->vui_mtd_pg) return;   /* another DISP page: maybe without the icon */
        if (c->vui_zoom) return;               /* group 90: the magnified view has no metering icon */
        c->vui_mtd_n++;
        vui_mtd_draw(c, 0u);
    }
}

#ifdef VUI_DIAG_FOCUS
/* Group 91: touch recorder (readout build). The touch page's root container hit test, wrapped; nothing changes. */
#define VUI_TP_PAGE     0xC2071154u
#define VUI_TP_ROOT_HIT 0x102BEE06u
IMG_FN static u32 vui_tp_hit(u32 obj, u32 ptp)
{
    cm_t *c = cm_get();
    u32 r = FW(find_fn, VUI_TP_ROOT_HIT)(obj, ptp), i, x = 0u;
    signed short rc[4];
    if (!c || !c->dg_buf || !ptp) return r;
    x = *(const u32 *)ptp;                     /* x | y << 16 */
    for (i = 0; i < 4u; i++) rc[i] = 0;
    if (r >= RAM_LO && r < RAM_HI && !(r & 1u))
        for (i = 0; i < 4u; i++) rc[i] = *(const signed short *)(r + 0x70u + 2u * i);
    c->vui_tp_n++;
    {   /* the press ring at 0xE00: 32 x 16 bytes, count at 0xDFC */
        u8 *t = (u8 *)c->dg_buf + 0xE00u + (*(u32 *)(c->dg_buf + 0xDFCu) & 31u) * 16u;
        *(u32 *)t = x;
        for (i = 0; i < 4u; i++) ((signed short *)(t + 4))[i] = rc[i];
        *(u16 *)(t + 12) = (r >= RAM_LO && r < RAM_HI && !(r & 1u)) ? *(const u16 *)(r + 8u) : 0u;
        t[14] = (u8)c->dg_ori; t[15] = *(volatile u8 *)VUI_UI;
        *(u32 *)(c->dg_buf + 0xDFCu) += 1u;
    }
    if (!c->dg_frozen) {
        u8 *e = (u8 *)c->dg_buf + DG_EV_OFF + c->dg_i * DG_EV_SIZE;
        c->dg_i = c->dg_i + 1u >= DG_EV_CAP ? 0u : c->dg_i + 1u;
        c->dg_n++;
        for (i = 0; i < DG_EV_SIZE / 4u; i++) ((u32 *)e)[i] = 0u;
        *(u32 *)e = *(volatile u32 *)CM_MS_CLOCK;
        e[4] = 0x44u; e[6] = (u8)c->dg_ori; e[7] = (u8)c->vui_af; e[8] = *(volatile u8 *)VUI_UI;
        e[9] = *(volatile u8 *)VUI_APP; e[11] = *(volatile u8 *)0xC2028442u;
        for (i = 0; i < 4u; i++) ((signed short *)(e + 12))[i] = rc[i];
        if (r >= RAM_LO && r < RAM_HI && !(r & 1u)) {
            for (i = 0; i < 4u; i++) ((signed short *)(e + 20))[i] = *(const signed short *)(r + 0x10u + 2u * i);
            *(u32 *)(e + 36) = *(const u32 *)(r + 8u);
            *(u32 *)(e + 40) = *(const u32 *)(r + 0x78u);
        }
        *(u32 *)(e + 28) = x; *(u32 *)(e + 32) = r; *(u32 *)(e + 44) = obj;
    }
    return r;
}
#endif

#ifdef VUI_DIAG_FOCUS
/* Group 95: read-only watch of a few state areas, to find the photo/video switch on the camera. */
#define VUI_W_WORDS 680u                 /* 40 snapshot words + 5 areas of 128 words (group 101: 0xC2028500) */
IMG_FN static void vui_dg_watch(cm_t *c)
{
    u32 *cp, i, n = 0u, k, base, len, ms = *(volatile u32 *)CM_MS_CLOCK;
    u8 *cnt;
    if (!c->dg_wbuf || !c->dg_buf || ms - c->dg_w_ms < 50u) return;
    c->dg_w_ms = ms;
    cp = (u32 *)c->dg_wbuf;
    cnt = (u8 *)(c->dg_wbuf + VUI_W_WORDS * 4u);
    for (k = 0; k < 6u; k++) {
        if (k == 0u) {
            base = FW(read_fn, VUI_SW_SNAPSHOT)(0u);
            len = 40u;
            if (base < RAM_LO || base >= RAM_HI - 0xA0u || (base & 3u)) { n += len; continue; }
        } else if (k == 1u) { base = 0xC2028300u; len = 128u; }
        else if (k == 2u) { base = 0xC2071100u; len = 128u; }   /* group 104: touch page object (was 0xC1811900) */
        else if (k == 3u) { base = 0xC1828900u; len = 128u; }
        else if (k == 4u) { base = 0xC1819C80u; len = 128u; }
        else { base = 0xC2028500u; len = 128u; }   /* group 101: 0xC20282B4 + 0x354 = focal length, 0.1 mm */
        for (i = 0; i < len; i++, n++) {
            u32 v = *(volatile u32 *)(base + i * 4u);
            u32 cap = (k == 5u && i >= 0x40u && i < 0x44u) ? 64u : 8u;
            if (c->dg_w_init && v != cp[n] && cnt[n] < cap) {
                u32 *t = (u32 *)(c->dg_buf + DG_WL_OFF + (*(u32 *)(c->dg_buf + 0xDF4u) & 511u) * 16u);
                cnt[n]++;
                t[0] = ms; t[1] = k == 0u ? (0x5u << 28) | (i * 4u) : base + i * 4u; t[2] = cp[n]; t[3] = v;
                *(u32 *)(c->dg_buf + 0xDF4u) += 1u;
            }
            cp[n] = v;
        }
    }
    c->dg_w_init = 1u;
}
#endif

/* Group 109: one touch control, given by its landscape box: placed in portrait as drawn; is the point on it? */
IMG_FN static u32 vui_t_try(cm_t *c, u32 ui, s32_v x, s32_v y, s32_v w, s32_v h, s32_v px, s32_v py)
{
    s32_v qx, qy;
    if (!vui_locate(ui, x, y, w, h, &qx, &qy)) return 0u;
    if (px < qx || px >= qx + w || py < qy || py >= qy + h) return 0u;
    c->vui_t_ox = x - qx; c->vui_t_oy = y - qy;
    return 1u;
}

/* Group 109: entry C. The panel point of a press on a portrait-placed control goes back to that control's
   landscape place; the same offset follows until release or cancel. Elsewhere nothing changes. */
IMG_FN static void vui_touch_map(cm_t *c, u8 *ev)
{
    u32 ori = c->vui_t_ori, ui = c->vui_t_ui, type = ev[9], fl = ev[8], row, col;
    s32_v X = *(const u16 *)(ev + 0x12), Y = *(const u16 *)(ev + 0x14), px, py, nx, ny;
    if (type == 9u || !ori) { c->vui_t_on = 0u; return; }
    if (!c->touch_on) { ev[9] = 9u; ev[8] = 0u; c->vui_t_on = 0u; return; }   /* group 119: portrait touch off: cancelled */
    if (ori == 2u) { px = 480 - Y; py = X; } else { px = Y; py = 720 - X; }
    if (ui == VUI_PAGE_SETTING && c->vui_det) {   /* group 116: third-level page at 2/3: the exact inverse */
        if (type == 1u && fl == 1u) {
            c->vui_t_on = py >= VUI_DET_PY ? 6u : 2u;
            if (c->vui_t_on == 6u) c->vui_t_n++;
        }
        if (c->vui_t_on == 2u) {               /* above the page: cancelled */
            ev[9] = 9u; ev[8] = 0u;
            if ((type == 1u && fl == 0u) || type == 3u) c->vui_t_on = 0u;
            return;
        }
        if (c->vui_t_on != 6u) return;
        nx = (px * 1535) >> 10; ny = ((py - VUI_DET_PY) * 1535) >> 10;
        if (nx < 0) nx = 0;
        if (nx > 719) nx = 719;
        if (ny < 0) ny = 0;
        if (ny > 479) ny = 479;
        *(u16 *)(ev + 0x12) = (u16)nx; *(u16 *)(ev + 0x14) = (u16)ny;
        if (fl == 0u || type == 3u) c->vui_t_on = 0u;
        return;
    }
    if (ui == VUI_PAGE_SETTING) {              /* group 110/112: the inverse of the setting layout, taken at the press */
        if (type == 1u && fl == 1u) {
            c->vui_t_on = 5u; c->vui_t_fit = 0u; c->vui_t_px = px;
            if (py >= VUI_PY_BAR) { c->vui_t_by = py - VUI_PY_BAR + 431; c->vui_t_bx = px + vui_bar_shift(c); }
            else if (py >= 240) {
                c->vui_t_by = py - 240;
                c->vui_t_fit = vui_is_fit(c, c->vui_t_by);
                c->vui_t_bx = c->vui_t_fit ? (px * 1535) >> 10 : px + 120 + vui_slide(c, c->vui_t_by);
            } else c->vui_t_on = 2u;           /* the title: this whole touch is cancelled */
            if (c->vui_t_on == 5u) c->vui_t_n++;
        }
        if (c->vui_t_on == 2u) {
            ev[9] = 9u; ev[8] = 0u;
            if ((type == 1u && fl == 0u) || type == 3u) c->vui_t_on = 0u;
            return;
        }
        if (c->vui_t_on != 5u) return;
        nx = px - c->vui_t_px;                 /* the finger's portrait movement along the row */
        nx = c->vui_t_bx + (c->vui_t_fit ? (nx * 1535) >> 10 : nx);
        ny = c->vui_t_by;                      /* the row of the press: no jump into the bar */
        if (nx < 0) nx = 0;
        if (nx > 719) nx = 719;
        if (ny < 0) ny = 0;
        if (ny > 479) ny = 479;
        *(u16 *)(ev + 0x12) = (u16)nx; *(u16 *)(ev + 0x14) = (u16)ny;
        if (fl == 0u || type == 3u) c->vui_t_on = 0u;
        return;
    }
    if (c->vui_adj && ui == 0u && type == 1u && ev[8] == 1u) {   /* group 111: adjust page row and back button */
        c->vui_t_on = 0u;
        if (px >= 240 - 191 && px < 240 + 191 && py >= 552 - 3 && py < 552 + 57) c->vui_t_on = 4u;
        else if (px >= 400 && px < 478 && py >= 120 && py < 166) { c->vui_t_on = 1u; c->vui_t_ox = 237; c->vui_t_oy = -115; }
    }
    if (c->vui_t_on == 4u) {                   /* exact inverse of the 7/10 placement, every event of this touch */
        nx = 360 + (((px - 240) * 1463) >> 10); ny = 355 + (((py - 552) * 1463) >> 10);
        if (nx < 0) nx = 0;
        if (nx > 719) nx = 719;
        if (ny < 0) ny = 0;
        if (ny > 479) ny = 479;
        *(u16 *)(ev + 0x12) = (u16)nx; *(u16 *)(ev + 0x14) = (u16)ny;
        if (type == 1u && fl == 1u) c->vui_t_n++;
        if (fl == 0u || type == 3u) c->vui_t_on = 0u;
        return;
    }
    if (type == 1u && ev[8] == 1u && !(c->vui_adj && ui == 0u && c->vui_t_on)) {   /* a press: which control (adjust page back button already taken) */
        c->vui_t_on = 0u;
        if (ui == 0u) {
            c->vui_t_on = vui_t_try(c, 0u, 2, 128, 80, 48, px, py) ||        /* touch shutter */
                          vui_t_try(c, 0u, 660, 128, 58, 48, px, py) ||      /* i button */
                          vui_t_try(c, 0u, 190, 368, 330, 57, px, py + 8) || /* hint row: OK / 退出, 8 px above its top too (group 111) */
                          vui_t_try(c, 0u, 128, 430, 124, 48, px, py) ||     /* shutter speed (group 110) */
                          vui_t_try(c, 0u, 266, 430, 104, 48, px, py) ||     /* aperture */
                          vui_t_try(c, 0u, 436, 430, 153, 48, px, py);       /* ISO */
        } else if (ui == 3u) {
            for (row = 0; row < 2u && !c->vui_t_on; row++)
                for (col = 0; col < 6u && !c->vui_t_on; col++)
                    c->vui_t_on = vui_t_try(c, 3u, (s32_v)col * VUI_CELL_W, VUI_GRID_TOP + (s32_v)row * VUI_CELL_H,
                                            VUI_CELL_W, VUI_CELL_H, px, py);
            if (c->vui_t_on) c->vui_ok_open = 1u;   /* group 110: a cell opens its page, as OK does */
            else c->vui_t_on = vui_t_try(c, 3u, 0, 430, 720, 50, px, py);   /* bottom row */
        }
        if (c->vui_t_on) c->vui_t_n++;
        else if (ui == 3u ||                   /* no landscape control in portrait (user) */
                 (X < 82 && Y >= 128 && Y < 176) || (X >= 660 && Y >= 128 && Y < 176) ||
                 (X >= 190 && X < 520 && Y >= 368 && Y < 425) || (X >= 128 && X < 589 && Y >= 430) ||
                 (c->vui_adj && ((X >= 88 && X < 634 && Y >= 352 && Y < 434) || (X >= 630 && Y < 60)))) {
            c->vui_t_on = 2u;                  /* the press and the rest of this touch become cancels */
            c->vui_t_x++;
        }
    }
    if (!c->vui_t_on) return;
    if (c->vui_t_on == 2u) {
        ev[9] = 9u; ev[8] = 0u;
        if ((type == 1u && fl == 0u) || type == 3u) c->vui_t_on = 0u;
        return;
    }
    nx = px + c->vui_t_ox; ny = py + c->vui_t_oy;
    if (ui == 0u && nx >= 190 && nx < 520 && ny >= 360 && ny < 425)   /* group 111: inside the hint buttons (y 373-419) */
        ny = ny < 375 ? 375 : ny > 417 ? 417 : ny;
    if (nx < 0) nx = 0;
    if (nx > 719) nx = 719;
    if (ny < 0) ny = 0;
    if (ny > 479) ny = 479;
    *(u16 *)(ev + 0x12) = (u16)nx; *(u16 *)(ev + 0x14) = (u16)ny;
    if (ev[8] == 0u || type == 3u) c->vui_t_on = 0u;   /* release or drag end: this press is over */
}

/* Group 115: the battery icon with the charge in percent. Coordinates are as the viewer sees them; o is the grip the
 * icon is shown for (0: landscape, 1/2: portrait through vui_prim). */
#define VUI_BAT_PCT 0xC2370E5Cu                /* battery-info block 0xC2370E44 + 0x18 (BatteryCheckService) */
IMG_FN static void vui_bat_rect(cm_t *c, u32 fn, u32 o, s32_v x, s32_v y, s32_v w, s32_v h, u32 col)
{
    s32_v d[7];
    if (o == 1u || o == 2u) { vui_prim(c, fn, o, x, y, w, h, col); return; }
    d[0] = x; d[1] = 480 - y - h; d[2] = w; d[3] = h; d[4] = (s32_v)col; d[5] = 0; d[6] = 0;
    c->vui_drawing = 1u;
    FW(read_fn, fn)((u32)d);
    c->vui_drawing = 0u;
}

IMG_FN static u32 vui_bat_glyph(u32 g)          /* 3 x 5, row 0 in bits 14-12; 10 is '%'. Branches, not a table. */
{
    u32 w;
    if (g >= 10u) return 0x52A5u;
    if (g >= 8u) w = 0x7BCF7BEFu;
    else if (g >= 6u) w = 0x724979EFu;
    else if (g >= 4u) w = 0x79CF5BC9u;
    else if (g >= 2u) w = 0x73CF73E7u;
    else w = 0x2C977B6Fu;
    return (g & 1u) ? w >> 16 : w & 0x7FFFu;
}

IMG_FN static void vui_bat_draw(cm_t *c, u32 o, s32_v bx, s32_v by, u32 pc)
{
    u32 col = pc <= 20u ? 0xE62E2EFFu : 0xFFFFFFFFu, g[4], n = 0u, t = 0u, i, row, k;
    s32_v x;
    vui_bat_rect(c, VUI_RECT_FN, o, bx + 2, by + 4, 52, 24, 0x80u);       /* body */
    vui_bat_rect(c, VUI_RECT_FN, o, bx, by + 2, 56, 2, col);              /* frame */
    vui_bat_rect(c, VUI_RECT_FN, o, bx, by + 28, 56, 2, col);
    vui_bat_rect(c, VUI_RECT_FN, o, bx, by + 4, 2, 24, col);
    vui_bat_rect(c, VUI_RECT_FN, o, bx + 54, by + 4, 2, 24, col);
    vui_bat_rect(c, VUI_RECT_FN, o, bx + 56, by + 11, 4, 10, col);        /* terminal */
    if (pc >= 100u) { g[n++] = 1u; g[n++] = 0u; g[n++] = 0u; }
    else {
        while (pc >= 10u) { pc -= 10u; t++; }  /* no division in card code */
        if (t) g[n++] = t;
        g[n++] = pc;
    }
    g[n++] = 10u;
    x = bx + 28 - (s32_v)(n * 12u - 3u) / 2;
    for (i = 0; i < n; i++, x += 12) {
        u32 m = vui_bat_glyph(g[i]);
        for (row = 0; row < 5u; row++) {
            u32 bits = (m >> (12u - row * 3u)) & 7u;
            for (k = 0; k < 3u; k++) {
                u32 len = 0u;
                if (!(bits & (4u >> k))) continue;
                while (k + len < 3u && (bits & (4u >> (k + len)))) len++;   /* a run in the row: one rect */
                vui_bat_rect(c, VUI_RECT_FN, o, x + (s32_v)k * 3, by + 8 + (s32_v)row * 3, (s32_v)len * 3, 3, col);
                k += len - 1u;
            }
        }
    }
#ifdef VUI_DIAG_FOCUS
    c->vui_bat_n++;
#endif
}

/* Group 117: one piece of a portrait drag paste. Texture half k (copy k); viewer interval [a, b) of the strip as the
 * copy holds it goes to viewer [da, da + b - a) on the current layer; the row band is panel x [bx, bx + bh). */
IMG_FN static void vui_drag_blit(cm_t *c, const u8 *s, u32 ori, u32 k, s32_v bx, s32_v bh, s32_v a, s32_v b, s32_v da)
{
    u32 d[0x5Cu / 4u], i;
    s32_v ox = (s32_v)(c->vui_cp[k * 2u] & 0xFFFFu), oy = (s32_v)(c->vui_cp[k * 2u] >> 16);
    s32_v tx = (s32_v)(signed short)(c->vui_cp[k * 2u + 1u] & 0xFFFFu);   /* copy's box x0 in the texture */
    s32_v sy0, sy1, dy0, dy1, sx0;
    if (b <= a) return;
    if (ori == 2u) { sy0 = 480 - b; sy1 = 480 - a; dy0 = 480 - (da + b - a); dy1 = 480 - da; }
    else { sy0 = a; sy1 = b; dy0 = da; dy1 = da + b - a; }
    sx0 = bx - ox + tx;
    for (i = 0; i < sizeof d / 4u; i++) d[i] = 0u;
    d[1] = *(const u32 *)(s + 4);              /* pixel format */
    d[2] = *(const u32 *)(s + 8);              /* texture */
    d[3] = *(const u32 *)(s + 12);
    d[4] = ((u32)sx0 & 0xFFFFu) | ((u32)(sy0 - oy) << 16);          /* source origin */
    d[5] = ((u32)(sx0 + bh) & 0xFFFFu) | ((u32)(sy1 - oy) << 16);   /* source end */
    d[8] = 0xFFFFFFFFu;
    ((u16 *)d)[12] = (u16)bx; ((u16 *)d)[13] = (u16)dy0; ((u16 *)d)[14] = (u16)(bx + bh); ((u16 *)d)[15] = (u16)dy1;
    d[0x24u / 4u] = *(const u32 *)(s + 0x2C);  /* flip */
    d[0x2Cu / 4u] = *(const u32 *)(s + 0x28);  /* blending */
    ((u8 *)d)[0x44] = s[0x30];
    ((u8 *)d)[0x45] = s[0x31];
    d[0x48u / 4u] = *(const u32 *)(s + 0x34);
    d[0x4Cu / 4u] = *(const u32 *)(s + 0x38);
    d[0x50u / 4u] = *(const u32 *)(s + 0x3C);
    c->vui_drawing = 1u;                       /* our own call passes the hooks untouched */
    FW(read_fn, VUI_IMAGE1)((u32)d);
    c->vui_drawing = 0u;
    c->vui_dr_n++;
}

/* Group 117: the stock paste (renderer 3) of a dragged strip, done for the portrait layout. Landscape: dest x =
 * x0 + (t - w0) for texture position t in the window [w0, w1); half k holds OSD x as t = x - ox_k + tx_k. */
IMG_FN static void vui_drag_paste(cm_t *c, u32 ori, const u8 *s)
{
    s32_v x0 = *(const s32_v *)(s + 0x18) >> 16, y0 = *(const s32_v *)(s + 0x1C) >> 16, y1 = *(const s32_v *)(s + 0x24) >> 16;
    s32_v w0 = (s32_v)(*(const u32 *)(s + 0x10) & 0xFFFFu), w1 = (s32_v)(*(const u32 *)(s + 0x14) & 0xFFFFu);
    s32_v bh = y1 - y0, py0 = y0 + 240, bx, k;
    c->vui_ps[0] = *(const u32 *)(s + 0x10); c->vui_ps[1] = *(const u32 *)(s + 0x14);
    if (bh <= 0 || w1 <= w0) return;
    bx = ori == 2u ? py0 : 720 - py0 - bh;     /* the row band across the panel (vui_panel_rect) */
    for (k = 0; k < 3; k++) {                  /* group 118: up to three copies (0-600, 600-1200, 1200-1800) */
        s32_v ox;
        if (!(c->vui_cp_ok & (1u << k))) continue;
        ox = (s32_v)(c->vui_cp[k * 2u] & 0xFFFFu);
        s32_v tx0 = (s32_v)(signed short)(c->vui_cp[k * 2u + 1u] & 0xFFFFu), tx1 = (s32_v)(signed short)(c->vui_cp[k * 2u + 1u] >> 16);
        s32_v t0 = w0 > tx0 ? w0 : tx0, t1 = w1 < tx1 ? w1 : tx1, xa, xb, sh;
        if (t1 <= t0) continue;
        xa = t0 + ox - tx0; xb = t1 + ox - tx0;    /* OSD x the copy took this part from */
        sh = x0 - w0 + tx0 - ox;                   /* landscape shift of this half's content */
        vui_drag_blit(c, s, ori, (u32)k, bx, bh, VUI_FIT(xa), VUI_FIT(xb), VUI_FIT(xa + sh));
    }
}

IMG_FN static void vui_hook(u32 kind, u32 *r)
{
    cm_t *c = cm_get();
    u32 ori, ui, app, active, now, sub, setting;
#ifdef VUI_DIAG_FOCUS
    u8 *dge = 0;
#endif
    if (c && VUI_IS_TOUCH(kind)) {             /* group 109: touch first, on the touch task: map, record, return */
        u8 *ev = (u8 *)r[0];
#ifdef VUI_DIAG_FOCUS
        u8 *t = 0;
        if (c->dg_buf) ((u32 *)(c->dg_buf + 0x50u))[kind]++;   /* group 112: census in the DIAG header */
        if (c->dg_buf && ev) {
            t = (u8 *)c->dg_buf + DG_TE_OFF + (*(u32 *)(c->dg_buf + 0xDF8u) & 255u) * 16u;
            *(u32 *)t = *(volatile u32 *)CM_MS_CLOCK;
            *(u16 *)(t + 4) = *(const u16 *)(ev + 0x12); *(u16 *)(t + 6) = *(const u16 *)(ev + 0x14);
            t[8] = (u8)((ev[8] & 0x1Fu) | (kind >= VUI_K_TOUCH2 ? 0x80u >> (kind - VUI_K_TOUCH2) : 0u));   /* entry B/C/D, no table */
            t[9] = ev[9]; t[10] = (u8)c->vui_t_ui; t[11] = (u8)c->vui_t_ori;
            *(u32 *)(c->dg_buf + 0xDF8u) += 1u;
        }
#endif
        if (kind == VUI_K_TOUCH3 && ev && c->vui) vui_touch_map(c, ev);
#ifdef VUI_DIAG_FOCUS
        if (t) { *(u16 *)(t + 12) = *(const u16 *)(ev + 0x12); *(u16 *)(t + 14) = *(const u16 *)(ev + 0x14); }
#endif
        return;
    }
#ifdef VUI_DIAG_FOCUS
    if (c) {                                   /* group 106: census and the key/touch ring, before any early return */
        if (kind < 20u && c->dg_buf) ((u32 *)(c->dg_buf + 0x50u))[kind]++;
        if (c->dg_buf && kind == VUI_K_KEY) {
            u32 *t = (u32 *)(c->dg_buf + DG_KR_OFF + (*(u32 *)(c->dg_buf + 0xDF0u) & 127u) * 64u), k;
            t[0] = *(volatile u32 *)CM_MS_CLOCK; t[1] = kind | (u32)*(volatile u8 *)VUI_UI << 8 | (u32)*(volatile u8 *)VUI_APP << 16;
            for (k = 0; k < 6u; k++) t[2u + k] = r[k];   /* r0-r3, r12, lr */
            for (k = 0; k < 8u; k++) t[8u + k] = r[6u + k];   /* stack above the entry frame */
            *(u32 *)(c->dg_buf + 0xDF0u) += 1u;
        }
    }
    if (c && c->vui) dge = vui_dg_in(c, kind, r);
#endif
    if (!c || c->vui_drawing) return;
#ifdef VUI_DIAG_FOCUS
    vui_dg_watch(c);                           /* group 95: every app, frozen or not */
#endif
    if (!custom_hide_native(c) && c->focal_on && *(volatile u8 *)VUI_APP == 3u && *(volatile u32 *)0xC2028608u) {   /* group 102: stock focal text for every CPU lens; menu switch (group 104) */
        u32 dsc = *(const u32 *)(0x10748504u + 0x3Cu * 4u);
        if (dsc >= RAM_LO && dsc < RAM_HI && !(dsc & 3u) && *(volatile u8 *)(dsc + 0x28u)) {
            u32 el = *(volatile u32 *)(dsc + 0x2Cu);
            if (el >= RAM_LO && el < RAM_HI && !(el & 3u) && *(volatile u8 *)(el + 1u) == 0u) {
                *(volatile u8 *)(el + 1u) = 1u;
                c->vui_ff_n++;
            }
        }
    }
    if (c->vui_half && *(volatile u32 *)CM_MS_CLOCK - c->vui_half_ms >= VUI_HALF_MS) {
        c->vui_half = 0u;                      /* before the switch check: never leave it pressed */
        vui_half_set(c, 0u);
    }
    {   /* group 116: nothing drawn for 2 s (screen off, standby): repaint the shooting page once it draws again */
        u32 ms = *(volatile u32 *)CM_MS_CLOCK;
        u32 set = 0u;
        if (c->vui_wk_ms && ms - c->vui_wk_ms >= 2000u) { c->vui_wk_due = (ms + 600u) | 1u; set = 1u; c->vui_wk_st += 1u; c->vui_wk_two = 1u; }
        else if (!c->vui_wk_ms) { c->vui_wk_due = (ms + 1500u) | 1u; set = 1u; c->vui_wk_st += 1u; c->vui_wk_two = 1u; }   /* group 117: start-up and wake (record 01) */
        if (set) c->vui_wk_t0 = ms;
        c->vui_wk_ms = ms | 1u;
        (void)set;
        if (kind == VUI_K_KEY && r[0] && r[0] != 0x13u) {   /* the user acts */
            if (c->vui_wk_due) c->vui_wk_st += 1u << 16;
            c->vui_wk_due = 0u; c->vui_wk_two = 0u; c->vui_wk_key = ms | 1u;
        }
        if (c->vui_wk_due && (s32_v)(ms - c->vui_wk_due) >= 3000) { c->vui_wk_due = 0u; c->vui_wk_st += 1u << 24; }   /* overdue */
    }
    /* The experimental detailed-page histogram is independent of the portrait-layout switch.
       Track native erases even with that layout disabled; drawing remains in the level callback. */
    if (c->vui_hist_bank && *(volatile u8 *)VUI_TARGET == VUI_TARGET_OSD) {
        if (*(volatile u8 *)VUI_DISP_PAGE != 0u || *(volatile u8 *)VUI_APP != 3u ||
            *(volatile u8 *)VUI_UI != 0u) {
            if (c->vui_hist_on) vui_hist(c, VUI_K_CLEAR, c->vui_hist_ori, 0u, *(volatile u8 *)VUI_UI);
            c->vui_hist_bank = 0u;
        } else if (c->vui_hist_on && kind == VUI_K_CLEAR && r[0]) {
            const s32_v *b = (const s32_v *)r[0];
            s32_v d[4];
            vui_hist_rect(c->vui_hist_ori, c->vui_hist_x, c->vui_hist_y, VUI_HW, VUI_HH, d);
            if (b[2] > 0 && b[3] > 0 && b[0] < d[0] + d[2] && d[0] < b[0] + b[2] &&
                b[1] < d[1] + d[3] && d[1] < b[1] + b[3]) c->vui_hist_dirty = 1u;
        }
    }
    if (!c->vui) { c->vui_t_ori = 0u; return; }
#ifdef VUI_DIAG_FOCUS
    if (c->vui_tp_fn && *(volatile u32 *)(VUI_TP_PAGE + 0x24u)) {   /* group 91: wrap the touch page's root */
        u32 root = *(volatile u32 *)(VUI_TP_PAGE + 0x0Cu);
        if (root >= RAM_LO && root < RAM_HI && !(root & 3u) && *(volatile u32 *)(root + 0x84u) == (VUI_TP_ROOT_HIT | 1u)) {
            *(volatile u32 *)(root + 0x84u) = c->vui_tp_fn;
            c->vui_tp_i++;
        }
    }
#endif
    ori = FW(read_fn, VUI_ORI_GET)(0u);
#ifdef VUI_DIAG_FOCUS
    c->dg_ori = ori;
#endif
    ui = *(volatile u8 *)VUI_UI;
    app = *(volatile u8 *)VUI_APP;
    if (app == 3u && (ui == 3u || ui == 1u || ui == 10u || (ui >= 11u && ui <= 13u))) {   /* group 111: i menu and adjust pages keep their grip */
        u32 raw = ori, ms = *(volatile u32 *)CM_MS_CLOCK;
        if (!c->vui_lk) c->vui_lk = 0x100u | ori;
        ori = c->vui_lk & 0xFFu;
        if (c->vui && ui == 3u && !c->vui_sub && raw <= 2u && raw != ori && !c->vui_ro) {   /* group 112: the grid follows the grip */
            if (c->vui_ro_o != (0x100u | raw)) { c->vui_ro_o = 0x100u | raw; c->vui_ro_ms = ms; }
            else if (ms - c->vui_ro_ms >= 400u) {
                c->vui_ro = 1u; c->vui_ro_ms = ms; c->vui_ro_o = 0u;
                if (!c->vui_half && vui_half_set(c, 1u)) {   /* close it with a half-press, as on the shooting page */
                    c->vui_half = 1u;
                    c->vui_half_ms = ms;
                }
            }
        } else if (raw == ori) c->vui_ro_o = 0u;
    } else if (app == 3u) c->vui_lk = 0u;
    if (c->vui_ro && (ui != 3u || *(volatile u32 *)CM_MS_CLOCK - c->vui_ro_ms >= 2000u)) c->vui_ro = 0u;   /* closed, or gave up */
#ifdef VUI_DIAG
    if (kind != VUI_K_KEY && kind != VUI_K_SETTER) vui_diag(c, kind, r, app, ui);
#endif
    {   /* group 100: the quick-adjust strip (ui_mode 9) is laid out on top of the shooting page */
        u32 strip = app == 3u && ui == 9u, adj = app == 3u && ui >= 11u && ui <= 13u;
        if (strip != c->vui_strip) { c->vui_strip = strip; c->vui_strip_end = !strip && (ori == 1u || ori == 2u); }
        if (strip) ui = 0u;
        c->vui_adj = adj;                      /* group 111: the adjust pages are the shooting page plus their row */
        if (adj) ui = 0u;
    }
#ifdef VUI_DIAG_FOCUS
#endif
    if (kind == VUI_K_FILL) {                  /* observed only: the fill stays where it is drawn */
        const s32_v *f = (const s32_v *)r[0];
        if (app == 3u && ui == 0u && (ori == 1u || ori == 2u) && r[0]) {   /* on the dispatcher's stack */
            c->vui_fill_n++;
            if (!f[5] && vui_is_focus_color((u32)f[4])) {   /* +0x14 clear: no other entry sees this one */
                c->vui_fill_red++;
                vui_focus_note(c, f[0], 480 - f[1] - f[3], f[2], f[3]);
            }
        }
        return;
    }
    if (kind >= VUI_K_PATH1) {                 /* path entries are only observed: they draw in place */
        if (app == 3u && ui == 0u && (ori == 1u || ori == 2u)) {
            c->vui_pk[kind - VUI_K_PATH1]++;
            if (kind == VUI_K_PATH3 && r[0] >= 0xC0000000u) {
                const u8 *d = (const u8 *)r[0];
                vui_pl_note(c, *(const signed short *)(d + 0x0C), *(const signed short *)(d + 0x0E),
                            *(const u16 *)(d + 0x10), *(const u16 *)(d + 0x12));
            }
        }
        return;
    }
    if (app == 3u && ui == 0u) c->vui_ok_open = 0u;
    /* White balance is ui 10 and Picture Control ui 1 (camera readout); opened from the i menu they are setting pages. */
    setting = app == 3u && (ui == 1u || ui == 10u) && c->vui_ok_open;
    /* The viewfinder has its own layout. Targets 15 and up were only drawn to in the viewfinder (camera readout). */
    {
        u32 target = *(volatile u8 *)VUI_TARGET, ms = *(volatile u32 *)CM_MS_CLOCK;
        if (target >= 15u && target < 32u && kind == VUI_K_IMAGE2 && r[0] &&
            *(const signed short *)(r[0] + 0x1Cu) >= 1000) { c->vui_evf = 1u; c->vui_evf_ms = ms; }   /* group 117 */
        else if (c->vui_evf && (ms - c->vui_evf_ms >= 3000u || (target == VUI_TARGET_OSD && ms - c->vui_evf_ms >= 300u)))
            c->vui_evf = 0u;                   /* group 111: back on the screen: no 3 s wait (capture 36) */
    }
    active = (ori == 1u || ori == 2u) && !vui_video(c) && *(volatile u8 *)VUI_TARGET != 10u && !c->vui_evf &&
             ((app == 3u && (ui == 0u || ui == 3u)) || setting);
    c->vui_t_ori = active ? ori : 0u;          /* group 109: read by the touch entry */
    if (kind == VUI_K_KEY) {
        if (app == 3u && active) vui_key(c, ori, r);
        return;
    }
    /* Only the on-screen layer. Anything else is an off-screen buffer that is later pasted as one image;
       turning its contents as well left white bars and noise in the dial strip's value row. */
    if (kind == VUI_K_CLEAR && r[0]) {         /* group 94: full clears of the screen buffers end the magnified view */
        const s32_v *fb = (const s32_v *)r[0];
        u32 tg = *(volatile u8 *)VUI_TARGET;
        if ((tg == 8u || tg == 9u) && fb[0] == 0 && fb[1] == 0 && fb[2] >= 720 && fb[3] >= 480) c->vui_zoom = 0u;
    }
    if (kind == VUI_K_RECT && r[0] && c->vui_zoom) {   /* group 95: a bottom box (shutter, aperture, ISO) is back */
        const s32_v *q = (const s32_v *)r[0];
        if (q[0] >= 120 && q[0] < 450 && 480 - q[1] - q[3] >= 425 && q[2] >= 80 && q[3] >= 40) c->vui_zoom = 0u;
    }
    {   /* group 117: the stock's two copies of the screen into the drag texture (target 7) */
        u32 tg = *(volatile u8 *)VUI_TARGET;
        if (!(c->vui_sub || setting)) c->vui_cp_ok = 0u;
        else if (tg == 7u && kind == VUI_K_IMAGE && r[0] && app == 3u && active) {
            const u8 *s = (const u8 *)r[0];
            s32_v bx0 = *(const signed short *)(s + 0x18), bx1 = *(const signed short *)(s + 0x1C);
            u32 k = bx0 < 300 ? 0u : bx0 < 900 ? 1u : 2u;   /* group 118: three slots (record 48/2) */
            c->vui_cp[k * 2u] = *(const u32 *)(s + 0x10);
            c->vui_cp[k * 2u + 1u] = ((u32)bx0 & 0xFFFFu) | ((u32)bx1 << 16);
            c->vui_cp_ok |= 1u << k;
        }
    }
    {   /* group 114: the dragged-strip paste on layer 9, 11 or 12 (a slice of the portrait screen) is not shown */
        u32 tg = *(volatile u8 *)VUI_TARGET;
        if ((tg == 9u || tg == 11u || tg == 12u) && app == 3u && active && (c->vui_sub || setting || c->vui_adj) &&
            r[0] && (kind == VUI_K_IMAGE || kind == VUI_K_IMAGE3)) {
            u8 *s = (u8 *)r[0];
            s32_v x0, y0, x1, y1;
            if (kind == VUI_K_IMAGE3) {
                x0 = *(const s32_v *)(s + 0x18) >> 16; y0 = *(const s32_v *)(s + 0x1C) >> 16;
                x1 = *(const s32_v *)(s + 0x20) >> 16; y1 = *(const s32_v *)(s + 0x24) >> 16;
            } else {                           /* renderer 1: box at +0x18 (x0, y0, x1, y1); +0x14 is the source size */
                x0 = *(const signed short *)(s + 0x18); y0 = *(const signed short *)(s + 0x1A);
                x1 = *(const signed short *)(s + 0x1C); y1 = *(const signed short *)(s + 0x1E);
            }
            if (y0 >= 250 && y1 <= 440 && y1 - y0 > 0 && y1 - y0 <= 140 && x1 - x0 >= 200) {
                if (kind == VUI_K_IMAGE3 && (c->vui_cp_ok & 2u) && (ori == 1u || ori == 2u) &&
                    vui_is_fit(c, y0)) vui_drag_paste(c, ori, s);   /* group 117: animated as the stock does */
                r[0] = 0u;                     /* both entries skip the stock draw when r0 is cleared */
            }
            return;
        }
    }
    if (kind != VUI_K_SETTER && *(volatile u8 *)VUI_TARGET != VUI_TARGET_OSD) return;
    if (kind != VUI_K_SETTER) {
        u32 hist_active = active || (ori == 0u && !c->custom_disp_rc &&
                                    *(volatile u8 *)VUI_DISP_PAGE == 0u &&
                                    !vui_video(c) && !c->vui_evf &&
                                    app == 3u && (ui == 0u || ui == 3u));
        vui_hist(c, kind, ori, hist_active && app == 3u, ui);
    }
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
    c->vui_t_ui = sub ? VUI_PAGE_SETTING : ui;   /* group 111: i-menu sub pages are drawn with the setting layout */
    if (!sub || app != 3u) c->vui_det = 0u;    /* group 116: third-level page (capture 45) */
    else if ((kind == VUI_K_RECT || kind == VUI_K_CLEAR) && r[0] && *(volatile u8 *)VUI_TARGET == VUI_TARGET_OSD) {
        const s32_v *bd = (const s32_v *)r[0];
        if (bd[3] >= 400 && bd[2] < 700) c->vui_det = 1u;
        else if (bd[0] == 0 && bd[2] >= 720 && bd[3] >= 100 && bd[3] < 480) c->vui_det = 0u;
    }
    c->vui_fit = sub;                          /* group 117: i-menu strips too (record 10: ends cut off) */
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
        if (c->vui_hist_bank) c->vui_hist_dirty = 1u;
        b[0] = 0; b[1] = 0; b[2] = 720; b[3] = 480;
        c->vui_mtd_go = 2u;                    /* group 88: the metering icon again, 500 and 800 ms later */
        c->vui_mtd_due = *(volatile u32 *)CM_MS_CLOCK + 500u;
        c->vui_bat_go = c->vui_zoom;           /* group 90: and the battery at once in the magnified view */
        return;
    }
    if (app == 3u && ui == 0u && !sub) vui_mtd(c, kind, r);
    if (c->vui_wk_due && kind == VUI_K_CLEAR && app == 3u && ui == 0u && !sub &&
        (s32_v)(*(volatile u32 *)CM_MS_CLOCK - c->vui_wk_due) >= 0 &&
        (!c->vui_wk_key || *(volatile u32 *)CM_MS_CLOCK - c->vui_wk_key >= 1000u)) {   /* group 116; 117: no key for 1 s */
        c->vui_wk_due = 0u;
        if (c->vui_bat_seen && (s32_v)(c->vui_bat_seen - c->vui_wk_t0) >= 0) {   /* the battery is up: nothing to repaint */
            c->vui_wk_two = 0u;
            c->vui_wk_st += 1u << 24;
        } else {
        c->vui_wk_st += 1u << 8;               /* group 118: counted */
        if (c->vui_wk_two) {                   /* group 118: once more 2 s later (the battery state may come late) */
            c->vui_wk_two = 0u;
            c->vui_wk_due = (*(volatile u32 *)CM_MS_CLOCK + 2000u) | 1u;
        }
        vui_repaint(c);
        }
    }
    if (app == 3u && ui == 0u && !sub && kind == VUI_K_IMAGE && r[0] &&
        *(volatile u8 *)VUI_TARGET == VUI_TARGET_OSD) {   /* group 115: the battery icon with its percent */
        const u8 *d = (const u8 *)r[0];
        s32_v x = *(const signed short *)(d + 0x18), y = *(const signed short *)(d + 0x1A);
        u32 pc = *(volatile u32 *)VUI_BAT_PCT;
        if (x < 80 && y >= 392 && y < 430 && *(const u16 *)(d + 0x14) == 60u && *(const u16 *)(d + 0x16) == 32u)
            c->vui_bat_seen = *(volatile u32 *)CM_MS_CLOCK | 1u;   /* group 118: the stock drew the battery */
        if (c->bat_on && x < 80 && y >= 392 && y < 430 && *(const u16 *)(d + 0x14) == 60u && *(const u16 *)(d + 0x16) == 32u &&
            pc <= 100u) {
            r[0] = 0u;                         /* both stock calls (outline, icon): the module draws it */
            if (*(const u32 *)(d + 0x20) != 0x000000FFu) {
                u32 o = active ? ori : 0u;
                if (o) { x += 416; y += 60; }  /* as vui_locate_lv places the battery */
                vui_bat_draw(c, o, x, y, pc);
                c->vui_bat_xy = ((u32)x & 0xFFFFu) | ((u32)y << 16);
                c->vui_bat_k = 1u | o << 1 | (u32)*(volatile u8 *)VUI_DISP_PAGE << 8;
                c->vui_bat_st = c->vui_state; c->vui_bat_pc = pc;
                c->vui_bat_ms = *(volatile u32 *)CM_MS_CLOCK;
            }
            return;
        }
    }
    if (kind == VUI_K_CLEAR && app == 3u && ui == 0u && !sub && c->bat_on && (c->vui_bat_k & 1u) &&
        *(volatile u8 *)VUI_TARGET == VUI_TARGET_OSD &&
        *(volatile u32 *)CM_MS_CLOCK - c->vui_bat_ms >= 1000u) {   /* group 115: a new charge, the icon still up */
        u32 pc = *(volatile u32 *)VUI_BAT_PCT, o = (c->vui_bat_k >> 1) & 3u;
        c->vui_bat_ms = *(volatile u32 *)CM_MS_CLOCK;
        if (c->vui_state != c->vui_bat_st || ((c->vui_bat_k >> 8) & 0xFFu) != *(volatile u8 *)VUI_DISP_PAGE)
            c->vui_bat_k = 0u;                 /* another layout or page: wait for the stock to draw it again */
        else if (pc <= 100u && pc != c->vui_bat_pc) {
            s32_v bx = (s32_v)(signed short)(c->vui_bat_xy & 0xFFFFu), by = (s32_v)(c->vui_bat_xy >> 16);
            vui_bat_rect(c, VUI_CLEAR_FN, o, bx, by, 60, 32, 0u);
            vui_bat_draw(c, o, bx, by, pc);
            c->vui_bat_pc = pc;
        }
    }
    if (app == 3u && ui == 0u && !sub && kind == VUI_K_IMAGE && r[0] && active &&
        !vui_is_focus_color(*(const u32 *)(r[0] + 0x20u))) vui_ic_note(c, (const u8 *)r[0]);   /* group 96 */
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
    if (!active) {
        /* Landscape repair/drawing is performed by vui_hist_before_level, never by an arbitrary clear. */
        return;
    }
    if (kind == VUI_K_CLEAR && ui == 0u && app == 3u) {
        u32 ms = *(volatile u32 *)CM_MS_CLOCK;
        if (ms - c->vui_af_ms >= 150u) {
            u32 m = FW(read_fn, SETTING_READ)(0x56Cu) & 0xFFu;   /* AF-area mode on the Z5 (0x56D is the focus mode) */
            c->vui_af_ms = ms;
            if (c->vui_af_ok && m != c->vui_af && VUI_AF_WINDOW) {
                u32 k;
                if (!c->vui_af_rep) c->vui_af_cut = 0u;   /* a window still open keeps its cut */
                c->vui_af_win = ms + 500u; c->vui_af_m++; c->vui_af_rep = 1u;
                for (k = 0; k < 16u; k++) {   /* clears moved in the last 300 ms, before the change was seen */
                    u32 *e = c->vui_mv + k * 5u;
                    if (e[2] && ms - e[4] < 300u &&
                        vui_af_corner((s32_v)e[0], 480 - (s32_v)e[1] - (s32_v)e[3], (s32_v)e[2], (s32_v)e[3])) {
                        s32_v d[7];
                        if (vui_occ_hit(c, (s32_v)e[0], 480 - (s32_v)e[1] - (s32_v)e[3], (s32_v)e[2], (s32_v)e[3]))
                            c->vui_af_cut = 1u;
                        d[0] = (s32_v)e[0]; d[1] = (s32_v)e[1]; d[2] = (s32_v)e[2]; d[3] = (s32_v)e[3];
                        d[4] = 0; d[5] = 0; d[6] = 0;
                        c->vui_af_n++;
                        c->vui_drawing = 1u;
                        FW(read_fn, 0x104A641Eu)((u32)d);
                        c->vui_drawing = 0u;
                    }
                }
            }
            if (c->vui_af_ok && m != c->vui_af && !VUI_AF_WINDOW) c->vui_af_m++;
            if (c->vui_af_ok && m != c->vui_af && c->vui_af == 6u && 0) { c->vui_arm_n = 2u;   /* group 93: off, the lines are erased in place */ c->vui_arm_due = ms; }
#ifdef VUI_DIAG_FOCUS
            if (c->vui_af_ok && m != c->vui_af) vui_dg_mark(c, c->vui_af, m);
#endif
            c->vui_af = m;
            c->vui_af_ok = 1u;
        }
        if (c->vui_strip_end) {                /* group 100: the strip has closed: its portrait area, then the icons it crossed */
            s32_v sb[7];
            c->vui_strip_end = 0u;
            vui_panel_rect(ori, 18, 308, 430, 90, sb);   /* the strip's portrait area (px 21-443, py 312-391) */
            sb[4] = 0; sb[5] = 0; sb[6] = 0;
            vui_ic_redo(c, ori, sb);
            c->vui_hist_dirty = 1u;
        }
        if (c->vui_arm_n && (s32_v)(ms - c->vui_arm_due) >= 0) {   /* group 81: bracket arms, in place */
            vui_arm_clear(c);
            c->vui_arm_n--;
            c->vui_arm_due = ms + 400u;
            if (!c->vui_mtd_go) { c->vui_mtd_go = 1u; c->vui_mtd_due = ms + 30u; }   /* the top-left strip crosses the metering icon */
        }
        if (c->vui_af_rep && (s32_v)(ms - c->vui_af_win) >= 0) {
            c->vui_af_rep = 0u;                /* the extra clears cut portrait items: redraw them */
            if (c->vui_af_cut) {
                c->vui_af_cut = 0u;
                c->vui_af_p++;
                vui_repaint(c);
            }
        }
    }
#ifdef VUI_DIAG_FOCUS
    u32 diag_lv = kind == VUI_K_CLEAR && app == 3u && ui == 0u && !sub && (ori == 1u || ori == 2u) && r[0];
    s32_v dx = 0, dy = 0, dw = 0, dh = 0;
    if (diag_lv) {                             /* the clear's own box, before the layout moves it */
        const s32_v *b0 = (const s32_v *)r[0];
        dx = b0[0]; dy = b0[1]; dw = b0[2]; dh = b0[3];
    }
#endif
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
#ifdef VUI_DIAG_FOCUS
    if (dge) vui_dg_out(dge, kind, r);
#endif
    if (kind == VUI_K_CLEAR && c->vui_hist_due && *(volatile u8 *)VUI_DISP_PAGE != 0u)
        vui_hist_draw(c, (s32_v *)r[0]);
}
