/* [PROBE-hd] Which sensor readout each movie format runs on, read from the coprocessor's working state at the
 * addresses the kit's read-only level 8 copies (ct6_level1_run.py at 2f2a021, lines 836-849):
 *
 *   0x011d1194  8 x u16: readout W, H; crop start X, Y; crop W, H; front end W, H
 *   0x011d11ac  u16 W, H, u32 drive-mode id: the size source and the readout mode
 *   0x0145091c  10 x u16: second scaler input W, H; window start X, Y; size W, H; then the four 12-bit fractions
 *
 * Those come from stock 6.02's coprocessor; v0.1.3's differs (Erlla's patches), so the first trace with 4K 3:2 on
 * must show readout 7032x4688 on mode 0x8c0 (0x880 on PAL), the kit's expected state, before any other line is
 * believed. Read-only: og_mem_read only. Traced only when any byte changed, so menu exits don't flood the file.
 */
#include <stdint.h>
#include "og_internal.h"
#include "og_probe.h"
#include "og_diag.h"

#define STATE_WINDOW 0x011d1194u
#define STATE_SOURCE 0x011d11acu
#define STATE_SCALER 0x0145091cu

typedef struct { uint16_t window[8]; uint16_t source[6]; uint16_t scaler[10]; } state;   /* source: W, H, mode (u32), spare */

static uint32_t last_sum;   /* checksum of the last traced state + 1; 0: none yet */

static uint64_t pair(const uint16_t *v) { return (uint64_t)v[0] << 16 | v[1]; }

void og_probe_state(void)
{
    og_mem *m;
    og_family family;
    state s = {0};
    og_status r = og_mem_begin(&m, &family);
    if (r != OG_OK) { og_diag("hd.begin", r, 0, 0, 0); return; }   /* [DEBUG-og2] */
    og_status a = og_mem_read(m, OG_CP, STATE_WINDOW, s.window, sizeof s.window);
    og_status b = og_mem_read(m, OG_CP, STATE_SOURCE, s.source, sizeof s.source);
    og_status c = og_mem_read(m, OG_CP, STATE_SCALER, s.scaler, sizeof s.scaler);
    og_mem_end(m);
    if (a || b || c) { og_diag("hd.read", a, b, c, 0); return; }   /* [DEBUG-og2] */
    uint32_t sum = 0;
    const uint8_t *p = (const uint8_t *)&s;
    for (unsigned i = 0; i < sizeof s; i++) sum = sum * 31u + p[i];
    if (sum + 1u == __atomic_exchange_n(&last_sum, sum + 1u, __ATOMIC_RELAXED)) return;
    /* [DEBUG-og2] each value WWWWHHHH in hex */
    og_diag("hd.window", pair(&s.window[0]), pair(&s.window[4]), pair(&s.window[6]), pair(&s.window[2]));
    og_diag("hd.mode", (uint32_t)s.source[3] << 16 | s.source[2], pair(s.source), family, 0);
    og_diag("hd.scaler", pair(&s.scaler[0]), pair(&s.scaler[2]), pair(&s.scaler[4]),
            (uint64_t)s.scaler[6] << 48 | (uint64_t)s.scaler[7] << 32 | (uint64_t)s.scaler[8] << 16 | s.scaler[9]);
}
