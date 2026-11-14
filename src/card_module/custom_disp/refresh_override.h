/* Temporary GUI-owned k overrides. Only each 8-byte entry's first word changes.
 * A single writer (the checked APPLV page wrapper) owns this state. */
#define REFRESH_K_SLOTS 16u
typedef struct {
    u32 saved[REFRESH_K_SLOTS], owned;
} refresh_override;

Z5_IMAGE(custom_disp_image) static void refresh_override_update(refresh_override *s,
                                                        volatile u32 *table, u32 active)
{
    for (u32 i = 0u; i < REFRESH_K_SLOTS; i++) {
        volatile u32 *word = table + i * 2u;
        u32 bit = 1u << i;
        if (active) {
            u32 value = *word;
            if (value != 1u && __sync_bool_compare_and_swap(word, value, 1u)) {
                s->saved[i] = value;
                s->owned |= bit;
            }
        } else if (s->owned & bit) {
            /* A different native value wins. Never restore the paired word. */
            __sync_bool_compare_and_swap(word, 1u, s->saved[i]);
            s->owned &= ~bit;
        }
    }
}
