#include "putfmt.h"

char *put_str(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    return p;
}

char *put_hex(char *p, uint32_t v)
{
    static const char digits[] = "0123456789abcdef";
    for (int i = 7; i >= 0; i--)
        *p++ = digits[(v >> (i * 4)) & 0xF];
    return p;
}

char *put_dec(char *p, uint32_t v)
{
    char tmp[10];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        *p++ = tmp[--n];
    return p;
}

char *put_int(char *p, int32_t v)
{
    if (v < 0) {
        *p++ = '-';
        return put_dec(p, 0u - (uint32_t)v);
    }
    return put_dec(p, (uint32_t)v);
}
