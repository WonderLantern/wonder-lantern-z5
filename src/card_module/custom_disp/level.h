/* Thin-line presentation, inspired by thin-line virtual horizon illustrations.
 * Geometry is original. No camera resources or firmware code are embedded. */
#define LEVEL_UNITS 8u
#define LEVEL_SCALE 2048u
#define LEVEL_COORD 16
#define LEVEL_PIXEL_LIMIT 16384u
#define LEVEL_HASH_LIMIT 32768u
#define LEVEL_HASH_SHIFT 17u
typedef struct { u32 offset, under, drawn; } level_pixel;
typedef struct {
    u32 surface, count, width, height;
    u16 lookup[LEVEL_HASH_LIMIT];
    level_pixel pixels[LEVEL_PIXEL_LIMIT];
} level_backing;
typedef struct {
    u32 busy, paints, count, reason, entries;
    signed short previous[6][4];
    u32 last_roll, last_pitch;
    u32 surface, key_roll, key_pitch, last_tick, force, hist_damage, last_cost, gui_delay;
    u32 window_start, window_span, window_calls, window_changes, window_seen;
    s32 window_roll, window_a, window_b;
    u32 window_cost;
    volatile u32 requests, drive_fault;
    u32 backing, current, surfaces;
    u32 width, height;
    volatile u32 sync_max, histogram_origin, histogram_count, attempts;
    u32 max_gap, last_pose_tick, publications;
    u32 rejects, pre_max, native_max, cache_max;
} level_state;

Z5_IMAGE(custom_disp_image) static level_state *level_data(void)
{
    return (level_state *)disp_metadata()[23] + (Z5_OVERLAY_OUTPUT == Z5_CUSTOM_DISP_EVF_PASS);
}


Z5_IMAGE(custom_disp_image) static void level_sparse_reset(u32 surface)
{
    lcd_state *s = lcd_data();
    s->sparse_surface = surface;
    for (u32 y = 0; y < 480u; y++) {
        s->sparse_rows[y][0] = 0xffffu;
        s->sparse_rows[y][1] = 0u;
    }
}

Z5_IMAGE(custom_disp_image) static void level_sparse_mark(u32 surface, u32 offset)
{
    if (offset >= 720u * 480u) return;
    lcd_state *s = lcd_data();
    if (s->sparse_surface != surface) level_sparse_reset(surface);
    u32 y = offset / 720u, x = offset - y * 720u;
    if (x < s->sparse_rows[y][0]) s->sparse_rows[y][0] = (u16)x;
    if (x + 1u > s->sparse_rows[y][1]) s->sparse_rows[y][1] = (u16)(x + 1u);
}

Z5_IMAGE(custom_disp_image) static u32 level_sparse_publish(u32 surface)
{
    lcd_state *s = lcd_data();
    if (!surface || (surface & 3u) || surface > 0xffffffffu - 720u*480u*4u) return 0;
    if (s->sparse_surface != surface) return 1;
    u32 first = 0, last = 0;
    for (u32 y = 0; y < 480u; y++) {
        u32 low = s->sparse_rows[y][0], high = s->sparse_rows[y][1];
        if (low >= high || high > 720u) continue;
        u32 begin = ((y * 720u + low) * 4u) & ~63u;
        u32 end = ((y * 720u + high) * 4u + 63u) & ~63u;
        if (last && begin <= last + 64u && end - first <= 65536u) {
            if (end > last) last = end;
        } else {
            if (last) Z5_LEVEL_CACHE_RANGE(surface + first, last - first);
            first = begin; last = end;
        }
    }
    if (last) Z5_LEVEL_CACHE_RANGE(surface + first, last - first);
    level_sparse_reset(surface);
    return 1;
}

/* Measure the native graphics wait without changing its ordering. */
Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2")))
static void level_sync(level_state *l)
{
    u32 start = Z5_FULL_TICK;
    Z5_LEVEL_SYNC();
    u32 elapsed = Z5_FULL_TICK - start;
    if (elapsed > l->sync_max) l->sync_max = elapsed;
}

