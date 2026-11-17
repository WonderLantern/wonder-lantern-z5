/* GUI-only logical DISP routing. The normal shooting key object is RAM;
 * its +0x14 callback and complete dispatch/eligibility chain are checked.
 * No native instruction or per-frame temporary property writes. */
typedef u32 (*custom_fn0)(void);
typedef void (*custom_set2)(u32, u32);
typedef void (*custom_set3)(u32, u32, u32);
#define CUSTOM_KEY_SLOT ((volatile u32 *)0xC18289F4u)
#define CUSTOM_NATIVE_KEY 0x10304109u
#define CUSTOM_NATIVE_PAGE (*(volatile u8 *)0xC2028442u)

IMG_FN static u32 custom_photo(void)
{
    return *(volatile u8 *)0xC18119A0u == 3u &&
        FW(read_fn, 0x10456098u)(0x168Cu) == 0u;
}

IMG_FN static void custom_route_sync(cm_t *c)
{
    custom_disp_state *s = (custom_disp_state *)c->custom_state;
    if (!s) return;
    u32 want = !c->cfg_state && c->custom_disp && !c->custom_disp_rc &&
        (c->custom_control & CUSTOM_CONTROL_ON) && custom_photo();
    if (want && *CUSTOM_KEY_SLOT == CUSTOM_NATIVE_KEY)
        __sync_bool_compare_and_swap(CUSTOM_KEY_SLOT, CUSTOM_NATIVE_KEY, s->key_entry);
    want = want && s->key_entry && *CUSTOM_KEY_SLOT == s->key_entry;
    if (!want) c->custom_control &= ~CUSTOM_CONTROL_ACTIVE;
    u32 output = custom_output();
    for (u32 o = 0u; o < 2u; o++) {
        u32 bit = 1u << o, key = 0x3B15u + o;
        u32 logical = CUSTOM_LOGICAL(c->custom_control, o);
        /* Native photo EVF skips the LCD-only display-off state. */
        if (o && logical == 4u && want) {
            logical = 0u;
            c->custom_control = CUSTOM_SET_LOGICAL(c->custom_control, o, logical);
        }
        u32 actual = FW(read_fn, 0x10456098u)(key);
        if (want && (s->route_owned & bit) && actual != s->route_applied[o] && actual <= 4u) {
            /* A native wake/non-DISP action can leave display-off. Accept
             * that native page change instead of forcing the display off
             * again; persist this event just like an accepted DISP press. */
            logical = o && actual == 4u ? 0u : actual;
            c->custom_control = CUSTOM_SET_LOGICAL(c->custom_control, o, logical);
            s->route_save = 1u;
        }
        u32 base = logical == 4u ? 4u : 0u;
        if (want) {
            if (!(s->route_owned & bit) || actual != base) {
                if (actual != base) FW(custom_set2, 0x104560A4u)(key, base);
                s->route_pending = 1u;
            }
            s->route_owned |= bit;
            s->route_applied[o] = base;
        } else if (s->route_owned & bit) {
            /* Transfer our last logical page back only while we still own
             * the native value; do not overwrite a later native change. */
            if (actual == s->route_applied[o] && actual != logical) {
                FW(custom_set2, 0x104560A4u)(key, logical);
                s->route_pending = 1u;
            }
            s->route_owned &= ~bit;
        }
    }
    if (want) c->custom_control |= CUSTOM_CONTROL_ACTIVE;
    else if (*CUSTOM_KEY_SLOT == s->key_entry && s->key_entry)
        __sync_bool_compare_and_swap(CUSTOM_KEY_SLOT, s->key_entry, CUSTOM_NATIVE_KEY);
    if (s->route_pending && custom_photo() &&
        *(volatile u8 *)0xC18289DCu == 0u &&
        !*(volatile u32 *)0xC18217C0u) {
        u32 logical = custom_logical_page(c);
        u32 next = want ? (logical == 4u ? 4u : 0u) : logical;
        if (CUSTOM_NATIVE_PAGE != next) {
            /* Current output only. The other property was updated above;
             * its snapshot will be regenerated when that output activates. */
            CUSTOM_NATIVE_PAGE = (u8)next;
        }
        s->pending = 1u;
        if (want && !output && next == 4u)
            FW(read_fn, 0x1042DE0Au)(0xC18289E0u);
        s->route_pending = 0u;
    }
    __sync_synchronize();
}

