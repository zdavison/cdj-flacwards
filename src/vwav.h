/* A WAV view of one FLAC file: a 44-byte RIFF header, then the decoded PCM
   (little-endian, interleaved). The firmware's WAV code reads this view as
   if it were the file. vwav has no firmware dependencies. */
#pragma once
#include <stdint.h>
#include "pool.h"

#define VWAV_HEADER   44
#define VWAV_CHANNELS 2
#define VWAV_CHUNK    256              /* PCM frames that one decode step gives */
#define VWAV_MEM      (64 * 1024)      /* dr_flac memory for one file */
#define VWAV_LOST     0xffffffffu      /* the decoder position is not known */
#define VWAV_RESUME_TRIES 8            /* blocks to try past damage before giving up */

enum { VWAV_E_IO = -1, VWAV_E_FORMAT = -2, VWAV_E_SCOPE = -3 };

typedef struct {
    /* Read up to n bytes of the real file. Return the count, or -1 on error. */
    int32_t (*read)(void *user, void *buf, uint32_t n);
    /* Go to byte pos of the real file. Return 0, or -1 on error. */
    int32_t (*seek)(void *user, uint32_t pos);
    void *user;
} vwav_io;

typedef struct {
    vwav_io io;
    uint32_t file_pos;          /* position in the real file */
    int io_error;               /* 1 if io.read failed since the last check */
    uint32_t file_bytes;        /* bytes read from the real file (statistics) */
    uint32_t seeks;             /* dr_flac seeks (statistics) */
    pool_t pool;
    drflac *flac;               /* NULL if the file is not open */
    uint32_t bytes_per_sample;  /* 2 or 3 */
    uint32_t block_align;       /* bytes in one PCM frame */
    uint32_t bytes_per_sec;     /* PCM bytes per second of audio */
    uint32_t total_frames;
    uint32_t length;            /* length of the virtual file */
    uint32_t pos;               /* position in the virtual file */
    uint32_t next_frame;        /* the frame that the decoder gives next, or VWAV_LOST */
    uint32_t dead_from;         /* frames from here on do not decode */
    uint32_t gap_from;          /* damaged frames [gap_from, gap_to) are silence */
    uint32_t gap_to;
    uint32_t chunk_frame;       /* first frame in chunk */
    uint32_t chunk_frames;      /* frames in chunk; 0 = empty */
    uint8_t header[VWAV_HEADER];
    drflac_int32 s32[VWAV_CHUNK * VWAV_CHANNELS];
    uint8_t chunk[VWAV_CHUNK * VWAV_CHANNELS * 3];
} vwav;

/* Open the FLAC file that io reads. mem holds the dr_flac state.
   Return 0, VWAV_E_IO, VWAV_E_FORMAT (not FLAC) or VWAV_E_SCOPE. */
int vwav_open(vwav *v, const vwav_io *io, void *mem, uint32_t mem_size);
/* Read up to n bytes at the current position. Return the count, or -1. */
int32_t vwav_read(vwav *v, void *buf, uint32_t n);
/* whence: 0 = start, 1 = current position, 2 = end. Return 0, or -1. */
int vwav_seek(vwav *v, int32_t off, int whence);
uint32_t vwav_tell(const vwav *v);
uint32_t vwav_length(const vwav *v);
int vwav_eof(const vwav *v);
void vwav_close(vwav *v);
