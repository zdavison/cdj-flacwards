#include "vwcheck.h"
#include "crc32.h"

#define PIECE    1000
#define MAX_READ 5000

static uint8_t buf[MAX_READ];

static uint32_t next(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

uint32_t vwcheck_seq(const vwcheck_file *f, int *err)
{
    uint32_t crc = crc32_begin();
    uint32_t off = 0;

    *err = 0;
    if (f->seek(f->user, 0, 0) != 0) {
        *err = 1;
        return 0;
    }
    for (;;) {
        int32_t got = f->read(f->user, buf, PIECE);
        if (got < 0) {
            *err = 2;
            break;
        }
        if (got == 0)
            break;
        crc = crc32_update(crc, buf, (uint32_t)got);
        if (f->seen)
            f->seen(f->seen_ctx, off, buf, (uint32_t)got);
        off += (uint32_t)got;
        if (got < PIECE)
            break;
    }
    if (*err == 0 && off != f->length)
        *err = 3;
    return crc32_end(crc);
}

uint32_t vwcheck_jumps(const vwcheck_file *f, uint32_t seed, uint32_t count, int *err)
{
    uint32_t crc = crc32_begin();
    uint32_t cur = 0;

    *err = 0;
    seed |= 1;
    if (f->seek(f->user, 0, 0) != 0) {
        *err = 1;
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = next(&seed) % (f->length + 1);
        uint32_t len = next(&seed) % MAX_READ + 1;
        int r;
        if (i == 0)
            off = 40;
        if (i == 1 && f->length > 7)
            off = f->length - 7;
        if (i % 3 == 0)
            r = f->seek(f->user, (int32_t)off, 0);
        else if (i % 3 == 1)
            r = f->seek(f->user, (int32_t)(off - cur), 1);
        else
            r = f->seek(f->user, (int32_t)(off - f->length), 2);
        if (r != 0) {
            *err = 4;
            break;
        }
        uint32_t want = len < f->length - off ? len : f->length - off;
        int32_t got = f->read(f->user, buf, len);
        if (got != (int32_t)want) {
            *err = 5;
            break;
        }
        crc = crc32_update(crc, buf, want);
        if (f->seen)
            f->seen(f->seen_ctx, off, buf, want);
        cur = off + want;
    }
    return crc32_end(crc);
}
