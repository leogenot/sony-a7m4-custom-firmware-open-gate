/* Owns the selected mode: the live copy and /setting/open_gate.dat (16 bytes, written to a .tmp file, synced and
 * renamed over the record, like custom_aspect_v2.dat).
 *
 * og_power_on runs on every shooting-screen creation. It applies nothing with the dial on movie (the kit applies on
 * stills only; the next creation retries), and costs nothing while the mode is Off and nothing was applied in this
 * process. Any other failure (coprocessor not ready, I/O) is simply retried on the next creation.
 */
#include "og_internal.h"
#include "og_sys.h"
#include "og_config.h"
#include "og_diag.h"   /* [DEBUG-og2] */

#ifndef OG_SETTING_DIR
#define OG_SETTING_DIR "/setting"
#endif

#define OG_RECORD_MAGIC 0x3154474fu   /* bytes 'O' 'G' 'T' '1' */
#define OG_RECORD_SCHEMA 1u

extern const volatile uint32_t *native_mode(void);   /* stock getter (layout.json): OG_DIAL_MOVIE in movie */

static const char record_path[] = OG_SETTING_DIR "/open_gate.dat";
static const char temporary_path[] = OG_SETTING_DIR "/open_gate.tmp";
static const char directory_path[] = OG_SETTING_DIR;

enum { UNLOADED = 0, LOADING = 1, LOADED = 2 };
static uint32_t selected;   /* og_mode, valid once record_state == LOADED */
static uint32_t record_state;
static uint32_t busy;
static uint32_t applied;    /* a reconcile to an open-gate mode ran in this process */

og_mode og_record_load(void)
{
    og_record r;
    uint8_t extra;
    long fd = og_openat(record_path, OG_O_RDONLY | OG_O_CLOEXEC, 0);
    if (fd < 0) return OG_OFF;
    long n = og_read(fd, &r, sizeof r), more = og_read(fd, &extra, 1), closed = og_close(fd);
    if (n == (long)sizeof r && more == 0 && closed == 0 && r.magic == OG_RECORD_MAGIC && r.schema == OG_RECORD_SCHEMA &&
        r.mode < OG_MODE_COUNT && r.check == ~r.mode)
        return (og_mode)r.mode;
    return OG_OFF;
}

og_status og_record_save(og_mode mode)
{
    og_record r = {OG_RECORD_MAGIC, OG_RECORD_SCHEMA, (uint32_t)mode, ~(uint32_t)mode};
    long fd = og_openat(temporary_path, OG_O_WRONLY | OG_O_CREAT | OG_O_TRUNC | OG_O_CLOEXEC, 0600);
    if (fd < 0) { og_diag("save.open", (uint64_t)fd, 0, 0, 0); return OG_NOT_SAVED; }   /* [DEBUG-og2] */
    long n = og_write(fd, &r, sizeof r), synced = og_fsync(fd), closed = og_close(fd);
    long renamed = n == (long)sizeof r && !synced && !closed ? og_rename(temporary_path, record_path) : -1;
    if (n != (long)sizeof r || synced || closed || renamed) {
        og_diag("save.write", (uint64_t)n, (uint64_t)synced, (uint64_t)closed, (uint64_t)renamed);   /* [DEBUG-og2] */
        return OG_NOT_SAVED;
    }
    if ((fd = og_openat(directory_path, OG_O_RDONLY | OG_O_CLOEXEC, 0)) < 0) {
        og_diag("save.dir", (uint64_t)fd, 0, 0, 0);   /* [DEBUG-og2] */
        return OG_NOT_SAVED;
    }
    synced = og_fsync(fd);
    closed = og_close(fd);
    if (synced || closed) og_diag("save.dirsync", (uint64_t)synced, (uint64_t)closed, 0, 0);   /* [DEBUG-og2] */
    return synced || closed ? OG_NOT_SAVED : OG_OK;
}

/* Load the record once per process; a concurrent caller sees Off until it is loaded. */
static og_mode load_once(void)
{
    uint32_t expected = UNLOADED;
    if (__atomic_compare_exchange_n(&record_state, &expected, LOADING, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&selected, (uint32_t)og_record_load(), __ATOMIC_RELAXED);
        __atomic_store_n(&record_state, LOADED, __ATOMIC_RELEASE);
    }
    return __atomic_load_n(&record_state, __ATOMIC_ACQUIRE) == LOADED ? (og_mode)__atomic_load_n(&selected, __ATOMIC_RELAXED)
                                                               : OG_OFF;
}

static int in_movie(void)
{
    const volatile uint32_t *dial = native_mode();
    return dial && *dial == OG_DIAL_MOVIE;
}

static og_status reconcile(og_mode mode)
{
    if (mode != OG_OFF) __atomic_store_n(&applied, 1, __ATOMIC_RELAXED);
    return og_reconcile(mode);
}

void og_power_on(void)
{
    const volatile uint32_t *dial = native_mode();   /* [DEBUG-og2] */
    og_diag("power_on", dial ? *dial : 0xffffffffu, (uint64_t)(uintptr_t)dial, __atomic_load_n(&busy, __ATOMIC_RELAXED),
            __atomic_load_n(&record_state, __ATOMIC_RELAXED));
    if (in_movie() || __atomic_exchange_n(&busy, 1, __ATOMIC_ACQUIRE)) return;
    og_mode mode = load_once();
    og_diag("power_on.mode", mode, __atomic_load_n(&applied, __ATOMIC_RELAXED), 0, 0);   /* [DEBUG-og2] */
    if (mode != OG_OFF || __atomic_load_n(&applied, __ATOMIC_RELAXED)) {
        og_status s = reconcile(mode);
        og_diag("power_on.reconcile", mode, s, 0, 0);   /* [DEBUG-og2] */
        og_diag_sync();
    }
    __atomic_store_n(&busy, 0, __ATOMIC_RELEASE);
}

og_status og_select(og_mode mode)
{
    const volatile uint32_t *dial = native_mode();   /* [DEBUG-og2] */
    og_diag("select", mode, dial ? *dial : 0xffffffffu, __atomic_load_n(&busy, __ATOMIC_RELAXED),
            __atomic_load_n(&record_state, __ATOMIC_RELAXED));
    if ((unsigned)mode >= OG_MODE_COUNT) return OG_FOREIGN;
    if (in_movie()) return OG_WRONG_DIAL;
    if (__atomic_exchange_n(&busy, 1, __ATOMIC_ACQUIRE)) return OG_BUSY;
    og_mode before = load_once();
    og_status s = reconcile(mode);
    og_diag("select.reconcile", mode, before, s, 0);   /* [DEBUG-og2] */
    if (s == OG_OK) {
        og_status saved = og_record_save(mode);
        og_diag("select.save", mode, saved, 0, 0);   /* [DEBUG-og2] */
        if (saved != OG_OK) s = OG_NOT_SAVED;
        __atomic_store_n(&selected, (uint32_t)mode, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&busy, 0, __ATOMIC_RELEASE);
    return s;
}

og_mode og_selected(void) { return load_once(); }
