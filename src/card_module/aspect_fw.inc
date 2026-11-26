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
 * A86 (A85 camera, DIAG09-11): a NEF-only file (type 1) is recorded with the FX main size 6016 x 4016 (file record
 * + 0x398, the file entry + 12 / 14 and the decode descriptor + 32 / 36 from the area table 0x10209328), while its
 * full-size JPEG is 5352 x 4016 / 6016 x 2576: zoom shows the cut-off parts as garbage, 2.33 playback is shifted
 * and hplay never matches it. When such a file's large preview (file service kind 5) is 1440 x 1080 or 1920 x 824,
 * all three get our L size, the same values a 4:3 / 2.33 JPEG of ours carries. Only on the six checked callers.
 * A87 (A86 camera, DIAG12-14): (a) instant review comes about 1 s after the shot, before the file is written, and
 * the file service gives 0 x 0: then, within AF_REVIEW_MS of the last remapped shot, the current ratio stands in for
 * the file's preview; (b) kind 25 (A87m stub 0x1034F65A): the playback decode setup's RAW branch skips hplay, so
 * NEF-only files were decoded with FX geometry (area 0); a file entry of the current ratio gets area 8 there.
 * A88 (A87 camera, DIAG15-17 + video): kind 25 never ran; NEF decode takes the other RAW paths, which meet at
 * 0x1034F6DE (kind 26). Same rule there. The zoom window (0x1034F56E) reads its main size / margins from the area
 * table by [info + 0x200]: area 8 gives our L size and margins (344, 8) / (12, 728) instead of FX.
 * A88 correction (Unicorn): 0x10209328 returns the RAW plane (FX 6040 x 4032) and area 8 uses the same plane, so
 * the area does not change that window; the A87 video matches the full-size JPEG placed in a 6040 x 4032 canvas
 * (4:3: 688 columns of garbage on the right, 2.33: 1456 rows at the bottom). The type-1 decode descriptor + 32 / 36
 * comes from that table (0x10354FFA), so A86's "6016 x 4016" test never matched it. Now: 6040 x 4032 -> our JPEG
 * size + (24, 16), the plane-like size a stock FX NEF has around its 6016 x 4016 JPEG: 5376 x 4032 / 6040 x 2592.
 * A90 (A89 camera snapshots): the last FX-sized value of our NEF files was the full-size image entry (kind 6,
 * desc + 0x60) from 0x10355DCC (output-size table by the file's area code), filled after the preview hooks.
 * Kind 27 (A90m stub) answers it with the record's main size for type-1 files whose main size is ours.
 * Same behaviour as the A84 firmware-only hooks (a84_fx43.S) with a parameter block present, which this module
 * always writes (a43_apply). "On" = area 8's group-1 pointer in RAM, as in the firmware. Only .text here (hook
 * image): no string literals or const tables. camera_verified: false. */
#define AF_K_CFG    19u
#define AF_K_OFF    20u
#define AF_K_TRIM   21u
#define AF_K_PVW    22u
#define AF_K_PLAY   23u
#define AF_K_EXT    24u
#define AF_K_NEF    25u
#define AF_K_FULL   27u             /* A90: 0x10355DCC, full-size image size of the playback prep */
#define AF_K_RAW    26u             /* A88: 0x1034F6DE, the join of the RAW paths ([r4] = 2) of the decode setup */
#define AF_K_LVSZ   28u             /* B2: 0x10197FFC / 0x1018B388, live-view window / fit area size query */
#define AF_LV_WIN   0x10198001u     /* B2: return of the window updater's selected-area query */
#define AF_LV_FIT   0x1018B38Du     /* B2: return of the display fit's area size query */
#define AF_LV_CLASS 0x101905C1u     /* B7: return of 0x10190598's area -> aspect class query (0x10190576; capture info
                                     * + 0x4E0, read by the ISP config 0x100C0C4A: class 3 -> ISP mode 4) */
#define AF_LV_CLASS2 0x10197033u    /* B8: the second class site (0x10196FE0, from capture info + 0x488 on a live-view
                                     * mode change; it wrote B7's 3 back to 0) */
#define AF_LV_CROP  0x10190625u     /* B4: return of the live-view area crop's size query (0x10190598, rect ->
                                     * 0x10188C28, LV state + 0x128; stock 1:1 (1000, 0, 5016, 4016)) */
