/* A84 (readout): A83 camera (DIAG01-05, README section 42): NEF-only shots take the configuration's second trim
 * table 0x116481EC (0x10142F62: info[0x220] | r2 != 0 -> 0x10142F6C), which had no hook, so area 8 got the stock
 * 1:1 trim (1000, 0) while the output was 4:3 / 2.33: a square stretched to the ratio, garbled playback.
 *   trim2   0x10142F6C  area 8, enabled, column r9 <= 2: the block's trim row (as `trim`), else the stock row
 * The A83 trace points kind 20 (descriptor) and kind 21 (0x1034E700, never called on the camera) are removed for
 * room; kind 19 (cfg entry) stays. camera_verified: false.
 *
 * ---- A83 header ---- */
/* A83 (readout): A82 + three trace points that call the card module through the release's generic entry
 * vui_common (0x121207A8; returns at once without the module). Behaviour otherwise A82.
 *   kind 19  cfg entry (0x10142A7C), frame r0 = obj, r1 = info, lr = caller (before any rewrite)
 *   kind 20  area descriptor 0x101DC3E8 entry, r0 = info + 0x200 (area at [r0], size at [r0 + 0x22]), lr = caller
 *   kind 21  0x1034E700 entry (playback aspect class from a size pair), r0 = the pair, lr = caller
 * Why: A82 on camera - NEF-only previews are a square sensor region stretched to 4:3, and NEF playback takes
 * the 0x1034E700 / 0x1034E692 branch that has no hook. camera_verified: false.
 *
 * ---- A82 header ---- */
/* A82: the Z30 v44 port (WonderLantern/wonder-lantern-z30 docs/aspect-raw-plane-v44.md, issue #38): NEF files
 * keep the stock FX RAW plane (6040 x 4032) so Adobe can read them; 4:3 / 2.33:1 cropping happens only in
 * processing (the module's tables: stock FX internal sizes, offset 0, the crop in the trim row), and an Adobe
 * crs: crop is appended to the camera's XMP text after its closing </rdf:Description>. Built from A81.
 *
 * A81 on camera (DIAG17): Lightroom rejects the custom NEF planes (6040 x 2592); NEF-only playback garbled at the
 * bottom, soft, no zoom: hsel328 handed the file record's main size (the JPEG size, written at 0x102B0E62..98
 * before the remap) to the RAW decoder instead of the plane size. With the stock plane the stock path decodes the
 * plane itself, so hsel328 is removed.
 *
 * Parameter block additions (module, A82): +32 descriptor override: 3 sizes x 10 halfwords = fields 2..11 of the
 * area descriptor (window, offset, centre, crop box) in stock-plane coordinates, written after the A75 rebuild when
 * +32 is not 0. The Adobe crs: crop is written by the module through the release's NEF crd gate (kind 18,
 * 0x101BCFA6 already hooked by the release firmware), not here. camera_verified: false.
 *
 * ---- A81 header ---- */
