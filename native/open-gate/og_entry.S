/* Shooting-screen hooks. appFw.so's BLs at the two shooting-screen creation sites (layout.json "hooks") call these
 * instead of the custom aspect feature's registration wrappers. Each calls that wrapper first, unchanged (it runs
 * the native creation and registers the aspect guide), then og_power_on with x0-x18 and NZCV preserved. */
    .text
    .balign 4

    .global og_hook_first
    .type og_hook_first, %function
og_hook_first:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    bl hook_first_previous
    bl og_after_hook
    ldp x29, x30, [sp], #16
    ret
    .size og_hook_first, . - og_hook_first

    .global og_hook_second
    .type og_hook_second, %function
og_hook_second:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    bl hook_second_previous
    bl og_after_hook
    ldp x29, x30, [sp], #16
    ret
    .size og_hook_second, . - og_hook_second

    .type og_after_hook, %function
og_after_hook:
    stp x29, x30, [sp, #-0xb0]!
    mov x29, sp
    stp x0, x1, [sp, #0x10]
    stp x2, x3, [sp, #0x20]
    stp x4, x5, [sp, #0x30]
    stp x6, x7, [sp, #0x40]
    stp x8, x9, [sp, #0x50]
    stp x10, x11, [sp, #0x60]
    stp x12, x13, [sp, #0x70]
    stp x14, x15, [sp, #0x80]
    stp x16, x17, [sp, #0x90]
    mrs x9, nzcv
    stp x18, x9, [sp, #0xa0]
    bl og_power_on
    ldp x18, x9, [sp, #0xa0]
    msr nzcv, x9
    ldp x16, x17, [sp, #0x90]
    ldp x14, x15, [sp, #0x80]
    ldp x12, x13, [sp, #0x70]
    ldp x10, x11, [sp, #0x60]
    ldp x8, x9, [sp, #0x50]
    ldp x6, x7, [sp, #0x40]
    ldp x4, x5, [sp, #0x30]
    ldp x2, x3, [sp, #0x20]
    ldp x0, x1, [sp, #0x10]
    ldp x29, x30, [sp], #0xb0
    ret
    .size og_after_hook, . - og_after_hook
