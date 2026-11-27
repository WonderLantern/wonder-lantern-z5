/* B10m (2026-11-27): B9m + the two kind=0x2b ELBY_fill (0x101460F2) call sites that live view actually uses on real
 * hardware (proven from B9 camera DIAG16-18 where kind=0x1b ELBY hit count was 0, and B5 RAMD2/RAMD3 where active ELBY
 * descriptors at 0xC2352C9C have plane_cb = plane_y + 0x8000, which only happens for r2 == 0x2b at 0x1014644C):
 *   - 0x10146E48 (lr 0x10146E4D, inside alloc_2b 0x10146E26, called from 0x100A33B2 / 0x100A33C0)
 *   - 0x10146E5A (lr 0x10146E5F, inside fill_2b  0x10146E50, called from 0x100A31FE)
 * Both `bl` sites route through the existing hszw veneer (0x105BA3C4, zero new trampolines) and fall back to stock
 * 0x101460F3 via `8f` when unhandled or when the module is absent.
 *
 * B9m (2026-11-27): B8m + ELBY_fill kind=0x1b sites (0x101464CA / 0x1014659A / 0x101465D2), LV_crop_tbl (0x101455C6),
 * and ELBR size query (0x10146BA2 -> 0x1020949A). On camera (DIAG17), ELBR (896x672) and LV_crop_tbl fired cleanly.
 * B8m / B7m / B4m / B3m / B2m / A90m..A84m: unchanged.
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
hszw:                                       @ kind 28 shared entry for all live-view bl sites
    .thumb_func
hszf:
    frame_call 28
    cmp     r0, #0
    frame_pop
    it      eq
    bxeq    lr                              @ handled by the module
    push    {r0}                            @ B7 / B8 / B9 / B10: dispatch stock tail by lr, all registers kept
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

    .ltorg
