/* Owns the four memory targets behind one read/write pair, as ArcaneLantern/a7iv-open-gate-test's helpers reach them:
 *
 *   OG_CP               coprocessor RAM through libsencore: dmm_alloc a 4 KB buffer, map it from /dev/mem (O_SYNC),
 *                       sen_dma_transfer(0, cp, phys, n) reads and (1, phys, cp, n) writes, in whole 64-byte lines
 *   OG_CP_DRIVE_RECORD  the stored drive-parameter record of the camera's family, found by its pointer chain
 *   OG_AVCAM            av-cam.bin's tables through a /dev/mem mapping, checked against pread of /dev/mem as well
 *   OG_MPR              libmpr.so in this process through /proc/self/mem
 *
 * Every write is read back. Camera constants come from og_config.h, generated from layout.json and stored_record.json.
 */
#include "og_image.h"
#include "og_sys.h"
#include "og_config.h"

typedef uint64_t (*dmm_alloc_fn)(uint32_t bytes, uint32_t *handle);
typedef void (*dmm_free_fn)(uint32_t handle);
typedef int (*dma_fn)(int direction, uint64_t src, uint64_t dst, uint32_t bytes);
extern void *dlsym(void *library, const char *name);   /* appFw.so's own PLT entry (layout.json) */

enum { DMA_FROM_CP = 0, DMA_TO_CP = 1, POISON = 0xee };
#define LINE 64u
#define PAGE 0x1000u

struct og_mem {
    int open, has_buffer;
    uint32_t handle;
    uint64_t phys;                     /* the DMA buffer */
    uint8_t *map;
    unsigned long map_bytes;
    volatile uint8_t *buf;
    uint32_t lo, hi;                   /* CP bytes [lo, hi) mirrored in buf; lo == hi: none */
    long sync_fd, mem_fd, self_fd;
    volatile uint8_t *avcam;           /* av-cam.bin from OG_AVCAM_MAP_OFFSET */
    uint32_t record;                   /* CP address of the family's stored record */
};

static og_mem the_mem;
static struct { dmm_alloc_fn alloc; dmm_free_fn free; dma_fn dma; uint64_t mpr, mpr_end; } process;   /* per process */

static int cp_valid(uint32_t va, uint32_t bytes)
{
    return va >= OG_CP_VA_MIN && va < OG_CP_VA_END && (va & 3u) == 0 && bytes <= OG_CP_VA_END - va;
}

static void poison(og_mem *m, uint32_t from, uint32_t to)
{
    for (uint32_t i = from; i < to; i++) m->buf[i] = POISON;
}

/* Hold CP bytes [addr, addr + len) in buf: from addr's line to the end of its 4 KB page (or of the run). */
static og_status cp_hold(og_mem *m, uint32_t addr, uint32_t len)
{
    if (m->lo < m->hi && addr >= m->lo && addr < m->hi && len <= m->hi - addr) return OG_OK;
    m->lo = m->hi = 0;
    if (addr < OG_CP_VA_MIN || addr >= OG_CP_VA_END || len > OG_CP_VA_END - addr) return OG_IO;
    uint32_t lo = addr & ~(LINE - 1u), hi = (addr + len + LINE - 1u) & ~(LINE - 1u), page_end = (addr | (PAGE - 1u)) + 1u;
    if (hi < page_end) hi = page_end;
    if (hi > OG_CP_VA_END) hi = OG_CP_VA_END;
    if (hi - lo > OG_DMM_BYTES) hi = lo + OG_DMM_BYTES;
    if (addr + len > hi) return OG_IO;
    poison(m, 0, hi - lo);
    if (process.dma(DMA_FROM_CP, lo, m->phys, hi - lo)) return OG_IO;
    m->lo = lo;
    m->hi = hi;
    return OG_OK;
}

static og_status cp_read(og_mem *m, uint32_t addr, void *dst, uint32_t len)
{
    og_status s = cp_hold(m, addr, len);
    for (uint32_t i = 0; s == OG_OK && i < len; i++) ((uint8_t *)dst)[i] = m->buf[addr - m->lo + i];
    return s;
}

