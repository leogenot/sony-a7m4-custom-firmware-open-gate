/* The two library calls a freestanding compiler may still emit for struct copies and zeroing. The volatile
 * stores keep it from turning these loops back into calls to themselves. */
#include <stddef.h>

void *memset(void *dst, int value, size_t n)
{
    volatile unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)value;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    volatile unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}
