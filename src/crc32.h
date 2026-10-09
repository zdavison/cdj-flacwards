/* CRC-32 with the zlib polynomial, for the PC tests and the console command. */
#pragma once
#include <stdint.h>

uint32_t crc32_begin(void);
uint32_t crc32_update(uint32_t crc, const void *data, uint32_t n);
uint32_t crc32_end(uint32_t crc);