Z5_IMAGE(custom_disp_image) static u32 level_div(u32 value, u32 denominator)
{
    u32 quotient = 0, remainder = 0;
    for (u32 i = 0; i < 32u; i++) {
        remainder = (remainder << 1) | (value >> 31);
        value <<= 1;
        quotient <<= 1;
        if (remainder >= denominator) { remainder -= denominator; quotient |= 1u; }
    }
    return quotient;
}

Z5_IMAGE(custom_disp_image) static void level_sparse_mark(u32 surface, u32 offset);

/* Original integer trigonometry approximation; no firmware angle table. */
Z5_IMAGE(custom_disp_image) static s32 level_sine(u32 angle)
{
    if (angle >= 360u * LEVEL_UNITS) angle -= 360u * LEVEL_UNITS;
    u32 negative = angle >= 180u * LEVEL_UNITS;
    if (negative) angle -= 180u * LEVEL_UNITS;
    u32 product = angle * (180u * LEVEL_UNITS - angle);
    u32 denominator = 40500u * LEVEL_UNITS * LEVEL_UNITS - product;
    s32 value = (s32)level_div(product * (4u * LEVEL_SCALE) + (denominator >> 1), denominator);
    return negative ? -value : value;
}

#include "compose.h"

Z5_IMAGE(custom_disp_image) static u32 level_angle(s32 raw)
{
    s32 angle = (raw + 4096) >> 13;
    if (angle < 0) angle += 2880;
    if (angle >= 2880) angle -= 2880;
    return (u32)angle;
}

/* Uniform Q4 scaling keeps rotated pitch marks parallel to roll. The signed
 * magnitude uses our freestanding divider rather than a compiler ABI helper. */
Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2")))
static s32 level_layout_offset(s32 value)
{
    if (Z5_OVERLAY_OUTPUT != Z5_CUSTOM_DISP_EVF_PASS) return value;
    u32 width = level_data()->width;
    if (value < 0) return -(s32)level_div((u32)-value * width, 720u);
    return (s32)level_div((u32)value * width, 720u);
}

