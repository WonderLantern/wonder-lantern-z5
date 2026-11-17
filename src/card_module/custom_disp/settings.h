/* Four logical DISP identities, five legacy-compatible settings bits per page.
 * Macros keep callers inside their own retained image (no transient helper). */
#ifndef Z5_CUSTOM_DISP_SETTINGS_H
#define Z5_CUSTOM_DISP_SETTINGS_H
#define CUSTOM_PAGE_COUNT 4u
#define CUSTOM_PAGE_FLAGS(value, page) __extension__ ({ \
    u32 custom_page_index = (page); \
    custom_page_index < CUSTOM_PAGE_COUNT ? (((value) >> (custom_page_index * 5u)) & 31u) : 0u; \
})
#define CUSTOM_PAGES_MASK 0xFFFFFu
#define CUSTOM_ENABLE_MASK 0x8421u
#define CUSTOM_DEFAULT_FLAGS (15u | (1u << 5) | (5u << 10) | (17u << 15))
#define CUSTOM_NORMALIZE(value) (((value) & ~(((value) & 0x84210u) >> 3u)) & CUSTOM_PAGES_MASK)
/* Sixteen-byte MENU.DAT: D8 preferences + CD50 control/page memory word.
 * Per-page enable bits are canonical ones; only CONTROL_ON enables takeover.
 * ACTIVE is GUI-published runtime state, never persisted. */
#define CUSTOM_CFG_TAG 0xD8000000u
#define CUSTOM_CFG_OLD_TAG 0xD7000000u
#define CUSTOM_CONTROL_TAG 0xCD500000u
#define CUSTOM_CONTROL_ON 1u
#define CUSTOM_CONTROL_ACTIVE 2u
#define CUSTOM_CONTROL_MASK 0x771u
#define CUSTOM_LOGICAL(control, output) (((control) >> ((output) ? 8u : 4u)) & 7u)
#define CUSTOM_SET_LOGICAL(control, output, page) \
    (((control) & ~(7u << ((output) ? 8u : 4u))) | ((page) << ((output) ? 8u : 4u)))
#define CUSTOM_ACTIVE_FLAGS(flags, control, output) \
    (((control) & 3u) == 3u ? CUSTOM_PAGE_FLAGS((flags), CUSTOM_LOGICAL((control), (output))) : 0u)
#define CUSTOM_CFG_MECH_OFF (1u << 20)
#define CUSTOM_CFG_SIL_OFF (1u << 21)
#endif
