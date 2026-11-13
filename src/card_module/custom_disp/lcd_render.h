/* L7: skip the native first walk, draw on the second and any later walks.
 * Native controls keep their usual order. No firmware or instruction edits. */
#define Z5_WALK_FIRST_RETURN  0x1030D8B5u
#define Z5_WALK_SECOND_RETURN 0x1030D8DFu
#define Z5_WALK_THIRD_RETURN  0x1030D8F9u
#define Z5_WALK_FOURTH_RETURN 0x1030D915u

Z5_IMAGE(custom_disp_image) static u32 lcd_native_walk_site(u32 entry_sp, u32 entry_lr)
{
    if (entry_lr != 0x102BEE53u || (entry_sp & 3u) ||
        entry_sp < Z5_OVERLAY_RAM_LOW || entry_sp > Z5_OVERLAY_RAM_HIGH - 48u)
        return 0u;
    const volatile u32 *stack = (const volatile u32 *)entry_sp;
    if (stack[7] != 0x10448B37u) return 0u;
    return stack[11];
}

Z5_IMAGE(custom_disp_image) static u32 lcd_native_later_walk(u32 site)
{
    return site == Z5_WALK_SECOND_RETURN || site == Z5_WALK_THIRD_RETURN ||
           site == Z5_WALK_FOURTH_RETURN;
}
/* Native GUI synchronization is optional. The real wrapper permits a zero
 * mode without claiming the busy flag; an enabled mode uses busy value one. */
Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2")))
static u32 lcd_source_gui_gate(void)
{
    u32 mode = Z5_LCD_GUI_MODE, held = Z5_LCD_GUI_LOCK;
    u32 state = (mode != 0u) | (held == 1u ? 2u : held ? 4u : 0u);
    if (Z5_LCD_GUI_MODE != mode) state |= 8u;
    lcd_data()->source_gate = state;
    return state == 0u || state == 3u;
}

/* Native list append and traversal are digest-checked. A valid later widget
 * means this snapshot would precede native rendering. Never move list nodes
 * from a paint callback, while the native iterator is using them. */
Z5_IMAGE(custom_disp_image) static u32 lcd_source_tail(u32 object)
{
    u32 canvas = Z5_HBASE_CANVAS;
    u32 head = *(volatile const u32 *)(canvas + 0x9cu);
    u32 tail = *(volatile const u32 *)(canvas + 0xa0u);
    u32 count = *(volatile const u32 *)(canvas + 0xa4u);
    if (!count || count > 65536u || (head & 3u) || (tail & 3u) ||
        head < Z5_OVERLAY_RAM_LOW || head > Z5_OVERLAY_RAM_HIGH - 12u ||
        tail < Z5_OVERLAY_RAM_LOW || tail > Z5_OVERLAY_RAM_HIGH - 12u) return 0;
    /* The native container is a circular doubly linked list. */
    if (*(volatile const u32 *)(tail + 4u) != head ||
        *(volatile const u32 *)head != tail) return 0;
    u32 item = *(volatile const u32 *)(tail + 8u);
    if ((item & 3u) || item < Z5_OVERLAY_RAM_LOW ||
        item > Z5_OVERLAY_RAM_HIGH - 256u) return 0;
    return item == object ? 1u : 2u;
}

/* Same GUI task, before the native child painters. This never clears UI
 * or forces native controls dirty; compare-and-restore preserves new UI. */
Z5_IMAGE(custom_disp_image) static void lcd_source_prepare(u32 object)
{
    volatile u32 *m = disp_metadata();
    if (m[18] || object != disp_pre_widget(0u) ||
        *(volatile const u32 *)(object + 0x4cu) != m[30] ||
        Z5_OVERLAY_OUTPUT != 0u || Z5_CUSTOM_DISP_PRIMARY_DISPLAY != 1u) return;
    u32 start = Z5_FULL_TICK;
    level_retire(0u);
    lcd_diag_cost(1u, Z5_FULL_TICK - start);
}

Z5_IMAGE(custom_disp_image) static u32 lcd_source_descriptor(void)
{
    u32 slot = Z5_LCD_PRESENT_SLOT;
    return slot < 3u ? Z5_LCD_PRESENT_POOL + slot * 0xea8u + 0x84u : 0u;
}

