/* Pre-scale packed grain at boot. Capture uses one tone bucket per four
 * adjacent pixels, one packed grain load and saturating byte operations. */
#define A44_TONE_BYTES (512u * A44_STRENGTHS)
#define A44_LEVELS 21u
#if defined(JPEG_A50)
/* A50 (2026-11-23): same output as A45, fewer instructions per word.
 * Lookup indexed by the USAD8 sum (no shift): per strength 1024 main + 1024 preview bytes.
 * Field split at boot: per field word {n0 | n2 << 16, (n1 | n3 << 16) << 4}, so each lane pair needs one MLA
 * and one AND (the odd rounding and shift are folded into the stored << 4). */
#define A50_LUT_BYTES (2048u * A44_STRENGTHS)
#define A44_DATA_BYTES (A50_LUT_BYTES + 2u * A43_TILE_BYTES)
#define A44_WORK_BYTES (A43_TEMP_BYTES + A43_TILE_BYTES + A44_TONE_BYTES)
#elif defined(JPEG_A45)
#define A44_DATA_BYTES (A44_TONE_BYTES + A43_TILE_BYTES)
#define A44_WORK_BYTES A43_TEMP_BYTES
#else
#define A44_DATA_BYTES (A44_TONE_BYTES + A44_LEVELS * A43_TILE_BYTES)
#define A44_WORK_BYTES (A43_TEMP_BYTES + A43_TILE_BYTES)
#endif

static void a44_prepare(u8 *data, u8 *work, u32 mode)
{
#if defined(JPEG_A50)
    u8 *tile = work + A43_TEMP_BYTES, *tone = tile + A43_TILE_BYTES;
    a43_prepare(tile, (short *)work, mode, tone);
    for (u32 s = 0; s < 2u * A44_STRENGTHS; ++s)          /* [strength][main, preview] */
        for (u32 sum = 0; sum < 1024u; ++sum)
            data[s * 1024u + sum] = tone[s * 256u + (sum >> 2)];
    u32 *split = (u32 *)(data + A50_LUT_BYTES);
    for (u32 i = 0; i < A43_TILE_BYTES / 4u; ++i) {
        u32 w = ((const u32 *)tile)[i];
        split[2u * i] = w & 0x00FF00FFu;
        split[2u * i + 1u] = ((w >> 8) & 0x00FF00FFu) << 4;
    }
#elif defined(JPEG_A45)
    a43_prepare(data + A44_TONE_BYTES, (short *)work, mode, data);
#else
    u8 *tile = work + A43_TEMP_BYTES;
    a43_prepare(tile, (short *)work, mode, data);
    u8 *packed = data + A44_TONE_BYTES;
    for (u32 step = 0; step < A44_LEVELS; ++step) {
        for (u32 i = 0; i < A43_TILE_BYTES; ++i) {
            int n = (int)tile[i] - 32;
            packed[(step << 16) | i] = (u8)((n * (int)step + 8) >> 4);
        }
    }
#endif
}

#if defined(JPEG_A50)
/* lut = this strength's 1024-byte table; f = {even lanes, odd lanes << 4} of the field word. */
static inline __attribute__((always_inline)) u32 a50_grain_word(u32 v, const u32 *f, const u8 *lut)
{
    u32 sum, g, b;
    __asm__("usad8 %0, %1, %2" : "=r"(sum) : "r"(v), "r"(0u));
    u32 step = lut[sum];
    u32 e = ((f[0] * step + 0x00080008u) >> 4) & 0x00FF00FFu;
    u32 o = (f[1] * step + 0x00800080u) & 0xFF00FF00u;
    g = e | o;
    b = step * 0x02020202u;
    __asm__("usub8 %0, %0, %1" : "+r"(g) : "r"(b));
    v ^= 0x80808080u;
    __asm__("qadd8 %0, %0, %1" : "+r"(v) : "r"(g));
    return v ^ 0x80808080u;
}

/* The same for the 8 words of a stack block: 15 instructions per word (GCC's version of the loop above: 22). */
static inline __attribute__((always_inline)) void a50_group(u32 *blk, const u32 *f, const u8 *lut)
{
    u32 v, s, te, to;
    __asm__ volatile(
        ".rept 8\n\t"
        "ldr     %[v], [%[b]]\n\t"
        "usad8   %[s], %[v], %[z]\n\t"
        "ldrd    %[te], %[to], [%[f]], #8\n\t"
        "ldrb    %[s], [%[l], %[s]]\n\t"
        "mla     %[te], %[te], %[s], %[re]\n\t"
        "mla     %[to], %[to], %[s], %[ro]\n\t"
        "and     %[te], %[m], %[te], lsr #4\n\t"
        "and     %[to], %[to], #0xFF00FF00\n\t"
        "orr     %[te], %[te], %[to]\n\t"
        "mul     %[s], %[s], %[k]\n\t"
        "usub8   %[te], %[te], %[s]\n\t"
        "eor     %[v], %[v], #0x80808080\n\t"
        "qadd8   %[v], %[v], %[te]\n\t"
        "eor     %[v], %[v], #0x80808080\n\t"
        "str     %[v], [%[b]], #4\n\t"
        ".endr"
        : [v] "=&r"(v), [s] "=&r"(s), [te] "=&r"(te), [to] "=&r"(to), [b] "+r"(blk), [f] "+r"(f)
        : [l] "r"(lut), [z] "r"(0u), [re] "r"(0x00080008u), [ro] "r"(0x00800080u),
          [m] "r"(0x00FF00FFu), [k] "r"(0x02020202u)
        : "memory");
}

