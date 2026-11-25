/* [FORMATS] Open gate as File Format entries (methods/11-open-gate/FILE-FORMATS.md).
 *
 * MENU > Shooting > Image Quality > File Format gains "4K 3:2 Open Gate" and "5.7K 3:2 Open Gate": two of its hidden
 * legacy rows (options 130, 131), copied from the XAVC HS 4K row with their own texts and values. A File Format
 * option stores a real Sony format, so the rows stand for "the camera's 4K format, with open gate on": XAVC HS 4K at
 * 24p on NTSC, XAVC S 4K at 25p on PAL (the formats open gate is made for). This module takes the item's getter,
 * setter and commit:
 *
 *   - getter: one of the rows while open gate is on and the format is HS or S 4K; else Sony's value.
 *   - setter: browsing a row passes Sony the matching 4K format.
 *   - commit: a row applies open gate (on movie, behind the crash guard, og.c), then commits the 4K format and its
 *     frame rate through Sony's own commits; any other option commits as usual and turns open gate Off.
 *
 * The option lookup (0x1c18004, before Erlla's chain) resolves the two rows to themselves, and while open gate is on
 * hides the Rec Frame Rate options it doesn't record (HS 4K: all but 24p; S 4K: all but 25p).
 */
#include <stdint.h>
#include "og.h"
#include "og_internal.h"
#include "og_formats.h"
#include "og_diag.h"

#define HIDDEN __attribute__((visibility("hidden")))

extern uint32_t og_formats_previous_getter(void) HIDDEN;             /* 0x1c34968 */
extern void og_formats_previous_setter(uint32_t raw) HIDDEN;         /* 0x1ca0f58 */
extern uint32_t og_formats_previous_commit(uint32_t option) HIDDEN;  /* 0x1cd2f18 */
extern uint32_t og_formats_rate_commit(uint32_t option) HIDDEN;      /* 0x1cd7190, every Rec Frame Rate item's commit */
extern uint32_t og_formats_previous_resolve_option(uint32_t option) HIDDEN;   /* Erlla's chain, 0x8fa1000 */

enum { FORMAT_XAVC_HS_4K = 6, FORMAT_XAVC_S_4K = 7 };
enum { OPTION_XAVC_HS_4K = 136, OPTION_XAVC_S_4K = 137 };   /* File Format */
enum { OPTION_HS_4K_24P = 151, OPTION_S_4K_25P = 161 };      /* Rec Frame Rate, HS 4K's item and S 4K's */

/* Rec Frame Rate options open gate records: HS 4K's 24p and 23.98p label, S 4K's 25p; '-' rows (157, 169) kept. */
static int rate_kept(uint32_t option)
{
    if (option >= 149 && option <= 157) return option == 151 || option == 155 || option == 157;
    if (option >= 158 && option <= 169) return option == 161 || option == 169;
    return 1;
}

uint32_t og_formats_option_answer(uint32_t option, uint32_t stock, int open_gate_on)
{
    if (option == OG_FORMATS_OPTION_4K || option == OG_FORMATS_OPTION_57K) return option;
    if (open_gate_on && !rate_kept(option)) return OG_FORMATS_HIDDEN;
    return stock;
}

uint32_t og_formats_resolve_option(uint32_t option)
{
    return og_formats_option_answer(option, og_formats_previous_resolve_option(option), og_selected() != OG_OFF);
}

/* PAL cameras record open gate in XAVC S 4K 25p, NTSC ones in XAVC HS 4K 24p. */
static int on_pal(void)
{
    og_mem *m;
    og_family family = OG_FAMILY_NTSC;
    if (og_mem_begin(&m, &family) != OG_OK) return 0;
    og_mem_end(m);
    return family == OG_FAMILY_PAL;
}

uint32_t og_formats_getter(void)
{
    uint32_t format = og_formats_previous_getter();
    og_mode mode = og_selected();
    if (mode == OG_OFF || (format != FORMAT_XAVC_HS_4K && format != FORMAT_XAVC_S_4K)) return format;
    return mode == OG_4K_32 ? OG_FORMATS_VALUE_4K : OG_FORMATS_VALUE_57K;
}

void og_formats_setter(uint32_t raw)
{
    if (raw == OG_FORMATS_VALUE_4K || raw == OG_FORMATS_VALUE_57K) raw = on_pal() ? FORMAT_XAVC_S_4K : FORMAT_XAVC_HS_4K;
    og_formats_previous_setter(raw);
}

uint32_t og_formats_commit(uint32_t option)
{
    if (option != OG_FORMATS_OPTION_4K && option != OG_FORMATS_OPTION_57K) {
        uint32_t r = og_formats_previous_commit(option);
        if (og_selected() != OG_OFF) og_diag("formats.off", option, og_select(OG_OFF), 0, 0);   /* [DEBUG-og2] */
        return r;
    }
    int pal = on_pal();
    og_status s = og_select(option == OG_FORMATS_OPTION_4K ? OG_4K_32 : OG_57K_32);
    og_diag("formats.select", option, s, pal, 0);   /* [DEBUG-og2] */
    if (s != OG_OK) return 0;
    uint32_t r = og_formats_previous_commit(pal ? OPTION_XAVC_S_4K : OPTION_XAVC_HS_4K);
    uint32_t rate = og_formats_rate_commit(pal ? OPTION_S_4K_25P : OPTION_HS_4K_24P);
    og_diag("formats.commit", option, r, rate, 0);   /* [DEBUG-og2] */
    og_diag_sync();
    return r;
}