/* Read-modify-write of the run's lines, re-read just before the write, then read back and compared. */
static og_status cp_write(og_mem *m, uint32_t addr, const uint8_t *src, uint32_t len)
{
    og_status s = cp_hold(m, addr, len);
    if (s != OG_OK) return s;
    uint32_t lo = addr & ~(LINE - 1u), hi = (addr + len + LINE - 1u) & ~(LINE - 1u);
    uint32_t from = lo - m->lo, to = hi - m->lo, at = addr - m->lo;
    uint64_t phys = m->phys + from;
    poison(m, from, to);
    if (process.dma(DMA_FROM_CP, lo, phys, hi - lo)) goto lost;
    for (uint32_t i = 0; i < len; i++) m->buf[at + i] = src[i];
    if (process.dma(DMA_TO_CP, phys, lo, hi - lo)) goto lost;
    poison(m, from, to);
    if (process.dma(DMA_FROM_CP, lo, phys, hi - lo)) goto lost;
    for (uint32_t i = 0; i < len; i++)
        if (m->buf[at + i] != src[i]) return OG_IO;
    return OG_OK;
lost:
    m->lo = m->hi = 0;
    return OG_IO;
}

static og_status cp_u32(og_mem *m, uint32_t addr, uint32_t *value)
{
    uint8_t b[4];
    og_status s = cp_read(m, addr, b, 4);
    *value = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    return s;
}

/* The stored record of drive_mode (stored_record.json locator): OG_FOREIGN when the mode list lacks it,
 * OG_NOT_READY while the chain is not valid. */
static og_status locate(og_mem *m, uint32_t drive_mode, uint32_t *record)
{
    uint32_t p, q, n, list, records, mode;
    og_status s = cp_u32(m, OG_RECORD_ROOT + OG_RECORD_P_FIELD, &p);
    if (s != OG_OK) return s;
    if (!cp_valid(p, OG_RECORD_Q_FIELD + 4u)) return OG_NOT_READY;
    if ((s = cp_u32(m, p + OG_RECORD_Q_FIELD, &q)) != OG_OK) return s;
    if (!cp_valid(q, OG_RECORD_RECORDS_FIELD + 4u)) return OG_NOT_READY;
    if ((s = cp_u32(m, q + OG_RECORD_COUNT_FIELD, &n)) != OG_OK || (s = cp_u32(m, q + OG_RECORD_LIST_FIELD, &list)) != OG_OK ||
        (s = cp_u32(m, q + OG_RECORD_RECORDS_FIELD, &records)) != OG_OK)
        return s;
    if (n < OG_RECORD_COUNT_MIN || n > OG_RECORD_COUNT_MAX || !cp_valid(list, 4u * n) ||
        !cp_valid(records, OG_RECORD_BYTES * n))
        return OG_NOT_READY;
    for (uint32_t i = 0; i < n; i++) {
        if ((s = cp_u32(m, list + 4u * i, &mode)) != OG_OK) return s;
        if (mode == drive_mode) {
            *record = records + OG_RECORD_BYTES * i;
            return OG_OK;
        }
    }
    return OG_FOREIGN;
}

typedef struct { og_mem *m; uint32_t record; } record_ctx;

static og_status read_record(void *ctx, uint32_t offset, void *dst, uint32_t len)
{
    record_ctx *c = ctx;
    if (offset >= OG_RECORD_BYTES || len > OG_RECORD_BYTES - offset) return OG_IO;
    return cp_read(c->m, c->record + offset, dst, len);
}

/* The family whose stored record holds that family's stock or open-gate bytes; exactly one must. */
static og_status detect(og_mem *m, og_family *family)
{
    static const uint32_t drive_mode[OG_FAMILY_COUNT] = {OG_DRIVE_MODE_NTSC, OG_DRIVE_MODE_PAL};
    int matches = 0;
    for (unsigned f = 0; f < OG_FAMILY_COUNT; f++) {
        record_ctx c = {m, 0};
        og_status s = locate(m, drive_mode[f], &c.record);
        if (s == OG_FOREIGN) continue;
        if (s != OG_OK) return s;
        int hit = og_record_matches((og_family)f, read_record, &c);
        if (hit < 0) return OG_IO;
        if (hit) {
            matches++;
            *family = (og_family)f;
            m->record = c.record;
        }
    }
    return matches == 1 ? OG_OK : OG_FOREIGN;
}

