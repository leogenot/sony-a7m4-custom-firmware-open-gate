/* Owns the order of work over the site image: check every site, undo all presets, apply the target, roll back.
 *
 * Nothing remembers what is applied: each call first reads every site of every preset. A site may hold its stock
 * bytes or the new bytes of any preset at that site; anything else is foreign and nothing is written. Then the
 * presets are undone, last preset and last run first (sites already holding what the target wants are left alone),
 * and the target's runs are applied in the designs' order. If a read or write fails, every touched site gets back
 * the bytes it held at the start. When memory already matches the target, the call only reads.
 */
#include "og_image.h"
#include "og_diag.h"   /* [DEBUG-og2] */

#define OG_MAX_PRESET_RUNS 1536u

enum { HELD_STOCK = 0, HELD_MASK = 0x0f, TOUCHED = 0x80, NONE = 0xff };

static uint8_t held[OG_MAX_RUNS];          /* per run: the bytes it held at the start, and if written */
static int image_state;                      /* 0 unchecked, 1 valid, -1 invalid */

static uint32_t crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xffffffffu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return ~c;
}

/* The image parses as open_gate_sites.SiteImage.decode accepts it, and fits held[] and the run index. */
static int image_valid(void)
{
    if (image_state) return image_state > 0;
    const og_image_header *h = og_header();
    uint32_t runs = 0, cursor = sizeof(*h);
    int ok = h->magic == OG_IMAGE_MAGIC && h->version == OG_IMAGE_VERSION && h->header_bytes == sizeof(*h) &&
             h->total_bytes >= sizeof(*h) && h->total_bytes <= 0xffffu && (h->total_bytes & 3u) == 0 &&
             h->preset_count && h->preset_count <= OG_PRESET_SLOTS &&
             crc32(og_sites + sizeof(*h), h->total_bytes - sizeof(*h)) == h->crc32;
    for (unsigned p = 0; ok && p < h->preset_count; p++) {
        ok = h->preset[p].offset == cursor && h->preset[p].bytes <= h->total_bytes - cursor &&
             h->preset[p].runs && h->preset[p].runs <= OG_MAX_PRESET_RUNS && !h->preset[p].reserved &&
             h->preset[p].mode > OG_OFF && h->preset[p].mode < OG_MODE_COUNT;
        uint32_t end = cursor + h->preset[p].bytes;
        const og_run *r = ok ? og_first_run(p) : 0;
        for (unsigned i = 0; ok && i < h->preset[p].runs; i++, r = og_next_run(r)) {
            uint32_t at = (uint32_t)((const uint8_t *)r - og_sites);
            ok = at + sizeof(og_run) <= end && r->space < OG_SPACE_COUNT && r->len && r->len <= OG_RUN_MAX &&
                 ((at + sizeof(og_run) + 2u * r->len + 3u) & ~3u) <= end;
        }
        ok = ok && (uint32_t)((const uint8_t *)r - og_sites) == end;
        if (ok) runs += h->preset[p].runs;
        cursor = end;
    }
    ok = ok && cursor == h->total_bytes && runs <= OG_MAX_RUNS;
    image_state = ok ? 1 : -1;
    return ok;
}

/* Offsets of preset p's runs from the image start, so they can be walked backwards. */
static unsigned index_runs(unsigned p, uint16_t *at)
{
    const og_run *r = og_first_run(p);
    for (unsigned i = 0; i < og_header()->preset[p].runs; i++, r = og_next_run(r))
        at[i] = (uint16_t)((const uint8_t *)r - og_sites);
    return og_header()->preset[p].runs;
}

static const og_run *run_at(uint16_t offset) { return (const og_run *)(const void *)(og_sites + offset); }

/* The bytes a held[] value stands for at a run's site: stock, or the new bytes of preset (value - 1) there. */
static const uint8_t *held_bytes(uint8_t v, const og_run *run)
{
    v &= HELD_MASK;
    return v == HELD_STOCK ? og_old(run) : og_new(og_same_site(v - 1u, run));
}

/* Bytes the target preset wants at a run's site: its new bytes there, or stock where it has no run. */
static const uint8_t *wanted(unsigned target, const og_run *run)
{
    const og_run *q = target == NONE ? 0 : og_same_site(target, run);
    return q ? og_new(q) : og_old(run);
}

/* Write bytes to a run's site unless it already holds them; marks the run touched before writing. */
static og_status put(og_mem *mem, const og_run *run, const uint8_t *bytes, uint8_t *mark)
{
    uint8_t cur[OG_RUN_MAX];
    og_status s = og_mem_read(mem, (og_space)run->space, run->addr, cur, run->len);
    if (s != OG_OK) og_diag("put.read", run->space, run->addr, run->len, s);   /* [DEBUG-og2] */
    if (s != OG_OK || og_same(cur, bytes, run->len)) return s;
    *mark |= TOUCHED;
    s = og_mem_write(mem, (og_space)run->space, run->addr, bytes, run->len);
    if (s != OG_OK) og_diag("put.write", run->space, run->addr, run->len, s);   /* [DEBUG-og2] */
    return s;
}

