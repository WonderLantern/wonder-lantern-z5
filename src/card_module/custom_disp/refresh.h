/* Shared 60 Hz request deadlines; output states have independent clocks. */
Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2"))) static u32 overlay_refresh_due(void)
{
    overlay_state *s = overlay_data();
    u32 now = Z5_FULL_TICK;
    if (overlay_reason()) {
        s->level_armed = 0;
        return 0;
    }
    if (!s->level_armed) {
        s->level_next = now;
        s->level_fraction = 0;
        s->level_armed = 1;
        s->level_start = s->level_last = s->level_frames = s->level_gap = 0;
    }
    if ((s32)(now - s->level_next) < 0) return 0;
    /* Skip expired deadlines; never issue a backlog of old frames. */
    if (now - s->level_next > 50u) s->level_next = now;
    do {
        u32 step = 16u;
        s->level_fraction += 40u;
        if (s->level_fraction >= 60u) { s->level_fraction -= 60u; step++; }
        s->level_next += step;
    } while ((s32)(now - s->level_next) >= 0);
    return 1;
}

Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2"))) static u32 overlay_refresh_sleep(void)
{
    overlay_state *s = overlay_data();
    volatile u32 *m = disp_metadata();
    if (!m[4] || m[3] || m[20] || !s->active || overlay_reason()) {
        s->level_armed = 0;
        return 33u;
    }
    if (!s->level_armed) return 1u;
    s32 left = (s32)(s->level_next - Z5_FULL_TICK);
    return left <= 0 ? 1u : left > 17 ? 17u : (u32)left;
}

Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2"))) static void overlay_refresh_completed(void)
{
    overlay_state *s = overlay_data();
    u32 now = Z5_FULL_TICK;
    if (!s->level_start) { s->level_start = s->level_last = now; return; }
    u32 gap = now - s->level_last;
    if (gap > s->level_gap) s->level_gap = gap;
    s->level_last = now;
    s->level_frames++;
    u32 span = now - s->level_start;
    if (span >= 1000u) {
        s->level_hz = level_div(s->level_frames * 1000u, span);
        s->level_gap_max = s->level_gap;
        s->level_start = now; s->level_frames = s->level_gap = 0;
    }
}
