/* A83 readout (2026-11-25): trace calls from the A83 firmware (research/aspect/a83_fx43.S in the development tree)
 * through vui_common. r = saved frame {r0, r1, r2, r3, r12, lr} of the traced function's entry.
 *   kind 19  configuration 0x10142A7C entry: r0 = obj, r1 = capture info, lr = caller (before the aspect rewrite)
 *   kind 20  area descriptor 0x101DC3E8 entry: r0 = info + 0x200 (area [r0], size [r0 + 0x22]), lr = caller
 *   kind 21  0x1034E700 entry (playback aspect class of a size pair): r0 = the pair, lr = caller
 * Why: on the camera, NEF-only previews were a square sensor region stretched to 4:3, and NEF playback goes through
 * 0x1034E700 / 0x1034E692, which the aspect hooks do not cover.
 *
 * Record at DIAG 0x38000:
 *   head [0] 'A83T' [1] ring events (total) [2] ring capacity [3] entry bytes [4] descriptor rows [5] rows dropped
 *        [6] kind 19 calls [7] kind 20 calls [8] kind 21 calls [9] ring index of the configuration still waiting
 *        for its result (+1, 0 = none)
 *   ring at +0x40: A83_RCAP x 64 bytes, wraps
 *     [0] kind | area << 8 | size << 16   [1] ms   [2] caller (lr)   [3] r0   [4] r1
 *     kind 19: [5] mode | kind << 8 | size +0x221 << 16 | size +0x222 << 24  [6] info + 0xFC  [7] call number
 *              [8..15] filled at the next kind 19 / 21 or outside descriptor call: obj + 0x22 parent, + 0x26 origin,
 *              + 0x38 trim, + 0x3C trimmed, + 0x56 output (each w | h << 16), [13] area | kind << 8 after,
 *              [14] ms of the snapshot, [15] 1
 *     kind 20: [5] row number   [6] r1   [7] call number of kind 19 so far (only the first 3 calls of each row)
 *     kind 21: [5] w | h << 16 of the pair   [6] [7] the two stack words above the frame   [8] call number of 19
 *   descriptor rows at +0x6400: A83_DCAP x 32 bytes: [0] caller [1] area | size << 8 [2] count [3] first ms
 *        [4] last ms [5] first kind-19 number [6] last kind-19 number [7] r0
 * A85m (-DA84_FW, 'A84T' head): the descriptor-row area (kind 20 is gone) holds playback preview-size rows from
 * aspect_fw.inc kinds 22 (0x1034E5E4) / 24 (0x1034E692), one row per caller + type / class / small + main size:
 *   head [10] kind 22 calls [11] kind 24 calls [12] rows [13] rows dropped
 *   A87: head [14] kind 25 / 26 calls | area-8 stores << 16, [15] file entry + 12 / 14 (w | h << 16) of the last call
 *   A88: head [13] high half = kind 26 calls (low half: rows dropped); row [10] = decode descriptor + 32 / 36 and
 *   [11] = file entry + 12 / 14 at the row's first call (after the hook), [12..14] = service kinds 3..5
 *   row (64 bytes) [0] caller [1] kind | type << 8 | class << 16 | small << 24 [2] record + 0x398 (w | h << 16)
 *       [3] record + 0x39C at the first call [4] count [5] first ms [6] last ms [7] how (0 stock, 1 shape, 2 file
 *       query; | 4 = A86 rewrote the NEF main size, [2] is then our size) of the last call [8] value written by the last call (0 = stock) [9] the decision's query result
 *       (~0 = no query) [10..14] the file's sizes of service kinds 1..5 at the first call (types 1 / 2 only,
 *       ~0 = failed) [15] kind-19 number at the first call
 * Only .text here (hook image): no string literals or const tables. camera_verified: false. */
#define A83_K_CFG     19u
#define A83_K_DESC    20u
#define A83_K_CLS     21u
#define A83_OFF       0x38000u
#define A83_MAGIC     0x54333841u     /* 'A83T' */
#define A83_RCAP      256u            /* power of two; 0x38040 .. 0x3C040 */
#define A83_DOFF      0x6400u
#define A83_DCAP      64u             /* 0x3E400 .. 0x3EC00 */
#ifdef A84_FW
/* A84m: kind 19 comes from aspect_fw.inc with the stub frame {r0-r6, r9, r12, lr}; 'A84T'; [5] byte 0 = info +
 * 0x220 (instead of info + 0), [6] = r2 (instead of info + 0xFC): the two values that pick the trim table */
#undef A83_MAGIC
#define A83_MAGIC     0x54383841u     /* 'A88T' (A84T, A86T, A87T before; a new tag clears stale rows) */
#define A83_LR        9u
#else
#define A83_LR        5u
#endif

IMG_FN static volatile u32 *a83_head(cm_t *c)
{
    volatile u32 *h = (volatile u32 *)(c->dg_buf + A83_OFF);
    if (h[0] != A83_MAGIC) {
        for (u32 i = 0; i < (A83_DOFF + A83_DCAP * 32u) / 4u; i++) h[i] = 0u;
        h[2] = A83_RCAP; h[3] = 64u;
        h[0] = A83_MAGIC;
    }
    return h;
}

