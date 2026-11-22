/* A43 uses one 256x256 periodic grain field and a packed luma lookup.
 * The field and lookup are assembled once before the capture hook can run. */
#if JPEG_A43 != 1 && JPEG_A43 != 2
#error JPEG_A43 must be 1 (Fuji 400) or 2 (monochrome)
#endif
#define A43_EDGE 256u
#define A43_TILE_BYTES (A43_EDGE * A43_EDGE)
#define A43_LOOKUP_BYTES (256u * 64u)
#define A43_DATA_BYTES (A43_TILE_BYTES + 2u * A43_LOOKUP_BYTES)
#define A43_TEMP_BYTES (A43_TILE_BYTES * 2u)

#if JPEG_A43 == 1
#define A43_K1 34
#define A43_K2 17
#define A43_K3 -2
#define A43_SCALE 14
#else
#define A43_K1 30
#define A43_K2 12
#define A43_K3 -4
#define A43_SCALE 16
#endif

static inline __attribute__((always_inline)) u32 a43_hash(u32 value)
{
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    return value ^ (value >> 16);
}

static inline __attribute__((always_inline)) int a43_white(u32 row, u32 x)
{
    u32 r = a43_hash((row << 8) | (x & 255u));
    return (int)(r & 255u) - (int)((r >> 8) & 255u);
}

static inline __attribute__((always_inline)) int a43_clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

/* Only the resulting buffers survive module initialization. */
static void a43_prepare(u8 *data, short *tmp
#ifdef JPEG_A44
                        , u32 profile, u8 *tone
#endif
)
{
#ifdef JPEG_A44
    const int k1 = profile == 1u ? 34 : 30;
    const int k2 = profile == 1u ? 17 : 12;
    const int k3 = profile == 1u ? -2 : -4;
    const int scale = profile == 1u ? 14 : 16;
#else
    const int k1 = A43_K1, k2 = A43_K2, k3 = A43_K3, scale = A43_SCALE;
#endif
    for (u32 y = 0; y < A43_EDGE; ++y) {
        for (u32 x = 0; x < A43_EDGE; ++x) {
            int n = 64 * a43_white(y, x) +
                k1 * (a43_white(y, x - 1u) + a43_white(y, x + 1u)) +
                k2 * (a43_white(y, x - 2u) + a43_white(y, x + 2u)) +
                k3 * (a43_white(y, x - 3u) + a43_white(y, x + 3u));
            tmp[(y << 8) | x] = (short)((n + 32) >> 6);
        }
    }
    for (u32 y = 0; y < A43_EDGE; ++y) {
        const short *a = tmp + (((y - 1u) & 255u) << 8);
        const short *b = tmp + (((y + 1u) & 255u) << 8);
        const short *c = tmp + (((y - 2u) & 255u) << 8);
        const short *d = tmp + (((y + 2u) & 255u) << 8);
        const short *e = tmp + (((y - 3u) & 255u) << 8);
        const short *f = tmp + (((y + 3u) & 255u) << 8);
        for (u32 x = 0; x < A43_EDGE; ++x) {
            int n = 64 * tmp[(y << 8) | x] + k1 * (a[x] + b[x]) +
                    k2 * (c[x] + d[x]) + k3 * (e[x] + f[x]);
            int level = (n * scale + 8192) >> 14;
            data[(y << 8) | x] = (u8)(a43_clamp(level, -31, 31) + 32);
        }
    }

#if defined(JPEG_A44)
    const unsigned short fuji[9] = {0, 77, 118, 156, 182, 151, 109, 64, 0};
    const unsigned short mono[9] = {0, 102, 128, 174, 187, 169, 139, 77, 0};
    const unsigned short *gain = profile == 1u ? fuji : mono;
    for (u32 y = 0; y < 256u; ++y) {
        u32 k = y >> 5, f = y & 31u;
        u32 gain_q8 = (gain[k] * (32u - f) + gain[k + 1u] * f + 16u) >> 5;
        for (u32 level = 0; level < A44_STRENGTHS; ++level) {
            u32 scaled = gain_q8 * (level + 2u);
            tone[512u * level + y] = (u8)((scaled + 32u) >> 6);
            tone[512u * level + 256u + y] = (u8)((scaled * 5u + 256u) >> 9);
        }
    }
#else
#if JPEG_A43 == 1
    /* Q8 gain at luma 0, 32, ..., 256. The field's RMS is approximately 10. */
    const unsigned short gain[9] = {0, 77, 118, 156, 182, 151, 109, 64, 0};
#else
    const unsigned short gain[9] = {0, 102, 128, 174, 187, 169, 139, 77, 0};
#endif
    u8 *main_lut = data + A43_TILE_BYTES;
    u8 *preview_lut = main_lut + A43_LOOKUP_BYTES;
    for (u32 y = 0; y < 256u; ++y) {
        u32 k = y >> 5, f = y & 31u;
        int strength = (int)((gain[k] * (32u - f) + gain[k + 1u] * f + 16u) >> 5);
        int preview_strength = (strength * 5 + 4) >> 3;
        for (u32 i = 0; i < 64u; ++i) {
            int n = (int)i - 32;
            main_lut[(y << 6) | i] =
                (u8)a43_clamp((int)y + ((n * strength + 128) >> 8), 0, 255);
            preview_lut[(y << 6) | i] =
                (u8)a43_clamp((int)y + ((n * preview_strength + 128) >> 8), 0, 255);
        }
    }
#endif
}

static inline __attribute__((always_inline)) const u32 *a43_tile_row(const u8 *field, u32 y, u32 phase)
{
    return (const u32 *)(field + (((y + (phase >> 8)) & 255u) << 8));
}

static inline __attribute__((always_inline)) u32 a43_grain_word(u32 v, u32 n, const u8 *lut)
{
    u32 a = lut[((v & 255u) << 6) | (n & 255u)];
    u32 b = lut[(((v >> 8) & 255u) << 6) | ((n >> 8) & 255u)];
    u32 c = lut[(((v >> 16) & 255u) << 6) | ((n >> 16) & 255u)];
    u32 d = lut[((v >> 24) << 6) | (n >> 24)];
    return a | (b << 8) | (c << 16) | (d << 24);
}
