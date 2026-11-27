/* B3m (2026-11-26): B2m with a handled path in the kind-28 stub: the module returns 0 and puts its own size pair in
 * frame r0 (our 4:3 size while 4:3 is chosen, independent of the area-8 table switch); otherwise as B2m. B2 camera
 * (DIAG18-20): the window updater ran before the tables were switched in, the window stayed FX, the fit went 4:3
 * on an EVF switch: squeezed picture.
 * B2m (2026-11-26): A90m + kind 28 at the two live-view calls of the area size selector 0x102092A6: the window
 * updater 0x10197FFC (selected area -> source window 0xC23802C8 + 0x5E) and the display fit 0x1018B388 (area size ->
 * LCD / EVF rectangle 0xC23802F4, 1920 x 1080 rectangle 0xC2380314). Both `bl 0x102092A6` become `bl` to a veneer;
 * the stub hands r0 (area) / r1 to the module, which may change frame r0 (FX 0 -> 8 while 4:3 is on: area 8's
 * size is then our 5352 x 4016), then tail-calls the stock selector with lr = the caller's return. B1 camera
 * (DIAG17) + research/aspect/b1_fit_replay.py: the stock fit gives (40, 0, 680, 480) on the LCD for 4:3.
 *   kind 28  hszw 0x10197FFC / hszf 0x1018B388   frame r0 = area to query or (B3, module returns 0) the size
 *            pair to return; lr 0x10198001 / 0x1018B38D
 * camera_verified: false. */
/* A90m (2026-11-25): A87m + kind 27 at 0x10355DCC, the full-size image size of the playback prep (out = output-size
 * table by the file's area code; kind-6 entry of the decode descriptor, desc + 0x60). A89 snapshots: our NEF files
 * still had 6016 x 4016 there while every other size was ours; the module returns the file record's main size for
 * our NEF files (callers 0x10355051 / 0x1034EA21 only). Returns 0 = handled (it wrote [r0]).
 * A87m (2026-11-25): A85m + the NEF branch of the playback decode setup 0x1034F5F6 (kind 25 at 0x1034F65A): files
 * with the RAW flag (0x103909B6(entry, 4 / 11)) skip the area store at 0x1034F6D4 (hplay), so NEF-only shots were
 * decoded with FX geometry (A86 camera, DIAG14: playback configurations at area 0, k220 1). The module sets area 8
 * there for a file of the current ratio, as hplay does for JPEG.
 * A85m (2026-11-25): A84m (a84m_fx43.S) + one more stub: the extended preview-size function 0x1034E692 (kind 24),
 * the twin of hpvw (0x1034E5E4, kind 22). Z30 v43 port (WonderLantern Z30 docs/aspect-raw-playback-v43.md): the card
 * module asks the native file service 0x103BC7E0 (Z30 0x103A1FA8) for the file's own small / large preview size
 * (kinds 4 / 5) instead of the stock size by aspect class. A84 camera (DIAG06-08): NEF-only geometry and previews in
 * the file are right, yet the camera shows them stretched / broken: the stock sizes by class (FX 1620 x 1080) do not
 * match the 1440 x 1080 / 1920 x 824 previews in the file.
 *
 * Frame (40 bytes, keeps 8-byte stack alignment): [0] r0 [1] r1 [2] r2 [3] r3 [4] r4 [5] r5 [6] r6 [7] r9 [8] r12
 * [9] lr; the caller's stack follows at +40. The module changes a frame word to change a register after the call.
 *   kind 19  cfg     0x10142A7C entry          trace + area-8 remap + descriptor rebuild / override (module)
 *   kind 20  offset  0x1020933E, area 8 only   frame r0 = the pair pointer when the module takes it
 *   kind 21  trim    0x10143032 / trim2 0x10142F6C, area 8 only (trim2 also column r9 <= 2): frame r1 = row
 *   kind 22  hpvw    0x1034E5E4 entry          module returns 0 = handled (it wrote [r0]): return to the caller
 *   kind 23  hplay   0x1034F6D4                frame r0 = area to store (8 for a file of the current ratio)
 *   kind 24  hext    0x1034E692 entry          as kind 22 (module returns 0 = handled, it wrote [r0])
 *   kind 25  hnef    0x1034F65A (RAW branch)   module may store area 8 into [r4 + 0x200]; caller [sp] = file entry
 *   kind 27  hfull   0x10355DCC entry          r0 = out (w, h), r1 = class, r2 = size; 0 = handled
 * Without the module every stub replays the stock instructions (r0 back from the frame, the kind otherwise).
 * camera_verified: false. */
    .syntax unified
    .thumb

    .equ vui_common, 0x121207A8

    .macro  frame_call kind
    push.w  {r0, r1, r2, r3, r4, r5, r6, r9, r12, lr}
    movs    r0, #\kind
    mov     r1, sp
    bl      vui_common
    .endm

    .macro  frame_pop
    pop.w   {r0, r1, r2, r3, r4, r5, r6, r9, r12, lr}
    .endm

    .global cfg
    .thumb_func