static int hex(const char **cursor, char stop, uint64_t *value)
{
    const char *s = *cursor;
    uint64_t v = 0;
    unsigned digits = 0;
    for (;; s++, digits++) {
        unsigned d;
        if (*s >= '0' && *s <= '9') d = (unsigned)(*s - '0');
        else if (*s >= 'a' && *s <= 'f') d = (unsigned)(*s - 'a' + 10);
        else break;
        if (digits == 16) return 0;
        v = v << 4 | d;
    }
    if (!digits || *s != stop) return 0;
    *cursor = s + 1;
    *value = v;
    return 1;
}

/* A /proc/self/maps line "start-end perms offset dev inode path": 1 with the range, when the path ends in
 * /libmpr.so, the file offset is 0 and the mapping is readable (libmpr's sites sit in its first segment, at
 * VA == file offset). */
static int mpr_line(const char *s, unsigned len)
{
    static const char tail[] = "/libmpr.so";
    const unsigned n = sizeof tail - 1u;
    uint64_t start, end, offset;
    if (len < n || !og_same(s + len - n, tail, n)) return 0;
    if (!hex(&s, '-', &start) || !hex(&s, ' ', &end) || s[0] != 'r' || !s[1] || !s[2] || !s[3] || s[4] != ' ') return 0;
    s += 5;
    if (!hex(&s, ' ', &offset) || offset || end <= start) return 0;
    process.mpr = start;
    process.mpr_end = end;
    return 1;
}

static void find_libmpr(void)
{
    char chunk[512], line[512];
    unsigned used = 0;
    int overflow = 0;
    int done = 0;
    long fd = og_openat("/proc/self/maps", OG_O_RDONLY | OG_O_CLOEXEC, 0), n;
    if (fd < 0) return;
    while (!done && (n = og_read(fd, chunk, sizeof chunk)) > 0) {
        for (long i = 0; i < n && !done; i++) {
            if (chunk[i] != '\n') {
                if (used < sizeof line - 1u) line[used++] = chunk[i];
                else overflow = 1;
                continue;
            }
            line[used] = 0;
            if (!overflow) done = mpr_line(line, used);
            used = 0;
            overflow = 0;
        }
    }
    og_close(fd);
}

static og_status open_all(og_mem *m)
{
    if (!process.alloc || !process.free || !process.dma) {
        process.alloc = (dmm_alloc_fn)dlsym(0, OG_SYM_DMM_ALLOC);
        process.free = (dmm_free_fn)dlsym(0, OG_SYM_DMM_FREE);
        process.dma = (dma_fn)dlsym(0, OG_SYM_DMA);
        if (!process.alloc || !process.free || !process.dma) return OG_NOT_READY;
    }
    m->phys = process.alloc(OG_DMM_BYTES, &m->handle);
    if (!m->phys) return OG_NOT_READY;
    m->has_buffer = 1;
    if (m->phys < OG_DMM_PHYS_MIN || m->phys > OG_DMM_PHYS_END - OG_DMM_BYTES || (m->phys & (LINE - 1u)))
        return OG_NOT_READY;
    if ((m->sync_fd = og_openat("/dev/mem", OG_O_RDWR | OG_O_SYNC | OG_O_CLOEXEC, 0)) < 0) return OG_NOT_READY;
    m->map_bytes = (unsigned long)(((m->phys & (PAGE - 1u)) + OG_DMM_BYTES + PAGE - 1u) & ~(uint64_t)(PAGE - 1u));
    if (!(m->map = og_mmap(m->map_bytes, m->sync_fd, m->phys & ~(uint64_t)(PAGE - 1u)))) return OG_NOT_READY;
    m->buf = m->map + (m->phys & (PAGE - 1u));
    if ((m->mem_fd = og_openat("/dev/mem", OG_O_RDWR | OG_O_CLOEXEC, 0)) < 0) return OG_NOT_READY;
    if (!(m->avcam = og_mmap(OG_AVCAM_MAP_BYTES, m->mem_fd, OG_AVCAM_PHYS + OG_AVCAM_MAP_OFFSET))) return OG_NOT_READY;
    if ((m->self_fd = og_openat("/proc/self/mem", OG_O_RDWR | OG_O_CLOEXEC, 0)) < 0) return OG_NOT_READY;
    if (!process.mpr) find_libmpr();
    return process.mpr ? OG_OK : OG_NOT_READY;
}

