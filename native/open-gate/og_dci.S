/* [DCI] 0x1c1116c maps a menu item id through a start-up table. It is a leaf that only writes x0-x4, and appFw.so is
 * built by GCC 6.4 (-fipa-ra), so its caller may keep values in the other scratch registers: this entry writes
 * nothing but x0 and the flags (as Erlla's own entry at 0x8fb90e0, which it goes before). Item 42 maps to itself. */
    .text
    .balign 4

    .global og_dci_map_item
    .type og_dci_map_item, %function
og_dci_map_item:
    cmp w1, #42
    b.ne og_dci_previous_map_item
    mov w0, w1
    ret
    .size og_dci_map_item, . - og_dci_map_item
