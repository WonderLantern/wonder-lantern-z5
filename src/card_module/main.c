/* cardmenu - the shutter-sound menu, entirely in the card module (Z5APP.BIN).
 *
 * Runs on the unchanged, camera-verified baseline firmware (stage0_c4, group 18):
 * the stage0 loader starts this module once at power-up (ctx->sequence == 2) and
 * again whenever the MY MENU row is drawn (sequence 1). Without the card nothing
 * of this exists: every table, page, label and handler lives in heap memory that
 * this module builds at its first run after power-up.
 *
 * First run:
 *   1. read A:\SHUTTER\MENU.DAT (on/off, type 1-5, volume 1-5; defaults on, 1, 5);
 *   2. start the SDK audio task with a short silent clip, then swap in
 *      A:\SHUTTER\<type>.WAV (canonical 48 kHz stereo 16-bit) scaled to the volume;
 *   3. hook the capture-sequence release slot (as bootsound v6, camera-verified):
 *      one play request per silent-photography frame while the sound is on;
 *   4. SETUP MENU (page 0x1CB, header RAM 0xC18750C0): heap copy of the 41 stock
 *      rows plus row 42 "Shutter sound" (text slot 250, control id 0x2FFF), whose
 *      OK opens the owned page;
 *   5. owned page = the unused kind-2 page-0 slot 0xC18324DC (the getter returns
 *      table + 0 * 44; FUN_10450814(2, 0) opens it), 4 rows built from template
 *      page 0x1FE: Shutter sound On/Off, Type 1-5, Volume 1-5, Reload. OK on a row
 *      changes its value in place, rewrites the label and saves MENU.DAT.
 * Labels: English when the language setting 0x1400 is 3, otherwise Simplified Chinese
 * (UTF-16 slot style 114 with stock font 2; see research/menu/README.md).
 *
 * Everything that runs after the first run (release callback, menu handlers, clip
 * loader, settings writer) is one Z5_IMAGE copied to the heap.
 *
 * camera_verified: false.
 */
#include "z5/audio.h"
#include "z5/fs.h"
#include "z5/image.h"
#include "z5/module.h"
#include "z5/state.h"
#include "z5/str.h"
#include "z5/ui.h"

#define SEQ_BOOT      2u

/* --- firmware, Z5 C1.50 -------------------------------------------------- */
#define SLOT_C        0xC18A7664u      /* capture-sequence release slot          */
#define SILENT_BYTE   0xC239F67Au      /* 2 = silent photography                 */
#define CODE_RELEASE  0x04200005u
#define AUDIO_MAGIC   0x5A3550BFu
#define CODE_LO       0x10020000u
#define CODE_HI       0x12100000u
#define RAM_LO        0xC1800000u
#define RAM_HI        0xC8700000u

#define SETTING_READ  0x10456098u      /* u32 read(key)                          */
#define LANG_KEY      0x1400u
#define LANG_ENGLISH  3u               /* read in the emulator with English UI; every
                                         * other language gets Simplified Chinese */
#define ASCII_SET     0x10447BAAu      /* (text, slot, style)                    */
#define UTF16_SET     0x10447C06u
#define HANS_STYLE    114u
#define HANS_FONT     2u
#define RUNTIME_STYLE (0xC2024C04u + 0xE10u + HANS_STYLE * 16u)
#define OPEN_PAGE     0x10450814u      /* (kind, index)                          */
#define REFRESH       0x10446C54u      /* (0x1044676B, 3, 7), as stock handlers   */
#define REFRESH_ARG   0x1044676Bu
#define TITLE_SLOT    ENTRY_SLOT       /* same text as the entry row */
#define TITLE_WORDS   24u              /* 3 page elements of 8 words: title, back icon, end */
#define EL_WORDS      8u
#define FW_ALLOC      0x1021C9E0u
#define FW_FREE       0x1021C9F2u
#define FW_BIND       0x1033A82Au
#define FW_OPEN       0x1033A82Eu
#define FW_CLOSE      0x1033A8BAu
#define FW_READAT     0x1020B0DEu
#define FW_WRITE      0x102CB162u
#define MEDIA_STATE   0xC1805A10u      /* engine buffer slot (see sdk/src/audio.c) */

#define SETUP_HDR     0xC18750C0u      /* runtime header of SETUP page 0x1CB     */
#define SETUP_ROWS    0x114A3A38u
#define SETUP_DISP    0x114DF54Cu
#define SETUP_COUNT   0x00080029u
#define STOCK_ROWS    41u
#define ROW_SIZE      0x1Cu
#define ROW_WORDS     7u
#define TEMPLATE_HDR  0x1209955Cu      /* ROM header of template page 0x1FE      */
#define PAGE0         0xC18324DCu      /* kind-2 page-0 slot (unused, 44 zero B) */
#define PAGE_PARENT   0xC1832508u
#define PAGE_CB       0x10764328u      /* template page 0x1FE's stub callbacks   */
#define WIDGET_FIND   0x10446EB8u      /* (widget id, [page + 0x0C]), as 0x1059E0F8 */
#define WIDGET_SLOT   0x1044736Eu      /* (widget, text slot)                     */
#define WIDGET_SHOW   0x10447358u      /* (widget, 1)                             */
#define TITLE_WIDGET  0x0A95u          /* template page title element, +0x1E      */
#define PAGE_CB_WORDS 5u
/* Histogram on the level page (research/overlay-investigation/z5_dual_display.py). */
#define HIST_MASKS    0xC1826118u      /* nine visibility masks of descriptor 32  */
#define HIST_COUNT    9u
#define HIST_STOCK    0x444u
#define HIST_DUAL     0xCCCu
#define HOOK_SAMPLING 0x1042E1BCu      /* present only on the firmware with hooks */
#define HOOK_WIDGET   0x103482A4u
#define HOOK_JUMP     0xF000F8DFu      /* ldr.w pc, [pc, #0]                      */
#define PAGE_CB_INIT  0x104D436Cu      /* word 0 of the template's callback table */
/* Grid styles and photo zebra (research/overlay-investigation/z5_grid_zebra.py). */
#define FX_ZEBRA_SITE 0x101782B2u      /* present only on the firmware with hooks */
#define FX_GRID_SITE  0x10388DCAu
#define FX_HOOK_HI    0xF8DFu          /* ldr.w pc, [pc, #4]                      */
#define FX_HOOK_LO    0xF004u
#define ZEBRA_WORD    (Z5_ANCHOR + 0x14u)
#define ZEBRA_ON      0x5A454252u
#define GRID_WORD     (Z5_ANCHOR + 0x18u)  /* template pointer, then its complement */
#define GRID_TEMPLATE 0x1074AD10u      /* three display modes, 32 bytes each      */
#define GRID_MODES    3u
#define GRID_REC      32u
#define GRID_COUNT_X  0x0Au
#define GRID_COUNT_Y  0x16u
#define STYLE_STOCK   0u
#define STYLE_3X3     1u
#define STYLE_CROSS   2u
#define GRID_HDR      0xC1871E80u      /* runtime header of the framing-grid page */
#define GRID_ROWS     0x113C1280u
#define GRID_DISP     0x113F375Cu
#define GRID_COUNT    0x00020002u
#define GRID_STOCK    2u
#define GRID_TOTAL    4u
#define GRID_ON_FN    0x10482397u      /* stock "On" handler                      */
#define LAYOUTS_2ROW  0x11547838u      /* row layouts of the stock 2-row grid page */
#define SLOT_TITLE_Z  239u
#define SLOT_Z_ON     240u
#define SLOT_Z_OFF    241u
#define SLOT_Z_ENTRY  242u
#define SLOT_3X3      243u
#define SLOT_CROSS    244u
#define ID_Z_ON       0x2FF5u
#define ID_Z_OFF      0x2FF6u
#define ID_Z_ENTRY    0x2FF7u
#define ID_3X3        0x2FF8u
#define ID_CROSS      0x2FF9u
#define SLOT_TITLE_V  238u
#define SLOT_V_ENTRY  245u
#define ID_V_ON       0x2FF2u
#define ID_V_OFF      0x2FF3u
#define ID_V_ENTRY    0x2FF4u
#define VUI_SITE      0x104A673Cu      /* B.W on the firmware with portrait hooks */
#define PLAIN_ROW     0x1000u          /* no value column, as the stock On/Off rows */
#define OWN_INDEX     0u

#define OPEN_SUBPAGE  0x10450B63u
#define EV_END        0x305u
#define STAY          0x406u
#define BACK          0x407u
#define MAP_MAX       400u

#define F_ASCII       0x200u
#define F_UTF16       0x2000u
#define ENTRY_ID      0x2FFFu
#define ENTRY_SLOT    250u
#define ENTRY_FLAGS   0x1001u
#define ROW_FIRST_ID  0x2FFBu          /* owned rows: ids 0x2FFB.., slots 246..  */
#define ROW_FIRST_SLOT 246u
#define NROWS         4u
#define R_ONOFF       0u
#define R_TYPE        1u
#define R_VOL         2u
#define R_RELOAD      3u
#define R_ENTRY       4u
#define R_TITLE       5u                /* page title (experiment)               */

#define WAV_CAP       (240u * 1024u)   /* 1.28 s of 48 kHz stereo                */
#define WAV_MIN       2000u
#ifndef BOOT_CLASS
#define BOOT_CLASS    4u               /* the draw class of the normal boot run   */
#endif
#define CURRENT_PAGE  0xC1819F90u      /* current page object (SDK, emulator)     */
#define PAGE_SHOOTING 0xC18216A0u      /* APPLV                                   */
#define PAGE_MENU     0xC182DC14u      /* APPMENU, emulator-observed              */
#define CM_MS_CLOCK   0xC18EB388u      /* firmware ms since this power-up         */
#ifndef CM_START_WINDOW_MS
#define CM_START_WINDOW_MS 6000u       /* camera boot runs came at 1.2-1.4 s      */
#endif
#define PH_CFG        1u               /* first-run progress, shown as P<n>       */
#define PH_DUAL       2u
#define PH_IMAGE      3u
#define PH_AUDIO      4u
#define PH_MENU       5u
#define PH_DONE       6u
#define CFG_TRIES     3u
#define SILENT_LEN    4800u            /* 25 ms placeholder clip                 */
#define CHUNK         512u