IMG_FN static volatile u32 *a83_event(volatile u32 *h, u32 *index)
{
    u32 n = h[1], i = n & (A83_RCAP - 1u), k;
    volatile u32 *e = h + 0x10u + i * 16u;
    for (k = 0; k < 16u; k++) e[k] = 0u;
    h[1] = n + 1u;
    if (index) *index = i;
    return e;
}

#define a83_ram(p, room) ((u32)(p) >= RAM_LO && (u32)(p) < RAM_HI - (room))

IMG_FN static void a83_settle(volatile u32 *h)
{   /* the configuration traced last has run by now: keep its result */
    u32 i = h[9];
    volatile u32 *e;
    if (!i) return;
    h[9] = 0u;
    e = h + 0x10u + (i - 1u) * 16u;
    if ((e[0] & 0xFFu) != A83_K_CFG || !a83_ram(e[3], 0x80u) || !a83_ram(e[4], 0x300u)) return;
    {
        volatile const u8 *o = (volatile const u8 *)e[3], *info = (volatile const u8 *)e[4];
        e[8] = (u32)*(volatile const u16 *)(o + 0x22u) | (u32)*(volatile const u16 *)(o + 0x24u) << 16;
        e[9] = (u32)*(volatile const u16 *)(o + 0x26u) | (u32)*(volatile const u16 *)(o + 0x28u) << 16;
        e[10] = (u32)*(volatile const u16 *)(o + 0x38u) | (u32)*(volatile const u16 *)(o + 0x3Au) << 16;
        e[11] = (u32)*(volatile const u16 *)(o + 0x3Cu) | (u32)*(volatile const u16 *)(o + 0x3Eu) << 16;
        e[12] = (u32)*(volatile const u16 *)(o + 0x56u) | (u32)*(volatile const u16 *)(o + 0x58u) << 16;
        e[13] = (u32)info[0x200] | (u32)info[0x21E] << 8;
        e[14] = *(volatile u32 *)CM_MS_CLOCK;
        e[15] = 1u;
    }
}

IMG_FN static void a83_trace(cm_t *c, u32 kind, u32 *r)
{
    volatile u32 *h, *e;
    u32 ms, lr = r[A83_LR], i;
    if (!c->dg_buf) return;
    h = a83_head(c);
    ms = *(volatile u32 *)CM_MS_CLOCK;
    if (kind == A83_K_CFG) {
        a83_settle(h);
        h[6] += 1u;
        e = a83_event(h, &i);
        e[1] = ms; e[2] = lr; e[3] = r[0]; e[4] = r[1]; e[7] = h[6];
        if (a83_ram(r[1], 0x300u)) {
            volatile const u8 *info = (volatile const u8 *)r[1];
            e[0] = kind | (u32)info[0x200] << 8 | (u32)info[0x221] << 16;
#ifdef A84_FW
            e[5] = (u32)info[0x220] | (u32)info[0x21E] << 8 | (u32)info[0x221] << 16 | (u32)info[0x222] << 24;
            e[6] = r[2];
#else
            e[5] = (u32)info[0] | (u32)info[0x21E] << 8 | (u32)info[0x221] << 16 | (u32)info[0x222] << 24;
            e[6] = *(volatile const u32 *)(info + 0xFCu);
#endif
        } else e[0] = kind;
        h[9] = i + 1u;
        return;
    }
    if (kind == A83_K_DESC) {
        u32 area = 0xFFu, size = 0xFFu, key, row, n = h[4];
        volatile u32 *d = h + A83_DOFF / 4u;
        h[7] += 1u;
        if (lr < 0x12120000u || lr >= 0x12121000u) a83_settle(h);   /* not the rebuild inside the cfg hook */
        if (a83_ram(r[0], 0x40u)) { area = *(volatile const u8 *)r[0]; size = *(volatile const u8 *)(r[0] + 0x22u); }
        key = area | size << 8;
        for (row = 0; row < n; row++) if (d[row * 8u] == lr && d[row * 8u + 1u] == key) break;
        if (row == n) {
            if (n >= A83_DCAP) { h[5] += 1u; return; }
            d[row * 8u] = lr; d[row * 8u + 1u] = key; d[row * 8u + 3u] = ms; d[row * 8u + 5u] = h[6];
            d[row * 8u + 7u] = r[0];
            h[4] = n + 1u;
        }
        d[row * 8u + 2u] += 1u; d[row * 8u + 4u] = ms; d[row * 8u + 6u] = h[6];
        if (d[row * 8u + 2u] <= 3u) {
            e = a83_event(h, 0);
            e[0] = kind | area << 8 | size << 16; e[1] = ms; e[2] = lr; e[3] = r[0]; e[4] = r[1];
            e[5] = row; e[6] = r[1]; e[7] = h[6];
        }
        return;
    }
    if (kind == A83_K_CLS) {
        a83_settle(h);
        h[8] += 1u;
        e = a83_event(h, 0);
        e[0] = kind; e[1] = ms; e[2] = lr; e[3] = r[0]; e[4] = r[1];
        if (a83_ram(r[0], 4u)) e[5] = (u32)*(volatile const u16 *)r[0] | (u32)*(volatile const u16 *)(r[0] + 2u) << 16;
        e[6] = r[6]; e[7] = r[7]; e[8] = h[6];
    }
}

