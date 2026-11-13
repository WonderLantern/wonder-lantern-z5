/* Sparse save-under records belong to each mapped surface, not to a pose.
 * No firmware image or graphics resources are stored here. */
#include "smooth.h"
Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2")))
static u32 level_begin(void)
{
    level_state *l = level_data();
    l->current = 0;
    if (!l->surface) { l->reason = 32; return 0; }
    level_backing *slots = (level_backing *)l->backing, *slot = 0;
    for (u32 i = 0; i < 4u; i++) {
        if (slots[i].surface == l->surface) { slot = slots + i; break; }
        if (!slots[i].surface && !slot) slot = slots + i;
    }
    if (!slot) { l->reason = 8; return 0; }
    if (!slot->surface) slot->surface = l->surface;
    /* A recycled bank with a different stride no longer owns these pixels. */
    if (slot->width != l->width || slot->height != l->height) slot->count = 0;
    slot->width = l->width; slot->height = l->height;
    /* Restoration belongs before native dispatch. A dirty rotating bank
     * reached afterwards must wait, rather than erase newly painted UI. */
    if (slot->count) { l->reason = 64; return 0; }
    volatile u16 *lookup = slot->lookup;
    for (u32 i = 0; i < LEVEL_HASH_LIMIT; i++) lookup[i] = 0;
    slot->count = 0;
    l->current = (u32)slot;
    l->reason = 0;
    return 1;
}

