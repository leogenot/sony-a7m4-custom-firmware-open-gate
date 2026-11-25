/* [DCI] XAVC S-I DCI 4K in File Format, and its Rec Frame Rate item (methods/13-dci-4k/RESEARCH.md). */
#ifndef OG_DCI_H
#define OG_DCI_H

#include <stdint.h>

#define OG_DCI_FORMAT_OPTION 141u  /* File Format: XAVC S-I DCI 4K (row 0x4007af0, value 12) */

/* 1 when an option id is one this build lets through: DCI in File Format, DCI 30p / 25p / 24p (and the 29.97p /
 * 23.98p labels). 60p, 50p and 24.00p stay hidden. */
int og_dci_option(uint32_t option);

/* 0x1c18004, an option id resolved for the menu: the DCI options resolve to themselves. */
uint32_t og_dci_resolve_option(uint32_t option);

/* [PROBE-dci] Vtable wrappers for Sony's per-option checks (layout.json "vtable_hooks"): yes for the DCI row, Sony's
 * answer otherwise; og_dci_shown (slot 0x1a8) also logs what each check answers for 139 and 141, once. */
uint32_t og_dci_shown(void *object, uint32_t alias);
uint32_t og_dci_slot_1b0(void *object, uint32_t alias);
uint32_t og_dci_slot_1f0(void *object, uint32_t alias);
uint32_t og_dci_slot_210(void *object, uint32_t alias);
uint32_t og_dci_slot_6f8(void *object, uint32_t alias);
uint32_t og_dci_slot_700(void *object, uint32_t alias);

#endif
