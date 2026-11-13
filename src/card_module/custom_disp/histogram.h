/* Original CPU implementation of the checked native LCD/EVF histogram layouts.
 * No firmware code, resources or copied instructions are embedded. */
Z5_IMAGE(custom_disp_image) static u32 histogram_information_visible(void)
{
    /* DISP-zero-only groups use mask one. Their payload visibility is native
     * state, after native filtering, rather than an inferred button value. */
    for (u32 i = 0; i < 64u; i++) {
        u32 address = Z5_LEVEL_INFO_TABLE[i];
        if ((address & 3u) || address < Z5_OVERLAY_RAM_LOW ||
            address > Z5_OVERLAY_RAM_HIGH - 48u) return 0;
        volatile const u32 *descriptor = (volatile const u32 *)address;
        /* Normal-scene mask words are per output: descriptor[1] for LCD,
         * descriptor[3] for EVF. Higher bits describe other shooting scenes;
         * they do not identify different physical outputs. */
        u32 mask = descriptor[Z5_OVERLAY_OUTPUT == Z5_CUSTOM_DISP_EVF_PASS ? 3u : 1u];
        if (mask != 1u) continue;
        u32 count = ((volatile const u8 *)address)[40], payload = descriptor[11];
        if (count > 96u || (count && ((payload & 3u) || payload < Z5_OVERLAY_RAM_LOW ||
                                   payload > Z5_OVERLAY_RAM_HIGH - count * 48u))) return 0;
        for (u32 j = 0; j < count; j++) {
            u32 visible = ((volatile const u8 *)payload)[j * 48u + 1u];
            if (visible > 1u) return 0;
            if (visible) return 1;
        }
    }
    return 0;
}

Z5_IMAGE(custom_disp_image) static u32 histogram_pixel(u32 index)
{
    u32 rgba = Z5_LEVEL_FAST_COLOR(index);
    return (rgba << 24) | ((rgba & 0xff00u) << 8) |
           ((rgba >> 8) & 0xff00u) | (rgba >> 24);
}

Z5_IMAGE(custom_disp_image) static void histogram_fill(u32 left, u32 top, u32 width, u32 height, u32 pixel)
{
    volatile u32 *surface = (volatile u32 *)level_data()->surface;
    for (u32 y = top; y < top + height; y++) {
        volatile u32 *row = surface + y * level_data()->width + left;
        for (u32 x = 0; x < width; x++) row[x] = pixel;
    }
}

Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2")))
static u32 histogram_paint(volatile const u32 *packet)
{
    level_state *l = level_data();
    level_prepare();
    if (!l->surface) { l->reason = 32; return 0; }
    u32 left = Z5_LEVEL_HIST_RECT(0), top = Z5_LEVEL_HIST_RECT(1);
    u32 right = Z5_LEVEL_HIST_RECT(2), bottom = Z5_LEVEL_HIST_RECT(3);
    u32 scale = packet[256];
    if (right > l->width || bottom > l->height || right < left || bottom < top ||
        right - left != 200u || bottom - top != 101u || !scale) {
        l->reason = 16; return 0;
    }
    level_backing *slots = (level_backing *)l->backing;
    for (u32 i = 0; i < 4u; i++) {
        if (slots[i].surface == l->surface && slots[i].count) {
            l->reason = 64; return 0;
        }
    }
    /* Use the LCD raster unchanged, anchored at the checked EVF native origin.
     * It fits inside the original 200x101 box in every supported EVF mode. */
#define HIST_FILL(x,y,w,h,p) histogram_fill(left + (x), top + (y), (w), (h), (p))
#define HIST_COLOR(i) histogram_pixel(i)
#define HIST_BIN(i) packet[i]
#define HIST_DIV(n,d) level_div((n),(d))
#include "histogram_raster.h"
#undef HIST_FILL
#undef HIST_COLOR
#undef HIST_BIN
#undef HIST_DIV
    return 1;
}