#define FS_OPEN_OK    0x043A0080u
#define FS_CREATE     0x30Au

/* --- persistent state (z5_state_user) ------------------------------------- */
#define CM_MAGIC      0x554E4D43u      /* 'CMNU' */
#define CFG_MAGIC     0x4D53355Au      /* 'Z5SM' */
#define L_EN          20u
#define L_HZ          8u
#define TEXT_CHARS    40u

#define ERR_NOFILE    1u
#define ERR_FORMAT    2u
#define ERR_MEM       3u
#define ERR_AUDIO     4u

typedef struct {
    u32 magic;
    u32 vui_fn;             /* portrait UI entry, +4: read by firmware stubs */
    u32 orig;               /* firmware release callback                     */
    u32 image;              /* heap image base (Thumb bit clear)             */
    u32 reqs;               /* play requests raised                          */
    u32 on, type, vol;      /* settings                                      */
    u32 hans;               /* labels in Simplified Chinese                  */
    u32 err;                /* last clip load result                         */
    u32 reload;             /* 0 not pressed, 1 done, 2 failed               */
    u32 saves, save_err;    /* settings writes                               */
    u32 install;            /* 0 not yet, 1 installed, >= 10 refused (why)   */
    u32 len;                /* current clip length, 0 = nothing to play      */
    u32 oks[5];             /* OK presses per handler (diagnostics)          */
    u32 stage;              /* last handler step reached (diagnostics)       */
    u32 cfg_state;          /* MENU.DAT at start: 0 ok, 1 no open, 2 bad data */
    u32 boot_hook;          /* release slot held a callback at the first run */
    u32 edited;             /* the user changed a value this session         */
    u32 lang;               /* language setting read at the first run        */
    u32 boot_class;         /* draw class of the run that started the module */
    u32 dual;               /* histogram enabled on the level page           */
    u32 grid;               /* STYLE_ grid style                             */
    u32 zebra;              /* photo zebra on                                */
    u32 fx;                 /* firmware has the grid and zebra hooks         */
    u32 grid_tab;           /* heap copy of the grid template                */
    u32 grid_install;       /* 1 installed, >= 30 refused (why)              */
    u32 zebra_install;      /* 1 installed, >= 40 refused (why)              */
    u32 title_slot;         /* text slot of the owned page's current title   */
    u32 own_hdr;            /* owned page: shutter-sound view                */
    u32 zebra_hdr;          /* owned page: photo-zebra view                  */
    u32 vui;                /* portrait UI switch                            */
    u32 vui_state, vui_pending;   /* last layout and a pending full clear    */
    u32 vui_sub;            /* an i-menu setting page is showing             */
    u32 vui_half, vui_half_ms;    /* synthetic half-press in progress        */
    u32 vui_sw[13];         /* switch snapshot passed to the firmware        */
    u32 vui_ok_open;        /* a page was opened from the i menu with OK     */
    u32 vui_bd_top, vui_min, vui_max;   /* setting page: top edge, option rows */
    int vui_hl_c, vui_hl_w, vui_idx;    /* setting page: highlight, cursor     */
    u32 vui_test;           /* only used by -DVUI_TEST_IMG3                  */
    u32 vui_bd_h;           /* setting page: backdrop height                 */
    u32 vui_bar_min, vui_bar_max, vui_bar_dirty, vui_band_dirty;   /* setting page: bottom bar, row moved */
    u32 vui_evf, vui_evf_ms;   /* the viewfinder is in use                   */
    u32 vui_zoom, vui_fit;     /* magnified view; row scaled to the screen   */
    u32 vui_drawing, vui_hist_on, vui_hist_ms, vui_hist_ori;   /* own portrait histogram */
    int vui_hist_x, vui_hist_y;    /* where it was last drawn                   */
    u32 vui_hm_saved, vui_hm[9];   /* stock histogram masks while hidden       */
    u32 vui_d_tup[4], vui_d_n, vui_d_k, vui_d_i2, vui_d_i3, vui_d_t, vui_d_h, vui_d_hs[4];   /* -DVUI_DIAG only */
    u32 vui_install;        /* 1 installed, >= 50 refused (why)              */
    u32 vui_hdr;            /* owned page: portrait-UI view                  */
    u16 hz_vui[L_HZ];
    char en_vui[L_EN];
    u16 hz_zebra[L_HZ];
    char en_zebra[L_EN];
    u32 phase;              /* first-run progress, PH_DONE when finished     */
    u32 start_ms, took_ms;  /* first run: start time and duration            */
    u32 entry_row;          /* heap SETUP row 42                             */
    u32 own_rows;           /* heap owned rows                               */
    u32 title;              /* heap title record                             */
    char wav_path[20];      /* "A:\SHUTTER\1.WAV"                            */
    char cfg_path[24];      /* "A:\SHUTTER\MENU.DAT"                         */
    char en[NROWS + 1][L_EN];
    u16 hz[NROWS + 1][L_HZ];
    char en_on[4], en_off[4], en_bad[4], en_done[8], en_fail[12];
    u16 hz_on[2], hz_off[2], hz_bad[4], hz_done[4], hz_fail[4];
    char text[TEXT_CHARS * 2];   /* MY MENU diagnostics line (UTF-16)        */
} cm_t;

typedef signed short s16;
typedef u32 (*fn4)(u32, u32, u32, u32);
typedef u32 (*read_fn)(u32);
typedef void *(*alloc_fn)(u32, u32);
typedef void (*free_fn)(void *);
typedef int (*bind_fn)(void *, const char *);
typedef int (*open_fn)(void *, u32, u32);
typedef int (*close_fn)(void *);
typedef int (*readat_fn)(void *, void *, u32, u32);
typedef int (*write_fn)(u32, const void *, u32, u32 *);
typedef void (*set_fn)(const void *, u32, u32);
typedef void (*page_fn)(u32, u32);
typedef void (*refresh_fn)(u32, u32, u32);
typedef u32 (*find_fn)(u32, u32);
typedef void (*widget_fn)(u32, u32);

#define FW(t, a) ((t)((a) | 1u))
#define IMG_FN __attribute__((section(".z5img.cm"), noinline, used, \
                              optimize("no-tree-loop-distribute-patterns")))

/* ===== heap image: everything from here to release_cb_end runs from the heap ===== */

Z5_IMAGE(cm)
static u32 release_cb(u32 a0, u32 a1, u32 a2, u32 a3)
{
    u32 orig = 0u;
    u32 p = *(volatile u32 *)Z5_STATE_SLOT;
    if (p >= RAM_LO && p < RAM_HI && *(volatile u32 *)Z5_STATE_CHECK == ~p) {
        u32 u = *(volatile u32 *)(p + 0x0Cu);              /* z5_state_user() block */
        if (u >= RAM_LO && u < RAM_HI && *(volatile u32 *)u == CM_MAGIC) {
            volatile cm_t *c = (volatile cm_t *)u;
            orig = c->orig;
            if (a0 == CODE_RELEASE && c->on && c->len &&
                *(volatile u8 *)SILENT_BYTE == 2u) {
                u32 s = *(volatile u32 *)Z5_ANCHOR_AUDIO;
                if (s >= RAM_LO && s < RAM_HI && *(volatile u32 *)(s + 0x08u) == AUDIO_MAGIC) {
                    *(volatile u32 *)(s + 0x48u) += 1u;     /* the play request */
                    c->reqs += 1u;
                }
            }
        }
    }
    if (orig >= CODE_LO && orig < CODE_HI)
        return ((fn4)orig)(a0, a1, a2, a3);
    return 0u;
}

IMG_FN static cm_t *cm_get(void)
{
    u32 p = *(volatile u32 *)Z5_STATE_SLOT;
    if (p >= RAM_LO && p < RAM_HI && *(volatile u32 *)Z5_STATE_CHECK == ~p) {
        u32 u = *(volatile u32 *)(p + 0x0Cu);
        if (u >= RAM_LO && u < RAM_HI && *(volatile u32 *)u == CM_MAGIC) return (cm_t *)u;
    }
    return 0;
}

/* Rewrite one row label (R_ONOFF..R_RELOAD, or R_ENTRY) from the settings. */
IMG_FN static void set_label(cm_t *c, u32 row)
{
    u32 slot = row == R_ENTRY ? ENTRY_SLOT : row == R_TITLE ? TITLE_SLOT : ROW_FIRST_SLOT + row;
    if (row == R_TITLE) row = R_ENTRY;                    /* same text as the entry row */
    u32 value = row == R_TYPE ? c->type : c->vol;
    if (c->hans) {
        u16 b[24];
        u32 n = 0, i;
        for (i = 0; i < L_HZ && c->hz[row][i]; i++) b[n++] = c->hz[row][i];
        if (row == R_ONOFF) {
            const u16 *s = c->on ? c->hz_on : c->hz_off;
            for (i = 0; i < 2 && s[i]; i++) b[n++] = s[i];
            if (c->cfg_state == 1u)                 /* no MENU.DAT: changes are not kept */
                for (i = 0; i < 4 && c->hz_bad[i]; i++) b[n++] = c->hz_bad[i];
        } else if (row == R_TYPE || row == R_VOL) {
            b[n++] = (u16)('0' + value);
            if (row == R_TYPE && c->err)
                for (i = 0; i < 4 && c->hz_bad[i]; i++) b[n++] = c->hz_bad[i];
        } else if (row == R_RELOAD && c->reload) {
            const u16 *s = c->reload == 1u ? c->hz_done : c->hz_fail;
            for (i = 0; i < 4 && s[i]; i++) b[n++] = s[i];
        }
        b[n] = 0;
        FW(set_fn, UTF16_SET)(b, slot, HANS_STYLE);
        *(volatile u8 *)RUNTIME_STYLE = HANS_FONT;           /* CJK font for the style */
    } else {
        char b[40];
        u32 n = 0, i;
        for (i = 0; i < L_EN && c->en[row][i]; i++) b[n++] = c->en[row][i];
        if (row == R_ONOFF) {
            const char *s = c->on ? c->en_on : c->en_off;
            for (i = 0; i < 4 && s[i]; i++) b[n++] = s[i];
            if (c->cfg_state == 1u)
                for (i = 0; i < 4 && c->en_bad[i]; i++) b[n++] = c->en_bad[i];
        } else if (row == R_TYPE || row == R_VOL) {
            b[n++] = (char)('0' + value);
            if (row == R_TYPE && c->err)
                for (i = 0; i < 4 && c->en_bad[i]; i++) b[n++] = c->en_bad[i];
        } else if (row == R_RELOAD && c->reload) {
            const char *s = c->reload == 1u ? c->en_done : c->en_fail;
            for (i = 0; i < 12 && s[i]; i++) b[n++] = s[i];
        }
        b[n] = 0;
        FW(set_fn, ASCII_SET)(b, slot, 0u);
    }
}

