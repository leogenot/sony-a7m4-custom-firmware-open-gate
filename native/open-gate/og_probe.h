/* [PROBE-hd] Read-only log of the live sensor state, for 1080p open gate (methods/11-open-gate/HD-RESEARCH.md).
 * Test build only. */
#ifndef OG_PROBE_H
#define OG_PROBE_H

/* Reads the coprocessor's working state and traces it when it changed since the last call. Writes nothing. */
void og_probe_state(void);

#endif
