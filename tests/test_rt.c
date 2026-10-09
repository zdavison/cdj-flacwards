/* PC test for the target's own memcpy and memset (src/rt.c), under other
   names so that the host libc is not used. */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define memcpy rt_memcpy
#define memmove rt_memmove
#define memset rt_memset
#define memcmp rt_memcmp
#define strlen rt_strlen
#include "../src/rt.c"
#undef memcpy
#undef memset

static int failures;

int main(void)
{
    static uint8_t src[2100], dst[2100], want[2100];
    static const size_t lens[] = { 0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65, 70, 1000, 2047 };
    for (size_t i = 0; i < sizeof src; i++)
        src[i] = (uint8_t)(i * 7 + 3);
    for (int so = 0; so < 8; so++)
        for (int doff = 0; doff < 8; doff++)
            for (size_t li = 0; li < sizeof lens / sizeof lens[0]; li++) {
                size_t n = lens[li];
                memset(dst, 0xAA, sizeof dst);
                memset(want, 0xAA, sizeof want);
                for (size_t i = 0; i < n; i++)
                    want[doff + i] = src[so + i];
                void *r = rt_memcpy(dst + doff, src + so, n);
                if (r != dst + doff || memcmp(dst, want, sizeof dst) != 0) {
                    printf("FAIL memcpy src+%d dst+%d n=%zu\n", so, doff, n);
                    failures++;
                }
                memset(want, 0xAA, sizeof want);
                memset(want + doff, 0x5C, n);
                memset(dst, 0xAA, sizeof dst);
                r = rt_memset(dst + doff, 0x5C, n);
                if (r != dst + doff || memcmp(dst, want, sizeof dst) != 0) {
                    printf("FAIL memset dst+%d n=%zu\n", doff, n);
                    failures++;
                }
            }
    printf("%d failures\n", failures);
    return failures != 0;
}
