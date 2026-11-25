/* [PROBE-angle] Host harness for native/open-gate/og_angle.c against a fake availability getter, shutter mode and dial.
 *
 *   og_angle_host
 *
 * Runs every check and prints "ok <check>" for each; any failure prints "FAIL <check>: ..." and exits 1.
 */
#include <stdint.h>
#include <stdio.h>
#include "og_angle.h"
#include "og_config.h"

static int failures;
#define CHECK(name, cond)                                                                      \
    do {                                                                                       \
        if (cond) printf("ok %s\n", name);                                                     \
        else { printf("FAIL %s: %s (line %d)\n", name, #cond, __LINE__); failures++; }         \
    } while (0)

static uint8_t real_byte;
static uint32_t mode_value, dial_value;
static int no_mode, no_dial, gate_calls;

const volatile uint8_t *og_angle_previous_gate(void) { gate_calls++; return &real_byte; }
const volatile uint32_t *shutter_mode(void) { return no_mode ? 0 : &mode_value; }
const volatile uint32_t *native_mode(void) { return no_dial ? 0 : &dial_value; }

static int item_calls, option_calls;
static void *seen_object;
uint32_t og_angle_previous_item_supported(void *object, uint32_t item) { item_calls++; seen_object = object; return item == 1592; }
uint32_t og_angle_previous_resolve_option(uint32_t option) { option_calls++; return option == 5607 ? 5607 : 0x15d8; }

int main(void)
{
    int all_real = 1, all_reads = 1;
    for (uint32_t mode = 0; mode < 3; mode++)
        for (uint32_t dial = 0; dial < 6; dial++)
            for (uint8_t byte = 0; byte < 2; byte++) {
                mode_value = mode, dial_value = dial, real_byte = byte;
                int before = gate_calls;
                const volatile uint8_t *p = og_angle_gate();
                all_reads &= gate_calls == before + 1;
                if (mode == 1 && dial == OG_DIAL_MOVIE) continue;
                all_real &= p == &real_byte;
            }
    CHECK("the real byte is read on every call", all_reads);
    CHECK("outside angle mode on movie the real byte is returned", all_real);

    mode_value = 1, dial_value = OG_DIAL_MOVIE, real_byte = 1;
    const volatile uint8_t *p = og_angle_gate();
    CHECK("angle mode on movie reads available even when inhibited", p != &real_byte && *p == 0);
    real_byte = 0;
    CHECK("angle mode on movie reads available when not inhibited", *og_angle_gate() == 0);

    no_mode = 1, real_byte = 1;
    CHECK("no shutter mode: the real byte", og_angle_gate() == &real_byte);
    no_mode = 0, no_dial = 1;
    CHECK("no dial: the real byte", og_angle_gate() == &real_byte);
    no_dial = 0;

    og_angle_log_drive(0, -1, 0x1baf410);
    og_angle_log_refused(0, 0x1baf424);
    CHECK("the trace calls return", 1);

    int object;
    CHECK("item 441 is supported, every time", og_angle_item_supported(&object, OG_ANGLE_ITEM) == 1 &&
                                                   og_angle_item_supported(&object, OG_ANGLE_ITEM) == 1);
    CHECK("other items get the chain's answer",
          og_angle_item_supported(&object, 1592) == 1 && og_angle_item_supported(&object, 440) == 0 &&
              og_angle_item_supported(&object, 442) == 0 && og_angle_item_supported(&object, 42) == 0);
    CHECK("the chain sees every item call and its object", item_calls == 6 && seen_object == &object);
    CHECK("Speed and Angle resolve to themselves, every time",
          og_angle_resolve_option(OG_ANGLE_SPEED_OPTION) == 1543 && og_angle_resolve_option(OG_ANGLE_ANGLE_OPTION) == 1544 &&
              og_angle_resolve_option(OG_ANGLE_ANGLE_OPTION) == 1544);
    CHECK("other options keep the chain's answer", og_angle_resolve_option(5607) == 5607 &&
                                                       og_angle_resolve_option(1542) == 0x15d8 &&
                                                       og_angle_resolve_option(1545) == 0x15d8);
    CHECK("the chain sees every option call", option_calls == 6);
    return failures ? 1 : 0;
}
