@ boot_multi.S - stage0 loader with a power-up trigger on several widget draw classes.
@ The class list is filled in by stage0_multi.py at the CLASSES marker: variant 'multi'
@ uses 17 classes, variant 'c4' only class 0 (the menu widget, needed for the
@ MY MENU row) and class 4 (0x103864E1, the class that started the module after
@ cold boot and standby wake on camera, 2026-11-07).
@
@ Camera tests (2026-11-07): the stage0_boot trigger (first draw of the menu
@ widget class 0x104B1F59 after power-up) fires ~2 s after a cold boot, but after
@ a standby wake that class is not drawn until a menu or the i menu opens. So the
@ same "run the module once after power-up" check is now placed in front of every
@ widget draw callback found stored at widget +0x4C (Ghidra census), except
@ 0x103AA8D1, whose value appears six times in the image.
@
@ Each class stub saves r0-r3, calls `common` with its class index in r4, then
@ tail-calls the class's original callback with r0-r3/lr unchanged.
@
@ common - the power-up check, on the SDK anchor pair at 0xC18F8120 (P, ~P):
@   P ^ ~P == 0xFFFFFFFF   module already ran (valid pair) or gave up -> nothing
@   P == 0                 attempting: ~P = attempts so far
@   anything else          RAM garbage after power-up: attempts = 0
@   attempts >= 8          give up: P = 0, ~P = 0xFFFFFFFF
@   attempts > 0 and less than 500 ms since the last one (anchor+8) -> wait
@   otherwise              P = 0, ~P = attempts + 1, anchor+8 = now, and run the
@                          module through body2 with ctx.sequence = 2 | (class << 8)
@ A failed load (card not ready, no file) leaves the pair invalid, so a later draw
@ retries; a successful load replaces it with the SDK's real state pointer.
@ anchor+8 is the SDK audio word, which z5_state() zeroes when it creates state.
@
@ body  = stage0 body (+0x2E: mov r0, ip), tail-calls the original 0x104B1F59;
@         used for the MY MENU row (id 0x410), ctx.sequence 1, exactly as stage0.
@ body2 = the same body with both tail calls (+0x0C, +0x1AA) redirected to
@         `ret` (bx lr), so it returns to `common` on every path.

    .syntax unified
    .thumb
    .text

    .equ ANCHOR_LO, 0x8120
    .equ ANCHOR_HI, 0xC18F
    .equ CLOCK_LO,  0xB388
    .equ CLOCK_HI,  0xC18E
    .equ LIMIT,     8
    .equ GAP_MS,    500

    .macro CLS idx, orig
    .global cls\idx
cls\idx:
    push    {r0-r3, r4-r6, lr}
    movs    r4, #\idx
    bl      common
    pop     {r0-r3, r4-r6, lr}
    movw    ip, #:lower16:\orig
    movt    ip, #:upper16:\orig
    bx      ip
    .endm

    .global entry
entry:                                  @ the stage0 literal (class 0, 0x104B1F59)
    ldr.w   ip, [r0, #8]
    cmp.w   ip, #0x410
    bne     cls0
    mov.w   ip, #1                      @ MY MENU row: ordinary stage0 load
    b.w     body_run

@@CLASSES@@

common:                                 @ r4 = class; caller's r0-r3 at [sp]
    push    {r7, lr}
    movw    r6, #ANCHOR_LO
    movt    r6, #ANCHOR_HI
    ldr     r0, [r6]
    ldr     r1, [r6, #4]
    eor     r2, r0, r1
    adds    r2, #1
    beq     done                        @ valid pair: ran already, or gave up
    cmp     r0, #0
    it      ne
    movne   r1, #0                      @ garbage: no attempt yet
    cmp     r1, #LIMIT
    blo     try
    movs    r0, #0
    mvns    r1, r0
    str     r0, [r6]                    @ give up: P = 0, ~P = 0xFFFFFFFF
    str     r1, [r6, #4]
    b       done
try:
    movw    r2, #CLOCK_LO
    movt    r2, #CLOCK_HI
    ldr     r2, [r2]                    @ now (ms)
    cbz     r1, go
    ldr     r3, [r6, #8]
    subs    r3, r2, r3
    cmp.w   r3, #GAP_MS
    blo     done                        @ too soon after the last attempt
go:
    movs    r0, #0
    str     r0, [r6]                    @ P  = 0
    adds    r1, #1
    str     r1, [r6, #4]                @ ~P = attempts
    str     r2, [r6, #8]                @ last attempt time
    add     r7, sp, #8
    ldm     r7, {r0-r3}                 @ the draw call's own arguments
    lsls    r5, r4, #8
    orr     r5, r5, #2
    mov     ip, r5                      @ ctx.sequence = 2 | class << 8
    bl      body2_run
done:
    pop     {r7, pc}

ret:
    bx      lr

    .balign 4, 0
body:
    .incbin "code_patched.bin"
    .set body_run, body + 0x16

    .balign 4, 0
body2:
    .incbin "code_patched.bin", 0, 0x0C
    movw    ip, #:lower16:(ret + 1)
    movt    ip, #:upper16:(ret + 1)
    .incbin "code_patched.bin", 0x14, 0x1AA - 0x14
    movw    ip, #:lower16:(ret + 1)
    movt    ip, #:upper16:(ret + 1)
    .incbin "code_patched.bin", 0x1B2
    .set body2_run, body2 + 0x16