/* Follow the current language: label kind of every row, then all labels. */
IMG_FN static void refresh_labels(cm_t *c)
{
    u32 hans = FW(read_fn, SETTING_READ)(LANG_KEY) != LANG_ENGLISH;
    u32 kind = hans ? F_UTF16 : F_ASCII;
    c->hans = hans;
    if (c->entry_row) {
        volatile u32 *r = (volatile u32 *)c->entry_row;
        r[4] = (r[4] & ~(F_ASCII | F_UTF16)) | kind;
    }
    if (c->title) ((volatile u32 *)c->title)[1] = hans ? 3u : 2u;
    for (u32 k = 0; c->own_rows && k < NROWS; k++) {
        volatile u32 *r = (volatile u32 *)(c->own_rows + k * ROW_SIZE);
        r[4] = (r[4] & ~(F_ASCII | F_UTF16)) | kind;
    }
    for (u32 k = 0; k <= R_TITLE; k++) set_label(c, k);
}

IMG_FN static void zero_words(u32 *p, u32 words)
{
    for (u32 i = 0; i < words; i++) p[i] = 0;
}

/* Write MENU.DAT: magic, on | type << 8 | vol << 16. */
IMG_FN static void save_cfg(cm_t *c)
{
    u32 f[16];
    u32 data[2];
    u32 out = 0;
    c->edited = 1u;
    /* Camera test 2026-11-08: a MENU.DAT created here never reaches the card, while
     * overwriting one that exists does. So only ever overwrite an existing file. */
    zero_words(f, 16);
    FW(bind_fn, FW_BIND)(f, c->cfg_path);
    if ((u32)FW(open_fn, FW_OPEN)(f, 0, 0) != FS_OPEN_OK) {
        c->save_err += 1u;
        return;
    }
    FW(close_fn, FW_CLOSE)(f);
    zero_words(f, 16);
    data[0] = CFG_MAGIC;
    data[1] = c->on | (c->type << 8) | (c->vol << 16) | (c->grid << 24) | (c->zebra << 26) | (c->vui << 27);
    FW(bind_fn, FW_BIND)(f, c->cfg_path);
    c->stage = 10u;
    if ((u32)FW(open_fn, FW_OPEN)(f, FS_CREATE, 0) != FS_OPEN_OK) {
        c->save_err += 1u;
        return;
    }
    FW(write_fn, FW_WRITE)(f[2], data, sizeof(data), &out);
    FW(close_fn, FW_CLOSE)(f);
    if (out == sizeof(data)) c->saves += 1u;
    else c->save_err += 1u;
}

/* Swap a new clip into the running SDK audio task; frees the old buffer. */
IMG_FN static u32 audio_swap(u32 buf, u32 len)
{
    u32 s = *(volatile u32 *)Z5_ANCHOR_AUDIO;
    if (!(s >= RAM_LO && s < RAM_HI && *(volatile u32 *)(s + 0x08u) == AUDIO_MAGIC &&
          *(volatile u32 *)(s + 0x1Cu) == s))
        return 1u;
    u32 old = *(volatile u32 *)(s + 0x28u);
    u32 msgs = *(volatile u32 *)(s + 0x2Cu);
    for (u32 i = 3u; i <= 4u; i++) {                       /* the two buffer messages */
        volatile u32 *d = (volatile u32 *)(msgs + i * 0x60u + 0x40u);
        d[0] = buf;
        d[1] = len;
    }
    *(volatile u32 *)(s + 0x28u) = buf;
    *(volatile u32 *)(s + 0x4Cu) = len;
    *(volatile u32 *)(s + 0x54u) = len / 192u + 40u;
    volatile u32 *media = (volatile u32 *)MEDIA_STATE;
    media[2] = len;
    media[3] = len;
    if (old >= RAM_LO && old < RAM_HI && old != buf) FW(free_fn, FW_FREE)((void *)old);
    return 0u;
}

IMG_FN static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* Load A:\SHUTTER\<type>.WAV, scale it to the volume and hand it to the audio
 * task in a buffer of four times its length (silence after the clip, as
 * z5_audio_init builds it). Returns 0 or an ERR_ code; c->len follows. */
IMG_FN static u32 load_clip(cm_t *c)
{
    u32 f[16];
    u8 h[64];
    zero_words(f, 16);
    zero_words((u32 *)h, 16);
    c->wav_path[11] = (char)('0' + c->type);
    FW(bind_fn, FW_BIND)(f, c->wav_path);
    if ((u32)FW(open_fn, FW_OPEN)(f, 0, 0) != FS_OPEN_OK) { c->len = 0; return ERR_NOFILE; }
    FW(readat_fn, FW_READAT)(f, h, 0, sizeof(h));
    u32 dlen = rd32(h + 40);
    if (rd32(h) != 0x46464952u || rd32(h + 8) != 0x45564157u ||      /* RIFF, WAVE */
        rd32(h + 12) != 0x20746D66u || rd32(h + 36) != 0x61746164u || /* fmt , data */
        rd32(h + 20) != 0x00020001u || rd32(h + 24) != 48000u ||      /* PCM, 2 ch  */
        (rd32(h + 32) >> 16) != 16u) {
        FW(close_fn, FW_CLOSE)(f);
        c->len = 0;
        return ERR_FORMAT;
    }
    if (dlen > WAV_CAP) dlen = WAV_CAP;
    dlen &= ~3u;
    if (dlen < WAV_MIN) { FW(close_fn, FW_CLOSE)(f); c->len = 0; return ERR_FORMAT; }
    /* Never allocate with a file open: z5mod docs/loader.md records that the file and
     * memory services busy at once froze an earlier loader. Close, allocate, reopen. */
    FW(close_fn, FW_CLOSE)(f);
    u8 *buf = (u8 *)FW(alloc_fn, FW_ALLOC)(dlen * 4u, 0);
    if (!buf) return ERR_MEM;
    zero_words(f, 16);
    FW(bind_fn, FW_BIND)(f, c->wav_path);
    if ((u32)FW(open_fn, FW_OPEN)(f, 0, 0) != FS_OPEN_OK) {
        FW(free_fn, FW_FREE)(buf);
        c->len = 0;
        return ERR_NOFILE;
    }
    for (u32 off = 0; off < dlen; off += CHUNK) {
        u32 n = dlen - off;
        if (n > CHUNK) n = CHUNK;
        FW(readat_fn, FW_READAT)(f, buf + off, 44u + off, n);
    }
    FW(close_fn, FW_CLOSE)(f);
    /* Volume 1-5: about -12, -8, -5, -2.5 and 0 dB. */
    s32 gain = c->vol >= 5u ? 256 : c->vol == 4u ? 192 : c->vol == 3u ? 144 : c->vol == 2u ? 102 : 64;
    if (gain != 256) {
        s16 *pcm = (s16 *)buf;
        for (u32 i = 0; i < dlen / 2u; i++) pcm[i] = (s16)(((s32)pcm[i] * gain) >> 8);
    }
    zero_words((u32 *)(buf + dlen), (dlen * 3u) / 4u);
    if (audio_swap((u32)buf, dlen)) {
        FW(free_fn, FW_FREE)(buf);
        return ERR_AUDIO;
    }
    c->len = dlen;
    return 0;
}

/* --- menu event handlers (event map functions; arguments unused) --------- */

/* Read MENU.DAT into the settings (defaults when it cannot be used). Returns and
 * records 0 ok, 1 cannot open, 2 unexpected content. Uses the file calls load_clip
 * and save_cfg already make, from the same contexts. */
IMG_FN static u32 cfg_load(cm_t *c)
{
    u32 f[16];
    u32 data[2];
    u32 state = 1u;
    zero_words(f, 16);
    data[0] = 0; data[1] = 0;
    c->on = 1u; c->type = 1u; c->vol = 5u; c->grid = STYLE_STOCK; c->zebra = 0u; c->vui = 0u;
    FW(bind_fn, FW_BIND)(f, c->cfg_path);
    if ((u32)FW(open_fn, FW_OPEN)(f, 0, 0) == FS_OPEN_OK) {
        FW(readat_fn, FW_READAT)(f, data, 0, 8u);
        FW(close_fn, FW_CLOSE)(f);
        state = data[0] == CFG_MAGIC ? 0u : 2u;
    }
    c->cfg_state = state;
    if (!state) {
        u32 on = data[1] & 0xFFu, type = (data[1] >> 8) & 0xFFu, vol = (data[1] >> 16) & 0xFFu;
        if (on <= 1u) c->on = on;
        if (type >= 1u && type <= 5u) c->type = type;
        if (vol >= 1u && vol <= 5u) c->vol = vol;
        if (((data[1] >> 24) & 3u) <= STYLE_CROSS) c->grid = (data[1] >> 24) & 3u;
        c->zebra = (data[1] >> 26) & 1u;
        c->vui = (data[1] >> 27) & 1u;
    }
    return state;
}

