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
 *
 * A80: icon memory against flicker. A74 on camera: a brief flicker near native icons (exposure scale) when the
 * instant review returns to live view: during the native redraw an icon pixel is transparent for a moment, got
 * the mask, then the icon. A79's 250 ms hold after large redraws made every half-press blink (the half-press
 * redraw is large too), so it is gone. Instead each bar pixel seen holding native UI (neither 0 nor the mask
 * colour) is remembered in a bitmap for one to two epochs of A43_MASK_EPOCH_MS; while remembered it is not filled
 * when it turns transparent. Everything else is filled at once.
 * Limits: LCD only; only on pages the custom DISP overlay draws on. camera_verified: false. */
#define A43_MASK_PTR    0xC18C7598u
#define A43_MASK_COLOR  0x80000000u
#define A43_MASK_SIDE   40u            /* 720 * 332 / 6016 = 39.7 */
#define A43_MASK_EPOCH_MS 3000u        /* A80: native pixels are remembered for 3-6 s */

Z5_IMAGE(custom_disp_image) static u32 aspect_mask_on(void)
{
    u32 p = *(volatile const u32 *)A43_MASK_PTR;
    return p >= 0xC1800000u && p < 0xC8700000u;
}

/* draw 1: fill transparent pixels not remembered as native UI (and remember native ones); 0: erase the mask
 * colour. Returns the number of pixels changed; marks changed rows for the sparse publish. */
Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2")))
static u32 aspect_mask_apply(overlay_state *s, u32 surface, u32 width, u32 height, u32 draw)
{
    volatile u32 *px = (volatile u32 *)surface;
    u32 changed = 0, bit = 0;
    if (width != 720u || height != 480u || !surface || (surface & 3u)) return 0;
    for (u32 y = 0; y < height; y++) {
        volatile u32 *row = px + y * width;
        for (u32 side = 0; side < 2u; side++) {
            u32 x0 = side ? width - A43_MASK_SIDE : 0u, first = 0xffffu, last = 0;
            for (u32 x = x0; x < x0 + A43_MASK_SIDE; x++, bit++) {
                u32 v = row[x], word = bit >> 5, b = 1u << (bit & 31u);
                if (!draw) {
                    if (v != A43_MASK_COLOR) continue;
                    row[x] = 0u;
                } else {
                    if (v == A43_MASK_COLOR) continue;
                    if (v) { s->mask_seen[0][word] |= b; continue; }
                    if ((s->mask_seen[0][word] | s->mask_seen[1][word]) & b) continue;
                    row[x] = A43_MASK_COLOR;
                }
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
    u32 on = aspect_mask_on(), i, slot = 4u, now = Z5_FULL_TICK;
    for (i = 0; i < 4u; i++) if (s->mask_owned[i] == surface) { slot = i; break; }
    if (now - s->mask_epoch_tick >= A43_MASK_EPOCH_MS) {
        for (i = 0; i < A43_MASK_SEEN_WORDS; i++) { s->mask_seen[1][i] = s->mask_seen[0][i]; s->mask_seen[0][i] = 0; }
        s->mask_epoch_tick = now;
    }
    if (on) {
        if (slot == 4u) {
            for (i = 0; i < 4u; i++) if (!s->mask_owned[i]) { slot = i; break; }
            if (slot == 4u) { slot = s->mask_next & 3u; s->mask_next++; }
            s->mask_owned[slot] = surface;
        }
        s->mask_pixels += aspect_mask_apply(s, surface, width, height, 1u);
        s->mask_paints++;
    } else if (slot != 4u) {
        s->mask_pixels += aspect_mask_apply(s, surface, width, height, 0u);
        s->mask_owned[slot] = 0;
        s->mask_erases++;
    }
}
