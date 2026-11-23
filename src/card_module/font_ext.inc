/* Extra Simplified Chinese glyphs for the stock UI font 2 (A54, 2026-11-23).
 *
 * The Z 5 UI fonts are Nikon `hif` outline fonts. Boot code 0x10445BE4 hands the font pointer table 0x10A0643C
 * (24 fonts) and the size table 0x10A0649C to setters that keep them in RAM at 0xC1D9178C / 0xC1D91790; the
 * engine reads font n through 0x10525E9A. At install the module builds, in heap RAM, a copy of font 2 with the
 * glyphs of font_glyphs.inc added (new glyph ids after the stock maximum, one new index range, records appended,
 * character map and header offsets shifted - the layout of WonderLantern/wonder-lantern-z30 extend_font, MIT),
 * plus copies of both tables with entry 2 replaced, and points the two RAM words at the copies. Firmware and
 * stock font data are unchanged; without the module the stock font is used. Any check failing leaves the stock
 * tables in place. Glyphs: Noto Sans CJK SC Regular 2.004 (SIL OFL 1.1), converted by research/fonts.
 * Record at DIAG 0x36300: 'FNT1', status (1 = installed, other = why not), font copy, its size, glyph count. */
#define FX_TABLE_SLOT  0xC1D9178Cu
#define FX_SIZE_SLOT   0xC1D91790u
#define FX_STOCK_TABLE 0x10A0643Cu
#define FX_STOCK_SIZES 0x10A0649Cu
#define FX_FONTS       24u
#define FX_FONT        2u
#define FX_LOG_OFF     0x36300u

#include "font_glyphs.inc"   /* FX_COUNT, fx_cp[], fx_off[], fx_rec[], FX_REC_BYTES */