/* Install the release hook if the firmware has (re)filled the slot since. */
IMG_FN static void ensure_hook(cm_t *c)
{
    volatile u32 *slot = (volatile u32 *)SLOT_C;
    u32 cb = c->image | 1u;                    /* release_cb is the image's first function */
    u32 cur = *slot;
    if (cur != cb && cur >= CODE_LO && cur < CODE_HI) {
        c->orig = cur;
        *slot = cb;
    }
}

/* Page callback word 0, the place stock pages bind their text widgets (0x1059E0D4):
 * find the title widget, attach the text slot, show it. */
IMG_FN static u32 title_cb(u32 page, u32 a1, u32 a2, u32 a3)
{
    u32 result = FW(fn4, PAGE_CB_INIT)(page, a1, a2, a3);    /* the template's own word 0 */
    u32 w = FW(find_fn, WIDGET_FIND)(TITLE_WIDGET, *(volatile u32 *)(page + 0x0Cu));
    if (w) {
        cm_t *c = cm_get();
        FW(widget_fn, WIDGET_SLOT)(w, c ? c->title_slot : TITLE_SLOT);
        FW(widget_fn, WIDGET_SHOW)(w, 1u);
    }
    return result;
}

IMG_FN static u32 entry_ok(u32 a0, u32 a1, u32 a2, u32 a3)
{
    cm_t *c = cm_get();
    (void)a0; (void)a1; (void)a2; (void)a3;
    if (!c) return 0;
    c->oks[4] += 1u;
    ensure_hook(c);
    if (c->cfg_state && !c->edited) {          /* the start-up read failed, nothing changed yet */
        if (cfg_load(c) == 0u) c->len = 0;     /* the saved type replaces the default clip */
    }
    if (!c->len) c->err = load_clip(c);        /* the start-up load may have failed */
    c->reload = 0;
    refresh_labels(c);
    c->title_slot = TITLE_SLOT;
    ((volatile u32 *)PAGE0)[10] = c->own_hdr;
    FW(page_fn, OPEN_PAGE)(2u, OWN_INDEX);
    return 0;
}

IMG_FN static u32 onoff_ok(u32 a0, u32 a1, u32 a2, u32 a3)
{
    cm_t *c = cm_get();
    (void)a0; (void)a1; (void)a2; (void)a3;
    if (!c) return 0;
    c->oks[0] += 1u;
    c->stage = 1u;
    c->on = !c->on;
    set_label(c, R_ONOFF);
    c->stage = 2u;
    save_cfg(c);
    c->stage = 3u;
#ifdef CM_REFRESH_FN
    FW(refresh_fn, REFRESH)(REFRESH_ARG, 3u, 7u);       /* redraw the rows */
#else
    FW(page_fn, OPEN_PAGE)(2u, OWN_INDEX);             /* rebuild the rows (labels are cached) */
#endif
    return 0;
}

IMG_FN static u32 type_ok(u32 a0, u32 a1, u32 a2, u32 a3)
{
    cm_t *c = cm_get();
    (void)a0; (void)a1; (void)a2; (void)a3;
    if (!c) return 0;
    c->oks[1] += 1u;
    c->type = c->type >= 5u ? 1u : c->type + 1u;
    c->err = load_clip(c);
    c->reload = 0;
    set_label(c, R_TYPE);
    set_label(c, R_RELOAD);
    save_cfg(c);
#ifdef CM_REFRESH_FN
    FW(refresh_fn, REFRESH)(REFRESH_ARG, 3u, 7u);       /* redraw the rows */
#else
    FW(page_fn, OPEN_PAGE)(2u, OWN_INDEX);             /* rebuild the rows (labels are cached) */
#endif
    return 0;
}

IMG_FN static u32 vol_ok(u32 a0, u32 a1, u32 a2, u32 a3)
{
    cm_t *c = cm_get();
    (void)a0; (void)a1; (void)a2; (void)a3;
    if (!c) return 0;
    c->oks[2] += 1u;
    c->vol = c->vol >= 5u ? 1u : c->vol + 1u;
    c->err = load_clip(c);
    set_label(c, R_VOL);
    set_label(c, R_TYPE);
    save_cfg(c);
#ifdef CM_REFRESH_FN
    FW(refresh_fn, REFRESH)(REFRESH_ARG, 3u, 7u);       /* redraw the rows */
#else
    FW(page_fn, OPEN_PAGE)(2u, OWN_INDEX);             /* rebuild the rows (labels are cached) */
#endif
    return 0;
}

IMG_FN static u32 reload_ok(u32 a0, u32 a1, u32 a2, u32 a3)
{
    cm_t *c = cm_get();
    (void)a0; (void)a1; (void)a2; (void)a3;
    if (!c) return 0;
    c->oks[3] += 1u;
    c->err = load_clip(c);
    c->reload = c->err ? 2u : 1u;
    set_label(c, R_TYPE);
    set_label(c, R_RELOAD);
#ifdef CM_REFRESH_FN
    FW(refresh_fn, REFRESH)(REFRESH_ARG, 3u, 7u);       /* redraw the rows */
#else
    FW(page_fn, OPEN_PAGE)(2u, OWN_INDEX);             /* rebuild the rows (labels are cached) */
#endif
    return 0;
}

/* Publish the zebra switch and the grid template the firmware hooks read. */
IMG_FN static void fx_apply(cm_t *c)
{
    volatile u32 *grid = (volatile u32 *)GRID_WORD;
    *(volatile u32 *)ZEBRA_WORD = c->fx && c->zebra ? ZEBRA_ON : 0u;
    grid[0] = 0u;                              /* invalid while the table changes */
    if (c->fx && c->grid != STYLE_STOCK && c->grid_tab) {
        u16 lines = c->grid == STYLE_3X3 ? 2u : 1u;
        for (u32 m = 0; m < GRID_MODES; m++) {
            u8 *r = (u8 *)c->grid_tab + m * GRID_REC;
            *(u16 *)(r + GRID_COUNT_X) = lines;
            *(u16 *)(r + GRID_COUNT_Y) = lines;
        }
        grid[1] = ~c->grid_tab;
        grid[0] = c->grid_tab;
    } else {
        grid[1] = 0u;
    }
}

/* SETUP row text: "photo zebra: on/off". */
IMG_FN static void zebra_label(cm_t *c)
{
    u32 n = 0, i;
    if (c->hans) {
        u16 b[12];
        const u16 *v = c->zebra ? c->hz_on : c->hz_off;
        for (i = 0; i < L_HZ && c->hz_zebra[i]; i++) b[n++] = c->hz_zebra[i];
        for (i = 0; i < 2 && v[i]; i++) b[n++] = v[i];
        b[n] = 0;
        FW(set_fn, UTF16_SET)(b, SLOT_Z_ENTRY, HANS_STYLE);
        *(volatile u8 *)RUNTIME_STYLE = HANS_FONT;
    } else {
        char b[28];
        const char *v = c->zebra ? c->en_on : c->en_off;
        for (i = 0; i < L_EN && c->en_zebra[i]; i++) b[n++] = c->en_zebra[i];
        for (i = 0; i < 4 && v[i]; i++) b[n++] = v[i];
        b[n] = 0;
        FW(set_fn, ASCII_SET)(b, SLOT_Z_ENTRY, 0u);
    }
}

IMG_FN static u32 zebra_entry_ok(u32 a0, u32 a1, u32 a2, u32 a3)
{
    cm_t *c = cm_get();
    (void)a0; (void)a1; (void)a2; (void)a3;
    if (!c || !c->zebra_hdr) return 0;
    c->title_slot = SLOT_TITLE_Z;
    ((volatile u32 *)PAGE0)[10] = c->zebra_hdr;
    FW(page_fn, OPEN_PAGE)(2u, OWN_INDEX);
    return 0;
}

IMG_FN static u32 zebra_set(u32 on)
{
    cm_t *c = cm_get();
    if (c) {
        c->zebra = on;
        fx_apply(c);
        zebra_label(c);
        save_cfg(c);
    }
    return 0;
}

IMG_FN static u32 zebra_on_ok(u32 a0, u32 a1, u32 a2, u32 a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    return zebra_set(1u);
}

IMG_FN static u32 zebra_off_ok(u32 a0, u32 a1, u32 a2, u32 a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    return zebra_set(0u);
}

/* SETUP row text: "portrait display: on/off". */
IMG_FN static void vui_label(cm_t *c)
{
    u32 n = 0, i;
    if (c->hans) {
        u16 b[12];
        const u16 *v = c->vui ? c->hz_on : c->hz_off;
        for (i = 0; i < L_HZ && c->hz_vui[i]; i++) b[n++] = c->hz_vui[i];
        for (i = 0; i < 2 && v[i]; i++) b[n++] = v[i];
        b[n] = 0;
        FW(set_fn, UTF16_SET)(b, SLOT_V_ENTRY, HANS_STYLE);
        *(volatile u8 *)RUNTIME_STYLE = HANS_FONT;
    } else {
        char b[28];
        const char *v = c->vui ? c->en_on : c->en_off;
        for (i = 0; i < L_EN && c->en_vui[i]; i++) b[n++] = c->en_vui[i];
        for (i = 0; i < 4 && v[i]; i++) b[n++] = v[i];
        b[n] = 0;
        FW(set_fn, ASCII_SET)(b, SLOT_V_ENTRY, 0u);
    }
}

