/* B5 readout (2026-11-26, -DB5_RAMDUMP): a 16 MiB RAM image 0xC1800000 .. 0xC2800000 after every "save test record",
 * so stock FX, stock 1:1 and our 4:3 live view can be compared offline for the real live-view crop.
 * Why (B2-B4 camera): the source window 0xC23802C8 + 0x5E, the display rectangles 0xC23802F4 / 0xC2380314 and the
 * live-view area crop 0x10190598 (-> LV state + 0x128) were all 4:3 and the picture stayed squeezed. The crop that
 * stock 1:1 applies is elsewhere, probably in live-view frame units, so a whole-RAM diff is the next step.
 *
 * The save action (save_ok, GUI) only queues the request (DIAG 0x37C00); the module task (burst_task, every 50 ms)
 * writes the image with the same file calls as diag_write, 64 KiB at a time through a staging buffer, yielding
 * between chunks so no camera task waits on it. Like the DIAG records the files must exist on the card:
 * A:\Z5LOG\RAMD1.BIN .. RAMD3.BIN (placeholders of B5_FILE_BYTES); the first one without the header magic is used,
 * else the oldest (lowest sequence). Reading RAM only; nothing else is written.
 *   file: 512-byte header [0] 'B5RD' [1] sequence [2] base [3] length [4] ms at start [5] DIAG index of the save
 *         [6] image area (0xC25043A4) [7] module aspect state (a43_is_on | sel << 4 | status << 8) [8] B2HS calls
 *         then the RAM image
 *   DIAG 0x37C00: [0] 'B5ST' [1] state (0 idle, 1 queued, 2 writing, 3 done, 0x80 | step = failed) [2] file 1..3
 *         [3] bytes written [4] ms start [5] ms end [6] requests [7] last error [8] sequence; path at +0x40
 * Only .text here (hook image): no string literals or const tables. camera_verified: false. */
#define B5_BASE       0xC1800000u
#ifndef B5_LEN
#define B5_LEN        0x01000000u     /* the emulator build passes a short length (its card stand-in logs every call) */
#endif
#define B5_HDR        512u
#define B5_FILE_BYTES (B5_HDR + B5_LEN)
#define B5_CHUNK      0x10000u
#define B5_ST_OFF     0x37C00u
#define B5_FILES      3u
#define B5_MAGIC      0x44523542u     /* 'B5RD' */
#define B5_SLEEP      ((s32 (*)(u32))0x100363A8u)   /* the sleep burst_task and the custom DISP task use */

IMG_FN static volatile u32 *b5_st(cm_t *c)
{
    volatile u32 *s = c->dg_buf ? (volatile u32 *)(c->dg_buf + B5_ST_OFF) : 0;
    if (s && s[0] != 0x54533542u) {          /* 'B5ST' */
        for (u32 i = 0; i < 0x100u / 4u; i++) s[i] = 0u;
        s[0] = 0x54533542u;
    }
    return s;
}

IMG_FN static void b5_name(char *p, u32 k)     /* "A:\Z5LOG\RAMDk.BIN" */
{
    u32 n = 0u;
    p[n++] = 'A'; p[n++] = ':'; p[n++] = '\\'; p[n++] = 'Z'; p[n++] = '5'; p[n++] = 'L'; p[n++] = 'O'; p[n++] = 'G';
    p[n++] = '\\'; p[n++] = 'R'; p[n++] = 'A'; p[n++] = 'M'; p[n++] = 'D'; p[n++] = (char)('0' + k);
    p[n++] = '.'; p[n++] = 'B'; p[n++] = 'I'; p[n++] = 'N'; p[n] = 0;
}

IMG_FN static void b5_request(cm_t *c)         /* save_ok, after diag_save */
{
    volatile u32 *s = b5_st(c);
    if (!s || s[1] == 1u || s[1] == 2u) return;
    s[6] += 1u;
    s[1] = 1u;
}

IMG_FN static void b5_fail(volatile u32 *s, u32 step, u32 rc)
{
    s[1] = 0x80u | step; s[7] = rc; s[5] = *(volatile u32 *)CM_MS_CLOCK;
}