/* A81: one crop path for every photo aspect (user 2026-11-24): the camera stays in FX; 4:3 and 2.33:1 both use the
 * crop area 8 (stock 1:1's path) with the module's table copies. Built from A78 (camera-proven for 4:3: shot, instant
 * review, playback, zoom); history of each hook in a68 .. a78_fx43.S and research/aspect/README.md.
 *
 * Parameter block (written by the module, copy base = area 8 group-1 pointer - 32, block at copy + 0x200):
 *   +0  'RATP' (0x50544152)   +4  area offset (x, y)   +8  configuration trim row, 3 x (x, y)
 *   +20 preview kind (u8)      +21 ratio (u8: 1 = 4:3, 2 = 2.33:1)
 * Without the block (an older module) everything is 4:3 as in A78.
 *   4:3:    offset (332, 0), trim (332, 0) x 3, kind 3 (stock 4:3 previews 640 x 480 / 1440 x 1080)
 *   2.33:1: offset (0, 720), trim (0, 0) x 3, kind 2 (the unused 5:4 kind; its preview table entries are changed to
 *           640 x 272 / 1920 x 824 by data edits in the builder)
 * Hooks:
 *   cfg     0x10142A7C  shot's first configuration call (caller 0x10143229), FX, photo branch, enabled: area 8, kind
 *                       from the block; then the area descriptor is rebuilt (A75); log keeps the call and remap
 *                       counters (copy + 224 / + 228); the per-call ring of A68 is dropped for room
 *   offset  0x1020933E  area 8 and enabled: the block's offset
 *   trim    0x10143032  area 8 and enabled: the block's trim row
 *   hpvw    0x1034E5E4  playback preview sizes from the file's own main size: 4:3 shapes 640 x 480 / 1440 x 1080,
 *                       2.33 shapes 640 x 272 / 1920 x 824 (checked playback callers only)
 *   hplay   0x1034F6D4  playback decode area 8 when enabled and the file's shape belongs to the current ratio
 * camera_verified: false. */
    .syntax unified
    .thumb

    .equ PTR_G1_A8, 0xC18C7598
    .equ RAM_LO,    0xC1800000
    .equ RAM_HI,    0xC8700000
    .equ PRM,       0x200 - 32              @ block offset from the group-1 pointer
    .equ MAGIC,     0x50544152

/* Z clear (ne) when enabled; uses \a and \b */
    .macro enabled a, b
    ldr     \a, =PTR_G1_A8
    ldr     \a, [\a]
    ldr     \b, =RAM_LO
    cmp     \a, \b
    blo     1001f
    ldr     \b, =RAM_HI
    cmp     \a, \b
    bhs     1001f
    movs    \a, #1
    b       1002f
1001:
    movs    \a, #0
1002:
    cmp     \a, #0
    .endm

/* r0 = parameter block when enabled and valid, else 0; uses r12 */
    .thumb_func
params:
    ldr     r0, =PTR_G1_A8
    ldr     r0, [r0]
    ldr     r12, =RAM_LO
    cmp     r0, r12
    blo     9f
    ldr     r12, =(RAM_HI - 0x400)
    cmp     r0, r12
    bhs     9f
    add.w   r0, r0, #PRM
    ldr     r12, [r0]
    push    {r1}
    ldr     r1, =MAGIC
    cmp     r12, r1
    pop     {r1}
    it      eq
    bxeq    lr
9:  movs    r0, #0
    bx      lr

    .global cfg
    .thumb_func