typedef struct {
    og_mem *mem;
    unsigned target;                   /* preset index, or NONE for OG_OFF */
    unsigned first[OG_PRESET_SLOTS];   /* index in held[] of each preset's first run */
} job;

/* Read every site once and record which bytes it holds. */
static og_status classify(job *j)
{
    const og_image_header *h = og_header();
    uint8_t cur[OG_RUN_MAX];
    unsigned n = 0;
    for (unsigned p = 0; p < h->preset_count; p++) {
        j->first[p] = n;
        const og_run *r = og_first_run(p);
        for (unsigned i = 0; i < h->preset[p].runs; i++, r = og_next_run(r), n++) {
            og_status s = og_mem_read(j->mem, (og_space)r->space, r->addr, cur, r->len);
            if (s != OG_OK) {
                og_diag("classify.read", p, i, r->space, ((uint64_t)r->addr << 16) | r->len);   /* [DEBUG-og2] */
                return s;
            }
            held[n] = og_same(cur, og_old(r), r->len) ? HELD_STOCK : NONE;
            for (unsigned k = 0; k < h->preset_count && held[n] == NONE; k++) {
                const og_run *q = og_same_site(k, r);
                if (q && og_same(cur, og_new(q), r->len)) held[n] = (uint8_t)(k + 1u);
            }
            if (held[n] == NONE) {   /* [DEBUG-og2] which site, what it holds, what stock is */
                og_diag("classify.foreign", p, i, r->space, ((uint64_t)r->addr << 16) | r->len);
                og_diag_bytes("classify.foreign.now", r->space, r->addr, cur, r->len);
                og_diag_bytes("classify.foreign.stock", r->space, r->addr, og_old(r), r->len);
                og_diag_bytes("classify.foreign.new", r->space, r->addr, og_new(r), r->len);
                return OG_FOREIGN;
            }
        }
    }
    return OG_OK;
}

/* Every touched site back to the bytes it held at the start, in the reverse of the order of work. */
static og_status roll_back(job *j, uint16_t *at)
{
    const og_image_header *h = og_header();
    og_status result = OG_IO;
    if (j->target != NONE) {
        for (unsigned i = index_runs(j->target, at); i-- > 0;) {
            uint8_t *mark = &held[j->first[j->target] + i];
            const og_run *r = run_at(at[i]);
            if ((*mark & TOUCHED) && put(j->mem, r, held_bytes(*mark, r), mark) != OG_OK) result = OG_UNDO_FAILED;
        }
    }
    for (unsigned p = 0; p < h->preset_count; p++) {
        const og_run *r = og_first_run(p);
        for (unsigned i = 0; i < h->preset[p].runs; i++, r = og_next_run(r)) {
            uint8_t *mark = &held[j->first[p] + i];
            if ((*mark & TOUCHED) && put(j->mem, r, held_bytes(*mark, r), mark) != OG_OK) result = OG_UNDO_FAILED;
        }
    }
    return result;
}

static og_status transition(job *j)
{
    const og_image_header *h = og_header();
    uint16_t at[OG_MAX_PRESET_RUNS];
    for (unsigned p = h->preset_count; p-- > 0;) {
        for (unsigned i = index_runs(p, at); i-- > 0;) {
            uint8_t *mark = &held[j->first[p] + i];
            const og_run *r = run_at(at[i]);
            if (og_same(held_bytes(*mark, r), wanted(j->target, r), r->len)) continue;
            if (put(j->mem, r, og_old(r), mark) != OG_OK) return roll_back(j, at);
        }
    }
    if (j->target == NONE) return OG_OK;
    const og_run *r = og_first_run(j->target);
    for (unsigned i = 0; i < h->preset[j->target].runs; i++, r = og_next_run(r))
        if (put(j->mem, r, og_new(r), &held[j->first[j->target] + i]) != OG_OK) return roll_back(j, at);
    return OG_OK;
}

og_status og_reconcile(og_mode target)
{
    int valid = (unsigned)target < OG_MODE_COUNT && image_valid();
    og_diag("reconcile", target, valid, og_header()->preset_count, og_header()->total_bytes);   /* [DEBUG-og2] */
    if (!valid) return OG_FOREIGN;
    job j = {.target = NONE};
    og_status s = og_mem_begin(&j.mem);
    og_diag("reconcile.begin", s, 0, 0, 0);   /* [DEBUG-og2] */
    if (s != OG_OK) return s;
    const og_image_header *h = og_header();
    for (unsigned p = 0; p < h->preset_count; p++)
        if (h->preset[p].mode == target) j.target = p;
    if (target != OG_OFF && j.target == NONE) s = OG_FOREIGN;
    if (s == OG_OK) s = classify(&j);
    og_diag("reconcile.classify", s, j.target, 0, 0);   /* [DEBUG-og2] */
    if (s == OG_OK) s = transition(&j);
    og_diag("reconcile.transition", s, j.target, 0, 0);   /* [DEBUG-og2] */
    og_mem_end(j.mem);
    return s;
}
