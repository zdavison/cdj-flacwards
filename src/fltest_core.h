/* Decode a whole FLAC file from memory and checksum the PCM output.
   Shared by the CDJ-900 console command and the host reference test. */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int status;             /* 0 = ok, 1 = open failed, 2 = short decode */
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits;
    uint32_t frames_total;  /* from STREAMINFO (low 32 bits) */
    uint32_t frames_read;
    uint32_t crc;           /* CRC-32 over the int32 samples as little-endian bytes */
    uint32_t pool_peak;     /* bytes of pool memory used at most */
} fltest_result;

void fltest_run(const void *data, size_t size, void *pool_buf, size_t pool_size,
                fltest_result *out);
