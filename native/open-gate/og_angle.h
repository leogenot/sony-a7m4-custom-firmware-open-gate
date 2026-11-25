/* [PROBE-angle] Shutter angle on the dial: test build only (methods/12-shutter-angle/RESEARCH.md).
 * The angle-availability getter's 14 callers, the angle drive's 9 callers and the dial's "unavailable" call are
 * pointed at og_angle.S (layout.json hooks), which calls these with every scratch register kept. */
#ifndef OG_ANGLE_H
#define OG_ANGLE_H

#include <stdint.h>

/* [PROBE-angle] Sony's own Shutter Mode menu item, the FX3's, which the A7 IV menu lists under Exposure (group 0x10)
 * but hides: item 441, leaf 0x590ab98 ("Shutter Mode"), options 1543 "Speed" and 1544 "Angle". */
#define OG_ANGLE_ITEM 441u
#define OG_ANGLE_SPEED_OPTION 1543u
#define OG_ANGLE_ANGLE_OPTION 1544u

/* For 0x293d634's callers: the byte they read, 0 = angle available. */
const volatile uint8_t *og_angle_gate(void);

/* Trace only: the angle drive (0x26e8b08) with its step, and the dial's "unavailable" answer (0x26e898c);
 * from = the caller's return address. */
void og_angle_log_drive(void *context, int32_t step, uint64_t from);
void og_angle_log_refused(void *context, uint64_t from);

/* 0x1c08bc0, "is this menu item supported?": item 441 is. */
uint32_t og_angle_item_supported(void *object, uint32_t item);

/* 0x1c18004, an option id resolved for the menu: Speed and Angle resolve to themselves. */
uint32_t og_angle_resolve_option(uint32_t option);

#endif
