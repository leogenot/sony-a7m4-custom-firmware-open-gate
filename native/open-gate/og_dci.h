/* [DCI] XAVC S-I DCI 4K in File Format, and its Rec Frame Rate item (methods/13-dci-4k/RESEARCH.md). */
#ifndef OG_DCI_H
#define OG_DCI_H

#include <stdint.h>

#define OG_DCI_ITEM 42u            /* the DCI Rec Frame Rate item (leaf 0x597ba50) */
#define OG_DCI_FORMAT_OPTION 141u  /* File Format: XAVC S-I DCI 4K (row 0x4007af0, value 12) */

/* 1 when an option id is one this build lets through: DCI in File Format, DCI 30p / 25p / 24p (and the 29.97p /
 * 23.98p labels). 60p, 50p and 24.00p stay hidden. */
int og_dci_option(uint32_t option);

/* 0x1c08bc0, "is this menu item supported?": item 42 is. */
uint32_t og_dci_item_supported(void *object, uint32_t item);

/* 0x1c18004, an option id resolved for the menu: the DCI options resolve to themselves. */
uint32_t og_dci_resolve_option(uint32_t option);

#endif