static u32 fx_rd16(const u8 *p) { return p[0] | (u32)p[1] << 8; }
static u32 fx_rd32(const u8 *p) { return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static void fx_wr16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void fx_wr32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
static void fx_copy(u8 *d, const u8 *s, u32 n) { while (n--) *d++ = *s++; }

/* Size of the extended font, or 0 if `blob` is not the expected mapped hif font. */
static u32 fx_size(const u8 *blob, u32 size, u32 n, u32 rec_bytes)
{
    if (size < 0x80u || blob[0] != 'h' || blob[1] != 'i' || blob[2] != 'f' || blob[3] != 1u || blob[6] != 6u)
        return 0u;
    u32 index = fx_rd32(blob + 0x2C), data = fx_rd32(blob + 0x30), features = fx_rd32(blob + 0x3C);
    u32 sections = fx_rd16(blob + 0x40);
    if (sections < 2u || sections > 8u || fx_rd32(blob + 0x42) != 5u * (sections - 1u) + 6u) return 0u;
    u32 primary = 0x46u + 5u * (sections - 1u), count = fx_rd16(blob + primary);
    u32 primary_end = primary + 2u + 6u * count, low = ~0u;
    for (u32 i = 0; i + 1u < sections; ++i) {
        u32 o = fx_rd32(blob + 0x47u + 5u * i);
        if (o < low) low = o;
    }
    if (primary_end > 0x40u + low || !(index < features && features < data && data <= size)) return 0u;
    u32 ranges = fx_rd16(blob + index), cursor = index + 2u;
    for (u32 r = 0; r < ranges; ++r) {
        cursor += 8u + 2u * fx_rd16(blob + cursor + 6u);
        if (cursor > features) return 0u;
    }
    if (cursor != features) return 0u;
    return size + 6u * n + 8u + 2u * n + rec_bytes;
}

/* Build the extended font into `out` (fx_size bytes). The new code points must be sorted and absent. */
static void fx_build(const u8 *blob, u32 size, u8 *out, const u16 *cp, const u16 *off, const u8 *rec,
                     u32 n, u32 rec_bytes)
{
    u32 index = fx_rd32(blob + 0x2C), data = fx_rd32(blob + 0x30), features = fx_rd32(blob + 0x3C);
    u32 sections = fx_rd16(blob + 0x40);
    u32 primary = 0x46u + 5u * (sections - 1u), count = fx_rd16(blob + primary);
    u32 primary_end = primary + 2u + 6u * count;
    u32 ranges = fx_rd16(blob + index), cursor = index + 2u, last = 0u;
    for (u32 r = 0; r < ranges; ++r) {
        u32 first = fx_rd16(blob + cursor), span = fx_rd16(blob + cursor + 6u);
        if (first + span > last) last = first + span;
        cursor += 8u + 2u * span;
    }
    u32 grow_map = 6u * n, grow_index = 8u + 2u * n;
    u8 *q = out;
    fx_copy(q, blob, primary);                                 /* header and section descriptors */
    fx_wr32(q + 0x2C, index + grow_map);
    fx_wr32(q + 0x30, data + grow_map + grow_index);
    fx_wr32(q + 0x3C, features + grow_map + grow_index);
    for (u32 i = 0; i + 1u < sections; ++i)
        fx_wr32(q + 0x47u + 5u * i, fx_rd32(blob + 0x47u + 5u * i) + grow_map);
    q += primary;
    fx_wr16(q, count + n); q += 2;                              /* character map, new entries merged in order */
    const u8 *e = blob + primary + 2u;
    u32 j = 0;
    for (u32 i = 0; i < count; ++i, e += 6) {
        u32 first = fx_rd16(e);
        for (; j < n && cp[j] < first; ++j, q += 6) { fx_wr16(q, cp[j]); fx_wr16(q + 2, last + j); fx_wr16(q + 4, 1u); }
        fx_copy(q, e, 6u); q += 6;
    }
    for (; j < n; ++j, q += 6) { fx_wr16(q, cp[j]); fx_wr16(q + 2, last + j); fx_wr16(q + 4, 1u); }
    fx_copy(q, blob + primary_end, index - primary_end); q += index - primary_end;   /* tagged maps */
    fx_copy(q, blob + index, features - index);                 /* glyph index + one range */
    fx_wr16(q, ranges + 1u); q += features - index;
    fx_wr16(q, last); fx_wr32(q + 2, size - data); fx_wr16(q + 6, n); q += 8;
    for (u32 i = 0; i < n; ++i, q += 2) fx_wr16(q, off[i]);
    fx_copy(q, blob + features, size - features); q += size - features;   /* features and glyph data */
    fx_copy(q, rec, rec_bytes);                                 /* new records after every stock record */
}

#ifndef FX_HOST_TEST
IMG_FN static void fx_install(cm_t *c)
{
    volatile u32 *log = c->dg_buf ? (volatile u32 *)(c->dg_buf + FX_LOG_OFF) : 0;
    u32 status = 2u, total = 0u;
    u8 *font = 0;
    if (log) { log[0] = 0x31544E46u; log[1] = 0u; }            /* 'FNT1' */
    if (*(volatile u32 *)FX_TABLE_SLOT != FX_STOCK_TABLE || *(volatile u32 *)FX_SIZE_SLOT != FX_STOCK_SIZES) {
        status = 3u;                                           /* not the stock tables (already replaced?) */
    } else {
        const u32 *table = (const u32 *)FX_STOCK_TABLE, *sizes = (const u32 *)FX_STOCK_SIZES;
        const u8 *blob = (const u8 *)table[FX_FONT];
        total = fx_size(blob, sizes[FX_FONT], FX_COUNT, FX_REC_BYTES);
        if (!total) status = 4u;                               /* font 2 is not the expected layout */
        else {
            font = (u8 *)FW(alloc_fn, FW_ALLOC)(total + 3u + 2u * 4u * FX_FONTS, 0);
            if (!font) status = 5u;
            else {
                u32 *new_table = (u32 *)(((u32)font + total + 3u) & ~3u);   /* tables after the font, aligned */
                u32 *new_sizes = new_table + FX_FONTS;
                if ((u8 *)(new_sizes + FX_FONTS) > font + total + 3u + 2u * 4u * FX_FONTS) {
                    new_table = (u32 *)font; status = 6u;
                } else {
                    fx_build(blob, sizes[FX_FONT], font, fx_cp, fx_off, fx_rec, FX_COUNT, FX_REC_BYTES);
                    for (u32 i = 0; i < FX_FONTS; ++i) { new_table[i] = table[i]; new_sizes[i] = sizes[i]; }
                    new_table[FX_FONT] = (u32)font;
                    new_sizes[FX_FONT] = total;
                    __asm__ volatile ("dsb sy" ::: "memory");
                    *(volatile u32 *)FX_SIZE_SLOT = (u32)new_sizes;
                    *(volatile u32 *)FX_TABLE_SLOT = (u32)new_table;
                    status = 1u;
                }
            }
        }
    }
    if (log) { log[2] = (u32)font; log[3] = total; log[4] = FX_COUNT; log[1] = status; }
}

IMG_FN static u32 fx_ready(cm_t *c)
{
    return c->dg_buf && ((volatile u32 *)(c->dg_buf + FX_LOG_OFF))[1] == 1u;
}
#endif
