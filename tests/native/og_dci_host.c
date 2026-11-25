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

static int item_calls, option_calls;
static void *seen_object;
uint32_t og_dci_previous_item_supported(void *object, uint32_t item) { item_calls++; seen_object = object; return item == 40; }
uint32_t og_dci_previous_resolve_option(uint32_t option) { option_calls++; return option == 139 ? 139 : 0x15d8; }

int main(void)
{
    int object;
    CHECK("item 42 is supported", og_dci_item_supported(&object, OG_DCI_ITEM) == 1);
    CHECK("other items get the chain's answer",
          og_dci_item_supported(&object, 40) == 1 && og_dci_item_supported(&object, 41) == 0 &&
              og_dci_item_supported(&object, 36) == 0);
    CHECK("the chain sees every item call and its object", item_calls == 4 && seen_object == &object);

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
    return failures ? 1 : 0;
}
