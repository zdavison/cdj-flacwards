/* A WAV view of one FLAC file. See vwav.h. */
#include <string.h>
#include "vwav.h"

#define MAX_LENGTH 0x7fffffffu

static uint32_t min32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v);
    put16(p + 2, v >> 16);
}

static size_t on_read(void *user, void *buf, size_t n)
{
    vwav *v = user;
    int32_t got = v->io.read(v->io.user, buf, (uint32_t)n);
    if (got < 0) {
        v->io_error = 1;
        return 0;
    }
    v->file_pos += (uint32_t)got;
    v->file_bytes += (uint32_t)got;
    return (size_t)got;
}

static drflac_bool32 on_seek(void *user, int offset, drflac_seek_origin origin)
{
    vwav *v = user;
    int64_t target;
    if (origin == DRFLAC_SEEK_SET)
        target = offset;
    else if (origin == DRFLAC_SEEK_CUR)
        target = (int64_t)v->file_pos + offset;
    else
        return DRFLAC_FALSE;
    if (target < 0 || target > (int64_t)0xffffffffu)
        return DRFLAC_FALSE;
    if (v->io.seek(v->io.user, (uint32_t)target) != 0)
        return DRFLAC_FALSE;
    v->file_pos = (uint32_t)target;
    return DRFLAC_TRUE;
}

static drflac_bool32 on_tell(void *user, drflac_int64 *cursor)
{
    *cursor = ((vwav *)user)->file_pos;
    return DRFLAC_TRUE;
}

static void make_header(vwav *v, uint32_t rate, uint32_t bits)
{
    uint8_t *h = v->header;
    memcpy(h, "RIFF", 4);
    put32(h + 4, v->length - 8);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    put16(h + 20, 1);                   /* WAVE_FORMAT_PCM */
    put16(h + 22, VWAV_CHANNELS);
    put32(h + 24, rate);
    put32(h + 28, rate * v->block_align);
    put16(h + 32, v->block_align);
    put16(h + 34, bits);
    memcpy(h + 36, "data", 4);
    put32(h + 40, v->length - VWAV_HEADER);
}

int vwav_open(vwav *v, const vwav_io *io, void *mem, uint32_t mem_size)
{
    uint8_t magic[4];
    int32_t got;

    memset(v, 0, sizeof *v);
    v->io = *io;
    if (io->seek(io->user, 0) != 0)
        return VWAV_E_IO;
    got = io->read(io->user, magic, 4);
    if (got < 0)
        return VWAV_E_IO;
    if (got != 4 || memcmp(magic, "fLaC", 4) != 0)
        return VWAV_E_FORMAT;
    if (io->seek(io->user, 0) != 0)
        return VWAV_E_IO;

    pool_init(&v->pool, mem, mem_size);
    drflac_allocation_callbacks cb = pool_callbacks(&v->pool);
    v->flac = drflac_open(on_read, on_seek, on_tell, v, &cb);
    if (v->flac == NULL)
        return v->io_error ? VWAV_E_IO : VWAV_E_FORMAT;

    drflac *f = v->flac;
    uint32_t bits = f->bitsPerSample;
    /* A brute force seek decodes from the start of the stream: megabytes of
       reads in one call. The binary search does not need it (see
       third_party/README), and a damaged frame must not cause it. */
    f->_noBruteForceSeek = DRFLAC_TRUE;
    if ((f->sampleRate != 44100 && f->sampleRate != 48000) || f->channels != VWAV_CHANNELS
        || (bits != 16 && bits != 24) || f->totalPCMFrameCount == 0) {
        vwav_close(v);
        return VWAV_E_SCOPE;
    }
    v->bytes_per_sample = bits / 8;
    v->block_align = VWAV_CHANNELS * v->bytes_per_sample;
    if (f->totalPCMFrameCount > (MAX_LENGTH - VWAV_HEADER) / v->block_align) {
        vwav_close(v);
        return VWAV_E_SCOPE;
    }
    v->total_frames = (uint32_t)f->totalPCMFrameCount;
    v->length = VWAV_HEADER + v->total_frames * v->block_align;
    v->dead_from = v->total_frames;
    v->bytes_per_sec = f->sampleRate * v->block_align;
    make_header(v, f->sampleRate, bits);
    return 0;
}

/* dr_flac gives samples left-aligned in 32 bits. Keep the top 16 or 24 bits.
   One loop for each sample size, so that no loop tests the size. */