cfg:                                        @ 0x10142A7C: push.w {r3-r11, lr}
    frame_call 19
    frame_pop
    push.w  {r3, r4, r5, r6, r7, r8, r9, r10, r11, lr}
    ldr     pc, =0x10142A81

    .global offset
    .thumb_func
offset:                                     @ 0x1020933E: r0 = area -> r0 = pair, r1 = table
    cmp     r0, #8
    bne     1f
    frame_call 20
    frame_pop
    cmp     r0, #8
    bne     2f                              @ the module gave the block's pair
1:  ldr     r1, =0x1167B7F4
    add.w   r0, r1, r0, lsl #2
    bx      lr
2:  ldr     r1, =0x1167B7F4
    bx      lr

    .global trim
    .thumb_func
trim:                                       @ 0x10143032 (r5 = capture info), then 0x10143044
    ldrb.w  r1, [r5, #0x200]
    ldr     r2, =0x11648258
    ldrb.w  r0, [r5, #0x221]
    add.w   r1, r1, r1, lsl #1
    add.w   r1, r2, r1, lsl #2
    bl      row8
    ldr     pc, =0x10143045

    .global trim2
    .thumb_func
trim2:                                      @ 0x10142F6C, then 0x10142F7A adds r9 * 4
    ldrb.w  r0, [r5, #0x200]
    ldr     r1, =0x116481EC
    add.w   r0, r0, r0, lsl #1
    add.w   r1, r1, r0, lsl #2
    cmp.w   r9, #2                          @ the block row has three columns
    it      ls
    blls    row8
    ldr     pc, =0x10142F7B

    .thumb_func
row8:                                       @ r1 = stock row; area 8: the module may replace it
    push    {r0, lr}
    ldrb.w  r0, [r5, #0x200]
    cmp     r0, #8
    bne     1f
    frame_call 21
    frame_pop
1:  pop     {r0, pc}

    .global hpvw
    .thumb_func
hpvw:                                       @ 0x1034E5E4: r0 = out (w, h), r1 = class, r2 = small
    frame_call 22
    cmp     r0, #0
    frame_pop
    it      eq
    bxeq    lr                              @ handled by the module
    push    {r4, r5, r6, lr}                @ the replaced push; cbz r2 relocated
    cmp     r2, #0
    beq     5f
    ldr     pc, =0x1034E5E9
5:  ldr     pc, =0x1034E625

    .global hext
    .thumb_func
hext:                                       @ 0x1034E692: r0 = out (w, h), r1 = class, r2 = small
    frame_call 24
    cmp     r0, #0
    frame_pop
    it      eq
    bxeq    lr                              @ handled by the module
    push    {r4, lr}                        @ the replaced push; cbz r2 relocated
    cmp     r2, #0
    beq     6f
    ldr     pc, =0x1034E697
6:  ldr     pc, =0x1034E6CF

    .global hnef
    .thumb_func
hnef:                                       @ 0x1034F65A: strb r6, [r4]; ldr r0, [sp], then 0x1034F65E
    frame_call 25
    frame_pop
    strb    r6, [r4, #0]
    ldr     r0, [sp]
    ldr     pc, =0x1034F65F

    .global hfull
    .thumb_func
hfull:                                      @ 0x10355DCC: mov r3, r1; mov r1, r2, then 0x10355DD0
    frame_call 27
    cmp     r0, #0
    frame_pop
    it      eq
    bxeq    lr                              @ handled by the module
    mov     r3, r1
    mov     r1, r2
    ldr     pc, =0x10355DD1

    .global hplay
    .thumb_func
hplay:                                      @ 0x1034F6D4: strb.w r0, [r4, #0x200], then 0x1034F6D8
    frame_call 23
    frame_pop
    strb.w  r0, [r4, #0x200]
    ldr     pc, =0x1034F6D9

    .global hszw
    .global hszf
    .thumb_func
hszw:                                       @ 0x10197FFC: bl 0x102092A6 (window updater, selected area)
    .thumb_func
hszf:                                       @ 0x1018B388: bl 0x102092A6 (display fit, area size)
    frame_call 28
    cmp     r0, #0
    frame_pop
    it      eq
    bxeq    lr                              @ handled: r0 = the module's size pair
    ldr     pc, =0x102092A7                 @ the stock selector with r0 (area), lr = caller

    .ltorg
