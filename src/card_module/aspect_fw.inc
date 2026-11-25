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
 *   24 hext   0x1034E692 entry (A85m): the extended twin of hpvw, same handling
 * A85 (Z30 v43 port, WonderLantern Z30 docs/aspect-raw-playback-v43.md): when the shape check does not take a file
 * (NEF-only shots: the record's main size is not one of our JPEG sizes), files of type 1 / 2 get the preview size the
 * file itself reports through the native file service 0x103BC7E0 (kind 4 small / 5 large; the stock prep calls it
 * the same way at 0x1034EA38), used only when it is non-zero, within 640 x 480 / 1920 x 1080 and not 3:2 (stock
 * 3:2 files keep the stock path). Not the current shooting setting: old photos keep their own sizes.
 * Same behaviour as the A84 firmware-only hooks (a84_fx43.S) with a parameter block present, which this module
 * always writes (a43_apply). "On" = area 8's group-1 pointer in RAM, as in the firmware. Only .text here (hook
 * image): no string literals or const tables. camera_verified: false. */
#define AF_K_CFG    19u
#define AF_K_OFF    20u
#define AF_K_TRIM   21u
#define AF_K_PVW    22u
#define AF_K_PLAY   23u
#define AF_K_EXT    24u
#define AF_SVC      0x103BC7E0u     /* file service: (kind, type, handle, u16 out[2]) -> 0 = ok */
#define AF_PTR      0xC18C7598u     /* area 8 group 1: the firmware's switch */
#define AF_R0       0u
#define AF_R1       1u
#define AF_R2       2u
#define AF_R4       4u
#define AF_LR       9u
#define AF_STACK    10u
#define AF_SHOT     0x10143229u     /* the shot's first configuration call */

typedef void (*af_desc_fn)(u32, u32);
typedef u32 (*af_svc_fn)(u32, u32, u32, u32);

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

IMG_FN static u32 af_caller(u32 kind, u32 lr)
{
    if (kind == AF_K_PVW) return lr == 0x1034E9CBu || lr == 0x10354FADu || lr == 0x10354FE5u;
    return lr == 0x1034E94Du || lr == 0x10354F03u || lr == 0x10354F43u;
}

/* the file's own preview size of kind q (4 small, 5 large) as w | h << 16; 0 = none or failed */
IMG_FN static u32 af_query(u32 ctx, u32 q)
{
    u16 out[2];
    u32 type = *(volatile const u8 *)(ctx + 4u);
    out[0] = 0; out[1] = 0;
    if (FW(af_svc_fn, AF_SVC)(q, type, *(volatile const u32 *)ctx, (u32)out)) return 0u;
    return (u32)out[0] | (u32)out[1] << 16;
}

/* kinds 22 / 24: 0 = handled (wrote [r0]); *how = 1 shape, 2 file query, 0 stock; *q = the query result (or ~0) */
IMG_FN static u32 af_pvw(u32 kind, u32 *r, u32 *how, u32 *q)
{
    u32 lr = r[AF_LR], ctx = r[AF_R4], rec, cls, v, w, h, small = r[AF_R2] != 0u, type;
    *how = 0u; *q = 0xFFFFFFFFu;
    if (!af_caller(kind, lr)) return 1u;
    if (ctx < RAM_LO || ctx >= RAM_HI - 0x40u) return 1u;
    rec = *(volatile const u32 *)(ctx + 12u);
    if (rec < RAM_LO || rec >= RAM_HI - 0x400u) return 1u;
    cls = af_shape((u32)*(volatile const u16 *)(rec + 0x398u) | (u32)*(volatile const u16 *)(rec + 0x39Au) << 16);
    if (cls == 1u) {
        *(volatile u32 *)r[AF_R0] = small ? (640u | 480u << 16) : (1440u | 1080u << 16);
        *how = 1u; return 0u;
    }
    if (cls == 2u) {
        *(volatile u32 *)r[AF_R0] = small ? (640u | 272u << 16) : (1920u | 824u << 16);
        *how = 1u; return 0u;
    }
    type = *(volatile const u8 *)(ctx + 4u);
    if (type != 1u && type != 2u) return 1u;
    v = af_query(ctx, small ? 4u : 5u);
    *q = v;
    w = v & 0xFFFFu; h = v >> 16;
    if (!w || !h || w > (small ? 640u : 1920u) || h > (small ? 480u : 1080u)) return 1u;
    if ((2u * w > 3u * h ? 2u * w - 3u * h : 3u * h - 2u * w) * 64u <= 2u * w) return 1u;   /* 3:2: stock */
    *(volatile u32 *)r[AF_R0] = v;
    *how = 2u;
    return 0u;
}

#ifdef A83_TRACE
/* A85m readout rows (layout in a83_trace.inc) */
IMG_FN static void a85_pvw_log(cm_t *c, u32 kind, u32 *r, u32 how, u32 q)
{
    volatile u32 *h, *d;
    u32 ctx = r[AF_R4], rec = 0, key1, key2 = 0, row, n, ms, k, type = 0xFFu;
    if (!c->dg_buf) return;
    h = a83_head(c);
    ms = *(volatile u32 *)CM_MS_CLOCK;
    h[kind == AF_K_PVW ? 10u : 11u] += 1u;
    if (a83_ram(ctx, 0x40u)) {
        type = *(volatile const u8 *)(ctx + 4u);
        rec = *(volatile const u32 *)(ctx + 12u);
        if (!a83_ram(rec, 0x400u)) rec = 0;
    }
    if (rec) key2 = *(volatile const u32 *)(rec + 0x398u);
    key1 = kind | type << 8 | (r[AF_R1] & 0xFFu) << 16 | (r[AF_R2] ? 1u : 0u) << 24;
    d = h + A83_DOFF / 4u;
    n = h[12];
    for (row = 0; row < n; row++)
        if (d[row * 16u] == r[AF_LR] && d[row * 16u + 1u] == key1 && d[row * 16u + 2u] == key2) break;
    if (row == n) {
        if (n >= A83_DCAP / 2u) { h[13] += 1u; return; }
        d += row * 16u;
        for (k = 0; k < 16u; k++) d[k] = 0u;
        d[0] = r[AF_LR]; d[1] = key1; d[2] = key2; d[5] = ms; d[15] = h[6];
        if (rec) d[3] = *(volatile const u32 *)(rec + 0x39Cu);
        for (k = 0; k < 5u; k++) {
            u32 v = 0xFFFFFFFFu;
            if (type == 1u || type == 2u) { v = af_query(ctx, k + 1u); if (!v) v = 0xFFFFFFFFu; }
            d[10u + k] = v;
        }
        h[12] = n + 1u;
    } else d += row * 16u;
    d[4] += 1u; d[6] = ms; d[7] = how; d[9] = q;
    d[8] = how && a83_ram(r[AF_R0], 4u) ? *(volatile const u32 *)r[AF_R0] : 0u;
}
#endif

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
    } else if (kind == AF_K_PVW || kind == AF_K_EXT) {
        u32 how, q, res = af_pvw(kind, r, &how, &q);
#ifdef A83_TRACE
        cm_t *c = cm_get();
        if (c) a85_pvw_log(c, kind, r, how, q);
#endif
        if (!res) return 0u;
    } else if (kind == AF_K_PLAY) {
        af_play(r);
    }
    return kind;
}
