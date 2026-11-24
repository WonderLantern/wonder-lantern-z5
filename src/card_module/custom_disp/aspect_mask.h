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
 * Limits: LCD only; only on pages the custom DISP overlay draws on; native redraws may briefly uncover a strip
 * until the next pass. camera_verified: false. */
#define A43_MASK_PTR    0xC18C7598u
#define A43_MASK_COLOR  0x80000000u
#define A43_MASK_SIDE   40u            /* 720 * 332 / 6016 = 39.7 */
#define A43_MASK_REDRAW 1500u          /* A79: unfilled pixels that mean a large native redraw */
#define A43_MASK_HOLD_MS 250u          /* A79: wait this long for the native controls to settle */
#define A43_MASK_FREE_MS 600u          /* A79: then fill every bank without a new hold */

Z5_IMAGE(custom_disp_image) static u32 aspect_mask_on(void)
{
    u32 p = *(volatile const u32 *)A43_MASK_PTR;
    return p >= 0xC1800000u && p < 0xC8700000u;
}

/* draw 1: fill transparent pixels; 0: erase the mask colour; 2: only count the transparent pixels.
 * Returns the number of pixels changed (or counted); marks changed rows for the sparse publish. */
Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2")))
static u32 aspect_mask_apply(u32 surface, u32 width, u32 height, u32 draw)
{
    volatile u32 *px = (volatile u32 *)surface;
    u32 changed = 0;
    if (width != 720u || height != 480u || !surface || (surface & 3u)) return 0;
    for (u32 y = 0; y < height; y++) {
        volatile u32 *row = px + y * width;
        for (u32 side = 0; side < 2u; side++) {
            u32 x0 = side ? width - A43_MASK_SIDE : 0u, first = 0xffffu, last = 0;
            for (u32 x = x0; x < x0 + A43_MASK_SIDE; x++) {
                u32 v = row[x];
                if (draw ? v != 0u : v != A43_MASK_COLOR) continue;
                if (draw == 2u) { changed++; continue; }
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
        /* A79: after a large native redraw (back from the instant review, page entry, power-on) the native
         * controls finish over several passes; filling at once lit the mask under icon pixels that were still
         * transparent (camera: brief flicker near the exposure scale). Wait until the redraw settles: a pass
         * that finds more than A43_MASK_REDRAW unfilled pixels starts a hold of A43_MASK_HOLD_MS; small
         * repairs during shooting are still filled at once. After a hold, every rotating bank is filled
         * freely for A43_MASK_FREE_MS, so the other (still empty) banks do not start a new hold each. */
        u32 now = Z5_FULL_TICK;
        if (s->mask_hold) {
            if (now - s->mask_hold_tick < A43_MASK_HOLD_MS) { s->mask_holds++; return; }
            s->mask_hold = 0;
            s->mask_free = 1; s->mask_free_tick = now;
        } else {
            if (s->mask_free && now - s->mask_free_tick >= A43_MASK_FREE_MS) s->mask_free = 0;
            if (!s->mask_free && aspect_mask_apply(surface, width, height, 2u) > A43_MASK_REDRAW) {
                s->mask_hold = 1; s->mask_hold_tick = now; s->mask_holds++;
                return;
            }
        }
        s->mask_pixels += aspect_mask_apply(surface, width, height, 1u);
        s->mask_paints++;
    } else if (slot != 4u) {
        s->mask_pixels += aspect_mask_apply(surface, width, height, 0u);
        s->mask_owned[slot] = 0;
        s->mask_erases++;
    }
}
