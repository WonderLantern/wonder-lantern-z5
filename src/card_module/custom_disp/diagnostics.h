/* R/U/C/F rates and maximum W/D/K/N durations. GUI snapshots atomically
 * collect the request task's counter; no lock blocks either task. */
Z5_IMAGE(custom_disp_image) static void lcd_diag_count(u32 index)
{
    __atomic_fetch_add(lcd_data()->diag_counts + index, 1u, __ATOMIC_RELAXED);
}

Z5_IMAGE(custom_disp_image) static void lcd_diag_cost(u32 index, u32 milliseconds)
{
    u32 *word = lcd_data()->diag_max + index;
    u32 old = __atomic_load_n(word, __ATOMIC_RELAXED);
    while (old < milliseconds &&
           !__atomic_compare_exchange_n(word, &old, milliseconds, 0,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) { }
}

Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2")))
static void lcd_diag_publish(void)
{
    lcd_state *s = lcd_data();
    u32 now = Z5_FULL_TICK;
    if (!s->diag_start) { s->diag_start = now; return; }
    u32 span = now - s->diag_start;
    if (span < 1000u) return;
    for (u32 i = 0; i < 4u; i++) {
        u32 count = __atomic_exchange_n(s->diag_counts + i, 0u, __ATOMIC_RELAXED);
        s->diag_rates[i] = level_div(count * 1000u, span);
        s->diag_costs[i] = __atomic_exchange_n(s->diag_max + i, 0u, __ATOMIC_RELAXED);
    }
    s->diag_start = now;
}
