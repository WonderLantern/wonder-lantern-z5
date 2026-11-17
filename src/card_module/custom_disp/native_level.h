/* L33 restores native pose/setup, scoped to one native APPLV callback.
 * The special native controller paints whenever dirty, even if hidden. */
typedef struct {
    u32 widgets[4], visible[4], parents[3];
    u32 active, mode, projected_mode, prepared, suppressed;
    u32 repair_key[2], repair_bank[2][4], repair_pending[2];
} native_level_state;

Z5_IMAGE(custom_disp_image) static native_level_state *native_level_data(void)
{ return (native_level_state *)disp_metadata()[31]; }

Z5_IMAGE(custom_disp_image) static u32 native_level_pointer(u32 p)
{
    return !(p & 3u) && p >= Z5_OVERLAY_RAM_LOW && p <= Z5_OVERLAY_RAM_HIGH - 256u;
}

Z5_IMAGE(custom_disp_image) static u32 native_level_wanted(void)
{
    u32 output = disp_output();
    if ((disp_flags() & 19u) != 17u || disp_metadata()[3] || disp_metadata()[20] ||
        !disp_pass_matches(output) || !disp_gui_ready() || Z5_NATIVE_DISP_PAGE >= CUSTOM_PAGE_COUNT)
        return 0u;
    return output || (Z5_CUSTOM_DISP_SHOOTING_PAGE() && Z5_GUI_UI_MODE() == 0u);
}

Z5_IMAGE(custom_disp_image) static void native_level_forget(void)
{
    native_level_state *s = native_level_data();
    for (u32 output = 0u; output < 2u; output++)
        s->repair_key[output] = s->repair_pending[output] = 0u;
}

Z5_IMAGE(custom_disp_image) static void native_level_release(void)
{
    native_level_state *s = native_level_data();
    /* The native handler may switch page internally, after begin() ran. */
    if (Z5_NATIVE_DISP_PAGE != 3u || !(disp_flags() & 1u)) native_level_forget();
    if (s->suppressed) {
        for (u32 i = 0u; i < 4u; i++) {
            u32 w = s->widgets[i];
            if (!(s->suppressed & (1u << i)) || !native_level_pointer(w) ||
                Z5_NATIVE_LEVEL_WIDGET(i) != w) continue;
            /* Image children also dispatch on dirty. Do not leave their
             * startup artwork queued after restoring visibility. */
            __sync_bool_compare_and_swap((volatile u32 *)(w + 0x44u), 1u, 0u);
            __sync_bool_compare_and_swap((volatile u32 *)(w + 0x28u), 0u, s->visible[i]);
        }
        s->suppressed = 0u;
    }
    if (!s->active) return;
    if (Z5_NATIVE_DISP_PAGE != 3u || (disp_flags() & 1u)) {
        u32 w = s->widgets[0];
        /* Native walker 0x102BEE12 invokes the special callback on dirty
         * alone. Disarm it BEFORE restoring old output mode/parent pointers;
         * setting visibility to zero does not prevent this callback. */
        if (native_level_pointer(w) && Z5_NATIVE_LEVEL_WIDGET(0u) == w &&
            *(volatile u32 *)(w + 0x4cu) == 0x104B9FEFu)
            __sync_bool_compare_and_swap((volatile u32 *)(w + 0x44u), 1u, 0u);
        for (u32 i = 0u; i < 4u; i++) {
            w = s->widgets[i];
            if (!native_level_pointer(w) || Z5_NATIVE_LEVEL_WIDGET(i) != w) continue;
            /* Do not rearm the special controller while hiding it. */
            __sync_bool_compare_and_swap((volatile u32 *)(w + 0x28u), 1u, s->visible[i]);
            if (i) __sync_bool_compare_and_swap((volatile u32 *)(w + 0x48u),
                s->projected_mode == 1u ? 0xC2029214u : 0xC20293A4u, s->parents[i - 1u]);
        }
        if (Z5_NATIVE_LEVEL_MODE == s->projected_mode) Z5_NATIVE_LEVEL_MODE = (u8)s->mode;
    }
    s->active = s->prepared = 0u;
}

/* Before forwarding the handler: repair each native drawing bank once on
 * custom DISP3 entry/mode change, after the leading guard is installed.
 * Snapshot invalidation makes native controls repaint after the local erase;
 * never invalidate a complete page on the steady-state refresh path. */
