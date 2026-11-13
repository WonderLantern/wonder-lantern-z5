/* Coverage-based strokes, composited once after all four moving segments.
 * Fixed reference bars are omitted to avoid duplicating native grid lines. */

Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2")))
static void level_cover(s32 x, s32 y, u32 pixel)
{
    if (x < 0 || (u32)x >= level_data()->width || y < 0 || (u32)y >= level_data()->height) return;
    level_state *l = level_data();
    level_backing *slot = (level_backing *)l->current;
    u32 offset = (u32)y * l->width + (u32)x;
    u32 hash = (offset * 2654435761u) >> LEVEL_HASH_SHIFT, index;
    for (;;) {
        index = slot->lookup[hash];
        if (!index || slot->pixels[index - 1u].offset == offset) break;
        hash = (hash + 1u) & (LEVEL_HASH_LIMIT - 1u);
    }
    if (!index) {
        u32 under = ((volatile const u32 *)l->surface)[offset];
        if ((under >> 24) == 255u) return;
        if (slot->count >= LEVEL_PIXEL_LIMIT) { l->reason = 4; return; }
        index = ++slot->count;
        slot->lookup[hash] = (u16)index;
        slot->pixels[index - 1u].offset = offset;
        slot->pixels[index - 1u].under = under;
        slot->pixels[index - 1u].drawn = pixel;
    } else {
        u32 old = slot->pixels[index - 1u].drawn, combined = 0;
        /* Bright cores win over all shadows, independent of segment order. */
        for (u32 shift = 0; shift < 32u; shift += 8u) {
            u32 a = (old >> shift) & 255u, b = (pixel >> shift) & 255u;
            combined |= (a > b ? a : b) << shift;
        }
        slot->pixels[index - 1u].drawn = combined;
    }
}

Z5_IMAGE(custom_disp_image) static u32 level_root(u32 n)
{
    u32 value = 0, bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= value + bit) { n -= value + bit; value = (value >> 1) + bit; }
        else value >>= 1;
        bit >>= 2;
    }
    return value;
}

/* Integral of a square pixel projected onto the line's minor axis (Q16).
 * This is the trapezoidal CDF of v - slope*u for a unit square. */
Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2")))
static u32 level_area_cdf(s32 z, u32 slope, u32 reciprocal)
{
    s32 a = (s32)((256u + slope) >> 1), b = (s32)((256u - slope) >> 1);
    if (z <= -a) return 0;
    if (z >= a) return 65536u;
    if (z < -b) {
        u32 t = (u32)(z + a);
        return (t * t * reciprocal + 32768u) >> 16;
    }
    if (z > b) {
        u32 t = (u32)(a - z);
        return 65536u - ((t * t * reciprocal + 32768u) >> 16);
    }
    return (u32)(z + 128) << 8;
}

#include "responsive.h"

/* Premultiplied source-over. No native pixel is treated as an exclusion. */
Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2")))
static u32 level_source_over(u32 source, u32 under)
{
    if (!(source >> 24)) return under;
    u32 inverse = 255u - (source >> 24), result = 0;
    for (u32 shift = 0; shift < 32u; shift += 8u) {
        u32 product = ((under >> shift) & 255u) * inverse + 128u;
        u32 value = ((source >> shift) & 255u) + ((product + (product >> 8)) >> 8);
        if (value > 255u) value = 255u;
        result |= value << shift;
    }
    return result;
}

Z5_IMAGE(custom_disp_image) static void level_flush(void)
{
    level_state *l = level_data();
    level_backing *slot = (level_backing *)l->current;
    if (l->reason) { slot->count = 0; return; }
    volatile u32 *surface = (volatile u32 *)l->surface;
    for (u32 i = 0; i < slot->count; i++) {
        level_pixel *p = slot->pixels + i;
        p->drawn = level_source_over(p->under, p->drawn);
        surface[p->offset] = p->drawn;
        level_sparse_mark(l->surface, p->offset, l->width, l->height);
    }
}
