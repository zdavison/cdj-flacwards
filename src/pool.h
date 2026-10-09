/* A bump allocator over a fixed buffer. dr_flac allocates a few blocks when a
   stream opens and frees them when it closes, so a reset per stream is enough. */
#pragma once
#include <stddef.h>
#include "../third_party/dr_flac.h"

typedef struct {
    unsigned char *base;
    size_t size;
    size_t used;
    size_t peak;
    int failed;
} pool_t;

void pool_init(pool_t *p, void *buf, size_t size);
drflac_allocation_callbacks pool_callbacks(pool_t *p);
