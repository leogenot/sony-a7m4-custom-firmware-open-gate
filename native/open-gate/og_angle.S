/* [PROBE-angle] Entries for og_angle.c (layout.json hooks). The availability getter 0x293d634 is a leaf that only
 * writes x0, and appFw.so is built by GCC 6.4, whose -fipa-ra may let a caller keep values in scratch registers
 * across such a call: og_angle_gate_entry therefore keeps x1-x18 and NZCV. The two trace entries keep the argument
 * registers x0-x8, call the logger with the caller's return address, then go on to the original function. */
    .text
    .balign 4

    .global og_angle_gate_entry
    .type og_angle_gate_entry, %function
og_angle_gate_entry:
    stp x29, x30, [sp, #-0xb0]!
    mov x29, sp
    stp x1, x2, [sp, #0x10]
    stp x3, x4, [sp, #0x20]
    stp x5, x6, [sp, #0x30]
    stp x7, x8, [sp, #0x40]
    stp x9, x10, [sp, #0x50]
    stp x11, x12, [sp, #0x60]
    stp x13, x14, [sp, #0x70]
    stp x15, x16, [sp, #0x80]
    stp x17, x18, [sp, #0x90]
    mrs x9, nzcv
    str x9, [sp, #0xa0]
    bl og_angle_gate
    ldr x9, [sp, #0xa0]
    msr nzcv, x9
    ldp x17, x18, [sp, #0x90]
    ldp x15, x16, [sp, #0x80]
    ldp x13, x14, [sp, #0x70]
    ldp x11, x12, [sp, #0x60]
    ldp x9, x10, [sp, #0x50]
    ldp x7, x8, [sp, #0x40]
    ldp x5, x6, [sp, #0x30]
    ldp x3, x4, [sp, #0x20]
    ldp x1, x2, [sp, #0x10]
    ldp x29, x30, [sp], #0xb0
    ret
    .size og_angle_gate_entry, . - og_angle_gate_entry

/* log: the C logger; from: the register that gets the return address; next: the original function. */
    .macro TRACE_THEN name, log, from, next
    .global \name
    .type \name, %function
\name:
    stp x29, x30, [sp, #-0x60]!
    mov x29, sp
    stp x0, x1, [sp, #0x10]
    stp x2, x3, [sp, #0x20]
    stp x4, x5, [sp, #0x30]
    stp x6, x7, [sp, #0x40]
    str x8, [sp, #0x50]
    mov \from, x30
    bl \log
    ldr x8, [sp, #0x50]
    ldp x6, x7, [sp, #0x40]
    ldp x4, x5, [sp, #0x30]
    ldp x2, x3, [sp, #0x20]
    ldp x0, x1, [sp, #0x10]
    ldp x29, x30, [sp], #0x60
    b \next
    .size \name, . - \name
    .endm

    TRACE_THEN og_angle_drive_entry, og_angle_log_drive, x2, og_angle_previous_drive
    TRACE_THEN og_angle_refused_entry, og_angle_log_refused, x1, og_angle_previous_refused
