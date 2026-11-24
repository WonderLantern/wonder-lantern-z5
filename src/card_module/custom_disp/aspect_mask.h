/* A74 (2026-11-24): 4:3 live-view mask on the LCD while the photo aspect is 4:3 in FX.
 *
 * The camera stays in FX; the photo is the centre 5352 of 6016 columns (332 each side). On the 720 x 480 LCD
 * canvas that is about 40 columns each side. Drawn in the custom DISP tail callback, after native controls, on
 * the same mapped UI surface as the level and histogram: only fully transparent pixels (alpha 0) become a 50%
 * black (premultiplied 0x80000000); opaque or semi-transparent native UI stays as it is. Pixels already equal to
 * the mask colour are skipped, so a steady screen writes nothing and publishes no cache range.
 * When the mask is off, pixels equal to the mask colour on a surface this code painted go back to 0. Up to four
 * rotating banks are remembered per output.
 * "On" = the A68/A69 4:3 pointer is in RAM (area 8 group 1, 0xC18C7598): the module turns it on only for 4:3 in
 * FX, so DX / 16:9 / stock 1:1 and "off" show no mask.
 * A79 (2026-11-24): the EVF too. Its native surfaces (720 x 480, 1024 x 682 / 768, 1280 x 960) carry the 3:2
 * live view across the full width, so each bar is width * 332 / 6016 columns (40 / 57 / 71), full height; on
 * the 4:3 surfaces the extra rows above / below are black or info bars, where 50% black changes nothing visible.
 * Limits (A74): only on pages the custom DISP overlay draws on; native redraws may briefly uncover a strip
 * until the next pass. camera_verified: false. */
#define A43_MASK_PTR    0xC18C7598u
#define A43_MASK_COLOR  0xF0000000u    /* A79: near black (94%, premultiplied); A74 was 0x80000000 (50%) */
#define A43_MASK_SIDE   40u            /* 720 * 332 / 6016 = 39.7 */

/* Bar width for a supported surface, 0 = not drawn. */
Z5_IMAGE(custom_disp_image) static u32 aspect_mask_side(u32 width, u32 height)
{
    if (width == 720u && height == 480u) return A43_MASK_SIDE;
    if (width == 1024u && (height == 682u || height == 768u)) return 57u;   /* 56.5 */
    if (width == 1280u && height == 960u) return 71u;                         /* 70.6 */
    return 0u;
}

Z5_IMAGE(custom_disp_image) static u32 aspect_mask_on(void)
{
    u32 p = *(volatile const u32 *)A43_MASK_PTR;
    return p >= 0xC1800000u && p < 0xC8700000u;
}

/* Returns the number of pixels changed; marks changed rows for the sparse publish. */
Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2")))
static u32 aspect_mask_apply(u32 surface, u32 width, u32 height, u32 draw)
{
    volatile u32 *px = (volatile u32 *)surface;
    u32 changed = 0, bar = aspect_mask_side(width, height);
    if (!bar || !surface || (surface & 3u)) return 0;
    for (u32 y = 0; y < height; y++) {
        volatile u32 *row = px + y * width;
        for (u32 side = 0; side < 2u; side++) {
            u32 x0 = side ? width - bar : 0u, first = 0xffffu, last = 0;
            for (u32 x = x0; x < x0 + bar; x++) {
                u32 v = row[x];
                if (draw ? v != 0u : v != A43_MASK_COLOR) continue;
                row[x] = draw ? A43_MASK_COLOR : 0u;
                if (x < first) first = x;
                last = x;
                changed++;
            }
            if (first <= last) {
                level_sparse_mark(surface, y * width + first, width, height);
                level_sparse_mark(surface, y * width + last, width, height);
            }
        }
    }
    return changed;
}

/* Called from the tail paint with the current mapped surface. The owned list is per output. */
Z5_IMAGE(custom_disp_image) static void aspect_mask_paint(overlay_state *s, u32 surface, u32 width, u32 height)
{
    u32 on = aspect_mask_on(), i, slot = 4u;
    for (i = 0; i < 4u; i++) if (s->mask_owned[i] == surface) { slot = i; break; }
    if (on) {
        if (slot == 4u) {
            for (i = 0; i < 4u; i++) if (!s->mask_owned[i]) { slot = i; break; }
            if (slot == 4u) { slot = s->mask_next & 3u; s->mask_next++; }
            s->mask_owned[slot] = surface;
        }
        s->mask_pixels += aspect_mask_apply(surface, width, height, 1u);
        s->mask_paints++;
    } else if (slot != 4u) {
        s->mask_pixels += aspect_mask_apply(surface, width, height, 0u);
        s->mask_owned[slot] = 0;
        s->mask_erases++;
    }
}
