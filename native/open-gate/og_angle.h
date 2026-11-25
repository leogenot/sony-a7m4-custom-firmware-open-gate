/* [PROBE-angle] Shutter angle on the dial: test build only (methods/12-shutter-angle/RESEARCH.md).
 * The angle-availability getter's 14 callers, the angle drive's 9 callers and the dial's "unavailable" call are
 * pointed at og_angle.S (layout.json hooks), which calls these with every scratch register kept. */
#ifndef OG_ANGLE_H
#define OG_ANGLE_H

#include <stdint.h>

/* For 0x293d634's callers: the byte they read, 0 = angle available. */
const volatile uint8_t *og_angle_gate(void);

/* Trace only: the angle drive (0x26e8b08) with its step, and the dial's "unavailable" answer (0x26e898c);
 * from = the caller's return address. */
void og_angle_log_drive(void *context, int32_t step, uint64_t from);
void og_angle_log_refused(void *context, uint64_t from);

#endif
