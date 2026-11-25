/* [DEBUG-og2] The diagnostic trace (og_diag.h). Each line: "<n> <ms> <tag> <hex values>". The first
 * line of a process truncates the file and names the build. At most 800 lines per process. [PROBE-movie] The
 * previous process's trace is first renamed to og_diag.prev.txt, so a battery pull after a freeze keeps it. */
#include "og_diag.h"

#if !__STDC_HOSTED__
#include "og_sys.h"

#ifndef OG_DIAG_SETTING
#define OG_DIAG_SETTING "/setting/og_diag.txt"
#endif
#ifndef OG_DIAG_PREVIOUS
#define OG_DIAG_PREVIOUS "/setting/og_diag.prev.txt"
#endif
#ifdef OG_HOST   /* host check of the format: -ffreestanding -DOG_HOST */
#include <time.h>
#define OG_O_APPEND O_APPEND
#else
#define OG_O_APPEND 0x400L
#endif
#define MAX_LINES 800u

static uint32_t diag_lines, diag_started;

static void put_char(char *buf, unsigned *at, char ch)
{
    if (*at < 254u) buf[(*at)++] = ch;
}

static void put_text(char *buf, unsigned *at, const char *s)
{
    while (*s) put_char(buf, at, *s++);
}

static void put_hex(char *buf, unsigned *at, uint64_t v)
{
    char digits[16];
    unsigned n = 0;
    do {
        digits[n++] = "0123456789abcdef"[v & 15u];
        v >>= 4;
    } while (v && n < 16u);
    while (n) put_char(buf, at, digits[--n]);
}

static uint64_t now_ms(void)
{
#ifdef OG_HOST
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
#else
    struct { long sec, nsec; } ts = {0, 0};
    og_svc(113, 1, (long)&ts, 0, 0, 0, 0);   /* clock_gettime(CLOCK_MONOTONIC) */
    return (uint64_t)ts.sec * 1000u + (uint64_t)ts.nsec / 1000000u;
#endif
}

static void emit(const char *buf, unsigned n)
{
    static const char head[] = "og-diag 1 alpha-angle\n";
    long first = !__atomic_exchange_n(&diag_started, 1, __ATOMIC_ACQ_REL);
    if (first) (void)og_rename(OG_DIAG_SETTING, OG_DIAG_PREVIOUS);   /* [PROBE-movie] */
    long flags = OG_O_WRONLY | OG_O_CREAT | OG_O_CLOEXEC | (first ? OG_O_TRUNC : OG_O_APPEND);
    long fd = og_openat(OG_DIAG_SETTING, flags, 0644);
    if (fd < 0) return;
    if (first) og_write(fd, head, sizeof head - 1);
    og_write(fd, buf, n);
    og_close(fd);
}

/* "<n> <ms> <tag>" into buf; 0 once MAX_LINES are written. */
static int start_line(char *buf, unsigned *at, const char *tag)
{
    uint32_t line = __atomic_fetch_add(&diag_lines, 1, __ATOMIC_RELAXED);
    if (line >= MAX_LINES) return 0;
    put_hex(buf, at, line);
    put_char(buf, at, ' ');
    put_hex(buf, at, now_ms());
    put_char(buf, at, ' ');
    put_text(buf, at, tag);
    return 1;
}

void og_diag(const char *tag, uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    char buf[256];
    unsigned at = 0;
    if (!start_line(buf, &at, tag)) return;
    const uint64_t v[4] = {a, b, c, d};
    for (unsigned i = 0; i < 4; i++) {
        put_char(buf, &at, ' ');
        put_hex(buf, &at, v[i]);
    }
    put_char(buf, &at, '\n');
    emit(buf, at);
}

void og_diag_sync(void)
{
    long fd = og_openat(OG_DIAG_SETTING, OG_O_WRONLY | OG_O_CLOEXEC, 0);
    if (fd < 0) return;
    og_fsync(fd);
    og_close(fd);
}
#endif
