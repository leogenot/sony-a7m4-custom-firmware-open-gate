/* [DCI] XAVC S-I DCI 4K in MENU > Shooting > Image Quality > File Format, and its own Rec Frame Rate item in Movie
 * Settings (methods/13-dci-4k/RESEARCH.md).
 *
 * Everything DCI records with is in the A7 IV's files (file format 12, its option row, the DCI frame-rate item 42,
 * size class 10, encoder and MP4 entries). The menu hides it with a per-product flag in the rows and the item's leaf,
 * off for the A7 IV, which the build switches on (layout.json "menu_edits"). Option ids also go through 0x1c18004
 * and item ids through 0x1c1116c, which Erlla's custom firmware chains to its own pages' ids; this module goes first
 * and answers for the DCI ids the way Erlla's code answers for its own: every id maps or resolves to itself.
 * Everything else, including which frame rates NTSC or PAL shows, is still Sony's decision.
 *
 * The step log records, once per id, what Sony's chain answered and what this module returned, for the DCI ids and
 * their visible neighbours (S-I 4K and S-I HD), so a camera run shows what a visible option resolves to.
 */
#include <stdint.h>
#include "og_dci.h"
#include "og_diag.h"

#define HIDDEN __attribute__((visibility("hidden")))

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

/* Traced once each: options 139-141 (File Format S-I 4K, S-I HD, DCI) and 182-209 (Rec Frame Rate, S-I 4K .. DCI).
 * Bit = id - the range's first id. */
static uint64_t traced_formats, traced_rates;

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

uint32_t og_dci_resolve_option(uint32_t option)
{
    uint32_t stock = og_dci_previous_resolve_option(option);
    uint32_t answer = og_dci_option(option) ? option : stock;
    if (first(&traced_formats, option, 139, OG_DCI_FORMAT_OPTION) || first(&traced_rates, option, 182, 209))
        og_diag("dci.option", option, stock, answer, condition());   /* [DEBUG-og2] */
    return answer;
}

/* [PROBE-dci] Camera run of alpha-dci-0.3: DCI listed in File Format, its row and item flags on, but greyed out.
 * Sony's menu object answers per-option questions through its vtable (base 0x58f7a68, a second copy at 0x58f8618;
 * layout.json "vtable_hooks" points their entries here). For the DCI row each wrapper answers yes; for every other
 * option it is Sony's own function. The first time the row builder asks about S-I 4K (139, selectable) and DCI (141),
 * og_dci_checks logs what Sony's own functions answer for each, so the log shows which one says no for DCI.
 * Slots 0x1f8 and 0x200 look the id up in another table (141 is Camera Set Memory 1 there): probed, not overridden. */
typedef uint32_t check(void *object, uint32_t alias);
extern check og_dci_previous_shown HIDDEN, og_dci_previous_slot_1b0 HIDDEN, og_dci_previous_slot_1f0 HIDDEN,
    og_dci_previous_slot_1f8 HIDDEN, og_dci_previous_slot_200 HIDDEN, og_dci_previous_slot_210 HIDDEN,
    og_dci_previous_slot_6f8 HIDDEN, og_dci_previous_slot_700 HIDDEN;

static check *const probed[] = {og_dci_previous_shown,    og_dci_previous_slot_1b0, og_dci_previous_slot_1f0,
                                og_dci_previous_slot_1f8, og_dci_previous_slot_200, og_dci_previous_slot_210,
                                og_dci_previous_slot_6f8, og_dci_previous_slot_700};
static volatile uint32_t probing;   /* nonzero while og_dci_checks runs: the wrappers pass Sony's answer through */
static uint32_t probed_aliases;    /* bit 0: 139 logged; bit 1: 141 logged */

static uint32_t forced(uint32_t alias)
{
    return alias == OG_DCI_FORMAT_OPTION && !probing;
}

/* Sony's checks return a C++ bool: only the low byte of w0 is defined. */
static uint32_t yes(uint32_t answer)
{
    return (answer & 0xff) != 0;
}

/* Bit i = probed[i]'s answer (0x1a8, 0x1b0, 0x1f0, 0x1f8, 0x200, 0x210, 0x6f8, 0x700). */
static void og_dci_checks(void *object, uint32_t alias)
{
    uint32_t bit;
    if (alias == 139) bit = 1;
    else if (alias == OG_DCI_FORMAT_OPTION) bit = 2;
    else return;
    if (__atomic_fetch_or(&probed_aliases, bit, __ATOMIC_RELAXED) & bit) return;
    uint64_t answers = 0;
    probing = 1;
    for (unsigned i = 0; i < sizeof probed / sizeof *probed; i++)
        answers |= (uint64_t)yes(probed[i](object, alias)) << i;
    probing = 0;
    og_diag("dci.checks", alias, answers, *(uint64_t *)object, 0);   /* [DEBUG-og2] the object's vtable pointer */
}

uint32_t og_dci_shown(void *object, uint32_t alias)
{
    og_dci_checks(object, alias);
    return forced(alias) || yes(og_dci_previous_shown(object, alias));
}

#define OVERRIDE(slot)                                                                    \
    uint32_t og_dci_slot_##slot(void *object, uint32_t alias)                             \
    {                                                                                     \
        return forced(alias) || yes(og_dci_previous_slot_##slot(object, alias));          \
    }
OVERRIDE(1b0)
OVERRIDE(1f0)
OVERRIDE(210)
OVERRIDE(6f8)
OVERRIDE(700)
