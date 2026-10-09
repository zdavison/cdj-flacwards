#include "fltest_core.h"
#include "pool.h"
#include "../third_party/dr_flac.h"

#define CHUNK_FRAMES 512

static uint32_t crc_table[256];

static void crc_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[i] = c;
    }
}

static uint32_t crc_update(uint32_t crc, const drflac_int32 *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        uint32_t v = (uint32_t)s[i];
        for (int b = 0; b < 4; b++) {
            crc = crc_table[(crc ^ v) & 0xFF] ^ (crc >> 8);
            v >>= 8;
        }
    }
    return crc;
}

void fltest_run(const void *data, size_t size, void *pool_buf, size_t pool_size,
                fltest_result *out)
{
    static drflac_int32 pcm[CHUNK_FRAMES * 8];
    pool_t pool;
    pool_init(&pool, pool_buf, pool_size);
    drflac_allocation_callbacks cb = pool_callbacks(&pool);

    *out = (fltest_result){0};
    crc_init();
    drflac *f = drflac_open_memory(data, size, &cb);
    if (f == NULL) {
        out->status = 1;
        out->pool_peak = (uint32_t)pool.peak;
        return;
    }
    out->sample_rate = f->sampleRate;
    out->channels = f->channels;
    out->bits = f->bitsPerSample;
    out->frames_total = (uint32_t)f->totalPCMFrameCount;

    uint32_t crc = 0xFFFFFFFFu;
    drflac_uint64 got;
    while ((got = drflac_read_pcm_frames_s32(f, CHUNK_FRAMES, pcm)) > 0) {
        crc = crc_update(crc, pcm, (size_t)got * f->channels);
        out->frames_read += (uint32_t)got;
    }
    out->crc = crc ^ 0xFFFFFFFFu;
    out->status = out->frames_read == out->frames_total ? 0 : 2;
    drflac_close(f);
    out->pool_peak = (uint32_t)pool.peak;
}
