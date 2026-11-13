/* Dirty row bounds only: no screen pixels or full-frame copies. */
typedef struct {
    u32 surface, width, height;
    u16 rows[CUSTOM_DISP_MAX_HEIGHT][2];
} sparse_dirty;

Z5_IMAGE(custom_disp_image) static void sparse_reset(sparse_dirty *s, u32 surface, u32 width, u32 height)
{
    s->surface = surface; s->width = width; s->height = height;
    for (u32 y = 0; y < CUSTOM_DISP_MAX_HEIGHT; y++) {
        s->rows[y][0] = 0xffffu;
        s->rows[y][1] = 0u;
    }
}

Z5_IMAGE(custom_disp_image) static void sparse_mark(sparse_dirty *s, u32 surface, u32 offset, u32 width, u32 height)
{
    if ((width != 720u && width != 1024u && width != 1280u) || !height || height > CUSTOM_DISP_MAX_HEIGHT ||
        offset >= width * height) return;
    if (s->surface != surface || s->width != width || s->height != height)
        sparse_reset(s, surface, width, height);
    /* Constant divisors keep the freestanding image free of libgcc calls. */
    u32 y = width == 720u ? offset / 720u : width == 1024u ? offset >> 10 : offset / 1280u;
    u32 x = offset - y * width;
    if (x < s->rows[y][0]) s->rows[y][0] = (u16)x;
    if (x + 1u > s->rows[y][1]) s->rows[y][1] = (u16)(x + 1u);
}

Z5_IMAGE(custom_disp_image) static u32 sparse_publish(sparse_dirty *s, u32 surface)
{
    u32 bytes = s->width * s->height * 4u;
    if (!surface || (surface & 3u)) return 0u;
    if (s->surface != surface) return 1u;
    if ((s->width != 720u && s->width != 1024u && s->width != 1280u) || !s->height ||
        s->height > CUSTOM_DISP_MAX_HEIGHT || surface > 0xffffffffu - bytes) return 0u;
    u32 first = 0, last = 0;
    for (u32 y = 0; y < s->height; y++) {
        u32 low = s->rows[y][0], high = s->rows[y][1];
        if (low >= high || high > s->width) continue;
        u32 begin = ((y * s->width + low) * 4u) & ~63u;
        u32 end = ((y * s->width + high) * 4u + 63u) & ~63u;
        if (last && begin <= last + 64u && end - first <= 65536u) {
            if (end > last) last = end;
        } else {
            if (last) Z5_LEVEL_CACHE_RANGE(surface + first, last - first);
            first = begin; last = end;
        }
    }
    if (last) Z5_LEVEL_CACHE_RANGE(surface + first, last - first);
    sparse_reset(s, surface, s->width, s->height);
    return 1u;
}