IMG_FN static u32 vui_entry_ok(u32 a0, u32 a1, u32 a2, u32 a3)
{
    cm_t *c = cm_get();
    (void)a0; (void)a1; (void)a2; (void)a3;
    if (!c || !c->vui_hdr) return 0;
    c->title_slot = SLOT_TITLE_V;
    ((volatile u32 *)PAGE0)[10] = c->vui_hdr;
    FW(page_fn, OPEN_PAGE)(2u, OWN_INDEX);
    return 0;
}

IMG_FN static u32 vui_set(u32 on)
{
    cm_t *c = cm_get();
    if (c) {
        c->vui = on;
        vui_label(c);
        save_cfg(c);
    }
    return 0;
}

IMG_FN static u32 vui_on_ok(u32 a0, u32 a1, u32 a2, u32 a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    return vui_set(1u);
}

IMG_FN static u32 vui_off_ok(u32 a0, u32 a1, u32 a2, u32 a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    return vui_set(0u);
}

#include "vui_card.inc"

/* Grid rows: choose the style, then let the stock "On" handler turn the grid on. */
IMG_FN static u32 grid_pick(u32 style, u32 a0, u32 a1, u32 a2, u32 a3)
{
    cm_t *c = cm_get();
    if (c) {
        c->grid = style;
        fx_apply(c);
        save_cfg(c);
    }
    return FW(fn4, GRID_ON_FN)(a0, a1, a2, a3);
}

IMG_FN static u32 grid_on_ok(u32 a0, u32 a1, u32 a2, u32 a3) { return grid_pick(STYLE_STOCK, a0, a1, a2, a3); }
IMG_FN static u32 grid_3x3_ok(u32 a0, u32 a1, u32 a2, u32 a3) { return grid_pick(STYLE_3X3, a0, a1, a2, a3); }
IMG_FN static u32 grid_crs_ok(u32 a0, u32 a1, u32 a2, u32 a3) { return grid_pick(STYLE_CROSS, a0, a1, a2, a3); }

Z5_IMAGE_END(cm) static void release_cb_end(void) { }

/* ===== module code: runs only while the loader holds the image ===== */

static u32 img_addr(cm_t *c, const void *fn)
{
    return (c->image + (((u32)fn & ~1u) - ((u32)release_cb & ~1u))) | 1u;
}

static void copy_str(char *dst, u32 cap, const char *s)
{
    u32 i = 0;
    for (; i + 1u < cap && s[i]; i++) dst[i] = s[i];
    dst[i] = 0;
}

static void copy_u16(u16 *dst, u32 cap, const u16 *s)
{
    u32 i = 0;
    for (; i + 1u < cap && s[i]; i++) dst[i] = s[i];
    if (i < cap) dst[i] = 0;
}

static void init_strings(cm_t *c)
{
    copy_str(c->wav_path, sizeof(c->wav_path), "A:\\SHUTTER\\1.WAV");
    copy_str(c->cfg_path, sizeof(c->cfg_path), "A:\\SHUTTER\\MENU.DAT");
    copy_str(c->en[R_ONOFF], L_EN, "Shutter sound: ");
    copy_str(c->en[R_TYPE], L_EN, "Type: ");
    copy_str(c->en[R_VOL], L_EN, "Volume: ");
    copy_str(c->en[R_RELOAD], L_EN, "Reload");
    copy_str(c->en[R_ENTRY], L_EN, "Shutter sound");
    copy_str(c->en_zebra, L_EN, "Photo zebra: ");
    copy_str(c->en_on, sizeof(c->en_on), "On");
    copy_str(c->en_off, sizeof(c->en_off), "Off");
    copy_str(c->en_bad, sizeof(c->en_bad), " !");
    copy_str(c->en_done, sizeof(c->en_done), ": OK");
    copy_str(c->en_fail, sizeof(c->en_fail), ": failed");
    copy_u16(c->hz[R_ONOFF], L_HZ, (const u16 *)u"快门声音：");
    copy_u16(c->hz[R_TYPE], L_HZ, (const u16 *)u"类型：");
    copy_u16(c->hz[R_VOL], L_HZ, (const u16 *)u"音量：");
    copy_u16(c->hz[R_RELOAD], L_HZ, (const u16 *)u"重新载入");
    copy_u16(c->hz[R_ENTRY], L_HZ, (const u16 *)u"快门声音");
    copy_u16(c->hz_zebra, L_HZ, (const u16 *)u"照片高光显示：");
    copy_u16(c->hz_vui, L_HZ, (const u16 *)u"竖拍显示：");
    copy_str(c->en_vui, L_EN, "Portrait UI: ");
    copy_u16(c->hz_on, 2, (const u16 *)u"开");
    copy_u16(c->hz_off, 2, (const u16 *)u"关");
    copy_u16(c->hz_bad, 4, (const u16 *)u" !");
    copy_u16(c->hz_done, 4, (const u16 *)u"：完成");
    copy_u16(c->hz_fail, 4, (const u16 *)u"：失败");
}

static const u32 navigation[] = {
    0x10450B49u, 0x10450B55u, 0x10450B61u, 0x10450B65u, 0x10450B67u, 0x10450B8Du, 0x10450BDBu,
};
static const u32 nav_predicates[] = { 0x10450BF1u, 0x10450BF5u, 0x10450C05u, 0x10450C35u };

static int in_list(u32 v, const u32 *list, u32 n)
{
    for (u32 i = 0; i < n; i++)
        if (list[i] == v) return 1;
    return 0;
}

static int ok_event(u32 ev)
{
    return ev == 0x24Au || ev == 0x24Fu || ev == 0x263u || ev == 0x270u;
}

/* Copy a stock event map up to EV_END: OK-type events call ok_fn and stay,
 * navigation is kept, every other handler is cleared. Returns a heap copy. */
static u32 build_map(u32 src, u32 ok_fn, u32 ok_dest)
{
    u32 n = 0;
    while (n < MAP_MAX && ((const u32 *)src)[n * 4u] != EV_END) n++;
    if (n >= MAP_MAX) return 0;
    u32 *out = (u32 *)z5_alloc((n + 1u) * 16u, 0);
    if (!out) return 0;
    u32 k = 0;
    for (u32 i = 0; i <= n; i++) {
        const u32 *e = (const u32 *)src + i * 4u;
        u32 ev = e[0], fn = e[1], pred = e[2], dest = e[3];
        if (ok_event(ev) || fn == OPEN_SUBPAGE) {
            fn = ok_fn; pred = 0; dest = ok_dest;
        } else if (!in_list(fn, navigation, sizeof(navigation) / 4u)) {
            fn = 0; pred = 0; dest = dest == BACK ? BACK : STAY;
        } else if (pred && !in_list(pred, nav_predicates, sizeof(nav_predicates) / 4u)) {
            pred = 0;
        }
        if (fn || dest != STAY || ev == EV_END) {
            out[k * 4u] = ev; out[k * 4u + 1u] = fn; out[k * 4u + 2u] = pred; out[k * 4u + 3u] = dest;
            k++;
        }
    }
    return (u32)out;
}

static void copy_words(u32 *dst, const u32 *src, u32 words)
{
    for (u32 i = 0; i < words; i++) dst[i] = src[i];
}

/* Photo zebra view of the owned page: two rows like the stock On/Off pages. */
static u32 install_zebra_page(cm_t *c, u32 *title, u32 first_map)
{
    const u32 *th = (const u32 *)TEMPLATE_HDR;
    if (((const u32 *)LAYOUTS_2ROW)[0] != 2u) return 40u;
    u32 *rows = (u32 *)z5_alloc(2u * ROW_SIZE, 0);
    u32 *disp = (u32 *)z5_alloc(2u * 4u, 0);
    u32 *hdr = (u32 *)z5_alloc(7u * 4u, 0);
    if (!rows || !disp || !hdr) return 41u;
    u32 kind = c->hans ? F_UTF16 : F_ASCII;
    copy_words(rows, (const u32 *)GRID_ROWS, ROW_WORDS);     /* same row shape as the stock On/Off page */
    copy_words(rows + ROW_WORDS, rows, ROW_WORDS);
    rows[0] = SLOT_Z_ON;  rows[4] = PLAIN_ROW | kind; rows[6] = ID_Z_ON;
    rows[ROW_WORDS] = SLOT_Z_OFF;  rows[ROW_WORDS + 4u] = PLAIN_ROW | kind; rows[ROW_WORDS + 6u] = ID_Z_OFF;
    disp[0] = build_map(first_map, img_addr(c, zebra_on_ok), BACK);
    disp[1] = build_map(first_map, img_addr(c, zebra_off_ok), BACK);
    if (!disp[0] || !disp[1]) return 42u;
    if (c->hans) {
        FW(set_fn, UTF16_SET)((const u16 *)u"照片高光显示", SLOT_TITLE_Z, HANS_STYLE);
        FW(set_fn, UTF16_SET)((const u16 *)u"开", SLOT_Z_ON, HANS_STYLE);
        FW(set_fn, UTF16_SET)((const u16 *)u"关", SLOT_Z_OFF, HANS_STYLE);
        *(volatile u8 *)RUNTIME_STYLE = HANS_FONT;
    } else {
        FW(set_fn, ASCII_SET)("Photo zebra", SLOT_TITLE_Z, 0u);
        FW(set_fn, ASCII_SET)("On", SLOT_Z_ON, 0u);
        FW(set_fn, ASCII_SET)("Off", SLOT_Z_OFF, 0u);
    }
    hdr[0] = th[0]; hdr[1] = LAYOUTS_2ROW; hdr[2] = (u32)title;
    hdr[3] = (u32)rows; hdr[4] = (u32)disp; hdr[5] = (2u << 16) | 2u; hdr[6] = th[6];
    c->zebra_hdr = (u32)hdr;
    return 1u;
}

