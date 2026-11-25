/* [DCI] Host harness for native/open-gate/og_dci.c against fake previous heads of the two menu chains.
 *
 *   og_dci_host
 *
 * Runs every check and prints "ok <check>" for each; any failure prints "FAIL <check>: ..." and exits 1.
 */
#include <stdint.h>
#include <stdio.h>
#include "og_dci.h"

static int failures;
#define CHECK(name, cond)                                                                      \
    do {                                                                                       \
        if (cond) printf("ok %s\n", name);                                                     \
        else { printf("FAIL %s: %s (line %d)\n", name, #cond, __LINE__); failures++; }         \
    } while (0)

static int option_calls;
uint32_t og_dci_previous_resolve_option(uint32_t option) { option_calls++; return option == 139 ? 139 : 0x15d8; }
static uint32_t condition_value;
const volatile uint32_t *dci_condition(void) { return &condition_value; }

/* [PROBE-dci] Sony's per-option checks: S-I 4K (139) passes all, DCI (141) fails all, and each call is counted. */
static int sony_calls;
static void *sony_object;
#define SONY(name)                                                                                 \
    uint32_t og_dci_previous_##name(void *object, uint32_t alias)                                  \
    {                                                                                              \
        sony_calls++, sony_object = object;                                                        \
        return alias == 139 ? 0x101 : 0x100;   /* only the low byte counts, as with Sony's bools */ \
    }
SONY(shown)
SONY(slot_1b0)
SONY(slot_1f0)
SONY(slot_1f8)
SONY(slot_200)
SONY(slot_210)
SONY(slot_6f8)
SONY(slot_700)

int main(void)
{

    static const uint32_t through[] = {141, 202, 203, 204, 206, 207};
    int all = 1;
    for (unsigned i = 0; i < sizeof through / sizeof *through; i++) all &= og_dci_resolve_option(through[i]) == through[i];
    CHECK("DCI and its 30p/25p/24p options resolve to themselves", all);
    CHECK("DCI 60p, 50p, 59.94p and 24.00p keep the chain's answer",
          og_dci_resolve_option(200) == 0x15d8 && og_dci_resolve_option(201) == 0x15d8 &&
              og_dci_resolve_option(205) == 0x15d8 && og_dci_resolve_option(208) == 0x15d8);
    CHECK("other options keep the chain's answer", og_dci_resolve_option(139) == 139 && og_dci_resolve_option(5607) == 0x15d8);
    CHECK("the chain sees every option call", option_calls == 6 + 4 + 2);
    CHECK("the option set is exactly the six", og_dci_option(141) && !og_dci_option(140) && !og_dci_option(142) &&
                                                     !og_dci_option(205) && !og_dci_option(208));
    uint64_t vtable = 0x58f7a78;
    void *object = &vtable;
    int calls = sony_calls;
    CHECK("the DCI row is shown and passes every overridden check without asking Sony",
          og_dci_shown(object, 141) && og_dci_slot_1b0(object, 141) && og_dci_slot_1f0(object, 141) &&
              og_dci_slot_210(object, 141) && og_dci_slot_6f8(object, 141) && og_dci_slot_700(object, 141) &&
              sony_calls == calls + 8);   /* only the probe's eight calls, on the first og_dci_shown */
    calls = sony_calls;
    CHECK("other options get Sony's answer, low byte only",
          og_dci_shown(object, 139) && !og_dci_slot_1b0(object, 140) && !og_dci_slot_700(object, 142) &&
              og_dci_slot_210(object, 139) && sony_calls == calls + 8 + 1 + 3 && sony_object == object);
    calls = sony_calls;
    og_dci_shown(object, 139), og_dci_shown(object, 141);
    CHECK("the probe runs once per alias", sony_calls == calls + 1);
    return failures ? 1 : 0;
}
