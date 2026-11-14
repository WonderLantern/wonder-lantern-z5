/* Q4 endpoints and finite, perpendicular-ended strokes. The interior uses
 * the existing pixel integral; only endpoint pixels need polygon clipping. */
Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2")))
static u32 level_cap_area(s32 distance, s32 slope, s32 near, s32 far, s32 radius)
{
    volatile s32 vertices[2][12][2];
    for (u32 i = 0; i < 4u; i++) {
        vertices[0][i][0] = i == 0u || i == 3u ? -128 : 128;
        vertices[0][i][1] = i < 2u ? -128 : 128;
    }
    u32 count = 4, bank = 0;
    for (u32 plane = 0; plane < 4u && count; plane++) {
        s32 a, b, c;
        if (plane == 0u) { a = -slope; b = 256; c = (distance - radius) * 256; }
        else if (plane == 1u) { a = slope; b = -256; c = (-distance - radius) * 256; }
        else if (plane == 2u) { a = -256; b = -slope; c = -near * 256; }
        else { a = 256; b = slope; c = -far * 256; }
        u32 out = 0;
        s32 px = vertices[bank][count - 1u][0], py = vertices[bank][count - 1u][1];
        s32 previous = a * px + b * py + c;
        for (u32 i = 0; i < count; i++) {
            s32 x = vertices[bank][i][0], y = vertices[bank][i][1];
            s32 current = a * x + b * y + c;
            if ((previous <= 0) != (current <= 0)) {
                u32 numerator = previous < 0 ? (u32)-previous : (u32)previous;
                s32 difference = previous - current;
                u32 denominator = difference < 0 ? (u32)-difference : (u32)difference;
                u32 t = level_div(numerator << 8, denominator);
                if (out == 12u) return 0;
                vertices[bank ^ 1u][out][0] = px + (((x - px) * (s32)t) >> 8);
                vertices[bank ^ 1u][out++][1] = py + (((y - py) * (s32)t) >> 8);
            }
            if (current <= 0) {
                if (out == 12u) return 0;
                vertices[bank ^ 1u][out][0] = x;
                vertices[bank ^ 1u][out++][1] = y;
            }
            px = x; py = y; previous = current;
        }
        count = out; bank ^= 1u;
    }
    s32 sum = 0;
    for (u32 i = 0; i < count; i++) {
        u32 j = i + 1u == count ? 0u : i + 1u;
        sum += vertices[bank][i][0] * vertices[bank][j][1] -
               vertices[bank][j][0] * vertices[bank][i][1];
    }
    u32 area = (u32)(sum < 0 ? -sum : sum) >> 1;
    return area > 65536u ? 65536u : area;
}

Z5_IMAGE(custom_disp_image) __attribute__((optimize("no-if-conversion,no-if-conversion2")))
static void level_antialias(const signed short *p, u32 color)
{
    s32 x0 = p[0], y0 = p[1], x1 = p[2], y1 = p[3];
    s32 dx = x1 - x0, dy = y1 - y0;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    u32 steep = dy > dx;
    if (steep) { s32 t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
    if (x0 > x1) { s32 t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    u32 major = (u32)(x1 - x0);
    if (!major) return;
    s32 delta = y1 - y0;
    u32 minor = delta < 0 ? (u32)-delta : (u32)delta;
    u32 length = level_root(major * major + minor * minor);
    /* The higher-resolution EVF needs a heavier core. Retain the LCD's
     * exact coverage and the one-pixel shadow on both native surfaces. */
    u32 radius = Z5_OVERLAY_OUTPUT == Z5_CUSTOM_DISP_EVF_PASS ? 576u : 384u;
    u32 core = level_div(length * radius, major);
    s32 step = (s32)level_div(minor << 16, major);
    u32 slope = ((u32)step + 128u) >> 8;
    u32 reciprocal = slope ? level_div(1u << 23, slope) : 0;
    u32 outer = core + 256u + slope, edge = (256u + slope) >> 1;
    s32 tangent = (s32)slope;
    if (delta < 0) { step = -step; tangent = -tangent; }
    s32 margin = (s32)((outer + 255u) >> 8) + 1;
    u32 rgb = (Z5_LEVEL_FAST_COLOR(color) >> 8) & 0x00ffffffu;
    for (s32 x = (x0 >> 4) - margin; x <= ((x1 + 15) >> 4) + margin; x++) {
        s32 center = y0 * 16 + (((x * 16 - x0) * step + 2048) >> 12);
        s32 first = (center - (s32)outer - (s32)edge) >> 8;
        s32 last = (center + (s32)outer + (s32)edge) >> 8;
        for (s32 y = first; y <= last; y++) {
            s32 near = (x * 16 - x0) * 16 + ((tangent * (y * 16 - y0)) >> 4);
            s32 far = (x1 - x * 16) * 16 + ((tangent * (y1 - y * 16)) >> 4);
            if (near <= -(s32)edge || far <= -(s32)edge) continue;
            s32 distance = y * 256 - center;
            u32 alpha, light;
            if (near < (s32)edge || far < (s32)edge) {
                alpha = level_cap_area(distance, tangent, near, far, (s32)outer);
                light = level_cap_area(distance, tangent, near, far, (s32)core);
            } else {
                if (distance < 0) distance = -distance;
                alpha = level_area_cdf(distance + (s32)outer, slope, reciprocal) -
                        level_area_cdf(distance - (s32)outer, slope, reciprocal);
                light = level_area_cdf(distance + (s32)core, slope, reciprocal) -
                        level_area_cdf(distance - (s32)core, slope, reciprocal);
            }
            if (!alpha) continue;
            u32 pixel = ((alpha * 255u + 32768u) >> 16) << 24;
            for (u32 shift = 0; shift < 24u; shift += 8u)
                pixel |= ((((rgb >> shift) & 255u) * light + 32768u) >> 16) << shift;
            level_cover(steep ? y : x, steep ? x : y, pixel);
        }
    }
}