Z5_IMAGE(custom_disp_image) static void level_geometry(signed short p[6][4], u32 roll, s32 pitch)
{
    s32 c = Z5_LEVEL_COS(roll), s = Z5_LEVEL_SIN(roll);
    u32 ac = c < 0 ? (u32)-c : (u32)c;
    u32 as = s < 0 ? (u32)-s : (u32)s;
    /* Extend beyond every viewport edge so only pixel clipping cuts the
     * outer ends. Four pixels also cover the shadow and antialiased fringe. */
    u32 length = ac ? level_div((level_data()->width / 2u + 4u) * LEVEL_SCALE * LEVEL_COORD, ac) : 16000u;
    u32 height_limit = as ? level_div((level_data()->height / 2u + 4u) * LEVEL_SCALE * LEVEL_COORD, as) : 16000u;
    if (height_limit < length) length = height_limit;
    s32 x = c * (s32)length / (s32)LEVEL_SCALE;
    s32 y = s * (s32)length / (s32)LEVEL_SCALE;
    /* Scale the central layout uniformly by viewport width. Scaling each
     * axis independently would tilt pitch away from roll on a different aspect.
     * Apply scaling after the original Q4 calculation to keep products bounded
     * and leave LCD coordinates byte-identical. */
    s32 gx = level_layout_offset(c * (60 * LEVEL_COORD) / (s32)LEVEL_SCALE);
    s32 gy = level_layout_offset(s * (60 * LEVEL_COORD) / (s32)LEVEL_SCALE);
    p[0][0] = (s32)(level_data()->width / 2u) * LEVEL_COORD - x; p[0][1] = (s32)(level_data()->height / 2u) * LEVEL_COORD - y;
    p[0][2] = (s32)(level_data()->width / 2u) * LEVEL_COORD - gx; p[0][3] = (s32)(level_data()->height / 2u) * LEVEL_COORD - gy;
    p[1][0] = (s32)(level_data()->width / 2u) * LEVEL_COORD + gx; p[1][1] = (s32)(level_data()->height / 2u) * LEVEL_COORD + gy;
    p[1][2] = (s32)(level_data()->width / 2u) * LEVEL_COORD + x; p[1][3] = (s32)(level_data()->height / 2u) * LEVEL_COORD + y;
    pitch = -pitch;
    if (pitch > 24 * (s32)LEVEL_UNITS) pitch = 24 * (s32)LEVEL_UNITS;
    if (pitch < -24 * (s32)LEVEL_UNITS) pitch = -24 * (s32)LEVEL_UNITS;
    /* Rotate the pitch marks and their perpendicular displacement with roll.
     * Retain fractional pitch precision until the final coordinate division. */
    for (u32 i = 2; i < 4u; i++) {
        s32 u = i == 2u ? -42 : 14;
        for (u32 end = 0; end < 2u; end++, u += 28) {
            s32 offset_x = (u * c * (s32)LEVEL_UNITS + pitch * 3 * s) *
                           LEVEL_COORD / (s32)(LEVEL_SCALE * LEVEL_UNITS);
            s32 offset_y = (u * s * (s32)LEVEL_UNITS - pitch * 3 * c) *
                           LEVEL_COORD / (s32)(LEVEL_SCALE * LEVEL_UNITS);
            p[i][end * 2u] = (s32)(level_data()->width / 2u) * LEVEL_COORD +
                            level_layout_offset(offset_x);
            p[i][end * 2u + 1u] = (s32)(level_data()->height / 2u) * LEVEL_COORD +
                                 level_layout_offset(offset_y);
        }
    }
    p[4][0] = p[4][2] = p[5][0] = p[5][2] = 360;
    p[4][1] = 140; p[4][3] = 180;
    p[5][1] = 300; p[5][3] = 340;
}

Z5_IMAGE(custom_disp_image) static void level_prepare(void)
{
    level_state *l = level_data();
    l->surface = 0;
    if (Z5_OVERLAY_OUTPUT == 0u && lcd_data()->ready) {
        l->surface = lcd_data()->drawing_image; l->width = 720u; l->height = 480u;
        return;
    }
    u32 output = Z5_OVERLAY_OUTPUT;
    if (output != 0u && output != Z5_CUSTOM_DISP_EVF_PASS) return;
    const u16 *size = Z5_LEVEL_FAST_SIZE(0u);
    if (!size) return;
    u32 width = size[0], height = size[1];
    if (output == 0u) {
        if (width != 720u || height != 480u) return;
    } else if (!((width == 720u && height == 480u) ||
                 (width == 1024u && (height == 682u || height == 768u)) ||
                 (width == 1280u && height == 960u))) return;
    l->width = width; l->height = height;
    u32 buffer = Z5_LEVEL_FAST_BUFFER;
    if (!buffer) return;
    u32 mapped = Z5_LEVEL_FAST_SURFACE(buffer);
    if (mapped && !(mapped & 3u)) l->surface = mapped;
}

/* An unretired current bank is not safe to repaint. Check before pose
 * sampling or another graphics wait; level_begin retains its final guard. */
Z5_IMAGE(custom_disp_image) static u32 level_pending(void)
{
    level_state *l = level_data();
    level_prepare();
    level_backing *slots = (level_backing *)l->backing;
    for (u32 i = 0; i < 4u; i++)
        if (slots[i].surface == l->surface && slots[i].count) return 1;
    return 0;
}

/* Limit maintenance to the current primary-display bitmap. Each native range stays below
 * its 0x45000 whole-cache threshold, avoiding the global kernel-cache path. */