/* Portrait UI view of the owned page: two rows like the stock On/Off pages. */
static u32 install_vui_page(cm_t *c, u32 *title, u32 first_map)
{
    const u32 *th = (const u32 *)TEMPLATE_HDR;
    if (((const u32 *)LAYOUTS_2ROW)[0] != 2u) return 50u;
    u32 *rows = (u32 *)z5_alloc(2u * ROW_SIZE, 0);
    u32 *disp = (u32 *)z5_alloc(2u * 4u, 0);
    u32 *hdr = (u32 *)z5_alloc(7u * 4u, 0);
    if (!rows || !disp || !hdr) return 51u;
    u32 kind = c->hans ? F_UTF16 : F_ASCII;
    copy_words(rows, (const u32 *)GRID_ROWS, ROW_WORDS);
    copy_words(rows + ROW_WORDS, rows, ROW_WORDS);
    rows[0] = SLOT_Z_ON;  rows[4] = PLAIN_ROW | kind; rows[6] = ID_V_ON;
    rows[ROW_WORDS] = SLOT_Z_OFF;  rows[ROW_WORDS + 4u] = PLAIN_ROW | kind; rows[ROW_WORDS + 6u] = ID_V_OFF;
    disp[0] = build_map(first_map, img_addr(c, vui_on_ok), BACK);
    disp[1] = build_map(first_map, img_addr(c, vui_off_ok), BACK);
    if (!disp[0] || !disp[1]) return 52u;
    if (c->hans) {
        FW(set_fn, UTF16_SET)((const u16 *)u"竖拍显示", SLOT_TITLE_V, HANS_STYLE);
        *(volatile u8 *)RUNTIME_STYLE = HANS_FONT;
    } else {
        FW(set_fn, ASCII_SET)("Portrait UI", SLOT_TITLE_V, 0u);
    }
    hdr[0] = th[0]; hdr[1] = LAYOUTS_2ROW; hdr[2] = (u32)title;
    hdr[3] = (u32)rows; hdr[4] = (u32)disp; hdr[5] = (2u << 16) | 2u; hdr[6] = th[6];
    c->vui_hdr = (u32)hdr;
    return 1u;
}

/* Build the SETUP entry row and the owned page. Returns 1 or a refusal >= 10. */
static u32 install_menu(cm_t *c)
{
    volatile u32 *hdr = (volatile u32 *)SETUP_HDR;
    volatile u32 *page = (volatile u32 *)PAGE0;
    const u32 *th = (const u32 *)TEMPLATE_HDR;
    if (hdr[3] != SETUP_ROWS || hdr[4] != SETUP_DISP || hdr[5] != SETUP_COUNT) return 10u;
    for (u32 i = 0; i < 11u; i++)
        if (page[i]) return 11u;
    if (th[0] != 0x1002u || th[5] != 0x30003u) return 12u;
    /* th[1] is a list of row layouts indexed by row count ({n, 3 words} each):
     * the template points at the 3-row one, the 4-row one follows it. */
    const u32 *layout4 = (const u32 *)(th[1] + 0x10u);
    if (((const u32 *)th[1])[0] != 3u || layout4[0] != NROWS) return 18u;

    /* SETUP: 41 stock rows + row 42 (copy of row 41 with our slot, flags, id). */
    u32 vui_ok = c->fx && *(volatile u16 *)VUI_SITE != 0xE92Du;
    u32 extra = c->fx ? (vui_ok ? 3u : 2u) : 1u;   /* shutter sound, photo zebra, portrait UI */
    u32 *rows = (u32 *)z5_alloc((STOCK_ROWS + extra) * ROW_SIZE, 0);
    u32 *disp = (u32 *)z5_alloc((STOCK_ROWS + extra) * 4u, 0);
    if (!rows || !disp) return 13u;
    copy_words(rows, (const u32 *)SETUP_ROWS, (STOCK_ROWS + 1u) * ROW_WORDS - ROW_WORDS);
    u32 *entry = rows + STOCK_ROWS * ROW_WORDS;
    copy_words(entry, (const u32 *)SETUP_ROWS + (STOCK_ROWS - 1u) * ROW_WORDS, ROW_WORDS);
    entry[0] = ENTRY_SLOT;
    entry[4] = ENTRY_FLAGS | (c->hans ? F_UTF16 : F_ASCII);
    entry[6] = ENTRY_ID;
    copy_words(disp, (const u32 *)SETUP_DISP, STOCK_ROWS);
    disp[STOCK_ROWS] = build_map(((const u32 *)SETUP_DISP)[STOCK_ROWS - 1u], img_addr(c, entry_ok), STAY);
    if (!disp[STOCK_ROWS]) return 14u;
    c->entry_row = (u32)entry;
    if (c->fx) {
        u32 *z = entry + ROW_WORDS;
        copy_words(z, entry, ROW_WORDS);
        z[0] = SLOT_Z_ENTRY;
        z[6] = ID_Z_ENTRY;
        disp[STOCK_ROWS + 1u] = build_map(((const u32 *)SETUP_DISP)[STOCK_ROWS - 1u], img_addr(c, zebra_entry_ok), STAY);
        if (!disp[STOCK_ROWS + 1u]) return 24u;
        if (vui_ok) {
            u32 *v = entry + 2u * ROW_WORDS;
            copy_words(v, entry, ROW_WORDS);
            v[0] = SLOT_V_ENTRY;
            v[6] = ID_V_ENTRY;
            disp[STOCK_ROWS + 2u] = build_map(((const u32 *)SETUP_DISP)[STOCK_ROWS - 1u], img_addr(c, vui_entry_ok), STAY);
            if (!disp[STOCK_ROWS + 2u]) return 25u;
        }
    }

    /* Owned page: 4 rows shaped like SETUP row 41 (plain list row, value column
     * "--"; the template's own rows ask a callback for their value and showed an
     * hourglass), one event map per row. */
    u32 *own = (u32 *)z5_alloc(NROWS * ROW_SIZE, 0);
    u32 *odisp = (u32 *)z5_alloc(NROWS * 4u, 0);
    u32 *ohdr = (u32 *)z5_alloc(7u * 4u, 0);
    if (!own || !odisp || !ohdr) return 15u;
    /* Handler addresses one by one: a static initialiser (array of function
     * pointers) holds link-time addresses in this -fpic module, not runtime ones. */
    u32 handlers[NROWS];
    handlers[R_ONOFF] = img_addr(c, onoff_ok);
    handlers[R_TYPE] = img_addr(c, type_ok);
    handlers[R_VOL] = img_addr(c, vol_ok);
    handlers[R_RELOAD] = img_addr(c, reload_ok);
    u32 first_map = ((const u32 *)th[4])[0];
    for (u32 k = 0; k < NROWS; k++) {
        u32 *r = own + k * ROW_WORDS;
        copy_words(r, (const u32 *)SETUP_ROWS + (STOCK_ROWS - 1u) * ROW_WORDS, ROW_WORDS);
        r[0] = ROW_FIRST_SLOT + k;
        r[4] = ENTRY_FLAGS | (c->hans ? F_UTF16 : F_ASCII);
        r[6] = ROW_FIRST_ID + k;
        odisp[k] = build_map(first_map, handlers[k], STAY);
        if (!odisp[k]) return 16u;
    }
    c->own_rows = (u32)own;
    /* th[2] is the page's element list (32 bytes each, +4 type, type 0 ends it):
     * the template has the title (type 1, string id at +0), the back icon (type 4)
     * and the end marker. +0x1E is the widget id. Type 1 builds a string label
     * (FUN_1044EE90); types 2/3 (FUN_1044EC44 / FUN_1044EBFA) build a text widget
     * that stock code binds to a text slot afterwards (e.g. 0x1050F37E: widget
     * lookup, setter, 0x10447358, 0x1044736E); title_cb does that for this page. */
    u32 *title = (u32 *)z5_alloc(TITLE_WORDS * 4u, 0);
    if (!title) return 19u;
    copy_words(title, (const u32 *)th[2], TITLE_WORDS);
    if (title[1] != 1u || title[EL_WORDS + 1u] != 4u || title[2u * EL_WORDS + 1u] != 0u ||
        (title[7] >> 16) != TITLE_WIDGET) return 20u;
    title[1] = c->hans ? 3u : 2u;
    c->title = (u32)title;
    ohdr[0] = th[0]; ohdr[1] = (u32)layout4; ohdr[2] = (u32)title;
    ohdr[3] = (u32)own; ohdr[4] = (u32)odisp; ohdr[5] = (NROWS << 16) | NROWS; ohdr[6] = th[6];
    c->own_hdr = (u32)ohdr;
    c->title_slot = TITLE_SLOT;
    if (c->fx) c->zebra_install = install_zebra_page(c, title, first_map);
    if (vui_ok) c->vui_install = install_vui_page(c, title, first_map);

    /* Labels before anything can show them. */
    for (u32 k = 0; k <= R_TITLE; k++) set_label(c, k);

#ifndef CM_SKIP_PAGE
    page[0] = (OWN_INDEX << 16) | 2u;
    page[1] = PAGE_PARENT;
    {
        u32 *cb = (u32 *)z5_alloc(PAGE_CB_WORDS * 4u, 0);
        if (!cb) return 22u;
        copy_words(cb, (const u32 *)PAGE_CB, PAGE_CB_WORDS);
        if (cb[0] != (PAGE_CB_INIT | 1u)) return 23u;
        cb[0] = img_addr(c, title_cb);
        page[4] = (u32)cb;
    }
    page[10] = (u32)ohdr;
#endif

    hdr[3] = (u32)rows;
    hdr[4] = (u32)disp;
    hdr[5] = SETUP_COUNT + extra;              /* count last: tables are complete */
    return 1u;
}

static int fx_hooks_present(void)
{
    return *(volatile u16 *)FX_ZEBRA_SITE == FX_HOOK_HI && *(volatile u16 *)(FX_ZEBRA_SITE + 2u) == FX_HOOK_LO &&
           *(volatile u16 *)FX_GRID_SITE == FX_HOOK_HI && *(volatile u16 *)(FX_GRID_SITE + 2u) == FX_HOOK_LO;
}

