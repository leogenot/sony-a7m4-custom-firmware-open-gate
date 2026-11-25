/* [FORMATS] Open gate as File Format entries: "4K 3:2 Open Gate" and "5.7K 3:2 Open Gate"
 * (methods/11-open-gate/FILE-FORMATS.md). */
#ifndef OG_FORMATS_H
#define OG_FORMATS_H

#include <stdint.h>

#define OG_FORMATS_OPTION_4K 130u    /* reused File Format row 0x4007570 */
#define OG_FORMATS_OPTION_57K 131u   /* reused File Format row 0x40075f0 */
#define OG_FORMATS_VALUE_4K 0x40u    /* the rows' own values: unique in File Format */
#define OG_FORMATS_VALUE_57K 0x41u
#define OG_FORMATS_HIDDEN 0x15d8u    /* what the option lookup answers for an option it doesn't know */

/* The File Format item's callbacks (its leaf's +0x78, +0x80, +0x88 point here). */
uint32_t og_formats_getter(void);
void og_formats_setter(uint32_t raw);
uint32_t og_formats_commit(uint32_t option);

/* 0x1c18004: an option id resolved for the menu. The two rows resolve to themselves; while open gate is on, the
 * Rec Frame Rate options open gate doesn't record are hidden. */
uint32_t og_formats_resolve_option(uint32_t option);

/* The option's answer, given what Sony's chain answered and the open-gate mode. */
uint32_t og_formats_option_answer(uint32_t option, uint32_t stock, int open_gate_on);

#endif
