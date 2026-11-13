/* Native detailed-page overlays and refresh diagnostics. */

typedef struct {
    u32 active, conflict, reason, pending, entries, installs, restores, invalidations;
    volatile u32 ready, paints, source_reason, source_busy, source_gate;
    u32 drawing_image;
    u32 late_frames, tail_moves, pre_passes, af_frames;
    u32 before_level;  /* retained card-module callback; no firmware patch or copied transient code */
    u32 level_next, level_fraction, level_armed;
    u32 level_start, level_frames, level_last, level_gap, level_hz, level_gap_max;
    u32 diag_start, diag_counts[4], diag_max[4], diag_rates[4], diag_costs[4];
    u32 diag_custom_ms;
    u32 walk_tick, walk_own_ms, walk_tracking, walk_delay;
    u32 sparse_surface;
    u16 sparse_rows[480][2];
} lcd_state;

Z5_IMAGE(custom_disp_image) static lcd_state *lcd_data(void)
{
    return (lcd_state *)disp_metadata()[33];
}


Z5_IMAGE(custom_disp_image) static u32 lcd_mask_reason(void)
{
    volatile const u8 *s = (volatile const u8 *)Z5_OVERLAY_SNAPSHOT;
    if (!Z5_CUSTOM_DISP_SHOOTING_PAGE() || Z5_CUSTOM_DISP_PRIMARY_DISPLAY != 1u ||
        Z5_PROBE_SELECTOR != 1u ||
        s[0x18e] != Z5_LCD_TARGET_DISP || s[0x40] || s[0x41] || s[0x196] ||
        (s[0x197] != 0u && s[0x197] != 3u) ||
        *(volatile const u32 *)(s + 0x2e8) > 1u ||
        *(volatile const u32 *)(s + 0x2f4) ||
        *(volatile const u16 *)(s + 0x358) > 2u) return 2u;
    if (Z5_FULL_BUSY_WORD || Z5_OVERLAY_CONDITION(12u) || Z5_OVERLAY_BUSY()) return 4u;
    return 0u;
}

Z5_IMAGE(custom_disp_image) static u32 lcd_reason(void)
{
    /* Temporary controls pause composition without restoring fixed labels. */
    if (Z5_LCD_UI_MODE() != 0u) return 8u;
    return lcd_mask_reason();
}

/* The original detailed page already owns its complete native UI. There are
 * no mask changes, visibility overrides, or extra whole-page invalidations. */
Z5_IMAGE(custom_disp_image) static void lcd_update(u32 event)
{
    lcd_state *s = lcd_data();
    s->entries++;
    u32 reason = lcd_mask_reason();
    u32 desired = !reason;
    if (desired && event != 9u && !s->active) desired = 0;
    if (desired != s->active) {
        s->active = desired;
        if (desired) s->installs++;
        else s->restores++;
    }
    s->reason = desired ? 0u : reason ? reason : 7u;
}

