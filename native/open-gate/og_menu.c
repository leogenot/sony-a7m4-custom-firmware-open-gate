/* The Open Gate setting: Off / 4K 3:2 / 5.7K 3:2. Calls only og.h (og_select, og_selected) and the dial getter.
 *
 * The setting is the custom firmware's Custom Shutter Sound switch (mid 1593) on its own sound page, which
 * a7m4/open_gate.py takes over: the page is renamed Open Gate and listed on MENU > Shooting > Media, the switch gets a
 * third option row, and its getter, setter and commit are pointed here (layout.json "menu"). The
 * page, its controller and the item's ids are the custom firmware's own, already registered in every native map and
 * accepted by its id filters, so nothing is registered here.
 *
 * The native apply method calls the getter (the browsed raw value, see the setter) to find the confirmed row, then
 * the commit with that row's alias, and ignores the commit's result. A failed og_select is therefore silent: the
 * staged value is dropped and the getter shows og_selected() again.
 *
 * The 0x1c14e6c check answers "disabled": its callers open an item only when it returns 0, and otherwise show the
 * "currently disabled" caution (0x1ba807c: nonzero -> 0x1c14efc, return); the custom firmware's own filters return 1
 * for the items they lock (0x8f983f0). Our item is disabled in movie only, as the kit applies in stills only. The
 * switch's profile keeps v0.1.3's stock "return 1" at +0x50.
 */
#include <stdint.h>
#include "og.h"
#include "og_menu.h"
#include "og_config.h"
#include "og_diag.h"   /* [DEBUG-og2] */

#define HIDDEN __attribute__((visibility("hidden")))

extern uint32_t og_menu_previous_disabled(uint32_t mid) HIDDEN;  /* the 0x1c14e6c chain's previous head */
extern const volatile uint32_t *native_mode(void);             /* stock getter: OG_DIAL_MOVIE in movie */

/* 0: nothing staged; otherwise the raw value the native setter staged while browsing, plus 1. */
static uint32_t staged;

static uint32_t last_getter;   /* [DEBUG-og2] last logged value + 1; 0: none yet */

uint32_t og_menu_getter(void)
{
    uint32_t raw = __atomic_load_n(&staged, __ATOMIC_ACQUIRE);
    uint32_t value = raw ? raw - 1 : (uint32_t)og_selected();
    if (value + 1 != __atomic_exchange_n(&last_getter, value + 1, __ATOMIC_RELAXED))
        og_diag("menu.getter", value, raw, 0, 0);   /* [DEBUG-og2] only when it changes */
    return value;
}

void og_menu_setter(uint32_t raw)
{
    og_diag("menu.setter", raw, __atomic_load_n(&staged, __ATOMIC_RELAXED), 0, 0);   /* [DEBUG-og2] */
    if (raw < OG_MODE_COUNT) __atomic_store_n(&staged, raw + 1, __ATOMIC_RELEASE);
}

int og_menu_commit(uint32_t alias)
{
    uint32_t mode = alias - OG_MENU_ALIAS_BASE;
    og_diag("menu.commit", alias, mode, __atomic_load_n(&staged, __ATOMIC_RELAXED), 0);   /* [DEBUG-og2] */
    og_status s = mode < OG_MODE_COUNT ? og_select((og_mode)mode) : OG_FOREIGN;
    int ok = mode < OG_MODE_COUNT && s == OG_OK;
    og_diag("menu.commit.done", alias, mode, s, ok);   /* [DEBUG-og2] */
    og_diag_sync();
    __atomic_store_n(&staged, 0, __ATOMIC_RELEASE);
    return ok;
}

static uint32_t in_movie(void)
{
    const volatile uint32_t *dial = native_mode();
    return dial && *dial == OG_DIAL_MOVIE;
}

static uint32_t last_disabled;   /* [DEBUG-og2] last logged value + 1; 0: none yet */

uint32_t og_menu_disabled(uint32_t mid)
{
    if (mid != OG_MENU_MID) return og_menu_previous_disabled(mid);
    uint32_t r = in_movie();
    if (r + 1 != __atomic_exchange_n(&last_disabled, r + 1, __ATOMIC_RELAXED)) og_diag("menu.disabled", r, mid, 0, 0);   /* [DEBUG-og2] */
    return r;
}
