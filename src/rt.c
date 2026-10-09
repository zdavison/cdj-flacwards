/* The C runtime pieces the compiler and dr_flac need. No libc on the target. */
#include <stddef.h>
#include <stdint.h>

/* 32-bit words that may alias any type. */
typedef uint32_t __attribute__((may_alias)) word_t;

/* The loops must stay loops: GCC must not turn them into a memcpy call. */
#define NO_LIBCALL __attribute__((optimize("no-tree-loop-distribute-patterns")))

NO_LIBCALL void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if ((((uintptr_t)d | (uintptr_t)s) & 3) == 0) {
        word_t *dw = (word_t *)d;
        const word_t *sw = (const word_t *)s;
        while (n >= 16) {
            dw[0] = sw[0];
            dw[1] = sw[1];
            dw[2] = sw[2];
            dw[3] = sw[3];
            dw += 4;
            sw += 4;
            n -= 16;
        }
        while (n >= 4) {
            *dw++ = *sw++;
            n -= 4;
        }
        d = (unsigned char *)dw;
        s = (const unsigned char *)sw;
    }
    while (n--)
        *d++ = *s++;
    return dst;
}

NO_LIBCALL void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        while (n--)
            d[n] = s[n];
    }
    return dst;
}

NO_LIBCALL void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    while (n && ((uintptr_t)d & 3)) {
        *d++ = (unsigned char)c;
        n--;
    }
    word_t w = (unsigned char)c * 0x01010101u;
    word_t *dw = (word_t *)d;
    while (n >= 4) {
        *dw++ = w;
        n -= 4;
    }
    d = (unsigned char *)dw;
    while (n--)
        *d++ = (unsigned char)c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y)
            return *x - *y;
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}
