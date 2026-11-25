/* [DCI] XAVC S-I DCI 4K in MENU > Shooting > Image Quality > File Format, and its own Rec Frame Rate item in Movie
 * Settings (methods/13-dci-4k/RESEARCH.md).
 *
 * Everything DCI records with is in the A7 IV's files (file format 12, its option row, the DCI frame-rate item 42,
 * size class 10, encoder and MP4 entries). The menu hides it: option ids go through 0x1c18004 and item ids through
 * 0x1c08bc0 and 0x1c1116c, which look them up in tables registered at start-up, where the A7 IV registers no DCI.
 * Erlla's custom firmware already chains those three to its own pages' ids; this module goes first and answers for
 * the DCI ids the way Erlla's code answers for its own: the item is supported and every id resolves to itself.
 * Everything else, including which frame rates NTSC or PAL shows, is still Sony's decision.
 *
 * The step log records, once per id, what Sony's chain answered and what this module returned, for the DCI ids and
 * their visible neighbours (S-I 4K and S-I HD), so a camera run shows what a visible option resolves to.
 */
#include <stdint.h>
#include "og_dci.h"
#include "og_diag.h"

#define HIDDEN __attribute__((visibility("hidden")))

extern uint32_t og_dci_previous_item_supported(void *object, uint32_t item) HIDDEN;   /* Erlla's chain, 0x8fb90c8 */
extern uint32_t og_dci_previous_resolve_option(uint32_t option) HIDDEN;              /* Erlla's chain, 0x8fa1000 */
extern const volatile uint32_t *dci_condition(void);   /* stock 0x2922784: setting 0x2530 (Sony: DCI row needs != 0) */

/* DCI options let through: File Format's DCI row; Rec Frame Rate 30p, 25p, 24p and the 29.97p, 23.98p labels. */
static const uint16_t options[] = {OG_DCI_FORMAT_OPTION, 202, 203, 204, 206, 207};

int og_dci_option(uint32_t option)
{
    for (unsigned i = 0; i < sizeof options / sizeof *options; i++)
        if (option == options[i]) return 1;
    return 0;
}

/* Traced once each: items 34 (File Format) and 39-42 (Rec Frame Rate, S-I 4K .. DCI); options 139-141 (File
 * Format S-I 4K, S-I HD, DCI) and 182-209 (Rec Frame Rate, S-I 4K .. DCI). Bit = id - the range's first id. */
static uint64_t traced_items, traced_formats, traced_rates;

/* The value of setting 0x2530, which Sony's own check of the DCI row wants nonzero (the build makes that row always
 * available instead); 9 if the getter returns nothing. */
static uint64_t condition(void)
{
    const volatile uint32_t *value = dci_condition();
    return value ? *value : 9;
}

static int first(uint64_t *mask, uint32_t id, uint32_t lo, uint32_t hi)
{
    if (id < lo || id > hi) return 0;
    uint64_t bit = 1ull << (id - lo);
    return !(__atomic_fetch_or(mask, bit, __ATOMIC_RELAXED) & bit);
}

uint32_t og_dci_item_supported(void *object, uint32_t item)
{
    uint32_t stock = og_dci_previous_item_supported(object, item);
    uint32_t answer = item == OG_DCI_ITEM ? 1u : stock;
    if (first(&traced_items, item, 34, OG_DCI_ITEM) && (item == 34 || item >= 39))
        og_diag("dci.item", item, stock, answer, 0);   /* [DEBUG-og2] */
    return answer;
}

uint32_t og_dci_resolve_option(uint32_t option)
{
    uint32_t stock = og_dci_previous_resolve_option(option);
    uint32_t answer = og_dci_option(option) ? option : stock;
    if (first(&traced_formats, option, 139, OG_DCI_FORMAT_OPTION) || first(&traced_rates, option, 182, 209))
        og_diag("dci.option", option, stock, answer, condition());   /* [DEBUG-og2] */
    return answer;
}