cfg:                                    @ r0 = obj, r1 = info, r2; entered by b.w from 0x10142A7C
    push    {r4, r5, r6, r7, r8, lr}
    push.w  {r0, r1, r2, r3, r12, lr}       @ A83: trace kind 19
    movs    r0, #19
    mov     r1, sp
    bl      vui_common
    pop.w   {r0, r1, r2, r3, r12, lr}
    mov     r4, r1
    enabled r5, r7
    beq     call
    ldr     r5, =PTR_G1_A8
    ldr     r5, [r5]
    adds    r5, #(224 - 32)             @ log after the seven 32-byte groups
    ldr     r7, [r5]
    adds    r7, #1
    str     r7, [r5]                    @ calls
    ldrb.w  r7, [r4, #0x200]
    cbnz    r7, call
    ldr.w   r7, [r4, #0xFC]
    cbnz    r7, call
    ldr     r7, =0x10143229             @ only the shot's first configuration call
    cmp     lr, r7
    bne     call
    movs    r7, #8
    strb.w  r7, [r4, #0x200]
    push    {r0, r1, r2, r3}
    bl      params
    movs    r7, #3                      @ no block: 4:3
    cbz     r0, 1f
    ldrb    r7, [r0, #20]
1:  strb.w  r7, [r4, #0x21E]
    ldr     r7, [r5, #4]
    adds    r7, #1
    str     r7, [r5, #4]                @ remapped calls
    add.w   r0, r4, #0x200
    addw    r1, r4, #0x202
    ldr     r7, =0x101DC3E9             @ area descriptor, now with area 8
    blx     r7
    bl      params                      @ A82: descriptor fields 2..11 in stock-plane coordinates
    cbz     r0, 2f
    ldrh    r1, [r0, #32]
    cbz     r1, 2f
    ldrb.w  r1, [r4, #0x222]            @ size the descriptor used
    cmp     r1, #2
    bhi     2f
    movs    r2, #20
    mla     r0, r1, r2, r0
    adds    r0, #32
    addw    r1, r4, #0x206
    movs    r2, #0
3:  ldrh    r3, [r0, r2]
    strh    r3, [r1, r2]
    adds    r2, #2
    cmp     r2, #20
    bne     3b
2:  pop     {r0, r1, r2, r3}
call:
    bl      orig
    pop     {r4, r5, r6, r7, r8, pc}

    .thumb_func
orig:                                   @ the replaced first instruction, then the rest of 0x10142A7C
    push.w  {r3, r4, r5, r6, r7, r8, r9, r10, r11, lr}
    ldr     pc, =0x10142A81

    .global offset
    .thumb_func
offset:                                 @ r0 = area; returns r0 = pair pointer, r1 = table (as stock)
    ldr     r1, =0x1167B7F4
    cmp     r0, #8
    bne     1f
    push    {r2, r3, lr}
    enabled r2, r3
    beq     2f
    bl      params
    cbz     r0, 3f
    adds    r0, #4
    ldr     r1, =0x1167B7F4
    pop     {r2, r3, pc}
3:  ldr     r0, =off43
    ldr     r1, =0x1167B7F4
    pop     {r2, r3, pc}
2:  pop     {r2, r3, lr}
    movs    r0, #8
1:  add.w   r0, r1, r0, lsl #2
    bx      lr

    .global trim
    .thumb_func
trim:                                   @ r5 = capture info; stock: r0 = size, r1 = row, r2 = table, then 0x10143044
    ldrb.w  r1, [r5, #0x200]
    ldr     r2, =0x11648258
    ldrb.w  r0, [r5, #0x221]
    add.w   r1, r1, r1, lsl #1
    add.w   r1, r2, r1, lsl #2
    bl      row8
    ldr     pc, =0x10143045

/* A84: r1 = the stock trim row; area 8 and enabled: r1 = the block's trim row (no block: trim43). Keeps r0, r2,
 * r3 (and r12 unless enabled; the configuration function holds nothing in r12 / lr there). */
    .thumb_func
row8:
    push    {r0, r2, r3, lr}
    ldrb.w  r0, [r5, #0x200]
    cmp     r0, #8
    bne     8f
    enabled r2, r3
    beq     8f
    bl      params
    ldr     r1, =trim43
    cbz     r0, 8f
    add.w   r1, r0, #8
8:  pop     {r0, r2, r3, pc}

    .ltorg
    .balign 4
off43:  .hword 332, 0
trim43: .hword 332, 0, 332, 0, 332, 0

/* ---- playback (A77 / A78, generalised) ---- */

/* r1 = width, r2 = height -> r0 = class (0 none, 1 4:3, 2 2.33); uses r3, r12 */
    .thumb_func
shape:
    ldr     r3, =shapes
    orr     r12, r1, r2, lsl #16
1:  ldr     r0, [r3], #8
    cbz     r0, 2f
    cmp     r0, r12
    bne     1b
    ldr     r0, [r3, #-4]
2:  bx      lr

/* caller's r4 = playback file context; r0 = class of its record's main size; uses r1, r2, r3, r12 */
    .thumb_func
fileclass:
    push    {lr}
    ldr     r3, [r4, #12]
    ldr     r1, =RAM_LO
    cmp     r3, r1
    blo     8f
    ldr     r1, =(RAM_HI - 0x400)
    cmp     r3, r1
    bhs     8f
    ldrh.w  r1, [r3, #0x398]
    ldrh.w  r2, [r3, #0x39A]
    bl      shape
    pop     {pc}
8:  movs    r0, #0
    pop     {pc}

    .global hpvw
    .thumb_func
hpvw:                                       @ 0x1034E5E4: r0 = out (w, h), r1 = class, r2 = small (!= 0)
    ldr     r12, =0x1034E9CB
    cmp     lr, r12
    beq     1f
    ldr     r12, =0x10354FAD
    cmp     lr, r12
    beq     1f
    ldr     r12, =0x10354FE5
    cmp     lr, r12
    bne     9f
1:  push    {r0, r1, r2, r3, lr}
    bl      fileclass
    cbz     r0, 8f
    ldr     r1, [sp, #8]                    @ small?
    subs    r0, #1
    lsls    r0, r0, #3
    cbz     r1, 2f
    adds    r0, #4
2:  ldr     r3, =pvsize
    ldr     r1, [r3, r0]                    @ (w | h << 16)
    ldr     r0, [sp]
    str     r1, [r0]
    pop     {r0, r1, r2, r3, pc}
8:  pop     {r0, r1, r2, r3, lr}
9:  push    {r4, r5, r6, lr}                @ the replaced push; cbz r2 relocated
    cmp     r2, #0
    beq     5f
    ldr     pc, =0x1034E5E9
5:  ldr     pc, =0x1034E625

    .global hplay
    .thumb_func
hplay:                                      @ 0x1034F6D4: strb.w r0, [r4, #0x200], then 0x1034F6D8
    push    {r0, r1, r2, r3, lr}
    enabled r1, r2
    beq     8f
    ldr     r3, [sp, #20]                   @ caller's [sp]: the file record
    ldr     r1, =RAM_LO
    cmp     r3, r1
    blo     8f
    ldr     r1, =(RAM_HI - 0x40)
    cmp     r3, r1
    bhs     8f
    ldrsh.w r1, [r3, #12]
    ldrsh.w r2, [r3, #14]
    bl      shape
    cbz     r0, 8f
    mov     r2, r0                          @ file class
    bl      params
    movs    r1, #1                          @ no block: 4:3
    cbz     r0, 1f
    ldrb    r1, [r0, #21]
1:  cmp     r1, r2
    bne     8f
    movs    r0, #8
    str     r0, [sp]
8:  pop     {r0, r1, r2, r3, lr}
    strb.w  r0, [r4, #0x200]
    ldr     pc, =0x1034F6D9

/* ---- A83 trace: vui_common entry of the release (returns at once without the module) ---- */
    .equ vui_common, 0x121207A8

/* ---- A84: the second trim table ---- */
    .global trim2
    .thumb_func
trim2:                                      @ 0x10142F6C; stock: r0 = area * 3, r1 = 0x116481EC + area * 12, then
    ldrb.w  r0, [r5, #0x200]                @ 0x10142F7A adds r9 * 4
    ldr     r1, =0x116481EC
    add.w   r0, r0, r0, lsl #1
    add.w   r1, r1, r0, lsl #2
    cmp.w   r9, #2                          @ the block row has three columns
    it      ls
    blls    row8
    ldr     pc, =0x10142F7B

    .ltorg
    .balign 4
shapes:                                    @ (w | h << 16), class
    .hword 5352, 4016
    .word  1
    .hword 4008, 3008
    .word  1
    .hword 2664, 2000
    .word  1
    .hword 6016, 2576
    .word  2
    .hword 4512, 1936
    .word  2
    .hword 3008, 1288
    .word  2
    .word  0, 0
pvsize:                                     @ per class: large, small
    .hword 1440, 1080, 640, 480
    .hword 1920, 824, 640, 272
