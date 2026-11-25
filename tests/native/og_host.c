/* Host harness for native/open-gate: og_reconcile.c and og.c against a fake og_mem.
 *
 *   og_host <cpapp-b.bin> <av-cam.bin> <libmpr.so> <ntsc|pal> <offsets,comma> <ntsc-old-hex> <pal-old-hex>
 *
 * The fake camera memory is the three files (CP at VA 0x108000, RAM beyond the image filled with 0x5a) plus two
 * 348-byte stored records (drive modes 0x8c0 and 0x880) holding the camera family's stock bytes at the record
 * offsets. Commands on stdin, one per line; each answers on stdout:
 *
 *   seed <space> <addr> <hex>      set bytes and their seed (CP RAM beyond the image)
 *   corrupt <space> <addr> <hex>   set bytes only (foreign bytes)
 *   reconcile <mode>               -> status <s> reads <n> writes <n>
 *   diff                           -> per byte differing from the seed "d <space> <addr> <byte>", or for the stored
 *                                     records "r <family> <offset> <byte>"; then "end"
 *   fail <n> <before|after|all>    the n-th write from now fails (before writing, after writing, or it and all later)
 *   ready <0|1>                    og_mem_begin answers OG_NOT_READY while 0
 *   dial <value> / select <mode> / power_on / selected / load    og.c entry points (status, mode)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "og_image.h"

#define CP_SIZE 0x2000000u
#define RECORD_BYTES 348u

struct og_mem { int open; og_family family; };
static og_mem mem;
static uint8_t *space_now[OG_SPACE_COUNT], *space_seed[OG_SPACE_COUNT];
static uint32_t space_size[OG_SPACE_COUNT];
static uint8_t records_now[OG_FAMILY_COUNT][RECORD_BYTES], records_seed[OG_FAMILY_COUNT][RECORD_BYTES];
static unsigned reads, writes, fail_at, fail_mode, ready = 1;
enum { FAIL_NONE, FAIL_BEFORE, FAIL_AFTER, FAIL_ALL };
static uint32_t dial_value;
static og_family camera_family;

const volatile uint32_t *native_mode(void) { return &dial_value; }

static uint8_t *load(const char *path, uint32_t *size, uint32_t at, uint32_t total)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (!total) total = (uint32_t)n;
    if (at + (uint32_t)n > total) { fprintf(stderr, "%s too large\n", path); exit(2); }
    uint8_t *p = malloc(total);
    memset(p, 0x5a, total);
    if (fread(p + at, 1, (size_t)n, f) != (size_t)n) { perror(path); exit(2); }
    fclose(f);
    *size = total;
    return p;
}

static void hex_bytes(const char *hex, uint8_t *out, unsigned n)
{
    for (unsigned i = 0; i < n; i++) { unsigned v; sscanf(hex + 2 * i, "%2x", &v); out[i] = (uint8_t)v; }
}

/* Where (space, addr, len) lives in the fake memory, or 0. Records: the given family's. */
static uint8_t *at(og_space space, uint32_t addr, uint32_t len, int seed, og_family family)
{
    if (space == OG_CP_DRIVE_RECORD) {
        if (addr >= RECORD_BYTES || len > RECORD_BYTES - addr) return 0;
        return (seed ? records_seed : records_now)[family] + addr;
    }
    if ((unsigned)space >= OG_SPACE_COUNT || addr >= space_size[space] || len > space_size[space] - addr) return 0;
    return (seed ? space_seed : space_now)[space] + addr;
}

static og_status record_read(void *ctx, uint32_t offset, void *dst, uint32_t len)
{
    const uint8_t *r = ctx;
    if (offset >= RECORD_BYTES || len > RECORD_BYTES - offset) return OG_IO;
    memcpy(dst, r + offset, len);
    return OG_OK;
}

og_status og_mem_begin(og_mem **out, og_family *family)
{
    *out = 0;
    if (mem.open) return OG_BUSY;
    if (!ready) return OG_NOT_READY;
    int matches = 0;
    for (unsigned f = 0; f < OG_FAMILY_COUNT; f++) {
        int hit = og_record_matches((og_family)f, record_read, records_now[f]);
        if (hit < 0) return OG_IO;
        if (hit) { matches++; *family = (og_family)f; }
    }
    if (matches != 1) return OG_FOREIGN;
    mem.family = *family;
    mem.open = 1;
    *out = &mem;
    return OG_OK;
}

og_status og_mem_read(og_mem *m, og_space space, uint32_t addr, void *dst, uint32_t len)
{
    uint8_t *p = m && m->open ? at(space, addr, len, 0, m->family) : 0;
    if (!p) return OG_IO;
    reads++;
    memcpy(dst, p, len);
    return OG_OK;
}

og_status og_mem_write(og_mem *m, og_space space, uint32_t addr, const void *src, uint32_t len)
{
    uint8_t *p = m && m->open ? at(space, addr, len, 0, m->family) : 0;
    if (!p) return OG_IO;
    writes++;
    if (fail_mode != FAIL_NONE && fail_at && --fail_at == 0) {
        if (fail_mode == FAIL_AFTER) memcpy(p, src, len);
        if (fail_mode == FAIL_ALL) fail_at = 1;
        else fail_mode = FAIL_NONE;
        return OG_IO;
    }
    memcpy(p, src, len);
    return OG_OK;
}

