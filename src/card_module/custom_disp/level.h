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
    u32 busy, reason;
    signed short previous[6][4];
    u32 surface, force;
    volatile u32 drive_fault;
    u32 backing, current;
    u32 width, height;
} level_state;

Z5_IMAGE(custom_disp_image) static level_state *level_data(void)
{
    return (level_state *)disp_metadata()[22] + (Z5_OVERLAY_OUTPUT == Z5_CUSTOM_DISP_EVF_PASS);
}


Z5_IMAGE(custom_disp_image) static void level_sparse_mark(u32 surface, u32 offset, u32 width, u32 height)
{
    sparse_mark(&overlay_data()->sparse, surface, offset, width, height);
}

Z5_IMAGE(custom_disp_image) static u32 level_sparse_publish(u32 surface)
{
    return sparse_publish(&overlay_data()->sparse, surface);
}

/* The shared tail callback measures this wait on both outputs. */
Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2")))
static void level_sync(void)
{
    Z5_LEVEL_SYNC();
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
    if (disp_pass_matches(disp_output()) && overlay_data()->ready) {
        l->surface = overlay_data()->drawing_image;
        return;
    }
}

/* Publish only ranges covering restored or drawn pixels on either output. */
Z5_IMAGE(custom_disp_image) static u32 level_publish(void)
{
    return level_sparse_publish(level_data()->surface);
}

Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2")))
static void level_retire(u32 output)
{
    level_state *l = (level_state *)disp_metadata()[22] + output;
    if (l->busy) return;
    level_sync();
    /* Restore before the complete native page handler, regardless of the
     * last physical pass. Both primary displays use UI bank zero. */
    l->surface = 0;
    const u16 *size = Z5_LEVEL_FAST_SIZE(0u);
    if (!size) return;
    u32 width = size[0], height = size[1];
    if (!disp_size_supported(output, width, height)) return;
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
        level_state *owner = (level_state *)disp_metadata()[22] + output;
        level_backing *slots = (level_backing *)owner->backing;
        for (u32 i = 0; i < 4u; i++) {
            if (slots[i].surface != l->surface) continue;
            u32 kept = 0;
            for (u32 j = 0; j < slots[i].count; j++) {
                level_pixel p = slots[i].pixels[j];
                if (p.offset >= pixels) { slots[i].pixels[kept++] = p; continue; }
                if (surface[p.offset] == p.drawn) {
                    surface[p.offset] = p.under;
                    level_sparse_mark(l->surface, p.offset, width, height);
                }
            }
            slots[i].count = kept;
        }
    }
    __asm__ volatile ("dmb sy" ::: "memory");
    /* Restored pixels must reach the GPU BEFORE native alpha blending. */
    level_sparse_publish(l->surface);
}

Z5_IMAGE(custom_disp_image) static u32 level_paint(void)
{
    level_state *l = level_data();
    if (l->busy) { l->reason = 1; return 0; }
    u32 reject = !overlay_data()->ready || !disp_view() || !disp_gui_ready() ? 2u : 0u;
    if (disp_metadata()[3]) reject |= 64u;
    if (reject) { l->reason = reject; return 0; }
    l->busy = 1;
    s32 raw_roll, raw_a, raw_b;
    Z5_LEVEL_POSE_READ(&raw_roll, &raw_a, &raw_b);
    if (!disp_view() || !overlay_data()->ready ||
        Z5_LEVEL_FAST_BUFFER != overlay_data()->drawing_bank) {
        l->reason = 2u; l->busy = 0; return 0;
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
    level_prepare();
    if (!level_begin()) { l->busy = 0; return 0; }
    if (status < 6u && roll < 360u * LEVEL_UNITS && a < 360u * LEVEL_UNITS && b < 360u * LEVEL_UNITS) {
        /* The native pitch painter uses the larger axis and folds about 180
         * degrees. Preserve its half-turn sign convention; camera calibration
         * and the direction of visible motion remain a hardware check. */
        level_geometry(l->previous, roll, pitch);
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
    }
    l->force = 0;
    l->busy = 0;
    __asm__ volatile ("dmb sy" ::: "memory");
    return 1;
}