Z5_IMAGE(custom_disp_image) static u32 level_publish(void)
{
    level_state *l = level_data();
    if (Z5_OVERLAY_OUTPUT == 0u && Z5_CUSTOM_DISP_PRIMARY_DISPLAY == 1u &&
        l->width == 720u && l->height == 480u) return level_sparse_publish(l->surface);
    u32 bytes = l->width * l->height * 4u;
    if (!l->surface || !bytes || l->surface > 0xffffffffu - bytes) return 0;
    for (u32 offset = 0; offset < bytes; offset += 65536u) {
        u32 count = bytes - offset;
        if (count > 65536u) count = 65536u;
        Z5_LEVEL_CACHE_RANGE(l->surface + offset, count);
    }
    return 1;
}

Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2")))
static void level_retire(u32 output)
{
    level_state *l = (level_state *)disp_metadata()[23] + output;
    if (l->busy) return;
    level_sync(l);
    /* Restore before the complete native page handler, regardless of the
     * last physical pass. Both primary displays use UI bank zero. */
    l->surface = 0;
    const u16 *size = Z5_LEVEL_FAST_SIZE(0u);
    if (!size) return;
    u32 width = size[0], height = size[1];
    if (!output) {
        if (width != 720u || height != 480u) return;
    } else if (!((width == 720u && height == 480u) ||
                 (width == 1024u && (height == 682u || height == 768u)) ||
                 (width == 1280u && height == 960u))) return;
    u32 buffer = Z5_LEVEL_FAST_BUFFER;
    if (!buffer) return;
    u32 mapped = Z5_LEVEL_FAST_SURFACE(buffer);
    if (!mapped || (mapped & 3u)) return;
    l->surface = mapped; l->width = width; l->height = height;
    /* Native output passes may share their scratch bank. Restore records from
     * either output only on the CURRENT mapping, before native UI drawing.
     * Keep inaccessible offsets for a later larger pass; never touch them
     * while a smaller canvas is current or follow a historical pointer. */
    if (!l->surface) return;
    volatile u32 *surface = (volatile u32 *)l->surface;
    u32 pixels = l->width * l->height;
    for (u32 output = 0; output < 2u; output++) {
        level_state *owner = (level_state *)disp_metadata()[23] + output;
        level_backing *slots = (level_backing *)owner->backing;
        for (u32 i = 0; i < 4u; i++) {
            if (slots[i].surface != l->surface) continue;
            u32 kept = 0;
            for (u32 j = 0; j < slots[i].count; j++) {
                level_pixel p = slots[i].pixels[j];
                if (p.offset >= pixels) { slots[i].pixels[kept++] = p; continue; }
                if (surface[p.offset] == p.drawn) {
                    surface[p.offset] = p.under;
                    if (Z5_CUSTOM_DISP_PRIMARY_DISPLAY == 1u && width == 720u && height == 480u)
                        level_sparse_mark(l->surface, p.offset);
                }
            }
            slots[i].count = kept;
            owner->count = 0;
        }
    }
    __asm__ volatile ("dmb sy" ::: "memory");
    /* Restored pixels must reach the GPU BEFORE native alpha blending. */
    if (Z5_CUSTOM_DISP_PRIMARY_DISPLAY == 1u && width == 720u && height == 480u)
        level_sparse_publish(l->surface);
}

