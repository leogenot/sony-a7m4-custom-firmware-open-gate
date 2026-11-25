/* Host harness for native/open-gate/og_menu.c against a fake og.h, dial getter and previous disabled check.
 *
 *   og_menu_host
 *
 * Runs every check and prints "ok <check>" for each; any failure prints "FAIL <check>: ..." and exits 1.
 */
#include <stdint.h>
#include <stdio.h>
#include "og.h"
#include "og_menu.h"
#include "og_config.h"

static int failures;
#define CHECK(name, cond)                                                                      \
    do {                                                                                       \
        if (cond) printf("ok %s\n", name);                                                     \
        else { printf("FAIL %s: %s (line %d)\n", name, #cond, __LINE__); failures++; }         \
    } while (0)

/* --- fake og.h ------------------------------------------------------------------------------------------------ */
static og_mode selected_mode;
static og_status select_status;
static int select_calls;
static og_mode select_last;

og_status og_select(og_mode mode)
{
    select_calls++;
    select_last = mode;
    if (select_status == OG_OK) selected_mode = mode;
    return select_status;
}

og_mode og_selected(void) { return selected_mode; }

/* --- fake dial getter and the previous head of the disabled chain ---------------------------------------------- */
static uint32_t dial;
static int dial_missing;
const volatile uint32_t *native_mode(void) { return dial_missing ? 0 : &dial; }

static int previous_calls;
static uint32_t previous_mid;
uint32_t og_menu_previous_disabled(uint32_t mid)
{
    previous_calls++;
    previous_mid = mid;
    return 0x5000 + mid;
}

int main(void)
{
    /* getter: og_selected() unless the setter staged a browsed value */
    for (og_mode m = OG_OFF; m < OG_MODE_COUNT; m++) {
        selected_mode = m;
        CHECK("getter reflects og_selected", og_menu_getter() == (uint32_t)m);
    }
    selected_mode = OG_OFF;
    og_menu_setter(OG_57K_32);
    CHECK("setter stages the browsed value", og_menu_getter() == OG_57K_32 && select_calls == 0);
    og_menu_setter(OG_MODE_COUNT);
    CHECK("setter ignores an unknown raw value", og_menu_getter() == OG_57K_32);

    /* commit: og_select with each option's mode; nonzero only for OG_OK */
    for (og_mode m = OG_OFF; m < OG_MODE_COUNT; m++) {
        select_status = OG_OK;
        selected_mode = m == OG_OFF ? OG_4K_32 : OG_OFF;
        og_menu_setter(m);
        int calls = select_calls;
        int r = og_menu_commit(OG_MENU_ALIAS_BASE + m);
        CHECK("commit selects the option's mode", r != 0 && select_calls == calls + 1 && select_last == m);
        CHECK("getter shows the committed mode", og_menu_getter() == (uint32_t)m);
    }
    for (og_status s = OG_BUSY; s <= OG_NOT_SAVED; s++) {
        select_status = s;
        selected_mode = OG_4K_32;
        og_menu_setter(OG_57K_32);
        int r = og_menu_commit(OG_MENU_ALIAS_BASE + OG_57K_32);
        CHECK("commit reports any other status as failure", r == 0 && select_last == OG_57K_32);
        CHECK("a failed commit drops the staged value", og_menu_getter() == OG_4K_32);
    }
    select_status = OG_OK;
    int calls = select_calls;
    CHECK("commit refuses an alias below the item's",
          og_menu_commit(OG_MENU_ALIAS_BASE - 1) == 0 && og_menu_commit(0) == 0 && select_calls == calls);
    CHECK("commit refuses an alias above the item's",
          og_menu_commit(OG_MENU_ALIAS_BASE + OG_MODE_COUNT) == 0 && og_menu_commit(0xffffffffu) == 0 &&
              select_calls == calls);

    /* disabled: 1 = the camera shows "currently disabled", so 1 in movie only; every other mid goes to the previous
     * check */
    for (uint32_t d = 0; d < 6; d++) {
        dial = d;
        CHECK("disabled is 1 exactly in movie", og_menu_disabled(OG_MENU_MID) == (d == OG_DIAL_MOVIE));
    }
    dial_missing = 1;
    CHECK("disabled is 0 without a dial", og_menu_disabled(OG_MENU_MID) == 0);
    dial_missing = 0;
    CHECK("the item never reaches the previous check", previous_calls == 0);
    dial = OG_DIAL_MOVIE;
    const uint32_t others[] = {0, 33, 34, 1592, 1594, 1598, 1599, OG_MENU_MID - 1, OG_MENU_MID + 1, 0xffffffffu};
    for (unsigned i = 0; i < sizeof others / sizeof *others; i++) {
        int before = previous_calls;
        CHECK("other mids chain to the previous check",
              og_menu_disabled(others[i]) == 0x5000 + others[i] && previous_calls == before + 1 && previous_mid == others[i]);
    }
    return failures ? 1 : 0;
}
