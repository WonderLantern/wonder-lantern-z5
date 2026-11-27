/* A84m (2026-11-25): the A84 hooks (a84_fx43.S) as thin stubs; the logic lives in the card module
 * (aspect_fw.inc in the release worktree), reached through the release's generic entry vui_common (0x121207A8:
 * r0 = kind, r1 = frame; it tail-calls the module and returns the module's r0, or returns at once with r0 = kind
 * when no module is there). User 2026-11-25: move the logic to the module to free MAIN append space.
 *
 * Frame (40 bytes, keeps 8-byte stack alignment): [0] r0 [1] r1 [2] r2 [3] r3 [4] r4 [5] r5 [6] r6 [7] r9 [8] r12
 * [9] lr; the caller's stack follows at +40. The module changes a frame word to change a register after the call.
 *   kind 19  cfg     0x10142A7C entry          trace + area-8 remap + descriptor rebuild / override (module)
 *   kind 20  offset  0x1020933E, area 8 only   frame r0 = the pair pointer when the module takes it
 *   kind 21  trim    0x10143032 / trim2 0x10142F6C, area 8 only (trim2 also column r9 <= 2): frame r1 = row
 *   kind 22  hpvw    0x1034E5E4 entry          module returns 0 = handled (it wrote [r0]): return to the caller
 *   kind 23  hplay   0x1034F6D4                frame r0 = area to store (8 for a file of the current ratio)
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

    .global hplay
    .thumb_func
hplay:                                      @ 0x1034F6D4: strb.w r0, [r4, #0x200], then 0x1034F6D8
    frame_call 23
    frame_pop
    strb.w  r0, [r4, #0x200]
    ldr     pc, =0x1034F6D9

    .ltorg