Z5_IMAGE(custom_disp_image) static u32 level_paint(void)
{
    level_state *l = level_data();
    l->entries++;
    l->hist_damage = 0;
    if (l->busy) { l->reason = 1; return 0; }
    u32 reject = !disp_view() || !disp_gui_ready() ? 2u : 0u;
    if (disp_metadata()[3]) reject |= 64u;
    if (reject) { l->reason = reject; return 0; }
    l->busy = 1;
    s32 raw_roll, raw_a, raw_b;
    Z5_LEVEL_POSE_READ(&raw_roll, &raw_a, &raw_b);
    u32 elapsed = Z5_FULL_TICK - l->window_start;
    l->window_span = elapsed < 8000u ? elapsed : 8000u;
    if (elapsed < 8000u) {
        u32 tick = Z5_FULL_TICK;
        if (l->window_seen) {
            u32 gap = tick - l->last_pose_tick;
            if (gap > l->max_gap) l->max_gap = gap;
        }
        l->last_pose_tick = tick;
        l->window_calls++;
        l->histogram_count = disp_metadata()[13] - l->histogram_origin;
        if (!l->window_seen || raw_roll != l->window_roll || raw_a != l->window_a || raw_b != l->window_b)
            l->window_changes++;
        l->window_roll = raw_roll; l->window_a = raw_a; l->window_b = raw_b;
        l->window_seen = 1;
    }
    u32 valid_roll = raw_roll >= -23592960 && raw_roll <= 23592960 && raw_roll != -1;
    u32 valid_a = raw_a >= 0 && raw_a <= 23592960;
    u32 valid_b = raw_b >= 0 && raw_b <= 23592960;
    u32 roll = valid_roll ? level_angle(raw_roll) : 0;
    u32 a = valid_a ? level_angle(raw_a) : 0;
    u32 b = valid_b ? level_angle(raw_b) : 0;
    u32 status = valid_roll && (valid_a || valid_b) ? 0u : 6u;
    u32 angle = a > b ? a : b;
    u32 negative = a >= 180u * LEVEL_UNITS || b >= 180u * LEVEL_UNITS;
    if (angle >= 180u * LEVEL_UNITS) angle -= 180u * LEVEL_UNITS;
    if (angle > 90u * LEVEL_UNITS) angle = 180u * LEVEL_UNITS - angle;
    s32 pitch = negative ? -(s32)angle : (s32)angle;
    u32 now = Z5_FULL_TICK;
    u32 repair = l->force || now - l->last_tick >= 150u;
    l->hist_damage = repair;
    if (Z5_OVERLAY_OUTPUT != 0u || !lcd_data()->ready)
    level_sync(l);
    level_prepare();
    if (!level_begin()) { l->busy = 0; return 0; }
    /* Erase our preceding strokes. Only the legacy variant also clears the
     * native horizon center rectangle. Never clear the whole canvas. */
    l->count = 0;
    if (status < 6u && roll < 360u * LEVEL_UNITS && a < 360u * LEVEL_UNITS && b < 360u * LEVEL_UNITS) {
        /* The native pitch painter uses the larger axis and folds about 180
         * degrees. Preserve its half-turn sign convention; camera calibration
         * and the direction of visible motion remain a hardware check. */
        l->last_roll = (roll + LEVEL_UNITS / 2u) / LEVEL_UNITS;
        if (l->last_roll == 360u) l->last_roll = 0;
        l->last_pitch = pitch < 0 ? (u32)-((-pitch + (s32)LEVEL_UNITS / 2) / (s32)LEVEL_UNITS) :
                                  (u32)((pitch + (s32)LEVEL_UNITS / 2) / (s32)LEVEL_UNITS);
        level_geometry(l->previous, roll, pitch);
        l->count = 4u;
        u32 upright = roll;
        while (upright >= 90u * LEVEL_UNITS) upright -= 90u * LEVEL_UNITS;
        u32 roll_color = upright <= LEVEL_UNITS || upright >= 89u * LEVEL_UNITS ? 4u : 1u;
        u32 pitch_color = angle <= LEVEL_UNITS ? 4u : 1u;
        for (u32 i = 0; i < 4u; i++)
            level_antialias(l->previous[i], i < 2u ? roll_color : pitch_color);
        level_flush();
        if (!l->surface) l->reason = 32;
    } else {
        l->reason = 128;
        l->last_roll = l->last_pitch = 0xfffu;
    }
    l->key_roll = roll; l->key_pitch = (u32)pitch;
    l->last_tick = now; l->force = 0;
    l->paints++;
    l->busy = 0;
    __asm__ volatile ("dmb sy" ::: "memory");
    return 1;
}