#define AF_LV_ELBY1 0x101464CFu     /* B9: ELBY_fill (0x101460F2) call sites (kind 0x1b, sensor crop + warp out) */
#define AF_LV_ELBY2 0x1014659Fu
#define AF_LV_ELBY3 0x101465D7u
#define AF_LV_ELBY4 0x10146E4Du     /* B10: ELBY_fill kind 0x2b alloc (0x10146E26, live view) */
#define AF_LV_ELBY5 0x10146E5Fu     /* B10: ELBY_fill kind 0x2b refill (0x10146E50, live view) */
#define AF_LV_LVCROP 0x101455CBu    /* B9: LV_crop_tbl (0x10145354) call site (sensor crop helper) */
#define AF_LV_ELBR  0x10146BA7u     /* B9: ELBR 0x1020949A(area, 2) call site (1024x680 / 768x768 -> 896x672) */
#define AF_LV_MSG   0x101904ADu     /* B12: 0x101904A8 bl 0x1018D014 (live-view manager; r0 ctx, r1 message, r2 block) */
#define AF_B2_OFF   0x3AFF0u        /* B2 counters (below the B1 record's end, -DB1_LVWIN) */
#ifndef AF_AREA_NOW
#define AF_AREA_NOW (*(volatile const u32 *)0xC25043A4u)   /* actual image area (setting 0x169E) */
#endif
#ifndef AF_LV43_PAIR
#define AF_LV43_PAIR(size) a43_lv_pair(size)   /* B3: aspect_menu.inc; the firmware check has a stand-in */
#endif
#ifdef B2_LVSZ
#define AF_K_LAST   AF_K_LVSZ       /* B2 / B3 firmware (b2m / b3m_fx43.S) calls kind 28 */
#else
#define AF_K_LAST   AF_K_FULL
#endif
#define AF_REVIEW_MS 6000u          /* A87: the shot whose instant review may stand in for the file service */
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
typedef void (*af_elby_fn)(u32, u32, u32);
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
    log[2] = *(volatile const u32 *)CM_MS_CLOCK;     /* A87: last remapped shot, for the instant review */
    log[3] = b ? b[21] : 1u;
    log[4] = (u32)info;                              /* A89 readout: the shot's capture info */
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

/* A86: NEF-only main size (see the head). Returns 1 when it rewrote the sizes. meta = the file entry (caller's
 * [sp + 12] in the descriptor prep 0x10354Dxx-0x103550xx, [sp] in the file prep 0x1034E7xx-0x1034EAxx). */
IMG_FN static u32 af_nef_main(u32 kind, u32 *r, u32 ctx, u32 rec)
{
    u32 lr = r[AF_LR], v, w, h, meta, desc;
    if (*(volatile const u8 *)(ctx + 4u) != 1u) return 0u;
    if (*(volatile const u16 *)(rec + 0x398u) != 6016u || *(volatile const u16 *)(rec + 0x39Au) != 4016u) return 0u;
    v = af_query(ctx, 5u);
    if (!v) {                                  /* A87: not written yet (instant review): the shot just taken */
        u32 p = af_on();
        volatile const u32 *log = (volatile const u32 *)(p + 192u);
        if (!p || !log[1] || *(volatile const u32 *)CM_MS_CLOCK - log[2] > AF_REVIEW_MS) return 0u;
        v = log[3] == 2u ? (1920u | 824u << 16) : (1440u | 1080u << 16);
    }
    if (v == (1440u | 1080u << 16)) { w = 5352u; h = 4016u; }
    else if (v == (1920u | 824u << 16)) { w = 6016u; h = 2576u; }
    else return 0u;
    *(volatile u16 *)(rec + 0x398u) = (u16)w; *(volatile u16 *)(rec + 0x39Au) = (u16)h;
    meta = lr == 0x1034E9CBu || lr == 0x1034E94Du ? r[AF_STACK] : r[AF_STACK + 3u];
    if (meta >= RAM_LO && meta < RAM_HI - 0x20u &&
        *(volatile const u16 *)(meta + 12u) == 6016u && *(volatile const u16 *)(meta + 14u) == 4016u) {
        *(volatile u16 *)(meta + 12u) = (u16)w; *(volatile u16 *)(meta + 14u) = (u16)h;
    }
    if (lr != 0x1034E9CBu && lr != 0x1034E94Du) {
        desc = *(volatile const u32 *)(ctx + 28u);
        if (desc >= RAM_LO && desc < RAM_HI - 0x80u) {
            u32 dw = *(volatile const u32 *)(desc + 32u), dh = *(volatile const u32 *)(desc + 36u);
            if (dw == 6040u && dh == 4032u) {          /* A88: the RAW plane, as the table gives it */
                *(volatile u32 *)(desc + 32u) = w + 24u; *(volatile u32 *)(desc + 36u) = h + 16u;
            } else if (dw == 6016u && dh == 4016u) {   /* A86 case kept */
                *(volatile u32 *)(desc + 32u) = w; *(volatile u32 *)(desc + 36u) = h;
            }
        }
    }
    (void)kind;
    return 1u;
}