og_status og_mem_begin(og_mem **out, og_family *family)
{
    og_mem *m = &the_mem;
    *out = 0;
    if (m->open) return OG_BUSY;
    m->open = 1;
    m->sync_fd = m->mem_fd = m->self_fd = -1;
    og_status s = open_all(m);
    if (s == OG_OK) s = detect(m, family);
    if (s != OG_OK) {
        og_mem_end(m);
        return s;
    }
    *out = m;
    return OG_OK;
}

static int mpr_range(uint32_t addr, uint32_t len) { return len <= process.mpr_end - process.mpr && addr <= process.mpr_end - process.mpr - len; }

static int avcam_range(uint32_t addr, uint32_t len)
{
    return addr >= OG_AVCAM_MAP_OFFSET && addr - OG_AVCAM_MAP_OFFSET < OG_AVCAM_MAP_BYTES &&
           len <= OG_AVCAM_MAP_BYTES - (addr - OG_AVCAM_MAP_OFFSET);
}

og_status og_mem_read(og_mem *m, og_space space, uint32_t addr, void *dst, uint32_t len)
{
    if (!m || !m->open || !len || len > OG_RUN_MAX) return OG_IO;
    switch (space) {
    case OG_CP:
        return cp_read(m, addr, dst, len);
    case OG_CP_DRIVE_RECORD: {
        record_ctx c = {m, m->record};
        return read_record(&c, addr, dst, len);
    }
    case OG_AVCAM:
        if (!avcam_range(addr, len)) return OG_IO;
        for (uint32_t i = 0; i < len; i++) ((uint8_t *)dst)[i] = m->avcam[addr - OG_AVCAM_MAP_OFFSET + i];
        return OG_OK;
    case OG_MPR:
        if (!mpr_range(addr, len)) return OG_IO;
        return og_pread(m->self_fd, dst, len, process.mpr + addr) == (long)len ? OG_OK : OG_IO;
    default:
        return OG_IO;
    }
}

og_status og_mem_write(og_mem *m, og_space space, uint32_t addr, const void *src, uint32_t len)
{
    const uint8_t *bytes = src;
    uint8_t back[OG_RUN_MAX];
    if (!m || !m->open || !len || len > OG_RUN_MAX) return OG_IO;
    switch (space) {
    case OG_CP:
        return cp_write(m, addr, bytes, len);
    case OG_CP_DRIVE_RECORD:
        if (addr >= OG_RECORD_BYTES || len > OG_RECORD_BYTES - addr) return OG_IO;
        return cp_write(m, m->record + addr, bytes, len);
    case OG_AVCAM:
        if (!avcam_range(addr, len)) return OG_IO;
        for (uint32_t i = 0; i < len; i++) m->avcam[addr - OG_AVCAM_MAP_OFFSET + i] = bytes[i];
        for (uint32_t i = 0; i < len; i++)
            if (m->avcam[addr - OG_AVCAM_MAP_OFFSET + i] != bytes[i]) return OG_IO;
        /* The kernel's own view of the same bytes. */
        return og_pread(m->mem_fd, back, len, OG_AVCAM_PHYS + addr) == (long)len && og_same(back, bytes, len) ? OG_OK
                                                                                                            : OG_IO;
    case OG_MPR:
        if (!mpr_range(addr, len)) return OG_IO;
        if (og_pwrite(m->self_fd, bytes, len, process.mpr + addr) != (long)len) return OG_IO;
        return og_pread(m->self_fd, back, len, process.mpr + addr) == (long)len && og_same(back, bytes, len) ? OG_OK
                                                                                                         : OG_IO;
    default:
        return OG_IO;
    }
}

void og_mem_end(og_mem *m)
{
    if (!m) return;
    if (m->avcam) og_munmap((void *)m->avcam, OG_AVCAM_MAP_BYTES);
    if (m->map) og_munmap(m->map, m->map_bytes);
    if (m->self_fd >= 0) og_close(m->self_fd);
    if (m->mem_fd >= 0) og_close(m->mem_fd);
    if (m->sync_fd >= 0) og_close(m->sync_fd);
    if (m->has_buffer) process.free(m->handle);
    m->avcam = 0;
    m->map = 0;
    m->buf = 0;
    m->self_fd = m->mem_fd = m->sync_fd = -1;
    m->has_buffer = 0;
    m->lo = m->hi = 0;
    m->record = 0;
    m->open = 0;
}
