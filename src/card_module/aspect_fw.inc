/* A84m (2026-11-25): the logic of the photo-aspect firmware hooks, moved from MAIN into the card module (user
 * 2026-11-25: free the MAIN append area). The A84m firmware (research/aspect/a84m_fx43.S in the development tree)
 * keeps only stubs that call vui_common with r0 = kind, r1 = frame; vui_entry (vui_card.inc) sends kinds 19-23
 * here and returns our value to the stub.
 *   frame r[0] r0 [1] r1 [2] r2 [3] r3 [4] r4 [5] r5 [6] r6 [7] r9 [8] r12 [9] lr; caller's stack from r[10]
 *   19 cfg    0x10142A7C entry: the shot's first configuration (caller 0x10143229, area 0, info + 0xFC == 0) ->
 *             area 8, preview kind from the block, area descriptor rebuilt, fields 2..11 from the block's override
 *   20 offset 0x1020933E, area 8 (the stub checks): r0 = the block's area offset
 *   21 trim   0x10143032 / 0x10142F6C, area 8 (the stub checks; second table only for column r9 <= 2): r1 = the
 *             block's trim row. A84: the second table (0x116481EC) is what NEF-only shots use
 *   22 hpvw   0x1034E5E4 entry: playback preview sizes of our 4:3 / 2.33 files (checked callers only); returns 0
 *             when it wrote [r0], the stub then returns to the caller
 *   23 hplay  0x1034F6D4: playback decode area 8 for a file of the current ratio
 * Same behaviour as the A84 firmware-only hooks (a84_fx43.S) with a parameter block present, which this module
 * always writes (a43_apply). "On" = area 8's group-1 pointer in RAM, as in the firmware. Only .text here (hook
 * image): no string literals or const tables. camera_verified: false. */
#define AF_K_CFG    19u
#define AF_K_OFF    20u
#define AF_K_TRIM   21u
#define AF_K_PVW    22u
#define AF_K_PLAY   23u
#define AF_PTR      0xC18C7598u     /* area 8 group 1: the firmware's switch */
#define AF_R0       0u
#define AF_R1       1u
#define AF_R2       2u
#define AF_R4       4u
#define AF_LR       9u
#define AF_STACK    10u
#define AF_SHOT     0x10143229u     /* the shot's first configuration call */

typedef void (*af_desc_fn)(u32, u32);

IMG_FN static u32 af_on(void)
{
    u32 p = *(volatile const u32 *)AF_PTR;
    return p >= RAM_LO && p < RAM_HI ? p : 0u;
}

IMG_FN static volatile u8 *af_block(void)
{
    u32 p = *(volatile const u32 *)AF_PTR;
    volatile u8 *b;
    if (p < RAM_LO || p >= RAM_HI - 0x400u) return 0;
    b = (volatile u8 *)(p - 32u + 0x200u);
    return *(volatile const u32 *)b == 0x50544152u ? b : 0;   /* 'RATP' */
}

IMG_FN static void af_cfg(u32 *r)
{
    u32 p = af_on(), s, i;
    volatile u8 *info = (volatile u8 *)r[AF_R1], *b;
    volatile u32 *log;
    if (!p) return;
    log = (volatile u32 *)(p + 192u);          /* copy + 224: calls, remapped calls */
    log[0] += 1u;
    if ((u32)info < RAM_LO || (u32)info >= RAM_HI - 0x300u) return;
    if (info[0x200] || *(volatile const u32 *)(info + 0xFCu) || r[AF_LR] != AF_SHOT) return;
    info[0x200] = 8u;
    b = af_block();
    info[0x21E] = b ? b[20] : 3u;
    log[1] += 1u;
    FW(af_desc_fn, 0x101DC3E8u)((u32)(info + 0x200), (u32)(info + 0x202));
    b = af_block();
    if (b && *(volatile const u16 *)(b + 32u)) {
        s = info[0x222];
        if (s <= 2u)
            for (i = 0; i < 20u; i += 2u)
                *(volatile u16 *)(info + 0x206u + i) = *(volatile const u16 *)(b + 32u + s * 20u + i);
    }
}

/* 1 = a 4:3 main size, 2 = 2.33:1, 0 = other; v = w | h << 16 as the firmware forms it */
IMG_FN static u32 af_shape(u32 v)
{
    if (v == (5352u | 4016u << 16) || v == (4008u | 3008u << 16) || v == (2664u | 2000u << 16)) return 1u;
    if (v == (6016u | 2576u << 16) || v == (4512u | 1936u << 16) || v == (3008u | 1288u << 16)) return 2u;
    return 0u;
}

IMG_FN static u32 af_pvw(u32 *r)
{
    u32 lr = r[AF_LR], ctx = r[AF_R4], rec, cls;
    if (lr != 0x1034E9CBu && lr != 0x10354FADu && lr != 0x10354FE5u) return 1u;
    if (ctx < RAM_LO || ctx >= RAM_HI - 0x40u) return 1u;
    rec = *(volatile const u32 *)(ctx + 12u);
    if (rec < RAM_LO || rec >= RAM_HI - 0x400u) return 1u;
    cls = af_shape((u32)*(volatile const u16 *)(rec + 0x398u) | (u32)*(volatile const u16 *)(rec + 0x39Au) << 16);
    if (!cls) return 1u;
    if (cls == 1u) *(volatile u32 *)r[AF_R0] = r[AF_R2] ? (640u | 480u << 16) : (1440u | 1080u << 16);
    else *(volatile u32 *)r[AF_R0] = r[AF_R2] ? (640u | 272u << 16) : (1920u | 824u << 16);
    return 0u;
}

IMG_FN static void af_play(u32 *r)
{
    u32 rec = r[AF_STACK], cls, ratio;
    volatile u8 *b;
    if (!af_on() || rec < RAM_LO || rec >= RAM_HI - 0x40u) return;
    cls = af_shape((u32)(int)*(volatile const s16 *)(rec + 12u) |
                   (u32)(int)*(volatile const s16 *)(rec + 14u) << 16);
    if (!cls) return;
    b = af_block();
    ratio = b ? b[21] : 1u;
    if (ratio == cls) r[AF_R0] = 8u;
}

IMG_FN static u32 af_hook(u32 kind, u32 *r)
{
    volatile u8 *b;
    if (kind == AF_K_CFG) {
#ifdef A83_TRACE
        cm_t *c = cm_get();
        if (c) a83_trace(c, kind, r);
#endif
        af_cfg(r);
    } else if (kind == AF_K_OFF) {
        if (af_on() && (b = af_block())) r[AF_R0] = (u32)(b + 4u);
    } else if (kind == AF_K_TRIM) {
        if (af_on() && (b = af_block())) r[AF_R1] = (u32)(b + 8u);
    } else if (kind == AF_K_PVW) {
        if (!af_pvw(r)) return 0u;
    } else if (kind == AF_K_PLAY) {
        af_play(r);
    }
    return kind;
}
