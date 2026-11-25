/* English for the custom firmware's LUT screens.
 *
 * Every text those screens show goes through one binder, 0x9000d54 (lut_text_v617_bind in Erlla's sources): it checks
 * the text (1..129 bytes, NUL included), converts it and copies it into the widget's parameter. It has 15 callers, all
 * BLs, in lut_stock_bind and p4_gui_render_overlay, and a7m4/open_gate.py points each one here. A text that is one of
 * lut_labels.json's Chinese labels is bound as its English; any other text (LUT names, percentages, page numbers)
 * goes to the binder unchanged. As the binder copies, the English may be longer than the Chinese.
 *
 * Erlla's upstream version binds the same labels to new string-bank records instead, so each camera language gets its
 * own text; that needs 34 grown string banks, which this build cannot write. Here every language gets English.
 */
#include <stdint.h>
#include "og_lut.h"
#include "og_lut_labels.h"

#define HIDDEN __attribute__((visibility("hidden")))

extern int32_t og_lut_previous_bind(void *param, uint32_t param_bytes, const char *text, uint32_t text_bytes,
                                    uint32_t resource) HIDDEN;   /* the binder itself */

/* "Chinese\0English\0" pairs, inline: no pointers, so no relocations. */
static const char labels[] = OG_LUT_LABELS;

static uint32_t length(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

const char *og_lut_english(const char *text, uint32_t text_bytes)
{
    if (!text) return 0;
    for (const char *zh = labels; zh < labels + sizeof(labels) - 1;) {
        uint32_t n = length(zh), i = 0;
        const char *en = zh + n + 1;
        /* The whole text, up to a NUL inside text_bytes, equals the label. */
        while (i < text_bytes && i <= n && text[i] == zh[i]) i++;
        if (i == n + 1) return en;
        zh = en + length(en) + 1;
    }
    return 0;
}

int32_t og_lut_text_bind(void *param, uint32_t param_bytes, const char *text, uint32_t text_bytes, uint32_t resource)
{
    const char *en = og_lut_english(text, text_bytes);
    return en ? og_lut_previous_bind(param, param_bytes, en, length(en) + 1, resource)
              : og_lut_previous_bind(param, param_bytes, text, text_bytes, resource);
}