/* Extend the stock framing-grid page: On, Off, 3x3, 2x2 (centre cross). Returns 1 or >= 30. */
static u32 install_grid(cm_t *c)
{
    volatile u32 *hdr = (volatile u32 *)GRID_HDR;
    if (hdr[3] != GRID_ROWS || hdr[4] != GRID_DISP || hdr[5] != GRID_COUNT) return 30u;
    const u32 *layout = (const u32 *)(hdr[1] + 0x20u);
    if (((const u32 *)hdr[1])[0] != GRID_STOCK || layout[0] != GRID_TOTAL) return 31u;
    u32 *rows = (u32 *)z5_alloc(GRID_TOTAL * ROW_SIZE, 0);
    u32 *disp = (u32 *)z5_alloc(GRID_TOTAL * 4u, 0);
    if (!rows || !disp) return 32u;
    u32 kind = c->hans ? F_UTF16 : F_ASCII;
    const u32 *stock_disp = (const u32 *)GRID_DISP;
    copy_words(rows, (const u32 *)GRID_ROWS, GRID_STOCK * ROW_WORDS);
    copy_words(rows + 2u * ROW_WORDS, (const u32 *)GRID_ROWS, ROW_WORDS);
    copy_words(rows + 3u * ROW_WORDS, (const u32 *)GRID_ROWS, ROW_WORDS);
    rows[2u * ROW_WORDS] = SLOT_3X3;
    rows[2u * ROW_WORDS + 4u] = PLAIN_ROW | kind;
    rows[2u * ROW_WORDS + 6u] = ID_3X3;
    rows[3u * ROW_WORDS] = SLOT_CROSS;
    rows[3u * ROW_WORDS + 4u] = PLAIN_ROW | kind;
    rows[3u * ROW_WORDS + 6u] = ID_CROSS;
    /* Rows with our handlers take the map the owned pages use: build_map clears the stock
     * first-row map's own movement handlers, and the cursor then cannot leave the row. */
    u32 row_map = ((const u32 *)((const u32 *)TEMPLATE_HDR)[4])[0];
    disp[0] = build_map(row_map, img_addr(c, grid_on_ok), BACK);
    disp[1] = stock_disp[1];
    disp[2] = build_map(row_map, img_addr(c, grid_3x3_ok), BACK);
    disp[3] = build_map(row_map, img_addr(c, grid_crs_ok), BACK);
    if (!disp[0] || !disp[2] || !disp[3]) return 33u;
    if (c->hans) {
        FW(set_fn, UTF16_SET)((const u16 *)u"3×3", SLOT_3X3, HANS_STYLE);
        FW(set_fn, UTF16_SET)((const u16 *)u"2×2", SLOT_CROSS, HANS_STYLE);
        *(volatile u8 *)RUNTIME_STYLE = HANS_FONT;
    } else {
        FW(set_fn, ASCII_SET)("3x3", SLOT_3X3, 0u);
        FW(set_fn, ASCII_SET)("2x2", SLOT_CROSS, 0u);
    }
    hdr[1] = (u32)layout;
    hdr[3] = (u32)rows;
    hdr[4] = (u32)disp;
    hdr[5] = (GRID_TOTAL << 16) | GRID_TOTAL;  /* count last: tables are complete */
    return 1u;
}

/* Only with the two firmware gates; otherwise the masks stay as they are. */
static void enable_dual_display(cm_t *c)
{
    volatile u32 *masks = (volatile u32 *)HIST_MASKS;
    if (*(volatile u32 *)HOOK_SAMPLING != HOOK_JUMP || *(volatile u32 *)HOOK_WIDGET != HOOK_JUMP) return;
    for (u32 i = 0; i < HIST_COUNT; i++)
        if (masks[i] != HIST_STOCK) return;
    for (u32 i = 0; i < HIST_COUNT; i++) masks[i] = HIST_DUAL;
    c->dual = 1u;
}

static void hook_release(cm_t *c)
{
    volatile u32 *slot = (volatile u32 *)SLOT_C;
    u32 cb = img_addr(c, release_cb);
    u32 cur = *slot;
    if (cur != cb && cur >= CODE_LO && cur < CODE_HI) {
        c->orig = cur;
        *slot = cb;
    }
}

static void first_run(cm_t *c)
{
    u32 cur = *(volatile u32 *)SLOT_C;
    init_strings(c);
    for (u32 i = 0; i < CFG_TRIES && cfg_load(c) == 1u; i++) { }
    c->boot_hook = cur >= CODE_LO && cur < CODE_HI;
    c->lang = FW(read_fn, SETTING_READ)(LANG_KEY);
    c->phase = PH_DUAL;
    enable_dual_display(c);
    c->hans = FW(read_fn, SETTING_READ)(LANG_KEY) != LANG_ENGLISH;
    c->phase = PH_IMAGE;
    c->image = (u32)z5_img_copy(release_cb, release_cb_end) & ~1u;
    if (!c->image) { c->install = 17u; return; }
    c->phase = PH_AUDIO;
    u8 *silent = (u8 *)z5_alloc(SILENT_LEN, 0);
    if (silent) {
        z5_memset(silent, 0, SILENT_LEN);
#ifndef CM_SKIP_CLIP
        if (z5_audio_init(silent, SILENT_LEN) == 0) c->err = load_clip(c);
#else
        if (z5_audio_init(silent, SILENT_LEN) == 0) c->err = 0;
#endif
        else c->err = ERR_AUDIO;
        z5_free(silent);
    } else {
        c->err = ERR_MEM;
    }
#ifndef CM_SKIP_MENU
    c->phase = PH_MENU;
    c->fx = fx_hooks_present();
    c->install = install_menu(c);
    if (c->vui_install == 1u) { vui_label(c); c->vui_fn = img_addr(c, vui_hook); }
    if (c->fx) {
        c->grid_tab = (u32)z5_alloc(GRID_MODES * GRID_REC, 0);
        if (c->grid_tab) {
            copy_words((u32 *)c->grid_tab, (const u32 *)GRID_TEMPLATE, GRID_MODES * GRID_REC / 4u);
            c->grid_install = install_grid(c);
        }
        fx_apply(c);
        zebra_label(c);
    }
#endif
}

/* The module's block as this anchor describes it, read without writing anything;
 * 0 when it does not look like ours. */
static cm_t *cm_peek(void)
{
    u32 u = *(volatile u32 *)(*(volatile u32 *)Z5_STATE_SLOT + 0x0Cu);
    if (u < RAM_LO || u >= RAM_HI || *(volatile u32 *)u != CM_MAGIC) return 0;
    return (cm_t *)u;
}

/* Why this power-up has no features, kept in unused anchor words: no state block exists
 * in these sessions. Word 3 is the note, words 4-7 the row text (8 UTF-16 characters).
 * X1 a menu draw came first, X2 boot run during a menu, X3 boot run after the window,
 * X4 anchor from an earlier power-up. A = loader tries, 9 = the loader gave up. */
#define STOP_NOTE     0x53540000u
#define STOP_NOTE_MASK 0xFFFF0000u
#define STOP_TEXT     (Z5_ANCHOR + 0x10u)
#define STOP_CHARS    8u

static void note_stop(u32 why)
{
    volatile u32 *a = (volatile u32 *)Z5_ANCHOR;
    if ((a[3] & STOP_NOTE_MASK) == STOP_NOTE) return;       /* keep the first reason */
    u32 tries = 0u;
    if (a[0] == 0u) tries = a[1] == 0xFFFFFFFFu ? 9u : a[1] <= 8u ? a[1] : 0u;
    a[3] = STOP_NOTE | (tries << 8) | why;
}

static void stop_loader(void)
{
    ((volatile u32 *)Z5_ANCHOR)[0] = 0u;       /* valid pair P = 0, ~P = -1: stop */
    ((volatile u32 *)Z5_ANCHOR)[1] = 0xFFFFFFFFu;
}

static u32 puts16(char *dst, u32 cap, u32 pos, const u16 *s)
{
    u16 *w = (u16 *)dst;
    while (*s && pos < cap) w[pos++] = *s++;
    return pos;
}

static int show_stop(z5_ctx *ctx)
{
    u32 note = ((volatile u32 *)Z5_ANCHOR)[3];
    char *line = (char *)STOP_TEXT;
    u32 pos = FW(read_fn, SETTING_READ)(LANG_KEY) != LANG_ENGLISH
        ? puts16(line, STOP_CHARS, 0u, (const u16 *)u"未启用")
        : z5_puts(line, STOP_CHARS, 0u, "NO ");
    pos = z5_puts(line, STOP_CHARS, pos, "X");
    pos = z5_putdec(line, STOP_CHARS, pos, note & 0xFFu);
    pos = z5_puts(line, STOP_CHARS, pos, " A");
    pos = z5_putdec(line, STOP_CHARS, pos, (note >> 8) & 0xFFu);
    z5_row_set_text(ctx->element, line, pos * 2u);
    z5_row_draw(ctx->element, ctx->r1, ctx->r2, ctx->r3);
    return 0;
}

/* A start that has not finished: its progress and how long it has been running. */
static int show_unfinished(z5_ctx *ctx, cm_t *s)
{
    char *line = (char *)STOP_TEXT;
    u32 ms = *(volatile u32 *)CM_MS_CLOCK - s->start_ms, sec = 0u;
    while (ms >= 1000u && sec < 99u) { ms -= 1000u; sec++; }
    u32 pos = FW(read_fn, SETTING_READ)(LANG_KEY) != LANG_ENGLISH
        ? puts16(line, STOP_CHARS, 0u, (const u16 *)u"未完成")
        : z5_puts(line, STOP_CHARS, 0u, "RUN");
    pos = z5_puts(line, STOP_CHARS, pos, "P");
    pos = z5_putdec(line, STOP_CHARS, pos, *(volatile u32 *)&s->phase);
    pos = z5_puts(line, STOP_CHARS, pos, " ");
    pos = z5_putdec(line, STOP_CHARS, pos, sec);
    z5_row_set_text(ctx->element, line, pos * 2u);
    z5_row_draw(ctx->element, ctx->r1, ctx->r2, ctx->r3);
    return 0;
}