IMG_FN static void b5_step(cm_t *c)            /* module task */
{
    volatile u32 *s = b5_st(c);
    u32 f[16], k, pick = 0u, low = 0xFFFFFFFFu, high = 0u, off, i, got, a;
    char *p;
    u32 *buf;
#ifdef B5_EMU_SELF
    if (s && !s[6] && *(volatile u32 *)CM_MS_CLOCK > 40000u) { s[6] = 1u; s[1] = 1u; }   /* emulator build only */
#endif
    if (!s || s[1] != 1u) return;
    s[1] = 2u; s[3] = 0u; s[4] = *(volatile u32 *)CM_MS_CLOCK; s[5] = 0u; s[7] = 0u;
    p = (char *)(s + 0x40u / 4u);
    for (k = 1u; k <= B5_FILES; k++) {         /* first file without a header, else the oldest */
        u32 w[2] = {0u, 0u};
        b5_name(p, k);
        zero_words(f, 16);
        FW(bind_fn, FW_BIND)(f, p);
        if ((u32)FW(open_fn, FW_OPEN)(f, 0, 0) != FS_OPEN_OK) continue;
        FW(readat_fn, FW_READAT)(f, w, 0, 8u);
        FW(close_fn, FW_CLOSE)(f);
        if (w[0] != B5_MAGIC) { pick = k; low = 0u; break; }
        if (w[1] < low) { low = w[1]; pick = k; }
        if (w[1] > high) high = w[1];
    }
    if (!pick) { b5_fail(s, 1u, 0u); return; }   /* no placeholder file on the card */
    b5_name(p, pick);
    s[2] = pick; s[8] = high + 1u;
    buf = (u32 *)FW(alloc_fn, FW_ALLOC)(B5_CHUNK, 0);
    if (!buf) { b5_fail(s, 2u, 0u); return; }
    zero_words(f, 16);
    FW(bind_fn, FW_BIND)(f, p);
    if ((u32)FW(open_fn, FW_OPEN)(f, 0, 0) != FS_OPEN_OK) { b5_fail(s, 3u, 0u); FW(free_fn, FW_FREE)(buf); return; }
    FW(close_fn, FW_CLOSE)(f);
    zero_words(f, 16);
    FW(bind_fn, FW_BIND)(f, p);
    {
        u32 rc = (u32)FW(open_fn, FW_OPEN)(f, FS_CREATE, 0);
        if (rc != FS_OPEN_OK) { b5_fail(s, 4u, rc); FW(free_fn, FW_FREE)(buf); return; }
    }
    for (i = 0; i < B5_HDR / 4u; i++) buf[i] = 0u;
    buf[0] = B5_MAGIC; buf[1] = s[8]; buf[2] = B5_BASE; buf[3] = B5_LEN; buf[4] = s[4];
    buf[5] = c->dg_ix; buf[6] = *(volatile const u32 *)0xC25043A4u;
#ifdef ASPECT43_FX
    buf[7] = BTP(c) ? (a43_is_on() ? 1u : 0u) | (BTP(c)->a43_sel & 0xFu) << 4 | (BTP(c)->a43_status & 0xFFu) << 8
                    : 0xFFFFu;
#endif
    if (c->dg_buf) buf[8] = ((volatile const u32 *)(c->dg_buf + 0x3AFF0u))[3];
    for (off = 0; off <= B5_LEN; off += B5_CHUNK) {   /* chunk 0 = header + first 64 KiB - 512 */
        u32 n = off == 0u ? B5_CHUNK : (off + B5_CHUNK - B5_HDR <= B5_LEN ? B5_CHUNK : B5_LEN + B5_HDR - off);
        u32 src = off == 0u ? B5_BASE : B5_BASE + off - B5_HDR, from = off == 0u ? B5_HDR / 4u : 0u;
        if (!n) break;
        for (i = from; i < n / 4u; i++) buf[i] = ((const volatile u32 *)src)[i - from];
        for (a = (u32)buf & ~31u; a < (u32)buf + n; a += 32u) __asm__ volatile ("mcr p15, 0, %0, c7, c10, 1" :: "r"(a) : "memory");
        __asm__ volatile ("dsb sy" ::: "memory");
        for (i = 0; i < n; i += 512u) {
            got = 0u;
            FW(write_fn, FW_WRITE)(f[2], (const u8 *)buf + i, 512u, &got);
            if (got != 512u) break;
            s[3] += got;
        }
        if (i < n) { FW(close_fn, FW_CLOSE)(f); FW(free_fn, FW_FREE)(buf); b5_fail(s, 5u, off + i); return; }
        B5_SLEEP(1u);                          /* let every other task run between chunks */
    }
    FW(close_fn, FW_CLOSE)(f);
    FW(free_fn, FW_FREE)(buf);
    s[5] = *(volatile u32 *)CM_MS_CLOCK;
    s[1] = s[3] == B5_FILE_BYTES ? 3u : 0x86u;
}
