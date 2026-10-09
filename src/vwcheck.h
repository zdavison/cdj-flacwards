/* The read pattern that the PC test and the console command share. Both
   print the same CRC-32 values, so the emulator result can be compared with
   the PC result. */
#pragma once
#include <stdint.h>

typedef struct {
    int32_t (*read)(void *user, void *buf, uint32_t n);    /* bytes, or -1 */
    int (*seek)(void *user, int32_t off, int whence);      /* 0, or -1 */
    uint32_t length;
    void *user;
    /* Optional: called with each block of bytes that was read, and its offset. */
    void (*seen)(void *ctx, uint32_t off, const uint8_t *data, uint32_t n);
    void *seen_ctx;
} vwcheck_file;

/* Read the file from the start in pieces of 1000 bytes. *err: 0 = ok. */
uint32_t vwcheck_seq(const vwcheck_file *f, int *err);
/* Make count jumps with whence 0, 1 and 2 in turn, and read 1 to 5000 bytes
   at each jump. The first jump crosses the header end. The second jump reads
   past the file end. *err: 0 = ok. */
uint32_t vwcheck_jumps(const vwcheck_file *f, uint32_t seed, uint32_t count, int *err);
