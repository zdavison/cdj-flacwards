#include <string.h>
#include "pool.h"

#define ALIGN 8u

void pool_init(pool_t *p, void *buf, size_t size)
{
    p->base = buf;
    p->size = size;
    p->used = 0;
    p->peak = 0;
    p->failed = 0;
}

/* Every block starts with its size, so realloc can copy the old contents. */
static void *pool_malloc(size_t n, void *user)
{
    pool_t *p = user;
    size_t need = ((n + ALIGN - 1) & ~(size_t)(ALIGN - 1)) + ALIGN;
    if (p->used + need > p->size) {
        p->failed = 1;
        return NULL;
    }
    unsigned char *block = p->base + p->used;
    *(size_t *)block = n;
    p->used += need;
    if (p->used > p->peak)
        p->peak = p->used;
    return block + ALIGN;
}

static void *pool_realloc(void *old, size_t n, void *user)
{
    if (old == NULL)
        return pool_malloc(n, user);
    size_t old_n = *(size_t *)((unsigned char *)old - ALIGN);
    if (n <= old_n)
        return old;
    void *fresh = pool_malloc(n, user);
    if (fresh != NULL)
        memcpy(fresh, old, old_n);
    return fresh;
}

static void pool_free(void *ptr, void *user)
{
    (void)ptr;
    (void)user;
}

drflac_allocation_callbacks pool_callbacks(pool_t *p)
{
    drflac_allocation_callbacks cb = { p, pool_malloc, pool_realloc, pool_free };
    return cb;
}