/* Two words interleaved (18 instructions per word) so a load or multiply result is not needed by the very next
 * instruction: for an in-order core the extra instructions may cost less than the waits they hide. */
static inline __attribute__((always_inline)) void a50_group2(u32 *blk, const u32 *f, const u8 *lut)
{
    u32 v0, v1, s0, s1, e0, o0, e1, o1;
    __asm__ volatile(
        ".rept 4\n\t"
        "ldrd    %[v0], %[v1], [%[b]]\n\t"
        "ldrd    %[e0], %[o0], [%[f]], #8\n\t"
        "usad8   %[s0], %[v0], %[z]\n\t"
        "usad8   %[s1], %[v1], %[z]\n\t"
        "ldrd    %[e1], %[o1], [%[f]], #8\n\t"
        "ldrb    %[s0], [%[l], %[s0]]\n\t"
        "ldrb    %[s1], [%[l], %[s1]]\n\t"
        "eor     %[v0], %[v0], #0x80808080\n\t"
        "eor     %[v1], %[v1], #0x80808080\n\t"
        "mul     %[e0], %[e0], %[s0]\n\t"
        "mul     %[o0], %[o0], %[s0]\n\t"
        "mul     %[e1], %[e1], %[s1]\n\t"
        "mul     %[o1], %[o1], %[s1]\n\t"
        "mul     %[s0], %[s0], %[k]\n\t"
        "mul     %[s1], %[s1], %[k]\n\t"
        "add     %[e0], %[e0], #0x00080008\n\t"
        "add     %[o0], %[o0], #0x00800080\n\t"
        "add     %[e1], %[e1], #0x00080008\n\t"
        "add     %[o1], %[o1], #0x00800080\n\t"
        "lsr     %[e0], %[e0], #4\n\t"
        "and     %[o0], %[o0], #0xFF00FF00\n\t"
        "lsr     %[e1], %[e1], #4\n\t"
        "and     %[o1], %[o1], #0xFF00FF00\n\t"
        "and     %[e0], %[e0], #0x00FF00FF\n\t"
        "and     %[e1], %[e1], #0x00FF00FF\n\t"
        "orr     %[e0], %[e0], %[o0]\n\t"
        "orr     %[e1], %[e1], %[o1]\n\t"
        "usub8   %[e0], %[e0], %[s0]\n\t"
        "usub8   %[e1], %[e1], %[s1]\n\t"
        "qadd8   %[v0], %[v0], %[e0]\n\t"
        "qadd8   %[v1], %[v1], %[e1]\n\t"
        "eor     %[v0], %[v0], #0x80808080\n\t"
        "eor     %[v1], %[v1], #0x80808080\n\t"
        "strd    %[v0], %[v1], [%[b]], #8\n\t"
        ".endr"
        : [v0] "=&r"(v0), [v1] "=&r"(v1), [s0] "=&r"(s0), [s1] "=&r"(s1),
          [e0] "=&r"(e0), [o0] "=&r"(o0), [e1] "=&r"(e1), [o1] "=&r"(o1), [b] "+r"(blk), [f] "+r"(f)
        : [l] "r"(lut), [z] "r"(0u), [k] "r"(0x02020202u)
        : "memory");
}
#endif

static inline __attribute__((always_inline)) u32 a44_grain_word(u32 v, const u8 *data,
                                                               const u8 *tone, u32 field_offset)
{
    u32 sum;
    __asm__("usad8 %0, %1, %2" : "=r"(sum) : "r"(v), "r"(0u));
    const u32 base = 0x80808080u;
#ifdef JPEG_A45
    /* Each multiply stays inside its 16-bit lane (63 * 20 + 8).
     * Subtract the field's bias after Q4 rounding, modulo each byte. */
    u32 step = tone[sum >> 2];
    u32 field = *(const u32 *)(data + A44_TONE_BYTES + field_offset);
    u32 even, odd;
    __asm__("uxtb16 %0, %2\nuxtb16 %1, %2, ror #8"
            : "=&r"(even), "=r"(odd) : "r"(field));
    even = ((even * step + 0x00080008u) >> 4) & 0x00FF00FFu;
    odd = ((odd * step + 0x00080008u) << 4) & 0xFF00FF00u;
    u32 packed = even | odd;
    __asm__("usub8 %0, %0, %1" : "+r"(packed) : "r"(0x02020202u * step));
#else
    u32 packed = *(const u32 *)(data + A44_TONE_BYTES +
                                ((u32)tone[sum >> 2] << 16) + field_offset);
#endif
    // Bias unsigned luma into signed lanes so QADD8 saturates to the JPEG range.
    v ^= base;
    __asm__("qadd8 %0, %0, %1" : "+r"(v) : "r"(packed));
    return v ^ base;
}
