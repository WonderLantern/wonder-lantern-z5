/* Custom DISP overlays on the detailed-information photo page, LCD and EVF. */
#ifndef Z5_CUSTOM_DISP_H
#define Z5_CUSTOM_DISP_H
#include "z5/z5.h"
/* Owner points into the card module's persistent cm_t, never its load image.
 * Called once from the upstream boot first_run, on the native GUI path. */
/* Retained callback void(bank,surface,packet,dirty_rows): surface=0 prepares GPU cleanup before sync;
 * nonzero surface paints cached statistics into checked CPU pixels after sync. */
int custom_disp_init(void **owner, u32 before_level, u32 *hist_diagnostics, u32 *custom_flags);
u32 custom_disp_status(void *owner);
u32 custom_disp_format(void *owner, char *text, u32 cap);
#endif