Z5_IMAGE(custom_disp_image) static void native_level_begin(void)
{
    native_level_state *s = native_level_data();
    u32 output = disp_output(), flags = disp_flags();
    u32 key = Z5_NATIVE_DISP_PAGE == 3u && (flags & 1u) ? flags & 19u : 0u;
    if (s->repair_key[output] != key) {
        s->repair_key[output] = key;
        for (u32 i = 0u; i < 4u; i++) s->repair_bank[output][i] = 0u;
        s->repair_pending[output] = 0u;
    }
    if (!key || disp_metadata()[3] || disp_metadata()[20] ||
        !(disp_metadata()[7] & (1u << output)) ||
        !disp_pass_matches(output) || !disp_gui_ready()) return;
    u32 bank = Z5_LEVEL_FAST_BUFFER;
    if (!bank || (bank & 3u)) return;
    for (u32 i = 0u; i < 4u; i++) if (s->repair_bank[output][i] == bank) return;
    /* All native banks fit the existing four-bank save-under capacity. */
    if (s->repair_bank[output][3]) return;
    s->repair_pending[output] = bank;
    Z5_NATIVE_LEVEL_INVALIDATE();
}

/* Inside the checked native walk, ahead of native child painters. The
 * official hide path erases only the level footprint (including axes), not
 * the whole screen. This also removes artwork painted before our first
 * leading widget was registered during cold boot. */
Z5_IMAGE(custom_disp_image) static void native_level_repair(void)
{
    native_level_state *s = native_level_data();
    u32 output = disp_output(), bank = s->repair_pending[output];
    u32 key = s->repair_key[output];
    if (!bank || disp_metadata()[3] || disp_metadata()[20] ||
        Z5_NATIVE_DISP_PAGE != 3u || key != (disp_flags() & 19u) ||
        !disp_pass_matches(output) || !disp_gui_ready()) return;
    Z5_LEVEL_SYNC();
    if (disp_metadata()[3] || disp_metadata()[20] ||
        disp_output() != output || !disp_pass_matches(output) ||
        Z5_NATIVE_DISP_PAGE != 3u || key != (disp_flags() & 19u) ||
        !disp_gui_ready() || Z5_LEVEL_FAST_BUFFER != bank) return;
    u32 mode = Z5_NATIVE_LEVEL_MODE;
    Z5_NATIVE_LEVEL_CLEAR();
    Z5_NATIVE_LEVEL_MODE = (u8)mode;
    for (u32 i = 0u; i < 4u; i++) if (!s->repair_bank[output][i]) {
        s->repair_bank[output][i] = bank;
        break;
    }
    s->repair_pending[output] = 0u;
}

/* Leading callback, before native child painters. Both output lists have a
 * leading widget, including before their first native-level transition. */
Z5_IMAGE(custom_disp_image) static void native_level_prepare(void)
{
    native_level_state *s = native_level_data();
    native_level_repair();
    u32 flags = disp_flags();
    if ((flags & 1u) && (flags & 19u) != 17u && disp_pass_matches(disp_output()) && disp_gui_ready()) {
        /* The stock DISP3 special controller dispatches on dirty alone.
         * Silence it on every leading walk for Off/TypeA; restore visibility
         * after the whole handler without rearming its dirty flag. */
        if (s->active) native_level_release();
        u32 controller = Z5_NATIVE_LEVEL_WIDGET(0u);
        if (!native_level_pointer(controller) ||
            *(volatile u32 *)(controller + 0x4cu) != 0x104B9FEFu) return;
        for (u32 i = 0u; i < 4u; i++) {
            u32 w = Z5_NATIVE_LEVEL_WIDGET(i);
            if (!native_level_pointer(w) || *(volatile u32 *)(w + 0x28u) > 1u) continue;
            if (!(s->suppressed & (1u << i))) {
                s->widgets[i] = w;
                s->visible[i] = *(volatile u32 *)(w + 0x28u);
            }
            s->suppressed |= 1u << i;
            *(volatile u32 *)(w + 0x44u) = 0u;
            *(volatile u32 *)(w + 0x28u) = 0u;
        }
        return;
    }
    if (!native_level_wanted()) { native_level_release(); return; }
    /* Use the same scoped pose/show on every custom page. DISP3's original
     * change-only gate alone cannot repair its suppressed startup children. */
    u32 mode = disp_output() ? 3u : 1u;
    if (s->active && s->projected_mode != mode) native_level_release();
    if (s->prepared) return;
    u32 w[4];
    for (u32 i = 0u; i < 4u; i++) {
        w[i] = Z5_NATIVE_LEVEL_WIDGET(i);
        if (!native_level_pointer(w[i]) || *(volatile u32 *)(w[i] + 0x28u) > 1u) return;
        if (s->active && s->widgets[i] != w[i]) { native_level_release(); return; }
    }
    if (*(volatile u32 *)(w[0] + 0x4cu) != 0x104B9FEFu) return;
    if (!s->active) {
        for (u32 i = 0u; i < 4u; i++) {
            s->widgets[i] = w[i]; s->visible[i] = *(volatile u32 *)(w[i] + 0x28u);
            if (i) s->parents[i - 1u] = *(volatile u32 *)(w[i] + 0x48u);
        }
        s->mode = Z5_NATIVE_LEVEL_MODE;
        s->projected_mode = mode; s->active = 1u;
    }
    Z5_NATIVE_LEVEL_POSE();
    Z5_NATIVE_LEVEL_SHOW();
    s->prepared = 1u;
}
