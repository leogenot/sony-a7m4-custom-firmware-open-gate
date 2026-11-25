/* English for the custom firmware's LUT screens: the 15 calls of its LUT text binder are pointed here
 * (layout.json hooks "og_lut_text_bind"). Calls only the binder. */
#ifndef OG_LUT_H
#define OG_LUT_H

#include <stdint.h>

/* The binder's own signature (0x9000d54, lut_text_v617_bind in Erlla's sources). */
int32_t og_lut_text_bind(void *param, uint32_t param_bytes, const char *text, uint32_t text_bytes, uint32_t resource);

/* The English of a Chinese label (lut_labels.json), or 0 when text is no label. */
const char *og_lut_english(const char *text, uint32_t text_bytes);

#endif
