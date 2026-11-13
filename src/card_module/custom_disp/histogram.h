/* Original CPU implementation of the checked native LCD/EVF histogram layouts.
 * No firmware code, resources or copied instructions are embedded. */
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
        level_sparse_mark(level_data()->surface, y * level_data()->width + left,
                          level_data()->width, level_data()->height);
        level_sparse_mark(level_data()->surface, y * level_data()->width + left + width - 1u,
                          level_data()->width, level_data()->height);
    }
}

Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2")))
static u32 histogram_paint(volatile const u32 *packet)
{
    level_state *l = level_data();
    /* The shared tail callback supplied this surface after GPU sync and
     * bank revalidation; never reopen the old independent EVF setup path. */
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
