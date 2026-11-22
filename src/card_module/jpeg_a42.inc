/* Add zero-centered, triangular luma grain to the JPEG encoder input. */
#define A42_JPEG_KIND  0xC252856Cu /* Z5 C1.50 setting 0x0501, 3 = JPEG only */
#define A42_S1 0x100AE75Bu
#define A42_S2 0x100AE8B9u
#define A42_S3 0x100AEAF5u
#define A42_S4 0x100AEC69u
#define A42_S5 0x100AEE01u
#ifdef JPEG_A44
#include "a44_strength.inc"
#endif
#ifdef JPEG_A43
#include "jpeg_a43.inc"
#ifdef JPEG_A44
#include "jpeg_a44.inc"
#if defined(LIGHTROOM_GRAIN)
#define A42_DONE 0xA47u
#elif defined(JPEG_A45)
#define A42_DONE 0xA45u
#else
#define A42_DONE 0xA44u
#endif
#else
#define A42_DONE 0xA43u
#endif
#else
#define A42_DONE 0xA42u
#endif

IMG_FN static u32 a42_short_mmu(void)
{
    u32 cpsr, sctlr, ttbcr;
    __asm__ volatile ("mrs %0, cpsr" : "=r"(cpsr));
    if ((cpsr & 31u) != 0x13u && (cpsr & 31u) != 0x1Fu) return 0u;
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 2" : "=r"(ttbcr));
    return (sctlr & 1u) && !(ttbcr & 0x80000000u);
}

#ifndef JPEG_A43
static inline __attribute__((always_inline)) u32 a42_grain_word(u32 v, u32 *seed)
{
    u32 r = *seed;
    r ^= r << 13; r ^= r >> 17; r ^= r << 5;
    *seed = r;
    u32 a = r & 0x07070707u, b = (r >> 4) & 0x07070707u;
    u32 pos, neg;
    /* Separate positive and negative parts before saturation so 0 and 255
     * obey clamp(luma + a - b) for each lane independently. */
    __asm__("uqsub8 %0, %2, %3\nuqsub8 %1, %3, %2"
            : "=&r"(pos), "=r"(neg) : "r"(a), "r"(b));
    __asm__("uqadd8 %0, %0, %1\nuqsub8 %0, %0, %2"
            : "+&r"(v) : "r"(pos), "r"(neg));
    return v;
}
#endif

IMG_FN static void a42_rows(volatile u32 *luma, u32 pitch, u32 words, u32 rows, u32 seed
#ifdef JPEG_A43
                            , const u8 *data, u32 preview
#endif
#ifdef JPEG_A44
                           , u32 strength
#endif
#ifdef JPEG_A50
                           , u32 kernel
#endif
)
{
    u32 block[8];
#ifdef JPEG_A43
#if defined(JPEG_A50)
    const u8 *lut = data + 2048u * (strength - 1u) + (preview ? 1024u : 0u);
    const u32 *split = (const u32 *)(data + A50_LUT_BYTES);
#elif defined(JPEG_A44)
    const u8 *lut = data + 512u * (strength - 1u) + (preview ? 256u : 0u);
#else
    const u8 *lut = data + A43_TILE_BYTES + (preview ? A43_LOOKUP_BYTES : 0u);
#endif
    const u32 *grain = (const u32 *)data;
    u32 offset = 0u;
#endif
    for (u32 y = 0; y < rows; ++y, luma += pitch) {
        u32 x = 0;
        for (; x + 8u <= words; x += 8u) {
            volatile u32 *p = luma + x;
#ifdef JPEG_A43
            if (!(x & 63u)) {
                /* Keep the same phase throughout a 256x256 region. Adjacent
                 * regions choose different phases to hide tile repetition. */
                u32 phase = a43_hash(seed ^ ((y >> 8) * 0x9E3779B1u) ^ (x * 0x85EBCA6Bu));
                grain = a43_tile_row(data, y, phase);
                offset = phase & 56u;
            }
#endif
            /* Transfer 8 words per bus access group. Only the small stack
             * block is accessed wordwise while computing the grain. */
            __asm__ volatile ("ldmia %0, {r2-r8, r12}\nstmia %1, {r2-r8, r12}"
                              :: "r"(p), "r"(block) : "r2", "r3", "r4", "r5", "r6",
                                 "r7", "r8", "r12", "memory");
#ifdef JPEG_A43
            const u32 *noise = grain + ((x + offset) & 63u);
#if defined(JPEG_A50)
            /* x and offset are multiples of 8: the 8 field words never wrap inside a group. */
            const u32 *fw = split + 2u * (u32)(noise - (const u32 *)data);
            if (kernel == 1u)
                a50_group(block, fw, lut);
            else if (kernel == 2u)
                a50_group2(block, fw, lut);
            else
                for (u32 i = 0; i < 8u; ++i) block[i] = a50_grain_word(block[i], fw + 2u * i, lut);
#elif defined(JPEG_A44)
            u32 base_offset = (u32)noise - (u32)data;
            for (u32 i = 0; i < 8u; ++i)
                block[i] = a44_grain_word(block[i], data, lut, base_offset + 4u * i);
#else
            for (u32 i = 0; i < 8u; ++i) block[i] = a43_grain_word(block[i], noise[i], lut);
#endif
#else
            for (u32 i = 0; i < 8u; ++i) block[i] = a42_grain_word(block[i], &seed);
#endif
            __asm__ volatile ("ldmia %1, {r2-r8, r12}\nstmia %0, {r2-r8, r12}"
                              :: "r"(p), "r"(block) : "r2", "r3", "r4", "r5", "r6",
                                 "r7", "r8", "r12", "memory");
        }
        /* The 1620-wide preview has 20 visible bytes after its last group. */
#ifdef JPEG_A43
        if (!(x & 63u) && x < words) {
            u32 phase = a43_hash(seed ^ ((y >> 8) * 0x9E3779B1u) ^ (x * 0x85EBCA6Bu));
            grain = a43_tile_row(data, y, phase);
            offset = phase & 56u;
        }
        for (; x < words; ++x) {
#if defined(JPEG_A50)
            luma[x] = a50_grain_word(luma[x], split + 2u * (u32)((grain + ((x + offset) & 63u)) - (const u32 *)data), lut);
#elif defined(JPEG_A44)
            u32 base_offset = (u32)(grain + ((x + offset) & 63u)) - (u32)data;
            luma[x] = a44_grain_word(luma[x], data, lut, base_offset);
#else
            luma[x] = a43_grain_word(luma[x], grain[(x + offset) & 63u], lut);
#endif
        }
#else
        for (; x < words; ++x) luma[x] = a42_grain_word(luma[x], &seed);
#endif
    }
}