static void convert(vwav *v, uint32_t frames)
{
    const uint32_t n = frames * VWAV_CHANNELS;
    const drflac_int32 *s = v->s32;
    uint8_t *d = v->chunk;
    if (v->bytes_per_sample == 2) {
        for (uint32_t i = 0; i < n; i++) {
            uint32_t x = (uint32_t)s[i];
            d[0] = (uint8_t)(x >> 16);
            d[1] = (uint8_t)(x >> 24);
            d += 2;
        }
    } else {
        for (uint32_t i = 0; i < n; i++) {
            uint32_t x = (uint32_t)s[i];
            d[0] = (uint8_t)(x >> 8);
            d[1] = (uint8_t)(x >> 16);
            d[2] = (uint8_t)(x >> 24);
            d += 3;
        }
    }
}

/* Seek the decoder to frame. If dr_flac cannot reach it (damaged data), try
   up to VWAV_RESUME_TRIES positions, each one block further on. Return the
   frame where the decoder is now, or VWAV_LOST. */
static uint32_t seek_or_skip(vwav *v, uint32_t frame)
{
    uint32_t step = v->flac->maxBlockSizeInPCMFrames ? v->flac->maxBlockSizeInPCMFrames : 4096;
    for (uint32_t k = 0; k <= VWAV_RESUME_TRIES; k++) {
        uint32_t f = frame + k * step;
        if (f >= v->total_frames)
            break;
        if (drflac_seek_to_pcm_frame(v->flac, f))
            return f;
        if (v->io_error)
            break;
    }
    return VWAV_LOST;
}

/* Decode the chunk that starts at frame. Frames that do not decode become
   silence, and decoding goes on after the damage. Return 0, or -1 if the real
   file gave a read error. */
static int fill(vwav *v, uint32_t frame)
{
    uint32_t want = min32(VWAV_CHUNK, v->total_frames - frame);
    uint32_t got = 0;

    v->io_error = 0;
    if (frame >= v->gap_from && frame < v->gap_to) {
        want = min32(want, v->gap_to - frame);
        goto silence;
    }
    if (frame < v->dead_from) {
        if (frame != v->next_frame) {
            v->seeks++;
            uint32_t at = seek_or_skip(v, frame);
            if (v->io_error)
                return -1;
            if (at == VWAV_LOST) {
                /* Nothing decodes from frame on (for example a cut file). */
                v->dead_from = frame;
                v->next_frame = VWAV_LOST;
                goto silence;
            }
            v->next_frame = at;
            if (at != frame) {
                v->gap_from = frame;
                v->gap_to = at;
                want = min32(want, at - frame);
                goto silence;
            }
        }
        got = (uint32_t)drflac_read_pcm_frames_s32(v->flac, want, v->s32);
        if (v->io_error) {
            v->next_frame = VWAV_LOST;
            return -1;
        }
        v->next_frame = got == want ? frame + got : VWAV_LOST;
    }
silence:
    memset(v->s32 + got * VWAV_CHANNELS, 0, (want - got) * VWAV_CHANNELS * sizeof v->s32[0]);
    convert(v, want);
    v->chunk_frame = frame;
    v->chunk_frames = want;
    return 0;
}

int32_t vwav_read(vwav *v, void *buf, uint32_t n)
{
    uint8_t *out = buf;
    uint32_t done = 0;

    if (v->flac == NULL)
        return -1;
    n = min32(n, v->length - v->pos);
    while (done < n) {
        uint32_t k;
        if (v->pos < VWAV_HEADER) {
            k = min32(VWAV_HEADER - v->pos, n - done);
            memcpy(out + done, v->header + v->pos, k);
        } else {
            uint32_t byte = v->pos - VWAV_HEADER;
            uint32_t frame = byte / v->block_align;
            if (v->chunk_frames == 0 || frame < v->chunk_frame
                || frame >= v->chunk_frame + v->chunk_frames) {
                if (fill(v, frame) != 0)
                    break;
            }
            uint32_t start = byte - v->chunk_frame * v->block_align;
            k = min32(v->chunk_frames * v->block_align - start, n - done);
            memcpy(out + done, v->chunk + start, k);
        }
        v->pos += k;
        done += k;
    }
    return done > 0 || n == 0 ? (int32_t)done : -1;
}

int vwav_seek(vwav *v, int32_t off, int whence)
{
    int64_t base;
    if (whence == 0)
        base = 0;
    else if (whence == 1)
        base = v->pos;
    else if (whence == 2)
        base = v->length;
    else
        return -1;
    int64_t target = base + off;
    if (target < 0 || target > (int64_t)v->length)
        return -1;
    v->pos = (uint32_t)target;
    return 0;
}

uint32_t vwav_tell(const vwav *v)
{
    return v->pos;
}

uint32_t vwav_length(const vwav *v)
{
    return v->length;
}

int vwav_eof(const vwav *v)
{
    return v->pos >= v->length;
}

void vwav_close(vwav *v)
{
    if (v->flac != NULL)
        drflac_close(v->flac);
    v->flac = NULL;
}
