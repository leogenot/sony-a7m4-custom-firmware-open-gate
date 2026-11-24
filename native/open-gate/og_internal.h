/* Internal to native/open-gate: never included outside this directory. */
#ifndef OG_INTERNAL_H
#define OG_INTERNAL_H

#include <stdint.h>
#include "og.h"

/* Memory targets behind one read/write pair. */
typedef enum {
    OG_CP = 0,            /* coprocessor RAM, CP virtual address (dmm_alloc + /dev/mem + sen_dma_transfer) */
    OG_CP_DRIVE_RECORD,   /* offset inside the stored drive-parameter record (root 0x12b9cc0 chain) */
    OG_AVCAM,             /* av-cam.bin image offset, through /dev/mem at its physical copy */
    OG_MPR,               /* libmpr.so file offset, in this process through /proc/self/mem */
    OG_SPACE_COUNT
} og_space;

typedef enum { OG_FAMILY_NTSC = 0, OG_FAMILY_PAL = 1, OG_FAMILY_COUNT } og_family;

/* Generated site image (a7m4/open_gate_sites.py SiteImage.encode), linked in with .incbin. */
typedef struct {
    uint32_t magic;          /* 'OGSI' */
    uint16_t version;
    uint16_t header_bytes;
    uint32_t total_bytes;
    uint32_t crc32;          /* over everything after this header */
    uint16_t preset_count;   /* (mode, family) pairs, OG_OFF excluded */
    uint16_t reserved;
    struct {
        uint32_t offset;     /* first og_run, from the image start */
        uint32_t bytes;
        uint16_t runs;
        uint8_t mode;        /* og_mode */
        uint8_t family;      /* og_family */
    } preset[4];
} og_image_header;

/* One contiguous site; followed by old[len] then new[len]. */
typedef struct {
    uint8_t space;           /* og_space */
    uint8_t flags;
    uint16_t len;
    uint32_t addr;
} og_run;

/* /setting/open_gate.dat, 16 bytes little-endian. */
typedef struct {
    uint32_t magic;          /* 'OGT1' */
    uint32_t schema;         /* 1 */
    uint32_t mode;           /* og_mode */
    uint32_t check;          /* ~mode */
} og_record;

typedef struct og_mem og_mem;

/* Undo every preset, check all sites are stock, apply target, read back; roll back on any error. */
og_status og_reconcile(og_mode target);

/* Opens every target once; finds the family from the stock bytes and the stored record via its pointer chain. */
og_status og_mem_begin(og_mem **out, og_family *family);
og_status og_mem_read(og_mem *mem, og_space space, uint32_t addr, void *dst, uint32_t len);
og_status og_mem_write(og_mem *mem, og_space space, uint32_t addr, const void *src, uint32_t len);
void og_mem_end(og_mem *mem);

og_mode og_record_load(void);
og_status og_record_save(og_mode mode);

extern const uint8_t og_sites[];      /* og_sites.S: .incbin "og_sites.bin" */

#endif
