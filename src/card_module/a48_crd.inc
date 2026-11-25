/* A48/A49 (2026-11-23): Lightroom grain written into the NEF's own XMP crd: block.
 *
 * The firmware gate at the end of the crd block (research/film-grain/z5_crd_fw.py) calls the module with kind 18
 * while bit 6 of P + 0x14 is set. r = saved {r0-r7, r12, lr}: r4 = metadata context, r7 = the block's writer
 * {cursor, bytes left, overflow}. Element writer 0x101BCFD2(writer, name, fmt, value) emits <crd:name>v</crd:name>.
 *
 * A48 on camera (records 20/01/02): the crd block of each NEF is built at capture, 1.3-1.6 s before that shot's S1
 * encode, so a queue filled at S1 hands each NEF the previous shot's settings. A49 reads the menu at crd time
 * (= the capture) and keeps the S1/S2 entries only as a record.
 * This code runs from the hook image, which carries .text only: no string literals or const tables here. Names
 * are built on the stack from immediates and the amounts are computed.
 *
 * Record at DIAG 0x35000: [0] 'CRD1' [1] events [2] event capacity [3] event bytes [6] encodes seen
 * [8] written blocks [9] skipped (no room).
 * Events at +0x240: 128 x 32 bytes:
 *   encode [0] 1 | call 1/2 << 16 | quality << 24, [1] ms, [2] seq, [3] flags at the encode, [4] seed,
 *          [5] grain status (q[16]), [6] context r4 of the encode
 *   crd    [0] 2 | wrote << 24, [1] ms, [2] crd seq, [3] flags used, [4] metadata context, [5] writer,
 *          [6] bytes left before, [7] bytes left after
 * flags = mode | level << 8 | (NEF write and FILM on) << 16.
 * camera_verified: false. */
#define VUI_K_CRD     18u
#define A48_OFF       0x35000u
#define A48_ECAP      128u
#define A48_MAGIC     0x31445243u     /* 'CRD1' */
#define A48_ROOM      160u            /* three elements need about 115 bytes */
#define A48_ELEMENT   ((void (*)(u32, const char *, const char *, ...))0x101BCFD3u)

IMG_FN static volatile u32 *a48_head(cm_t *c)
{
    volatile u32 *h = (volatile u32 *)(c->dg_buf + A48_OFF);
    if (h[0] != A48_MAGIC) {
        for (u32 i = 0; i < (0x240u + A48_ECAP * 32u) / 4u; i++) h[i] = 0u;
        h[2] = A48_ECAP; h[3] = 32u;
        h[0] = A48_MAGIC;
    }
    return h;
}

IMG_FN static volatile u32 *a48_event(volatile u32 *h)
{
    volatile u32 *e = h + 0x240u / 4u + (h[1] & (A48_ECAP - 1u)) * 8u;
    for (u32 i = 0; i < 8u; i++) e[i] = 0u;
    h[1] += 1u;
    return e;
}

IMG_FN static u32 a48_flags(cm_t *c)
{
    u32 mode = A44_LOG(c)[A44_MODE];
    return mode | (a44_strength(A44_LOG(c)[A44_LEVEL]) << 8) |
           ((A44_LOG(c)[A47_LIGHTROOM] && (mode == 1u || mode == 2u)) ? 1u << 16 : 0u);
}

/* Record only: one entry per NEF capture (S1 of RAW+JPEG, S2 of RAW only). */
IMG_FN static void a48_push(cm_t *c, u32 call, u32 quality, u32 seed, u32 mode, u32 level, u32 enabled,
                            u32 status, u32 context)
{
    volatile u32 *h, *e;
    if (!c->dg_buf) return;
    h = a48_head(c);
    e = a48_event(h);
    e[1] = *(volatile u32 *)CM_MS_CLOCK; e[2] = h[6]; h[6] += 1u;
    e[3] = mode | (level << 8) | (enabled << 16); e[4] = seed; e[5] = status; e[6] = context;
    e[0] = 1u | ((call == A42_S1 ? 1u : 2u) << 16) | (quality << 24);
}

IMG_FN static void a48_put(volatile u32 *w, u32 n0, u32 n1, u32 n2, u32 n3, u32 value)
{
    volatile u32 name[4], fmt[1];
    name[0] = n0; name[1] = n1; name[2] = n2; name[3] = n3;
    fmt[0] = 0x7525u;                                    /* "%u" */
    A48_ELEMENT((u32)w, (const char *)name, (const char *)fmt, value);
}

IMG_FN static void a48_crd(cm_t *c, u32 *r)
{
    volatile u32 *h, *e;
    volatile u32 *w = (volatile u32 *)r[7];
    u32 flags, mode, level, wrote = 0u, before = 0u, after = 0u, seq;
    if (!c->dg_buf) return;
    h = a48_head(c);
    seq = h[7]; h[7] = seq + 1u;
    flags = a48_flags(c);
    mode = flags & 255u; level = (flags >> 8) & 255u;
    if (JL_IN(w) && !((u32)w & 3u)) {
        before = w[1];
        if ((flags >> 16) & 1u && (mode == 1u || mode == 2u) && level >= 1u && level <= 6u) {
            if (w[2] == 0u && before >= A48_ROOM) {
                u32 amount = mode == 1u ? 8u * (level + 1u) : (28u * (level + 1u) + 1u) / 3u;
                a48_put(w, 0x69617247u, 0x6F6D416Eu, 0x00746E75u, 0u, amount);         /* GrainAmount */
                a48_put(w, 0x69617247u, 0x7A69536Eu, 0x00000065u, 0u, mode == 1u ? 40u : 33u); /* GrainSize */
                a48_put(w, 0x69617247u, 0x6572466Eu, 0x6E657571u, 0x00007963u, 65u);   /* GrainFrequency */
                wrote = 1u; h[8] += 1u;
            } else {
                wrote = 2u; h[9] += 1u;
            }
        }
        /* A82: Adobe crop of the selected photo aspect (Z30 v44): the A82 block holds the text (+24) and its
         * length (+28); it closes the crd description and opens a crs: one that the stock closer ends. */
        u32 p = *(volatile const u32 *)0xC18C7598u;
        if (p >= RAM_LO && p < RAM_HI - 0x400u) {
            volatile const u32 *pb = (volatile const u32 *)(p - 32u + 0x200u);
            if (pb[0] == 0x50544152u && pb[6] && w[2] == 0u && w[1] > pb[7] + 1u) {
                ((void (*)(u32, const char *, ...))0x101BCFC1u)((u32)w, (const char *)pb[6]);
                wrote |= 4u;
            }
        }
        after = w[1];
    }
    e = a48_event(h);
    e[1] = *(volatile u32 *)CM_MS_CLOCK; e[2] = seq; e[3] = flags; e[4] = r[4]; e[5] = r[7];
    e[6] = before; e[7] = after;
    e[0] = 2u | (wrote << 24);
}