/* kinds 22 / 24: 0 = handled (wrote [r0]); *how = 1 shape, 2 file query, 0 stock, | 4 when A86 rewrote the NEF
 * main size; *q = the query result (or ~0) */
IMG_FN static u32 af_pvw(u32 kind, u32 *r, u32 *how, u32 *q)
{
    u32 lr = r[AF_LR], ctx = r[AF_R4], rec, cls, v, w, h, small = r[AF_R2] != 0u, type, fixed;
    *how = 0u; *q = 0xFFFFFFFFu;
    if (!af_caller(kind, lr)) return 1u;
    if (ctx < RAM_LO || ctx >= RAM_HI - 0x40u) return 1u;
    rec = *(volatile const u32 *)(ctx + 12u);
    if (rec < RAM_LO || rec >= RAM_HI - 0x400u) return 1u;
    fixed = af_nef_main(kind, r, ctx, rec) ? 4u : 0u;
    *how = fixed;
    cls = af_shape((u32)*(volatile const u16 *)(rec + 0x398u) | (u32)*(volatile const u16 *)(rec + 0x39Au) << 16);
    if (cls == 1u) {
        *(volatile u32 *)r[AF_R0] = small ? (640u | 480u << 16) : (1440u | 1080u << 16);
        *how = fixed | 1u; return 0u;
    }
    if (cls == 2u) {
        *(volatile u32 *)r[AF_R0] = small ? (640u | 272u << 16) : (1920u | 824u << 16);
        *how = fixed | 1u; return 0u;
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
/* A89 readout: a snapshot per new preview row (first 10 rows) at DIAG 0x39840 (A90), 0x200 bytes each, to compare a stock
 * NEF with ours where instant review refuses zoom and the navigator shows a strip:
 *   +0x00 ms, caller, kind | type | class | small (row key), ctx, desc, rec, file entry, last shot's capture info
 *   +0x20 decode descriptor [ctx + 28] 0xA0 bytes   +0xC0 file record + 0x380, 0x40 bytes
 *   +0x100 ctx 0x40 bytes   +0x140 file entry 0x20 bytes   +0x160 capture info + 0x200, 0x40 bytes
 *   +0x1A0 the caller's stack, 24 words (frame + 40 on) */
#define A89_SNAP_OFF  0x39840u        /* A90: 10 x 0x200 up to 0x3A840 (A89 0x3EC00 was inside the live rings) */
IMG_FN static void a89_copy(volatile u32 *dst, u32 src, u32 bytes)
{
    u32 i;
    if (!a83_ram(src, bytes)) { for (i = 0; i < bytes / 4u; i++) dst[i] = 0xDEADDEADu; return; }
    for (i = 0; i < bytes / 4u; i++) dst[i] = ((volatile const u32 *)src)[i];
}

IMG_FN static void a89_snap(cm_t *c, u32 *r, u32 ctx, u32 rec, u32 row)
{
    volatile u32 *z;
    u32 lr = r[AF_LR], meta, info = 0, p = af_on(), i;
    if (row >= 10u || !c->dg_buf) return;
    z = (volatile u32 *)(c->dg_buf + A89_SNAP_OFF + row * 0x200u);
    meta = lr == 0x1034E9CBu || lr == 0x1034E94Du ? r[AF_STACK] : r[AF_STACK + 3u];
    if (p) info = ((volatile const u32 *)(p + 192u))[4];
    z[0] = *(volatile u32 *)CM_MS_CLOCK; z[1] = lr;
    z[2] = r[AF_R1] | (r[AF_R2] ? 1u : 0u) << 8; z[3] = ctx;
    z[4] = a83_ram(ctx, 0x40u) ? *(volatile const u32 *)(ctx + 28u) : 0u; z[5] = rec; z[6] = meta; z[7] = info;
    a89_copy(z + 0x20u / 4u, z[4], 0xA0u);
    a89_copy(z + 0xC0u / 4u, rec ? rec + 0x380u : 0u, 0x40u);
    a89_copy(z + 0x100u / 4u, ctx, 0x40u);
    a89_copy(z + 0x140u / 4u, meta, 0x20u);
    a89_copy(z + 0x160u / 4u, info ? info + 0x200u : 0u, 0x40u);
    for (i = 0; i < 24u; i++) z[0x1A0u / 4u + i] = r[AF_STACK + i];
}

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
        for (k = 2; k < 5u; k++) {             /* A88: kinds 1 / 2 were always 6040 x 4032 / 160 x 120 */
            u32 v = 0xFFFFFFFFu;
            if (type == 1u || type == 2u) { v = af_query(ctx, k + 1u); if (!v) v = 0xFFFFFFFFu; }
            d[10u + k] = v;
        }
        {
            u32 desc = *(volatile const u32 *)(ctx + 28u), lr = r[AF_LR], meta;
            d[10] = a83_ram(desc, 0x80u) ?
                (*(volatile const u32 *)(desc + 32u) & 0xFFFFu) | *(volatile const u32 *)(desc + 36u) << 16 : 0u;
            meta = lr == 0x1034E9CBu || lr == 0x1034E94Du ? r[AF_STACK] : r[AF_STACK + 3u];
            d[11] = a83_ram(meta, 0x20u) ?
                (u32)*(volatile const u16 *)(meta + 12u) | (u32)*(volatile const u16 *)(meta + 14u) << 16 : 0u;
        }
        h[12] = n + 1u;
        (void)a89_snap;                        /* A91: snapshots off (A89/A90 found what they were for) */
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

/* A90 kind 27: 0x10355DCC(out, class, size) from the playback prep (callers 0x10355051 descriptor, 0x1034EA21
 * file prep; r4 = the prep's file context). 0 = handled (wrote [out]). */
IMG_FN static u32 af_full(u32 *r)
{
    u32 lr = r[AF_LR], ctx = r[AF_R4], rec, v;
    if (lr != 0x10355051u && lr != 0x1034EA21u) return 1u;
    if (ctx < RAM_LO || ctx >= RAM_HI - 0x40u || *(volatile const u8 *)(ctx + 4u) != 1u) return 1u;
    rec = *(volatile const u32 *)(ctx + 12u);
    if (rec < RAM_LO || rec >= RAM_HI - 0x400u) return 1u;
    v = (u32)*(volatile const u16 *)(rec + 0x398u) | (u32)*(volatile const u16 *)(rec + 0x39Au) << 16;
    if (!af_shape(v) || r[AF_R0] < RAM_LO || r[AF_R0] >= RAM_HI - 4u) return 1u;
    *(volatile u16 *)r[AF_R0] = (u16)(v & 0xFFFFu);
    *(volatile u16 *)(r[AF_R0] + 2u) = (u16)(v >> 16);
    return 0u;
}

/* A87 kind 25: RAW branch of the playback decode setup; r[AF_R4] = decode info, caller [sp] = file entry */
IMG_FN static u32 af_nef(u32 *r)
{
    u32 meta = r[AF_STACK], info = r[AF_R4], cls, ratio;
    volatile u8 *b;
    if (!af_on() || meta < RAM_LO || meta >= RAM_HI - 0x20u || info < RAM_LO || info >= RAM_HI - 0x300u) return 0u;
    cls = af_shape((u32)(int)*(volatile const s16 *)(meta + 12u) |
                   (u32)(int)*(volatile const s16 *)(meta + 14u) << 16);
    if (!cls) return 0u;
    b = af_block();
    ratio = b ? b[21] : 1u;
    if (ratio != cls) return 0u;
    *(volatile u8 *)(info + 0x200u) = 8u;
    return 1u;
}

/* B2 (B1 camera, DIAG17 + b1_fit_replay.py): the stock live view letterboxes by the window's own aspect (16:9
 * (0, 36, 720, 442), 1:1 (120, 0, 600, 480)); for 4:3 the stock fit gives (40, 0, 680, 480).
 * B3 (B2 camera, DIAG18-20): B2 asked for area 8 only while the 4:3 tables were switched in. The window updater
 * runs the moment the area becomes FX, before a43_watch (50 ms) switches them in, so the window stayed FX while the
 * fit (recomputed later, e.g. on an EVF switch) went 4:3: the picture was squeezed (user). Now: while 4:3 is chosen
 * and prepared, an FX query from the two live-view sites gets our own 4:3 L / M / S size (the stash copy of group
 * 1, independent of the switch), and the B3 stub returns it directly (module returns 0 = handled, frame r0 = the
 * pair). 2.33:1 stays stock (the fit puts it in the 16:9 class). Other callers of 0x102092A6 are not hooked. */
#ifdef B2_LVSZ
/* B12: live-view manager message (0x101904A8 -> 0x1018D014; r1 = message, 0x160 bytes, r2 = block right after it).
 * Its builder 0x1018CF28 reads "0x1624 present" at msg + 0x64 (value byte + 0x68) and then sets flag bit 3, which
 * makes 0x1018D014 rebuild the fit (0x1018B4A2) and the window, as on a DX -> FX switch. After a 4:3 toggle
 * a43_watch sets a43_lv_inject; the next message in FX gets 0x1624 = 0 (FX) marked present.
 * Readout (DIAG 0x37400 .. 0x37BA0, decode_b12.py): header 'B12M', [1] calls, [2] injections (bit 16: next keep
 * slot), [3] entries written; a ring of 16 entries of 8 words from 0x37410 (ms, nonzero-word masks of message words 0-31 /
 * 32-63 / 64-87, block[0], block[1], fit x0 | x1 << 16 at the call, repeats | fit h << 16 | injected << 31; equal
 * consecutive entries are counted as repeats); the last message + block at 0x37690 (0x1A0 bytes, then its fit x0|x1,
 * fit h, ms); whenever the fit changed since the previous message, that previous message is kept in one of two
 * slots (0x37840 / 0x379F0: 'B12K', its ms, fit x0 | x1 before, fit x0 | x1 after, then the 0x1A0 bytes). */
#define AF_B12_OFF   0x37400u
#define AF_B12_LAST  0x37690u
#define AF_B12_END   0x37BA0u
IMG_FN static void af_lvmsg(u32 *r)
{
#ifdef ASPECT43_FX                             /* the card module (not the offline harness, which has no cm_t) */
    cm_t *c = cm_get();
    volatile u32 *msg = (volatile u32 *)r[AF_R1], *blk = (volatile u32 *)r[AF_R2];
    volatile const u16 *fit = (volatile const u16 *)0xC23802F4u;
    u32 inj = 0u, i, f, fh, m0 = 0u, m1 = 0u, m2 = 0u;
    if ((u32)msg < RAM_LO || (u32)msg >= RAM_HI - 0x1A0u || (u32)blk != (u32)msg + 0x160u) return;
    /* B13: also when 4:3 is turned off (no AF_LV43_PAIR then), and only while the manager is active (its context
     * + 4; 0x1018D014 ignores the message otherwise, as at menu exit) */
    if (c && BTP(c) && BTP(c)->a43_lv_inject && AF_AREA_NOW == 0u && r[AF_R0] >= RAM_LO && r[AF_R0] < RAM_HI - 8u &&
        *(volatile const u8 *)(r[AF_R0] + 4u)) {
        msg[0x64u / 4u] = 1u;                  /* 0x1624 present */
        *(volatile u8 *)((u32)msg + 0x68u) = 0u;   /* = FX */
        BTP(c)->a43_lv_inject = 0u;
        inj = 1u;
    }
    if (!c || !c->dg_buf) return;
    volatile u32 *h = (volatile u32 *)(c->dg_buf + AF_B12_OFF);
    volatile u32 *last = (volatile u32 *)(c->dg_buf + AF_B12_LAST);
    if (h[0] != 0x4D323142u) {                 /* 'B12M' */
        for (i = 1u; i < (AF_B12_END - AF_B12_OFF) / 4u; i++) h[i] = 0u;
        h[0] = 0x4D323142u;
    }
    for (i = 0u; i < 88u; i++) {
        if (!msg[i]) continue;
        if (i < 32u) m0 |= 1u << i;
        else if (i < 64u) m1 |= 1u << (i - 32u);
        else m2 |= 1u << (i - 64u);
    }
    f = (u32)fit[0] | (u32)fit[2] << 16;
    fh = fit[3] > fit[1] ? ((u32)fit[3] - fit[1]) & 0x7FFFu : 0u;
    if (h[1] && (last[0x1A0u / 4u] != f || last[0x1A4u / 4u] != fh)) {   /* the previous message changed the fit */
        volatile u32 *kp = (volatile u32 *)(c->dg_buf + ((h[2] >> 16) & 1u ? 0x379F0u : 0x37840u));
        kp[1] = last[0x1A8u / 4u];
        kp[2] = last[0x1A0u / 4u];
        kp[3] = f;
        for (i = 0u; i < 0x1A0u / 4u; i++) kp[4u + i] = last[i];
        kp[0] = 0x4B323142u;                   /* 'B12K' */
        h[2] ^= 0x10000u;
    }
    h[1] += 1u;
    h[2] += inj;
    {
        u32 n = h[3];
        volatile u32 *p = (volatile u32 *)(c->dg_buf + AF_B12_OFF + 0x10u + ((n - 1u) & 15u) * 32u);
        if (n && !inj && !(p[7] >> 31) && p[1] == m0 && p[2] == m1 && p[3] == m2 && p[4] == blk[0] &&
            p[5] == blk[1] && p[6] == f && ((p[7] >> 16) & 0x7FFFu) == fh) {
            if ((p[7] & 0xFFFFu) != 0xFFFFu) p[7] += 1u;
        } else {
            p = (volatile u32 *)(c->dg_buf + AF_B12_OFF + 0x10u + (n & 15u) * 32u);
            p[0] = *(volatile u32 *)CM_MS_CLOCK; p[1] = m0; p[2] = m1; p[3] = m2;
            p[4] = blk[0]; p[5] = blk[1]; p[6] = f;
            p[7] = fh << 16 | inj << 31 |
                   (c && BTP(c) && BTP(c)->a43_lv_inject ? 1u << 30 : 0u);   /* B13: injection still pending */
            h[3] = n + 1u;
        }
    }
    for (i = 0u; i < 0x1A0u / 4u; i++) last[i] = msg[i];   /* message + block (contiguous), after the injection */
    last[0x1A0u / 4u] = f;
    last[0x1A4u / 4u] = fh;
    last[0x1A8u / 4u] = *(volatile u32 *)CM_MS_CLOCK;
#else
    (void)r;
#endif
}

IMG_FN static u32 af_lvsz(u32 *r)
{
    u32 lr = r[AF_LR], p;
    if (lr == AF_LV_CLASS || lr == AF_LV_CLASS2) {   /* B7 / B8: FX live view, 4:3 chosen -> class 3 */
        if (r[AF_R0] != 0u || AF_AREA_NOW != 0u || !AF_LV43_PAIR(0u)) return 0u;
        r[AF_R0] = 3u;
        return 1u;
    }
    if (lr == AF_LV_ELBY1 || lr == AF_LV_ELBY2 || lr == AF_LV_ELBY3 ||
        lr == AF_LV_ELBY4 || lr == AF_LV_ELBY5) {   /* B9 / B10: ELBY_fill 4:3 crop + warp out (0x1b & 0x2b) */
        u32 desc = r[AF_R0], info = r[AF_R1];
        if (AF_AREA_NOW != 0u || !AF_LV43_PAIR(0u)) return 0u;
        if (desc < RAM_LO || desc >= RAM_HI - 0x80u || info < RAM_LO || info >= RAM_HI - 0x500u) return 0u;
        if (*(volatile const u8 *)info || *(volatile const u8 *)(info + 1u) ||
            *(volatile const u32 *)(info + 0x10u) || *(volatile const u8 *)(info + 0x4CCu)) return 0u;
        FW(af_elby_fn, 0x101460F2u)(desc, info, r[AF_R2]);
        if (*(volatile const u16 *)(desc + 0x1Eu) != 1508u || *(volatile const u16 *)(desc + 0x20u) != 1006u)
            return 1u;
        *(volatile u16 *)(desc + 0x1Au) = 1344u;
        *(volatile u16 *)(desc + 0x1Cu) = 1344u;
        *(volatile u16 *)(desc + 0x1Eu) = 1340u; *(volatile u16 *)(desc + 0x20u) = 1006u;
        *(volatile u16 *)(desc + 0x22u) = 1340u; *(volatile u16 *)(desc + 0x24u) = 1006u;
        *(volatile u16 *)(desc + 0x26u) = 84u;   *(volatile u16 *)(desc + 0x28u) = 0u;
        *(volatile u16 *)(desc + 0x38u) = 3u;    *(volatile u16 *)(desc + 0x3Au) = 3u;
        *(volatile u16 *)(desc + 0x3Cu) = 1334u; *(volatile u16 *)(desc + 0x3Eu) = 1000u;
        *(volatile u16 *)(desc + 0x44u) = 1536u; *(volatile u16 *)(desc + 0x46u) = 1152u;
        if (r[AF_R2] != 0x2Bu)                      /* B10: kind 0x2b keeps stock plane_y + 0x8000 at +0x30 */
            *(volatile u32 *)(desc + 0x30u) = (*(volatile const u32 *)(desc + 0x2Cu) + 1344u * 1006u + 15u) & ~15u;
        return 1u;
    }
    if (lr == AF_LV_LVCROP) {                        /* B9: LV_crop_tbl (0x10145354) 4:3 crop */
        u32 info = r[AF_R0], out_sz = r[AF_R1], out_rect = r[AF_R2];
        if (AF_AREA_NOW != 0u || !AF_LV43_PAIR(0u) || !out_sz || !out_rect) return 0u;
        if (info < RAM_LO || info >= RAM_HI - 0x500u) return 0u;
        if (*(volatile const u8 *)info || *(volatile const u8 *)(info + 1u) ||
            *(volatile const u32 *)(info + 0x10u) || *(volatile const u8 *)(info + 0x4CCu)) return 0u;
        FW(af_elby_fn, 0x10145354u)(info, out_sz, out_rect);
        if (*(volatile const u16 *)out_sz != 1508u || *(volatile const u16 *)(out_sz + 2u) != 1006u) return 1u;
        *(volatile u16 *)(out_sz + 0u) = 1340u;  *(volatile u16 *)(out_sz + 2u) = 1006u;
        *(volatile u16 *)(out_rect + 0u) = 87u;  *(volatile u16 *)(out_rect + 2u) = 3u;
        *(volatile u16 *)(out_rect + 4u) = 1421u; *(volatile u16 *)(out_rect + 6u) = 1003u;
        return 1u;
    }
    if (lr == AF_LV_ELBR) {                          /* B9: ELBR 0x1020949A(0, 2) -> 896 x 672 (exact 4:3) */
        if (r[AF_R0] != 0u || r[AF_R1] != 2u || AF_AREA_NOW != 0u || !AF_LV43_PAIR(0u)) return 0u;
        r[AF_STACK + 4u] = 896u | (672u << 16);      /* caller's [sp + 0x10]: read right after bl returns */
        r[AF_R0] = (u32)&r[AF_STACK + 4u];
        return 1u;
    }
    if (r[AF_R0] != 0u || (lr != AF_LV_WIN && lr != AF_LV_FIT && lr != AF_LV_CROP)) return 0u;
    if (lr == AF_LV_CROP && AF_AREA_NOW != 0u) return 0u;   /* B4: DX also queries FX there (sensor mode) */
    p = AF_LV43_PAIR(r[AF_R1]);
    if (!p) return 0u;
    r[AF_R0] = p;
    return 1u;
}
#endif

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
    } else if (kind == AF_K_FULL) {
        u32 res = af_full(r);
#ifdef A83_TRACE
        cm_t *c = cm_get();
        if (c && c->dg_buf) {
            volatile u32 *h = a83_head(c);
            h[13] += res ? 0x10000u : 0x1000000u;   /* A90: [13] bits 16-23 kind 27 stock, bits 24-31 kind 27 handled */
        }
#endif
        if (!res) return 0u;
#ifdef B2_LVSZ
    } else if (kind == AF_K_LVSZ) {
        if (r[AF_LR] == AF_LV_MSG) {           /* B12: never handled: the stub always runs the stock manager */
            af_lvmsg(r);
            return kind;
        }
        u32 set = af_lvsz(r);
        (void)set;
#ifdef B6_LVSNAP
        if (r[AF_LR] == AF_LV_CROP) {          /* B6: the live-view state 0x10190598 works on (caller's r4, pushed
                                                * at its entry; [sp + 12] of that frame = r[AF_STACK + 3]) */
            cm_t *c6 = cm_get();
            if (c6 && c6->dg_buf) {
                volatile u32 *q6 = (volatile u32 *)(c6->dg_buf + 0x37D00u);
                if (q6[0] != 0x54503642u) { q6[1] = 0u; q6[2] = 0u; q6[3] = 0u; q6[0] = 0x54503642u; }   /* 'B6PT' */
                q6[1] = r[AF_STACK + 3u]; q6[2] += 1u; q6[3] = *(volatile u32 *)CM_MS_CLOCK;
            }
        }
        if (set && (r[AF_LR] == AF_LV_ELBY1 || r[AF_LR] == AF_LV_ELBY2 || r[AF_LR] == AF_LV_ELBY3 ||
                    r[AF_LR] == AF_LV_ELBY4 || r[AF_LR] == AF_LV_ELBY5 ||
                    r[AF_LR] == AF_LV_ELBR || r[AF_LR] == AF_LV_LVCROP)) {
            cm_t *c9 = cm_get();
            if (c9 && c9->dg_buf) {
                volatile u32 *q9 = (volatile u32 *)(c9->dg_buf + 0x37D10u);
                if (q9[0] != 0x54433942u) { q9[1] = 0u; q9[2] = 0u; q9[3] = 0u; q9[0] = 0x54433942u; }   /* 'B9CT' */
                if (r[AF_LR] == AF_LV_ELBR) q9[2] += 1u;
                else if (r[AF_LR] == AF_LV_LVCROP) q9[3] += 1u;
                else q9[1] += 1u;
            }
        }
#endif
#if defined(A83_TRACE) && defined(B1_LVWIN)
        cm_t *c = cm_get();
        if (c && c->dg_buf) {
            volatile u32 *q = (volatile u32 *)(c->dg_buf + AF_B2_OFF);
            (void)a83_head(c);                 /* sets up 0x38000 .. 0x3B000 once, so it never clears these */
            if (q[0] != 0x53483242u) { q[1] = 0u; q[2] = 0u; q[3] = 0u; q[0] = 0x53483242u; }   /* 'B2HS' */
            q[3] += 1u;                        /* calls */
            if (set) {                         /* answered with our 4:3: [1] window | crop << 16 (B4), [2] fit | cls << 16 */
                if (r[AF_LR] == AF_LV_FIT) q[2] += 1u;
                else if (r[AF_LR] == AF_LV_CLASS || r[AF_LR] == AF_LV_CLASS2) q[2] += 0x10000u;   /* B7 / B8 */
                else if (r[AF_LR] == AF_LV_CROP) q[1] += 0x10000u;
                else if (r[AF_LR] == AF_LV_WIN) q[1] += 1u;
            }
        }
#endif
        if (set) return 0u;                    /* B3: handled, frame r0 = the size pair */
#endif
    } else if (kind == AF_K_NEF || kind == AF_K_RAW) {
        u32 set = af_nef(r);
        (void)set;
#ifdef A83_TRACE
        cm_t *c = cm_get();
        if (c && c->dg_buf) {
            volatile u32 *h = a83_head(c);
            u32 meta = r[AF_STACK];
            h[14] += set ? 0x10001u : 1u;      /* calls | area-8 stores << 16 (kinds 25 and 26) */
            (void)kind;                                /* A90: kind 26 has no stub; [13] high half is kind 27 */
            h[15] = meta >= RAM_LO && meta < RAM_HI - 0x20u ?
                (u32)*(volatile const u16 *)(meta + 12u) | (u32)*(volatile const u16 *)(meta + 14u) << 16 : 0u;
        }
#endif
    }
    return kind;
}
