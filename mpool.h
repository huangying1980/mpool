/* huangying */
#ifndef MPOOL_H
#define MPOOL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mpool;

#ifdef MPOOL_MT
void* mpool_alloc_mt(size_t size);
void mpool_free_mt(void* ptr);
void* mpool_calloc_mt(size_t size);
void* mpool_alloc_align_mt(size_t size, size_t align);
void* mpool_calloc_align_mt(size_t size, size_t align);
void mpool_cleanup_mt(void);
#endif

struct mpool* mpool_create(size_t size);
void  mpool_release(struct mpool* pool);
void* mpool_alloc(struct mpool* pool, size_t size);
void mpool_free(struct mpool* pool, void* ptr);
void* mpool_calloc(struct mpool* pool, size_t size);
void* mpool_alloc_align(struct mpool* pool, size_t size,
    size_t align);
void* mpool_calloc_align(struct mpool* pool, size_t size,
    size_t align);

#ifdef __cplusplus
}
#endif

#endif
