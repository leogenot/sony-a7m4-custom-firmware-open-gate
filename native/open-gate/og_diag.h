/* [DEBUG-og2] Diagnostic trace of the open-gate module: one text line per event, appended to /setting/og_diag.txt
 * and read back over USB by tools/og_read_diag.py. Camera build only: in a hosted (test) build these are no-ops.
 * Not for release. */
#ifndef OG_DIAG_H
#define OG_DIAG_H

#include <stdint.h>

#if !__STDC_HOSTED__
void og_diag(const char *tag, uint64_t a, uint64_t b, uint64_t c, uint64_t d);
void og_diag_sync(void);
#else
static inline void og_diag(const char *tag, uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    (void)tag; (void)a; (void)b; (void)c; (void)d;
}
static inline void og_diag_sync(void) {}
#endif

/* Up to the first 8 bytes, as hex reads them: byte 0 first. */
static inline uint64_t og_diag_pack(const volatile void *bytes, uint32_t n)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < n && i < 8u; i++) v = v << 8 | ((const volatile uint8_t *)bytes)[i];
    return v;
}

#endif
