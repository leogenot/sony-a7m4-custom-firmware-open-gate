/* [PROBE-angle] Shutter angle on the dial: test build only (methods/12-shutter-angle/RESEARCH.md).
 *
 * With the backup property 0x02cf1702 = 1 the camera shows the shutter as an angle, but turning the dial gives the
 * "unavailable" answer. The dial dispatcher (0x1baf328) reads the shutter mode (0x28f6e38: 0 speed, 1 angle) and,
 * in angle mode, a generated availability byte through 0x293d634 (entry 0x281 of the HAITA table; nonzero =
 * inhibited): nonzero sends it to 0x26e898c ("unavailable"), zero to the angle drive 0x26e8b08, which reads the byte
 * again. Every reader of that byte (14 BLs) now comes here: in angle mode with the dial on movie it reads 0, so the
 * drive runs as it does on the FX3; anywhere else it reads the real byte. The real byte and the override are traced
 * whenever they change, and each drive step and refusal is traced too.
 *
 * The Shutter Mode menu item is Sony's own (the FX3's): the A7 IV lists it first under Exposure, but its leaf and two
 * rows carry the per-product flag off for the A7 IV, which the build switches on (layout.json "menu_edits"), as for DCI
 * (methods/13-dci-4k on alpha-dci). Item 441 and the two options also go through two lookups Erlla's custom firmware
 * chains for its own pages (the item map and the option resolve); this module goes first and answers for them the way
 * Erlla's code answers for its own: every id maps or resolves to itself. Choosing a row runs Sony's own commit
 * (0x1cd40d8), which sets the mode live, saves it and tells the screens.
 */
#include <stdint.h>
#include "og_angle.h"
#include "og_config.h"
#include "og_diag.h"

#define HIDDEN __attribute__((visibility("hidden")))

extern const volatile uint8_t *og_angle_previous_gate(void) HIDDEN;   /* 0x293d634 itself */
extern const volatile uint32_t *native_mode(void);                    /* stock: OG_DIAL_MOVIE in movie */
extern const volatile uint32_t *shutter_mode(void);                   /* stock 0x28f6e38: 0 speed, 1 angle */
extern uint32_t og_angle_previous_resolve_option(uint32_t option) HIDDEN;              /* Erlla's chain, 0x8fa1000 */

#define SHUTTER_MODE_ANGLE 1u

static uint8_t available;   /* always 0: what the gate's readers see when the angle is let through */
static uint32_t last;       /* last traced state + 1; 0: none yet */

const volatile uint8_t *og_angle_gate(void)
{
    const volatile uint8_t *real = og_angle_previous_gate();
    const volatile uint32_t *mode = shutter_mode(), *dial = native_mode();
    uint32_t angle = mode && *mode == SHUTTER_MODE_ANGLE, movie = dial && *dial == OG_DIAL_MOVIE;
    uint32_t blocked = *real != 0, lift = angle && movie;
    uint32_t state = blocked | angle << 1 | movie << 2;
    if (state + 1 != __atomic_exchange_n(&last, state + 1, __ATOMIC_RELAXED))
        og_diag("angle.gate", blocked, mode ? *mode : 9, dial ? *dial : 9, lift);   /* [DEBUG-og2] */
    return lift ? &available : real;
}

void og_angle_log_drive(void *context, int32_t step, uint64_t from)
{
    (void)context;
    og_diag("angle.drive", (uint64_t)(int64_t)step, from - 4, 0, 0);   /* [DEBUG-og2] from - 4: the BL */
}

void og_angle_log_refused(void *context, uint64_t from)
{
    (void)context;
    og_diag("angle.refused", from - 4, 0, 0, 0);   /* [DEBUG-og2] */
}

enum { TRACED_SPEED = 1, TRACED_ANGLE = 2 };
static uint32_t traced;   /* the TRACED_ bits already logged */

static int first(uint32_t bit)
{
    return !(__atomic_fetch_or(&traced, bit, __ATOMIC_RELAXED) & bit);
}

uint32_t og_angle_resolve_option(uint32_t option)
{
    uint32_t stock = og_angle_previous_resolve_option(option);
    if (option != OG_ANGLE_SPEED_OPTION && option != OG_ANGLE_ANGLE_OPTION) return stock;
    if (first(option == OG_ANGLE_SPEED_OPTION ? TRACED_SPEED : TRACED_ANGLE))
        og_diag("angle.option", option, stock, option, 0);   /* [DEBUG-og2] */
    return option;
}