/* A DISP action uses the same native prefix/suffix as 0x10304108.
 * Keep all other actions on the original callback. On DISP, the original
 * 0x10341AAA eligibility/side effects run once, selecting 0->1 or 4->0.
 * We map that selection before any call to native GUI 0x1030D744, so no
 * GUI snapshot/statistics/painter ever observes a custom DISP2 or DISP3. */
IMG_FN static u32 custom_key_dispatch(u32 action)
{
    cm_t *c = cm_get();
    if (!c || action != 0x29u || !c->custom_state ||
        (c->custom_control & 3u) != 3u || !custom_photo() ||
        *(volatile u8 *)0xC18289DCu != 0u)
        return FW(read_fn, CUSTOM_NATIVE_KEY)(action);
    custom_disp_state *s = (custom_disp_state *)c->custom_state;
    if (s->cfg_busy) return 0u;
    /* Native outer callback: release/temporary-UI handling precedes DISP. */
    u32 prefix = FW(custom_fn0, 0x10368156u)();
    if (prefix) return prefix;
    u32 first = FW(read_fn, 0x10307184u)(action);
    u32 second = FW(read_fn, 0x10307236u)(action);
    u32 third = FW(read_fn, 0x10307250u)(action);
    if (first | second | third) {
        if (FW(custom_fn0, 0x102F5F56u)()) return FW(custom_fn0, 0x102F5F46u)();
        return 0u;
    }
    FW(custom_fn0, 0x103489E8u)();
    FW(custom_fn0, 0x1042D5FAu)();
    u32 output = custom_output(), page = custom_logical_page(c);
    u32 before = CUSTOM_NATIVE_PAGE;
    u32 result = FW(custom_fn0, 0x10341AAAu)();
    u32 selected = CUSTOM_NATIVE_PAGE;
    u32 changed = (before == 0u && selected == 1u) || (before == 4u && selected == 0u);
    if (output != custom_output() || !custom_photo() ||
        *(volatile u8 *)0xC18289DCu != 0u || (c->custom_control & 3u) != 3u) {
        /* A yielding native service can switch output/application. Reconcile
         * the current properties; never write the old output's logical page
         * through the new output's native setter. */
        changed = 0u;
        custom_apply(c);
    }
    if (changed) {
        u32 next = page >= 4u ? 0u : page + 1u;
        if (output && next == 4u) next = 0u;
        c->custom_control = CUSTOM_SET_LOGICAL(c->custom_control, output, next);
        u32 base = next == 4u ? 4u : 0u;
        if (selected != base) FW(read_fn, 0x10341A92u)(base);
        s->route_applied[output] = base;
        if (base == 4u) {
            FW(read_fn, 0x1042DE0Au)(0xC18289E0u);
            result = 2u;
        }
        s->pending = 1u;
        custom_apply(c);
    }
    if (FW(custom_fn0, 0x1042C7A4u)()) result |= 0x100u;
    if ((result & 255u) == 2u || (result & 255u) == 3u) result &= ~0x100u;
    u32 key = (result & 255u) == 1u ? action : 0u;
    FW(read_fn, 0x104555AAu)(0u);
    u32 mapped = FW(read_fn, 0x10455712u)(key);
    FW(custom_set3, 0x10455570u)(0u, mapped, 0u);
    u32 value = 1u; /* Native outer callback leaves r0=1 on its no-render exit. */
    if ((result >> 8u) == 1u) value = FW(read_fn, 0x1030D744u)(0u);
    /* Immediate, verified write after the GUI work, only on an accepted
     * page change; no periodic per-frame saves and no shutdown hook. */
    if (changed) save_cfg(c);
    return value;
}
