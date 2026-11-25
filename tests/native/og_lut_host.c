/* Host harness for native/open-gate/og_lut.c against a fake LUT text binder.
 *
 *   og_lut_host <labels.tsv>
 *
 * labels.tsv holds lut_labels.json's pairs, one "Chinese<TAB>English" per line. Runs every check and prints
 * "ok <check>" for each; any failure prints "FAIL <check>: ..." and exits 1.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "og_lut.h"

static int failures;
#define CHECK(name, cond)                                                                      \
    do {                                                                                       \
        if (cond) printf("ok %s\n", name);                                                     \
        else { printf("FAIL %s: %s (line %d)\n", name, #cond, __LINE__); failures++; }         \
    } while (0)

/* --- fake binder: records what it was given --------------------------------------------------------------------- */
static struct { void *param; uint32_t param_bytes; const char *text; uint32_t text_bytes, resource; int calls; } bound;

int32_t og_lut_previous_bind(void *param, uint32_t param_bytes, const char *text, uint32_t text_bytes, uint32_t resource)
{
    bound.param = param;
    bound.param_bytes = param_bytes;
    bound.text = text;
    bound.text_bytes = text_bytes;
    bound.resource = resource;
    bound.calls++;
    return 7;
}

static int32_t bind(const char *text, uint32_t text_bytes)
{
    memset(&bound, 0, sizeof bound);
    return og_lut_text_bind(&bound, 0x658, text, text_bytes, 0x0200a0b3u);
}

static int passed_on(const char *text, uint32_t text_bytes)
{
    return bound.calls == 1 && bound.text == text && bound.text_bytes == text_bytes && bound.param == &bound
           && bound.param_bytes == 0x658 && bound.resource == 0x0200a0b3u;
}

int main(int argc, char **argv)
{
    FILE *tsv = argc == 2 ? fopen(argv[1], "r") : 0;
    if (!tsv) { printf("FAIL usage: og_lut_host <labels.tsv>\n"); return 1; }
    char line[512];
    int labels = 0, english = 0, arguments = 0, sized = 0;
    while (fgets(line, sizeof line, tsv)) {
        char *tab = strchr(line, '\t'), *end = strchr(line, '\n');
        if (!tab || !end) { printf("FAIL bad line: %s\n", line); return 1; }
        *tab = *end = 0;
        const char *zh = line, *en = tab + 1;
        uint32_t n = (uint32_t)strlen(zh) + 1;
        labels++;
        int32_t r = bind(zh, n);
        english += r == 7 && bound.calls == 1 && bound.text_bytes == strlen(en) + 1 && !strcmp(bound.text, en);
        arguments += bound.param == &bound && bound.param_bytes == 0x658 && bound.resource == 0x0200a0b3u;
        /* A caller may give its buffer's size: the label still ends at its NUL inside text_bytes. */
        char buffer[96] = {0};
        memcpy(buffer, zh, n);
        bind(buffer, sizeof buffer);
        sized += bound.calls == 1 && !strcmp(bound.text, en);
    }
    fclose(tsv);
    CHECK("every label binds its English, NUL included", labels > 0 && english == labels);
    CHECK("the widget, its size and the resource are passed on", arguments == labels);
    CHECK("a label inside a larger buffer binds its English", sized == labels);

    static const char name[] = "My Film Look", percent[] = "70%", prefix[] = "\345\205\263\345\205\263"; /* 关关 */
    CHECK("a LUT name goes to the binder unchanged", bind(name, sizeof name) == 7 && passed_on(name, sizeof name));
    CHECK("a percentage goes to the binder unchanged", bind(percent, sizeof percent) == 7 && passed_on(percent, sizeof percent));
    CHECK("a text that only starts with a label is unchanged", bind(prefix, sizeof prefix) == 7 && passed_on(prefix, sizeof prefix));
    static const char off[] = "\345\205\263";   /* 关 */
    CHECK("a label cut short by text_bytes is unchanged", bind(off, 3) == 7 && passed_on(off, 3));
    CHECK("an empty length is unchanged", bind(off, 0) == 7 && passed_on(off, 0));
    CHECK("no text is unchanged", bind(0, 4) == 7 && passed_on(0, 4));
    CHECK("the lookup answers 0 for no label", !og_lut_english(name, sizeof name) && !og_lut_english(0, 4));
    return failures ? 1 : 0;
}