IMG_FN static u32 a42_ms(void)
{
    __asm__ volatile ("dsb sy" ::: "memory");
    return *(volatile u32 *)CM_MS_CLOCK;
}

IMG_FN static void jpeg_a42(u32 *q, const volatile u8 *desc, u32 call, u32 seed
#ifdef JPEG_A43
                            , const u8 *data
#endif
#ifdef JPEG_A44
                            , u32 strength
#endif
#ifdef JPEG_A50
                            , u32 kernel
#endif
)
{
#ifdef JPEG_A45
    q[23] = *(const volatile u32 *)A42_JPEG_KIND;
#endif
#ifdef LIGHTROOM_GRAIN
    /* RAW-only requests have no independent JPEG. Only their full embedded
     * JPEG may receive grain explicitly. The paired S2 inherits it from S1. */
    u32 raw_preview = q[23] == 1u && call == A42_S2;
#else
    const u32 raw_preview = 0u;
#endif
    q[16] = 1u;
    if (!raw_preview && call != A42_S1 && call != A42_S3 && call != A42_S4 && call != A42_S5) return;
    q[16] = 2u;
#ifdef JPEG_A45
    if (!raw_preview && q[23] != 3u && q[23] != 0u) return;
    if (q[23] == 0u && call != A42_S1) {
        q[16] = 8u; /* RAW+JPEG: do not write the two sets of preview inputs. */
        return;
    }
#else
    q[23] = *(const volatile u32 *)A42_JPEG_KIND;
    if (q[23] != 3u) return;
#endif

    u32 pitch = desc[2] | (u32)desc[3] << 8;
    u32 width = desc[10] | (u32)desc[11] << 8;
    u32 rows = desc[12] | (u32)desc[13] << 8;
    u32 yb = ((const volatile u32 *)desc)[5], cb = ((const volatile u32 *)desc)[6];
    q[16] = 3u;
    if (desc[0] != 1u || ((yb | width | pitch) & 3u) || pitch < width) return;
    if (call == A42_S1 || raw_preview) {
        if (width < 3008u || width > 6016u || rows < 2008u || rows > 4016u || pitch != width ||
            (yb != 0xF4C7D000u && yb != 0xF7AD8800u && yb != 0xFA934000u) || cb >= 0xFE000000u)
            return;
    } else {
        if (yb < 0xCE000000u || cb >= 0xD0000000u) return;
        if (call == A42_S3 && (width != 160u || pitch != 160u || rows != 120u)) return;
        if (call == A42_S4 && (width != 640u || pitch != 640u || rows != 424u)) return;
        if (call == A42_S5 && (width != 1620u || pitch != 1664u || rows != 1080u)) return;
    }
    u32 end = yb + (rows - 1u) * pitch + width;
    if (end > cb) return;
    q[16] = 4u;
    if (!a42_short_mmu()) return;

#ifdef JPEG_A43
    q[16] = 6u;
    if (!data) return;
#endif
    u32 begin = a42_ms();
    q[16] = 5u;
    for (u32 va = yb & ~0xFFFu; va < end; va += 0x1000u) {
        u32 par = jpeg_translate(va, 1u);
        if ((par & 0x7Du) || ((par & 2u) ? ((par ^ va) & 0xFF000000u) :
                                            ((par ^ va) & 0xFFFFF000u))) {
            q[21] = va; q[22] = par;
            return;
        }
    }
    u32 start = a42_ms();
    a42_rows((volatile u32 *)yb, pitch / 4u, width / 4u, rows, seed | 1u
#ifdef JPEG_A43
             , data, call != A42_S1 && !raw_preview
#endif
#ifdef JPEG_A44
             , a44_strength(strength)
#endif
#ifdef JPEG_A50
#ifdef A51_KERNEL2
             , 2u                                             /* A51: interleaved kernel everywhere */
#else
             , (call == A42_S1 || raw_preview) ? kernel : 1u   /* previews: the sequential kernel */
#endif
#endif
    );
    u32 done = a42_ms();
    q[17] = done - start;
    q[18] = width * rows;
    q[19] = start - begin;
    q[20] = done - begin;
    q[16] = A42_DONE;
}
