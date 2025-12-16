#include <stdio.h>
#include "mpool.h"


int main(void)
{
    struct mpool*   pool;
    size_t          pool_size = 1 << 20;
    void*           ptr;

    pool = mpool_create(pool_size);
    if (!pool) {
        fprintf(stderr, "mpool_create failed for size %lu\n", pool_size);
    }

    ptr = mpool_alloc(pool, 2 << 20);
    if (!ptr) {
        fprintf(stderr, "mpool_alloc failed\n");
        return -1;
    }
    fprintf(stderr, "ptr %p\n", ptr);
    mpool_free(pool, ptr);

    ptr = mpool_alloc(pool, 10 << 10);
    if (!ptr) {
        fprintf(stderr, "mpool_alloc failed\n");
        return -1;
    }
    fprintf(stderr, "ptr %p\n", ptr);
    mpool_free(pool, ptr);
    
    ptr = mpool_alloc_align(pool, 10 << 11, 64);
    if (!ptr) {
        fprintf(stderr, "mpool_alloc_align failed\n");
        return -1;
    }
    fprintf(stderr, "ptr %p\n", ptr);
    mpool_free(pool, ptr);
    mpool_release(pool);

    return 0;
}
