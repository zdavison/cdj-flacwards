#include "crc32.h"

static uint32_t table[256];
static int table_ready;

uint32_t crc32_begin(void)
{
    if (!table_ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        table_ready = 1;
    }
    return 0xFFFFFFFFu;
}

uint32_t crc32_update(uint32_t crc, const void *data, uint32_t n)
{
    const uint8_t *p = data;
    while (n--)
        crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return crc;
}

uint32_t crc32_end(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFu;
}
