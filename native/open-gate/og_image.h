/* Private to native/open-gate: reading the generated site image (format: a7m4/open_gate_sites.py). */
#ifndef OG_IMAGE_H
#define OG_IMAGE_H

#include "og_internal.h"

#define OG_IMAGE_MAGIC 0x4953474fu     /* bytes 'O' 'G' 'S' 'I' */
#define OG_IMAGE_VERSION 1u
#define OG_RUN_MAX 256u                /* open_gate_sites.RUN_MAX */
#define OG_PRESET_SLOTS 4u
#define OG_MAX_FAMILY_RUNS 1536u       /* runs of one family's presets; checked by og_image_valid */

_Static_assert(sizeof(og_image_header) == 68, "og_image_header layout");
_Static_assert(sizeof(og_run) == 8, "og_run layout");

static inline const og_image_header *og_header(void) { return (const og_image_header *)(const void *)og_sites; }

static inline const og_run *og_first_run(unsigned preset)
{
    return (const og_run *)(const void *)(og_sites + og_header()->preset[preset].offset);
}

static inline const og_run *og_next_run(const og_run *run)
{
    return (const og_run *)(const void *)((const uint8_t *)run + ((sizeof(og_run) + 2u * run->len + 3u) & ~3u));
}

static inline const uint8_t *og_old(const og_run *run) { return (const uint8_t *)(run + 1); }
static inline const uint8_t *og_new(const og_run *run) { return og_old(run) + run->len; }

static inline int og_same(const void *a, const void *b, uint32_t len)
{
    const uint8_t *x = a, *y = b;
    for (uint32_t i = 0; i < len; i++)
        if (x[i] != y[i]) return 0;
    return 1;
}

/* The run of preset p at run's site, or 0. Runs of one space either coincide or share no byte. */
static inline const og_run *og_same_site(unsigned p, const og_run *run)
{
    const og_run *q = og_first_run(p);
    for (unsigned i = 0; i < og_header()->preset[p].runs; i++, q = og_next_run(q))
        if (q->space == run->space && q->addr == run->addr && q->len == run->len) return q;
    return 0;
}

/* Stored-record reader for og_record_matches: len bytes at offset inside one candidate record. */
typedef og_status (*og_record_reader)(void *ctx, uint32_t offset, void *dst, uint32_t len);

/* 1 when every stored-record run of the family's presets holds its stock bytes or a family preset's new bytes,
 * 0 when one does not or the family has no record runs, -1 when a read failed. */
static inline int og_record_matches(og_family family, og_record_reader read, void *ctx)
{
    const og_image_header *h = og_header();
    uint8_t cur[OG_RUN_MAX];
    int runs = 0;
    for (unsigned p = 0; p < h->preset_count; p++) {
        if (h->preset[p].family != family) continue;
        const og_run *r = og_first_run(p);
        for (unsigned i = 0; i < h->preset[p].runs; i++, r = og_next_run(r)) {
            if (r->space != OG_CP_DRIVE_RECORD) continue;
            if (read(ctx, r->addr, cur, r->len) != OG_OK) return -1;
            int known = og_same(cur, og_old(r), r->len);
            for (unsigned k = 0; !known && k < h->preset_count; k++) {
                const og_run *q = h->preset[k].family == family ? og_same_site(k, r) : 0;
                known = q && og_same(cur, og_new(q), r->len);
            }
            if (!known) return 0;
            runs++;
        }
    }
    return runs > 0;
}

#endif