Z5_IMAGE(custom_disp_image) static u32 lcd_source_refuse(u32 reason)
{
    lcd_state *s = lcd_data();
    s->source_reason = reason;
    __asm__ volatile ("dmb sy" ::: "memory");
    s->source_busy = 0;
    return 1u;
}

Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2")))
static u32 lcd_source_paint_impl(u32 object)
{
    lcd_state *s = lcd_data();
    volatile u32 *m = disp_metadata();
    /* Native traversal clears the trailing dirty word after this return.
     * Rearm the leading widget, which schedules the next pass's trailing
     * callback even when several GUI walks occur in one APPLV handler. */
    if (*(volatile const u32 *)(object + 0x4cu) == m[8] &&
        *(volatile const u32 *)(disp_pre_widget(0u) + 0x4cu) == m[30])
        *(volatile u32 *)(disp_pre_widget(0u) + 0x44u) = 1;
    if (!__sync_bool_compare_and_swap(&s->source_busy, 0u, 1u)) return 1u;
    if (m[18] || !m[4] || m[3] || !s->active || lcd_reason() ||
        *(volatile const u32 *)(object + 0x4cu) != m[8]) return lcd_source_refuse(2u);
    if (lcd_source_tail(object) != 1u) {
        s->late_frames++;
        return lcd_source_refuse(64u);
    }
    if (!lcd_source_gui_gate()) return lcd_source_refuse(4u);
    u32 descriptor = lcd_source_descriptor();
    if (!descriptor) return lcd_source_refuse(16u);
    volatile const u32 *d = (volatile const u32 *)descriptor;
    u32 native = Z5_LEVEL_FAST_BUFFER;
    if (d[0] != 1u || d[1] != 1u || d[0x4cu / 4u] ||
        d[0x48u / 4u] != 1u || !native || (native & 3u)) return lcd_source_refuse(16u);
    const u16 *size = Z5_LEVEL_FAST_SIZE(0u);
    if (!size || size[0] != 720u || size[1] != 480u ||
        d[2] != ((480u << 16) | 720u) || d[0x38u / 4u] != 0u)
        return lcd_source_refuse(16u);
    /* No private input, source replacement or full-screen UI snapshot. */
    if (*(volatile const u32 *)(descriptor + 0x28u) != native)
        return lcd_source_refuse(32u);
    u32 mapped = Z5_LEVEL_FAST_SURFACE(native);
    const u32 bytes = 720u * 480u * 4u;
    if (!mapped || (mapped & 3u) || mapped > 0xffffffffu - bytes)
        return lcd_source_refuse(8u);
    m[18] = 1u;
    level_state *l = (level_state *)m[23];
    /* Retire/move old histogram geometry using native cleanup, before the GPU fence. No statistics read. */
    if (s->before_level) ((void (*)(u32,u32,const u32 *,u16 *))s->before_level)(native, 0u, 0, 0);
    u32 start = Z5_FULL_TICK;
    level_sync(l);
    lcd_diag_cost(0u, Z5_FULL_TICK - start);
    /* The wait may yield. Revalidate before the first destructive store. */
    if (!lcd_source_gui_gate() || lcd_source_tail(object) != 1u ||
        Z5_LEVEL_FAST_BUFFER != native || lcd_reason() || !m[4] || m[21] ||
        *(volatile const u32 *)(descriptor + 0x28u) != native) {
        m[18] = 0;
        return lcd_source_refuse(32u);
    }
    /* Pin a completed packet exactly as on the original EVF path. The producer never waits for GUI work. */
    if (s->before_level) {
        u32 publication = m[12], index = publication & 3u;
        if (index) {
            m[11] = index;
            __asm__ volatile ("dmb sy" ::: "memory");
            if (publication == m[12]) {
                if (s->sparse_surface != mapped) level_sparse_reset(mapped);
                const u32 *packet = (const u32 *)(m[10] + (index - 1u) * PACKET_BYTES);
                ((void (*)(u32,u32,const u32 *,u16 *))s->before_level)
                    (native, mapped, packet, (u16 *)s->sparse_rows);
            }
            __asm__ volatile ("dmb sy" ::: "memory");
            m[11] = 0u;
        }
    }
    start = Z5_FULL_TICK;
    /* The leading callback retired this bank before native painters. Do not
     * erase or restore it here: native controls have just finished drawing. */
    s->drawing_image = mapped; s->ready = 1u;
    u32 painted = (disp_flags() & 2u) ? level_paint() : 1u;
    s->ready = 0; s->drawing_image = 0;
    lcd_diag_cost(1u, Z5_FULL_TICK - start);
    start = Z5_FULL_TICK;
    /* Publish only cache ranges covering our changed LCD pixels. */
    u32 published = level_publish();
    lcd_diag_cost(2u, Z5_FULL_TICK - start);
    m[18] = 0;
    if (!painted || !published) return lcd_source_refuse(l->reason ? l->reason : 8u);
    s->paints++; s->source_reason = l->reason;
    lcd_level60_completed(); lcd_diag_count(3u);
    Z5_HBASE_DIRTY = 1;
    __asm__ volatile ("dmb sy" ::: "memory");
    s->source_busy = 0;
    return 1u;
}

Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2")))
static u32 lcd_source_paint(u32 object, u32 walk_site)
{
    u32 diagnostic = lcd_data()->active && !lcd_reason();
    u32 start = Z5_FULL_TICK;
    if (diagnostic) lcd_diag_count(2u);
    u32 result = 1u;
    if (walk_site == Z5_WALK_FIRST_RETURN) {
        /* The first walk clears our trailing dirty word. Rearm the leading
         * widget so the second walk reaches both callbacks even though this
         * first one deliberately paints no level. */
        volatile u32 *m = disp_metadata();
        if (object == disp_widget(0u) &&
            *(volatile const u32 *)(object + 0x4cu) == m[8] &&
            *(volatile const u32 *)(disp_pre_widget(0u) + 0x4cu) == m[30])
            *(volatile u32 *)(disp_pre_widget(0u) + 0x44u) = 1u;
    } else if (lcd_native_later_walk(walk_site)) {
        lcd_state *s = lcd_data();
        if (diagnostic && s->walk_tracking) {
            u32 span = Z5_FULL_TICK - s->walk_tick;
            lcd_diag_cost(3u, span > s->walk_own_ms ? span - s->walk_own_ms : 0u);
        }
        result = lcd_source_paint_impl(object);
    } else {
        /* An unfamiliar caller must not be mistaken for a final native walk. */
        lcd_data()->source_reason = 128u;
    }
    if (diagnostic) {
        u32 own = Z5_FULL_TICK - start;
        lcd_data()->diag_custom_ms += own;
        if (lcd_data()->walk_tracking) lcd_data()->walk_own_ms += own;
        lcd_diag_publish();
    }
    return result;
}