int z5_main(z5_ctx *ctx)
{
    int boot = (ctx->sequence & 0xFFu) == SEQ_BOOT;
    /* Camera report 2026-11-08 (group 22): when the first run waited for the card, it
     * could end up in a menu draw and the camera froze hard. The first run now happens
     * only in the boot run of the class seen on camera, never in a menu draw. */
#ifdef CM_TEST_AFTER_MS   /* emulator reproduction only: hold the first run until a menu is up */
    if (!z5_anchor_valid() && boot &&
        (*(volatile u32 *)0xC18EB388u < CM_TEST_AFTER_MS || (ctx->sequence >> 8) != BOOT_CLASS)) {
        ((volatile u32 *)Z5_ANCHOR)[1] = 1u;   /* keep the loader's attempts */
        return 1;
    }
#endif
    if (!z5_anchor_valid()) {
        /* Camera report 2026-11-09: after a standby wake the module never started, because
         * the first draw that reached here was not class 4 and the old rule stopped the
         * loader for good. Now only a menu stops it (group 22/25 froze with the first run
         * inside a menu draw): the shooting page may start from any class. */
        u32 page = *(volatile u32 *)CURRENT_PAGE;
        u32 cls = ctx->sequence >> 8;
        /* Camera report 2026-11-09 (group 29): the first run came late, while a menu was
         * opening (the entry row appeared only after re-entering the menu), and the camera
         * froze on returning to shooting. All three freezes had a missing entry row, so a
         * start after the power-up window is refused: no features this session, no freeze. */
        if (!boot || page == PAGE_MENU || *(volatile u32 *)CM_MS_CLOCK >= CM_START_WINDOW_MS) {
            note_stop(!boot ? 1u : page == PAGE_MENU ? 2u : 3u);
            stop_loader();
            return boot ? 1 : show_stop(ctx);
        }
        if (cls != BOOT_CLASS && page != PAGE_SHOOTING) return 1;   /* the loader retries */
    } else if (!boot) {
        /* A menu draw never starts or joins a first run, and never writes through an
         * anchor it cannot trust. The anchor lies in RAM that boot does not clear, while
         * the SETUP header is re-initialised at every boot: stock rows with a finished
         * install mean the anchor came from an earlier power-up. */
        cm_t *s = cm_peek();
        if (!s) {                              /* X5: valid anchor, block not recognised */
            note_stop(5u);
            return show_stop(ctx);
        }
        if (*(volatile u32 *)&s->phase != PH_DONE) return show_unfinished(ctx, s);
        if (s->install == 1u && ((volatile u32 *)SETUP_HDR)[3] == SETUP_ROWS) {
            note_stop(4u);
            stop_loader();
            return show_stop(ctx);
        }
    }
    cm_t *c = (cm_t *)z5_state_user(sizeof(cm_t));
    if (!c) return 1;
    if (c->magic != CM_MAGIC) {                /* block is zeroed on first use */
        *(volatile u32 *)&c->phase = PH_CFG;   /* before the magic: a draw on another task
                                                * must see an unfinished start */
        *(volatile u32 *)&c->magic = CM_MAGIC;
        c->boot_class = ctx->sequence >> 8;
        c->start_ms = *(volatile u32 *)CM_MS_CLOCK;
        first_run(c);
        c->took_ms = *(volatile u32 *)CM_MS_CLOCK - c->start_ms;
#ifdef CM_TEST_STUCK_PHASE   /* emulator check of the unfinished-start row only */
        *(volatile u32 *)&c->phase = CM_TEST_STUCK_PHASE;
#else
        *(volatile u32 *)&c->phase = PH_DONE;
#endif
    }
    if (c->image) hook_release(c);
    if (boot) return 1;                        /* never touch the boot widget */

    /* MY MENU row, one line in either language. Healthy: type, start time + duration (ms),
     * play requests. Otherwise the codes: D<level-page histogram> E<clip> I<install>
     * C<MENU.DAT> P<start progress> B<draw class of the start>. */
    char *line = c->text;
    u32 pos = 0;
    u32 hans = FW(read_fn, SETTING_READ)(LANG_KEY) != LANG_ENGLISH;
    if (c->dual && !c->err && c->install == 1u && !c->cfg_state) {
#ifdef VUI_DIAG         /* oldest first: application.ui.target, then the entries seen in the quality-label area */
        {
            /* Read-only search for the stock histogram: 256 words, all below 2^20, most of them non-zero, going up
               and down (a lookup table only rises). Only around the histogram state block at 0xC2028094 (the Z6II
               counterpart keeps its 256 counts inside that block). Shows the two best windows as
               (address - 0xC2026000) / 4 and their sums. */
            const volatile u32 *m = (const volatile u32 *)0xC2026000u;
            u32 i, nz = 0u, big = 0u, dec = 0u, sum = 0u, prev = 0u;
            u32 a1 = 0u, n1 = 0u, s1 = 0u, a2 = 0u, n2 = 0u, s2 = 0u;
            for (i = 0u; i < 0x1800u; i++) {
                u32 v = m[i];
                if (v) nz++;
                if (v >= 0x100000u) big++; else sum += v;
                if (i && v < prev) dec++;
                if (i >= 256u) {
                    u32 o = m[i - 256u], op = m[i - 257u];
                    if (o) nz--;
                    if (o >= 0x100000u) big--; else sum -= o;
                    if (o < op) dec--;
                }
                prev = v;
                if (i >= 256u && !big && dec >= 16u && sum >= 4096u && nz >= 64u) {
                    u32 at = i - 255u;
                    if (nz > n1) {
                        if (at - a1 >= 256u && a1 - at >= 256u) { a2 = a1; n2 = n1; s2 = s1; }
                        a1 = at; n1 = nz; s1 = sum;
                    } else if (nz > n2 && at - a1 >= 256u && a1 - at >= 256u) {
                        a2 = at; n2 = nz; s2 = sum;
                    }
                }
            }
            pos = z5_puts(line, TEXT_CHARS, pos, "A");
            pos = z5_putdec(line, TEXT_CHARS, pos, a1);
            pos = z5_puts(line, TEXT_CHARS, pos, " S");
            pos = z5_putdec(line, TEXT_CHARS, pos, s1);
            pos = z5_puts(line, TEXT_CHARS, pos, " B");
            pos = z5_putdec(line, TEXT_CHARS, pos, a2);
            pos = z5_puts(line, TEXT_CHARS, pos, " S");
            pos = z5_putdec(line, TEXT_CHARS, pos, s2);
            z5_row_set_text(ctx->element, line, pos * 2);
            z5_row_draw(ctx->element, ctx->r1, ctx->r2, ctx->r3);
            return 0;
        }
#endif
#ifdef CM_ORIENT_TEST   /* read-only: stock orientation getter and the two words it reads */
        pos = z5_puts(line, TEXT_CHARS, pos, "ORI ");
        pos = z5_putdec(line, TEXT_CHARS, pos, FW(read_fn, 0x101EA71Eu)(0));
        pos = z5_puts(line, TEXT_CHARS, pos, " S");
        pos = z5_putdec(line, TEXT_CHARS, pos, *(volatile u32 *)(0xC1D4126Cu + 0x1A4u));
        pos = z5_puts(line, TEXT_CHARS, pos, " M");
        pos = z5_putdec(line, TEXT_CHARS, pos, *(volatile u32 *)(0xC1807DDCu + 0xCu));
        z5_row_set_text(ctx->element, line, pos * 2);
        z5_row_draw(ctx->element, ctx->r1, ctx->r2, ctx->r3);
        return 0;
#endif
        if (hans) {
            pos = puts16(line, TEXT_CHARS, pos, (const u16 *)u"正常 类型");
        } else {
            pos = z5_puts(line, TEXT_CHARS, pos, "OK T");
        }
        pos = z5_putdec(line, TEXT_CHARS, pos, c->type);
        if (hans) pos = puts16(line, TEXT_CHARS, pos, (const u16 *)u" 启动");
        else pos = z5_puts(line, TEXT_CHARS, pos, " t");
        pos = z5_putdec(line, TEXT_CHARS, pos, c->start_ms);
        pos = z5_puts(line, TEXT_CHARS, pos, "+");
        pos = z5_putdec(line, TEXT_CHARS, pos, c->took_ms);
        if (hans) pos = puts16(line, TEXT_CHARS, pos, (const u16 *)u" 声");
        else pos = z5_puts(line, TEXT_CHARS, pos, " r");
        pos = z5_putdec(line, TEXT_CHARS, pos, c->reqs);
    } else {
        if (hans) pos = puts16(line, TEXT_CHARS, pos, (const u16 *)u"异常 D");
        else pos = z5_puts(line, TEXT_CHARS, pos, "ERR D");
        pos = z5_putdec(line, TEXT_CHARS, pos, c->dual);
        pos = z5_puts(line, TEXT_CHARS, pos, "E");
        pos = z5_putdec(line, TEXT_CHARS, pos, c->err);
        pos = z5_puts(line, TEXT_CHARS, pos, "I");
        pos = z5_putdec(line, TEXT_CHARS, pos, c->install);
        pos = z5_puts(line, TEXT_CHARS, pos, "C");
        pos = z5_putdec(line, TEXT_CHARS, pos, c->cfg_state);
        pos = z5_puts(line, TEXT_CHARS, pos, "P");
        pos = z5_putdec(line, TEXT_CHARS, pos, c->phase);
        pos = z5_puts(line, TEXT_CHARS, pos, " B");
        pos = z5_putdec(line, TEXT_CHARS, pos, c->boot_class);
    }
    z5_row_set_text(ctx->element, line, pos * 2);
    z5_row_draw(ctx->element, ctx->r1, ctx->r2, ctx->r3);
    return 0;
}
