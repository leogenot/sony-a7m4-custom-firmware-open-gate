/* Private to native/open-gate: the few Linux calls the module makes. The module is freestanding (no libc), so on
 * the camera these are raw AArch64 system calls; the host test (OG_HOST) maps them to its own libc. Every call
 * returns a negative value on failure. */
#ifndef OG_SYS_H
#define OG_SYS_H

#include <stdint.h>

#ifdef OG_HOST
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#define OG_O_RDONLY O_RDONLY
#define OG_O_WRONLY O_WRONLY
#define OG_O_CREAT O_CREAT
#define OG_O_TRUNC O_TRUNC
#define OG_O_CLOEXEC O_CLOEXEC

static inline long og_openat(const char *path, long flags, long mode) { return openat(AT_FDCWD, path, (int)flags, (int)mode); }
static inline long og_close(long fd) { return close((int)fd); }
static inline long og_read(long fd, void *dst, unsigned long n) { return read((int)fd, dst, n); }
static inline long og_write(long fd, const void *src, unsigned long n) { return write((int)fd, src, n); }
static inline long og_fsync(long fd) { return fsync((int)fd); }
static inline long og_rename(const char *from, const char *to) { return renameat(AT_FDCWD, from, AT_FDCWD, to); }

#else

#define OG_AT_FDCWD (-100L)
#define OG_O_RDONLY 0x0L
#define OG_O_WRONLY 0x1L
#define OG_O_RDWR 0x2L
#define OG_O_CREAT 0x40L
#define OG_O_TRUNC 0x200L
#define OG_O_SYNC 0x101000L
#define OG_O_CLOEXEC 0x80000L
#define OG_PROT_RW 0x3L
#define OG_MAP_SHARED 0x1L

enum { SYS_RENAMEAT = 38, SYS_OPENAT = 56, SYS_CLOSE = 57, SYS_READ = 63, SYS_WRITE = 64, SYS_PREAD64 = 67,
       SYS_PWRITE64 = 68, SYS_FSYNC = 82, SYS_MUNMAP = 215, SYS_MMAP = 222 };

static inline long og_svc(long n, long a, long b, long c, long d, long e, long f)
{
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory", "cc");
    return x0;
}

static inline long og_openat(const char *path, long flags, long mode)
{
    return og_svc(SYS_OPENAT, OG_AT_FDCWD, (long)path, flags, mode, 0, 0);
}
static inline long og_close(long fd) { return og_svc(SYS_CLOSE, fd, 0, 0, 0, 0, 0); }
static inline long og_read(long fd, void *dst, unsigned long n) { return og_svc(SYS_READ, fd, (long)dst, (long)n, 0, 0, 0); }
static inline long og_write(long fd, const void *src, unsigned long n)
{
    return og_svc(SYS_WRITE, fd, (long)src, (long)n, 0, 0, 0);
}
static inline long og_pread(long fd, void *dst, unsigned long n, uint64_t at)
{
    return og_svc(SYS_PREAD64, fd, (long)dst, (long)n, (long)at, 0, 0);
}
static inline long og_pwrite(long fd, const void *src, unsigned long n, uint64_t at)
{
    return og_svc(SYS_PWRITE64, fd, (long)src, (long)n, (long)at, 0, 0);
}
static inline long og_fsync(long fd) { return og_svc(SYS_FSYNC, fd, 0, 0, 0, 0, 0); }
static inline long og_rename(const char *from, const char *to)
{
    return og_svc(SYS_RENAMEAT, OG_AT_FDCWD, (long)from, OG_AT_FDCWD, (long)to, 0, 0);
}
/* Returns the mapping, or 0 on failure (the kernel returns -4095..-1). */
static inline void *og_mmap(unsigned long len, long fd, uint64_t offset)
{
    long r = og_svc(SYS_MMAP, 0, (long)len, OG_PROT_RW, OG_MAP_SHARED, fd, (long)offset);
    return (unsigned long)r > (unsigned long)-4096L ? (void *)0 : (void *)r;
}
static inline void og_munmap(void *at, unsigned long len) { og_svc(SYS_MUNMAP, (long)at, (long)len, 0, 0, 0, 0); }

#endif
#endif
