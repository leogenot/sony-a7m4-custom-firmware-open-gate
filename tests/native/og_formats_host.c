/* [FORMATS] Host harness for native/open-gate/og_formats.c against fake Sony callbacks, og.h and og_mem.
 *
 *   og_formats_host
 *
 * Runs every check and prints "ok <check>" for each; any failure prints "FAIL <check>: ..." and exits 1.
 */
#include <stdint.h>
#include <stdio.h>
#include "og.h"
#include "og_internal.h"
#include "og_formats.h"

static int failures;
#define CHECK(name, cond)                                                                      \
    do {                                                                                       \
        if (cond) printf("ok %s\n", name);                                                     \
        else { printf("FAIL %s: %s (line %d)\n", name, #cond, __LINE__); failures++; }         \
    } while (0)

/* --- fakes ------------------------------------------------------------------------------------------------------ */
static uint32_t format = 6, set_raw, committed, rate_committed, commits, rate_commits;
static og_mode mode;
static og_status select_status;
static int pal, mem_fails, selects;
static og_mode last_select;

uint32_t og_formats_previous_getter(void) { return format; }
void og_formats_previous_setter(uint32_t raw) { set_raw = raw; }
uint32_t og_formats_previous_commit(uint32_t option) { commits++; committed = option; return 0x77; }
uint32_t og_formats_rate_commit(uint32_t option) { rate_commits++; rate_committed = option; return 0x66; }
uint32_t og_formats_previous_resolve_option(uint32_t option) { return option == 999 ? 999 : 0x1234; }
og_mode og_selected(void) { return mode; }
og_status og_select(og_mode m)
{
    selects++;
    last_select = m;
    if (select_status == OG_OK) mode = m;
    return select_status;
}
static og_mem *fake = (og_mem *)&fake;
og_status og_mem_begin(og_mem **out, og_family *family)
{
    if (mem_fails) return OG_NOT_READY;
    *out = fake;
    *family = pal ? OG_FAMILY_PAL : OG_FAMILY_NTSC;
    return OG_OK;
}
void og_mem_end(og_mem *m) { (void)m; }

static void reset(void) { committed = rate_committed = commits = rate_commits = 0; selects = 0; select_status = OG_OK; }

int main(void)
{
    /* getter */
    mode = OG_OFF, format = 6;
    CHECK("getter: Sony's format while open gate is off", og_formats_getter() == 6);
    mode = OG_4K_32;
    CHECK("getter: the 4K row on HS 4K with 4K 3:2 on", og_formats_getter() == OG_FORMATS_VALUE_4K);
    format = 7, mode = OG_57K_32;
    CHECK("getter: the 5.7K row on S 4K with 5.7K 3:2 on", og_formats_getter() == OG_FORMATS_VALUE_57K);
    format = 9;
    CHECK("getter: Sony's format on another format", og_formats_getter() == 9);

    /* setter */
    pal = 0;
    og_formats_setter(OG_FORMATS_VALUE_4K);
    CHECK("setter: a row browses HS 4K on NTSC", set_raw == 6);
    pal = 1;
    og_formats_setter(OG_FORMATS_VALUE_57K);
    CHECK("setter: a row browses S 4K on PAL", set_raw == 7);
    og_formats_setter(10);
    CHECK("setter: other values unchanged", set_raw == 10);

    /* commit: a row */
    reset(), mode = OG_OFF, pal = 0;
    CHECK("commit: the 4K row on NTSC", og_formats_commit(OG_FORMATS_OPTION_4K) == 0x77 && last_select == OG_4K_32 &&
                                           committed == 136 && rate_committed == 151 && mode == OG_4K_32);
    reset(), pal = 1;
    CHECK("commit: the 5.7K row on PAL", og_formats_commit(OG_FORMATS_OPTION_57K) == 0x77 && last_select == OG_57K_32 &&
                                            committed == 137 && rate_committed == 161);
    reset(), mode = OG_OFF, select_status = OG_FOREIGN;
    CHECK("commit: a failed apply commits nothing", og_formats_commit(OG_FORMATS_OPTION_4K) == 0 && commits == 0 &&
                                                       rate_commits == 0 && mode == OG_OFF);

    /* commit: another option */
    reset(), mode = OG_4K_32;
    CHECK("commit: another format turns open gate off", og_formats_commit(139) == 0x77 && committed == 139 &&
                                                           rate_commits == 0 && last_select == OG_OFF && mode == OG_OFF);
    reset(), mode = OG_OFF;
    CHECK("commit: another format with open gate off selects nothing", og_formats_commit(138) == 0x77 && selects == 0);

    /* option answers */
    CHECK("option: the rows resolve to themselves",
          og_formats_option_answer(130, 0x15d8, 0) == 130 && og_formats_option_answer(131, 0x15d8, 1) == 131);
    CHECK("option: rates untouched while open gate is off",
          og_formats_option_answer(149, 149, 0) == 149 && og_formats_option_answer(158, 158, 0) == 158);
    int kept = 1, hidden = 1;
    for (uint32_t o = 149; o <= 169; o++) {
        uint32_t a = og_formats_option_answer(o, o, 1);
        if (o == 151 || o == 155 || o == 157 || o == 161 || o == 169) kept &= a == o;
        else hidden &= a == OG_FORMATS_HIDDEN;
    }
    CHECK("option: open gate keeps HS 4K 24p / 23.98p and S 4K 25p", kept);
    CHECK("option: open gate hides every other HS 4K and S 4K rate", hidden);
    CHECK("option: other options keep the chain's answer", og_formats_option_answer(999, 999, 1) == 999 &&
                                                              og_formats_option_answer(139, 7, 1) == 7);
    mode = OG_4K_32;
    CHECK("option: resolve uses the chain and the mode", og_formats_resolve_option(150) == OG_FORMATS_HIDDEN &&
                                                            og_formats_resolve_option(999) == 999);
    mem_fails = 1, pal = 1;
    og_formats_setter(OG_FORMATS_VALUE_4K);
    CHECK("no memory access: NTSC formats", set_raw == 6);
    return failures ? 1 : 0;
}