void og_mem_end(og_mem *m)
{
    if (m) m->open = 0;
}

static void diff(void)
{
    for (unsigned s = 0; s < OG_SPACE_COUNT; s++) {
        if (s == OG_CP_DRIVE_RECORD) {
            for (unsigned f = 0; f < OG_FAMILY_COUNT; f++)
                for (unsigned i = 0; i < RECORD_BYTES; i++)
                    if (records_now[f][i] != records_seed[f][i]) printf("r %u %u %u\n", f, i, records_now[f][i]);
            continue;
        }
        for (uint32_t i = 0; i < space_size[s]; i++)
            if (space_now[s][i] != space_seed[s][i]) printf("d %u %u %u\n", s, i, space_now[s][i]);
    }
    printf("end\n");
}

int main(int argc, char **argv)
{
    if (argc != 8) { fprintf(stderr, "usage: see the header\n"); return 2; }
    space_now[OG_CP] = load(argv[1], &space_size[OG_CP], 0x108000u, CP_SIZE);
    space_now[OG_AVCAM] = load(argv[2], &space_size[OG_AVCAM], 0, 0);
    space_now[OG_MPR] = load(argv[3], &space_size[OG_MPR], 0, 0);
    int pal = !strcmp(argv[4], "pal");
    camera_family = pal ? OG_FAMILY_PAL : OG_FAMILY_NTSC;
    uint8_t old[2][64];
    unsigned offsets[64], n = 0;
    for (char *tok = strtok(argv[5], ","); tok && n < 64; tok = strtok(0, ",")) offsets[n++] = (unsigned)atoi(tok);
    hex_bytes(argv[6], old[0], n);
    hex_bytes(argv[7], old[1], n);
    for (unsigned f = 0; f < OG_FAMILY_COUNT; f++) {
        memset(records_now[f], 0x5a, RECORD_BYTES);
        for (unsigned i = 0; i < n; i++) records_now[f][offsets[i]] = old[pal][i];   /* the camera family's bytes */
    }
    for (unsigned s = 0; s < OG_SPACE_COUNT; s++) {
        if (!space_now[s]) continue;
        space_seed[s] = malloc(space_size[s]);
        memcpy(space_seed[s], space_now[s], space_size[s]);
    }
    memcpy(records_seed, records_now, sizeof records_now);

    char line[4096];
    setvbuf(stdout, 0, _IOLBF, 0);
    while (fgets(line, sizeof line, stdin)) {
        char cmd[32], arg[4000] = "";
        unsigned a = 0, b = 0;
        if (sscanf(line, "%31s", cmd) != 1) continue;
        if (!strcmp(cmd, "seed") && sscanf(line, "%*s %u %u %3999s", &a, &b, arg) == 3) {
            unsigned len = (unsigned)strlen(arg) / 2;
            uint8_t *p = at((og_space)a, b, len, 0, camera_family), *q = at((og_space)a, b, len, 1, camera_family);
            if (!p) { printf("error\n"); continue; }
            hex_bytes(arg, p, len);
            memcpy(q, p, len);
            printf("ok\n");
        } else if (!strcmp(cmd, "corrupt") && sscanf(line, "%*s %u %u %3999s", &a, &b, arg) == 3) {
            unsigned len = (unsigned)strlen(arg) / 2;
            uint8_t *p = at((og_space)a, b, len, 0, camera_family);
            if (!p) { printf("error\n"); continue; }
            hex_bytes(arg, p, len);
            printf("ok\n");
        } else if (!strcmp(cmd, "reconcile") && sscanf(line, "%*s %u", &a) == 1) {
            reads = writes = 0;
            og_status s = og_reconcile((og_mode)a);
            printf("status %d reads %u writes %u\n", s, reads, writes);
        } else if (!strcmp(cmd, "diff")) {
            diff();
        } else if (!strcmp(cmd, "fail") && sscanf(line, "%*s %u %31s", &a, arg) == 2) {
            fail_at = a;
            fail_mode = !strcmp(arg, "before") ? FAIL_BEFORE : !strcmp(arg, "after") ? FAIL_AFTER : FAIL_ALL;
            printf("ok\n");
        } else if (!strcmp(cmd, "ready") && sscanf(line, "%*s %u", &a) == 1) {
            ready = a;
            printf("ok\n");
        } else if (!strcmp(cmd, "dial") && sscanf(line, "%*s %u", &a) == 1) {
            dial_value = a;
            printf("ok\n");
        } else if (!strcmp(cmd, "select") && sscanf(line, "%*s %u", &a) == 1) {
            reads = writes = 0;
            og_status s = og_select((og_mode)a);
            printf("status %d reads %u writes %u\n", s, reads, writes);
        } else if (!strcmp(cmd, "power_on")) {
            reads = writes = 0;
            og_power_on();
            printf("reads %u writes %u\n", reads, writes);
        } else if (!strcmp(cmd, "selected")) {
            printf("mode %d\n", og_selected());
        } else if (!strcmp(cmd, "load")) {
            printf("mode %d\n", og_record_load());
        } else {
            printf("error\n");
        }
    }
    return 0;
}
