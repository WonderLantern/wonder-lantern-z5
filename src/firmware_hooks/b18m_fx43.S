/* B18m (2026-11-27): B15m + the four preview-kind selector functions (0x102092FA / 0x10209304 / 0x1020930E /
 * 0x10209318) and three capture preview-buffer table loads (0x10143572 / 0x101435D4 / 0x10143F2E) routed through
 * hszw's veneer (0x105BA3C4, zero new trampolines). Stock Z5 kept native 5:4 preview entries (600x480, 1350x1080,
 * 150x120, 468x375) in kind 2, which A81 repurposed in ROM for 2.33:1 (640x272, 1920x824, 160x68, 570x244). When
 * 5:4 is active, the module supplies the stock Nikon 5:4 entries for kind 2 (including 0x1018B3A0 inside the native
 * live-view display fit 0x1018B4A2); otherwise (no module, off, 4:3, 2.33:1, or kind != 2) the stub replays the
 * exact stock instructions. camera_verified: false.
 *
 * B15m (2026-11-27): B12m + the live-view manager's fit call 0x1018D268 (`bl 0x1018B4A2`, lr 0x1018D26D) through
 * hszw's veneer. For 2.33:1 the module runs the stock fit itself and then turns the 16:9 class rectangles into 2.33:1
 * (the stock fit only knows 3:2 / 4:3 / 5:4 / 16:9 / 1:1, 0x101DC978); otherwise the stock 0x1018B4A3 runs.
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
hszw:                                       @ kind 28 shared entry for all live-view and B18 preview bl sites
    .thumb_func
hszf:
    frame_call 28
    cmp     r0, #0
    frame_pop
    it      eq
    bxeq    lr                              @ handled by the module
    push    {r0}                            @ B7..B18: dispatch stock tail by lr, all registers kept
    ldr     r0, =0x101905C1                 @ B7: area -> aspect class
    cmp     lr, r0
    beq     7f
    ldr     r0, =0x10197033                 @ B8: area -> aspect class (mode change)
    cmp     lr, r0
    beq     7f
    ldr     r0, =0x101464CF                 @ B9: ELBY_fill site 1 (0x1b)
    cmp     lr, r0
    beq     8f
    ldr     r0, =0x1014659F                 @ B9: ELBY_fill site 2 (0x1b)
    cmp     lr, r0
    beq     8f
    ldr     r0, =0x101465D7                 @ B9: ELBY_fill site 3 (0x1b pool cb)
    cmp     lr, r0
    beq     8f
    ldr     r0, =0x10146E4D                 @ B10: ELBY_fill site 4 (0x2b alloc, live view)
    cmp     lr, r0
    beq     8f
    ldr     r0, =0x10146E5F                 @ B10: ELBY_fill site 5 (0x2b refill, live view)
    cmp     lr, r0
    beq     8f
    ldr     r0, =0x101455CB                 @ B9: LV_crop_tbl
    cmp     lr, r0
    beq     9f
    ldr     r0, =0x10146BA7                 @ B9: ELBR 0x1020949A
    cmp     lr, r0
    beq     10f
    ldr     r0, =0x101904AD                 @ B12: live-view manager message
    cmp     lr, r0
    beq     11f
    ldr     r0, =0x1018D26D                 @ B15: live-view fit
    cmp     lr, r0
    beq     12f
    ldr     r0, =0x10209301                 @ B18: preview selector 0x102092FA (small 640)
    cmp     lr, r0
    beq     13f
    ldr     r0, =0x1020930B                 @ B18: preview selector 0x10209304 (large 1920)
    cmp     lr, r0
    beq     14f
    ldr     r0, =0x10209315                 @ B18: preview selector 0x1020930E (thumb 160)
    cmp     lr, r0
    beq     15f
    ldr     r0, =0x1020931F                 @ B18: preview selector 0x10209318 (disp 570)
    cmp     lr, r0
    beq     16f
    ldr     r0, =0x10143577                 @ B18: buffer init small preview load
    cmp     lr, r0
    beq     17f
    ldr     r0, =0x101435D9                 @ B18: buffer init large preview load
    cmp     lr, r0
    beq     18f
    ldr     r0, =0x10143F33                 @ B18: secondary buffer init large preview load
    cmp     lr, r0
    beq     19f
    pop     {r0}
    ldr     pc, =0x102092A7                 @ stock area size selector (WIN / FIT / CROP)
7:  pop     {r0}
    ldr     pc, =0x10190577                 @ stock area -> class map
8:  pop     {r0}
    ldr     pc, =0x101460F3                 @ stock ELBY_fill
9:  pop     {r0}
    ldr     pc, =0x10145355                 @ stock LV_crop_tbl
10: pop     {r0}
    ldr     pc, =0x1020949B                 @ stock area_tbl_fc (0x1020949A)
11: pop     {r0}
    ldr     pc, =0x1018D015                 @ stock live-view manager (always runs)
12: pop     {r0}
    ldr     pc, =0x1018B4A3                 @ stock live-view fit
13: pop     {r0}
    ldr     r1, =0x1167B6E8
    adds    r1, #0xBC
    b       20f
14: pop     {r0}
    ldr     r1, =0x1167B6E8
    adds    r1, #0xD0
    b       20f
15: pop     {r0}
    ldr     r1, =0x1167B6E8
    adds    r1, #0xE4
    b       20f
16: pop     {r0}
    ldr     r1, =0x1167B6E8
    adds    r1, #0xF8
20: add.w   r0, r1, r0, lsl #2
    bx      lr
17: pop     {r0}
    ldrb.w  r0, [r6, #0x21E]
    ldr     r3, =0x116489E0
    bx      lr
18: pop     {r0}
    ldrb.w  r1, [r6, #0x21E]
    ldr     r3, =0x116489E0
    bx      lr
19: pop     {r0}
    ldr     r0, =0x116489F4
    add.w   r1, r0, r1, lsl #2
    bx      lr

    .ltorg
