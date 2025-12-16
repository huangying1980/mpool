/* huangying */
#include "mpool.h"

#include <sys/mman.h>
#include <sys/statfs.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GCC_VER (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)

#if defined MPOOL_DEBUG
#   define MDBG(format, args...) \
do {\
    fprintf(stderr, "[MDBG] ");  \
    fprintf(stderr, format, ##args); \
    fprintf(stderr, "\n");\
} while (0)

#elif defined MPOOL_DEBUG_VERBOSE
#   define MDBG(format, args...) \
do {\
    fprintf(stderr, "[%s:%d %s MDBG] ", __FILE__, __LINE__, __func__);\
    fprintf(stderr, format, ##args);\
    fprintf(stderr, "\n");\
} while (0)
#else
#   define MDBG(format, args...) do{}while(0)
#endif

#ifdef MPOOL_ERROR_VERBOSE
#   define MERR(format, args...) \
do {\
    fprintf(stderr, "[%s:%d %s MERR] ", __FILE__, __LINE__, __func__);\
    fprintf(stderr, format, ##args);\
    fprintf(stderr, "\n");\
} while (0)

#   define MWARN(format, args...) \
do {\
    fprintf(stderr, "[%s:%d %s MWARN] ", __FILE__, __LINE__, __func__);\
    fprintf(stderr, format, ##args);\
    fprintf(stderr, "\n");\
} while (0)

#   define MINFO(format, args...) \
do {\
    fprintf(stderr, "[%s:%d %s MINFO] ", __FILE__, __LINE__, __func__);\
    fprintf(stderr, format, ##args);\
    fprintf(stderr, "\n");\
} while (0)

#   define MPANIC(format, args...) \
fprintf(stderr, "[%s:%d %s MOOPS] ", __FILE__, __LINE__, __func__); \
fprintf(stderr, format, ##args);\
fprintf(stderr, "\n"); \
abort()

#else

#   define MERR(format, args...) \
do {\
    fprintf(stderr, "[MERR] ");\
    fprintf(stderr, format, ##args); \
    fprintf(stderr, "\n");\
} while (0)

#   define MWARN(format, args...) \
do {\
    fprintf(stderr, "[MWARN] ");  \
    fprintf(stderr, format, ##args); \
    fprintf(stderr, "\n");\
} while (0)

#   define MINFO(format, args...) \
do {\
    fprintf(stderr, "[MINFO] ");  \
    fprintf(stderr, format, ##args); \
    fprintf(stderr, "\n");\
} while (0)

#   define MPANIC(format, args...) \
fprintf(stderr, "[MOOPS] "); \
fprintf(stderr, format, ##args); \
fprintf(stderr, "\n");\
abort()

#endif

#define MPOOL_UNUSED(_sym) _sym __attribute__((unused))

#if defined (__alpha__) || defined (__ia64__) || defined (__x86_64__) \
    || defined (_WIN64) || defined (__LP64__) || defined (__LLP64__)
#   define MPOOL_64BIT
#endif

#ifndef MPOOL_PATH_MAX
#   define MPOOL_PATH_MAX (4096)
#endif

#ifndef MPOOL_HUGEPAGE_ENV
#   define MPOOL_HUGEPAGE_ENV "MPOOL_HUGEPAGE_HOME"
#endif

#ifndef MPOOL_HUGEPAGE_FILE
#   define MPOOL_HUGEPAGE_FILE "mpool_hugepage"
#endif

#ifdef MPOOL_ENABLE_MLOCK
#   define MMEM_LOCK(_addr, _sz) mlock((_addr), (_sz))    
#   define MMEM_UNLOCK(_addr, _sz) munlock((_addr), (_sz))    
#else
#   define MMEM_LOCK(_addr, _sz) do{}while(0)
#   define MMEM_UNLOCK(_addr, _sz) do{}while(0) 
#endif

#ifndef MPOOL_SIZE_ENV
#   define MPOOL_SIZE_ENV "MPOOL_MT_SIZE"
#endif

#ifndef MPOO_MT_DEFAULT_SIZE
#   define MPOOL_MT_DEFAULT_SIZE (1ul << 21)
#endif

#define MCACHE_LINE (64u)
#define MPAGE_SIZE  (4096u)

#define MATTR_ALIGN(_s) __attribute__((aligned(_s)))
#define MATTR_PAGE_ALIGN MATTR_ALIGN(MPAGE_SIZE)
#define MATTR_CACHELINE_ALIGN MATTR_ALIGN(MCACHE_LINE)

#define MPTR_ALIGNED(_ptr, _m) (!((uintptr_t)(_ptr) & ((_m) - 1)))
#define MSIZE_ALIGN_DOWN(_s, _m) (((size_t)(_s)) - (((size_t)(_s)) & ((_m) - 1)))
#define MSIZE_ALIGN_UP(_s, _m) ((((size_t)(_s)) + (_m) - 1) & ~((_m) - 1))
#define MCACHELINE_ALIGN(_s) MSIZE_ALIGN_UP(_s, MCACHE_LINE)
#define MPAGE_ALIGN(_s) MSIZE_ALIGN_UP(_s, MPAGE_SIZE)

#ifndef MPOOL_SLICE_SIZE_MIN
#   define MPOOL_SLICE_SIZE_MIN   (1UL << 20)
#endif

enum {
#ifdef MPOOL_64BIT
    MEL_IDX_MAX = 32,
    MALIGN_LOG2 = 3
#else 
    MEL_IDX_MAX = 30,
    MALIGN_LOG2 = 2
#endif
};

#define MLL_IDX_CNT_LOG2  (5)
#define MLL_IDX_CNT (1 << MLL_IDX_CNT_LOG2)
#define MALIGN_SIZE (1 << MALIGN_LOG2)
#define MEL_SHIFT (MLL_IDX_CNT_LOG2 + MALIGN_LOG2)
#define MEL_IDX_CNT (MEL_IDX_MAX - MEL_SHIFT + 1)
#define MBLOCK_SIZE_SMALL (1 << MEL_SHIFT)

#define _MCONCAT(_s1, _s2)  _s1##_s2
#define MCONCAT(_s1, _s2) _MCONCAT(_s1, _s2)
#define MSTATIC_CHECK(_exp) \
typedef char MCONCAT(static_check, __LINE__)[_exp ? 1 : -1]

MSTATIC_CHECK(sizeof(int) * CHAR_BIT == 32);
MSTATIC_CHECK(sizeof(size_t) * CHAR_BIT >= 32);
MSTATIC_CHECK(sizeof(size_t) * CHAR_BIT <= 64);
MSTATIC_CHECK(sizeof(unsigned int) * CHAR_BIT >= MLL_IDX_CNT);
MSTATIC_CHECK(MALIGN_SIZE == MBLOCK_SIZE_SMALL / MLL_IDX_CNT);

#ifdef MPOOL_MT
static __thread struct mpool* tpool = NULL;
#endif

struct mslice;
struct mblock {
    struct mblock*      prev_block;

#ifdef MPOOL_MT
    struct mslice*      owner;  
#endif

    size_t              size;
    struct mblock*      prev_free;
    struct mblock*      next_free;
};


struct mslice {
    size_t                          size;
    struct mslice*                  next;

#ifdef MPOOL_MT
    struct mpool*                   owner;
    volatile struct mblock*         free_list;
#endif

};

struct control {
    struct mblock       empty_block;
    unsigned int        el_bitmap;
    unsigned int        ll_bitmap[MEL_IDX_CNT];
    struct mblock*      blocks[MEL_IDX_CNT][MLL_IDX_CNT]; 
};

struct mpool {
    struct mslice*      slice_head;
    size_t              slice_size;
    struct mblock       empty_block;
    unsigned int        el_bitmap;
    unsigned int        ll_bitmap[MEL_IDX_CNT];
    struct mblock*      blocks[MEL_IDX_CNT][MLL_IDX_CNT]; 
};


#define MOFFSETOF(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
static const size_t MBLOCK_FREE_BIT = 1;
static const size_t MBLOCK_PREV_FREE_BIT = 2;

#ifdef MPOOL_MT
static const size_t MBLOCK_OVERHEAD = sizeof(size_t) + sizeof(struct mslice *);
#else
static const size_t MBLOCK_OVERHEAD = sizeof(size_t);
#endif

static const size_t MBLOCK_START_OFFSET = MOFFSETOF(struct mblock, size) + sizeof(size_t);
static const size_t MBLOCK_SIZE_MIN = sizeof(struct mblock) - sizeof(struct mblock *);
static const size_t MBLOCK_SIZE_MAX = (size_t)1 << MEL_IDX_MAX;

#define MBLOCK(_ptr, _s) ((struct mblock *)((char *)(_ptr) + (_s)))

#define MBLOCK_GET_SIZE(_b) \
    (MBLOCK(_b, 0)->size & ~(MBLOCK_FREE_BIT | MBLOCK_PREV_FREE_BIT))

#define MBLOCK_SET_SIZE(_b, _s)\
do {\
    const size_t _oldsize = MBLOCK(_b, 0)->size;\
    MBLOCK(_b, 0)->size = (_s) | (_oldsize & (MBLOCK_FREE_BIT | MBLOCK_PREV_FREE_BIT));\
} while (0)

#define MBLOCK_IS_LAST(_b) (!BLOCK_GET_SIZE(_b))
        
#define MBLOCK_IS_FREE(_b) (MBLOCK(_b, 0)->size & MBLOCK_FREE_BIT)
#define MBLOCK_SET_FREE(_b) (MBLOCK(_b, 0)->size |= MBLOCK_FREE_BIT)
#define MBLOCK_SET_USED(_b) (MBLOCK(_b, 0)->size &= ~MBLOCK_FREE_BIT)
#define MBLOCK_PREV_IS_FREE(_b) (MBLOCK(_b, 0)->size & MBLOCK_PREV_FREE_BIT)    
#define MBLOCK_PREV_SET_FREE(_b) (MBLOCK(_b, 0)->size |= MBLOCK_PREV_FREE_BIT)
#define MBLOCK_PREV_SET_USED(_b) (MBLOCK(_b, 0)->size &= ~MBLOCK_PREV_FREE_BIT)    
#define MBLOCK_TO_ADDR(_b) (((char *)MBLOCK(_b, 0)) + MBLOCK_START_OFFSET)
#define MBLOCK_FROM_ADDR(_addr) MBLOCK((((char *)(_addr)) - MBLOCK_START_OFFSET), 0)
#define MBLOCK_PREV(_b) (_b)->prev_block
#define MBLOCK_NEXT(_b) \
    MBLOCK(MBLOCK_TO_ADDR(_b), MBLOCK_GET_SIZE(_b) - MBLOCK_OVERHEAD)
#define MBLOCK_CAN_SPLIT(_b, _sz) \
    (MBLOCK_GET_SIZE(_b) >= sizeof(struct mblock) + (_sz))

#define MSLICE_INIT(_sl, _sz) \
do {\
    (_sl)->size = (_sz);\
    (_sl)->next = NULL;\
} while (0)

#define MAUTO_CLEAN_DEFINE(_class, _obj, _destructor) \
    _class* _obj __attribute__((__cleanup__(_destructor)))
#define OBJ_RETURN(_obj) ({typeof(_obj) __obj = (_obj); (_obj) = NULL; __obj;})

static struct mslice* mslice_create(size_t size);
static void mslice_release(struct mslice* slice);
static int mslice_add(struct mpool* pool, struct mslice* slice);
static void mpool_destructor(void* pool);
static int mprepare_hugepage(size_t* size);
static size_t mblock_request_size(size_t size, size_t align);
static struct mblock* mblock_find_free(struct mpool* pool, size_t size);
static struct mblock* mblock_trim_free(struct mpool* pool,
    struct mblock* block, size_t size);
static void* mblock_ready_used(struct mpool* pool, struct mblock* block,
    size_t size);
static struct mblock* mblock_join_prev(struct mpool* pool,
    struct mblock* block);
static struct mblock* mblock_join_next(struct mpool* pool,
    struct mblock* block);
static void mblock_mark_free(struct mblock* block);
static void mblock_remove_free(struct mpool* pool, struct mblock* block,
    size_t el, size_t ll);
static void mblock_insert(struct mpool* pool, struct mblock* block);
static struct mblock* mblock_link_next(struct mblock* block);

#ifdef MPOOL_MT
static inline size_t mblock_free_collect(void);
static inline void mblock_push_remote(struct mblock* block);
#endif

struct mpool* mpool_create(size_t size)
{
    MAUTO_CLEAN_DEFINE(struct mpool, pool, mpool_destructor);
    struct mslice*  slice;
    int             i;
    int             j;

    if (size < MPOOL_SLICE_SIZE_MIN) {
        size = MPOOL_SLICE_SIZE_MIN;
    }

    pool = malloc(sizeof(struct mpool));
    if (!pool) {
        MERR("failed, malloc error");
        return NULL;
    }
    memset(pool, 0, sizeof(struct mpool));
    pool->empty_block.next_free = &pool->empty_block;
    pool->empty_block.prev_free = &pool->empty_block;
    pool->el_bitmap = 0;
    for (i = 0; i < MEL_IDX_CNT; ++i) {
        pool->ll_bitmap[i] = 0;
        for (j = 0; j < MLL_IDX_CNT; ++j) {
            pool->blocks[i][j] = &pool->empty_block;
        }
    }

    slice = mslice_create(size);
    if (!slice) {
        MERR("failed, mslice_create error");
        return NULL;
    }
    pool->slice_size = size;

    if (mslice_add(pool, slice) < 0) {
        return NULL;
    }

    return OBJ_RETURN(pool);
}

void mpool_release(struct mpool* pool)
{
    struct mpool*    mpool = (struct mpool *)pool;
    struct mslice*   slice;
    struct mslice*   next;

    if (!mpool) {
        return;
    }

    for (slice = mpool->slice_head; slice; slice = next) {
        next = slice->next;
        mslice_release(slice);
    }

    free(pool);
}

static void mpool_destructor(void *pool)
{
    struct mpool* mpool = *(struct mpool **)pool;
    mpool_release(mpool);
}

static struct mslice* mslice_create(size_t size)
{
    struct mslice* slice;
    size_t         total_size;
    size_t         tmp_size;
    int            fd = -1;
    int            flags = MAP_PRIVATE | MAP_POPULATE;

    if (size < MPOOL_SLICE_SIZE_MIN) {
        size = MPOOL_SLICE_SIZE_MIN;
    }

    total_size = MPAGE_ALIGN(size);
    tmp_size = total_size;
    fd = mprepare_hugepage(&total_size); 
    if (fd < 0) {
        goto MSLICE_DO_NORMAL_MMAP;
    }
    slice = (struct mslice *)mmap(NULL, total_size,
        PROT_READ | PROT_WRITE, flags, fd, 0); 
    if (slice != (struct mslice *)MAP_FAILED) {
        goto MSLICE_OUT;
    }

MSLICE_DO_NORMAL_MMAP:
    MWARN("mmap for hugepage failed, error %s", strerror(errno));
    MWARN("mmap 4KB page");
    total_size = tmp_size;
    flags |= MAP_ANONYMOUS;
    slice = mmap(NULL, total_size, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (slice == (struct mslice *)MAP_FAILED) {
        MERR("memory slice create failed, mmap error %s, total_size %lu",
            strerror(errno), total_size);    
        slice = NULL;
        goto MSLICE_ERR;
    }

MSLICE_OUT:
    MSLICE_INIT(slice, total_size);
    MMEM_LOCK(slice, total_size);
    
MSLICE_ERR:
    if (fd > 0) {
        close(fd);
    }

    return slice;
}

static void mslice_release(struct mslice* slice)
{
    size_t size;

    if (!slice) {
        return;
    }

    size = slice->size;
    MMEM_UNLOCK(slice, size);
    munmap(slice, size);
}

static int mslice_add(struct mpool* pool, struct mslice* slice)
{
    struct mblock*  block;
    struct mblock*  next;
    const size_t    overhead = MBLOCK_OVERHEAD << 1;    
    const size_t    size = slice->size - sizeof(struct mslice);
    const size_t    available = MSIZE_ALIGN_DOWN(size - overhead, MALIGN_SIZE);
    void*           addr = &slice[1];

    if (!MPTR_ALIGNED(addr, MALIGN_SIZE)) {
        MERR("addr %p is not aligned to %u", addr, MALIGN_SIZE);
        return -1;
    }

    if (available < MBLOCK_SIZE_MIN || available > MBLOCK_SIZE_MAX) {
        MERR("available %lu is out of range [%lu, %lu]", available,
            MBLOCK_SIZE_MIN, MBLOCK_SIZE_MAX);
        return -1;
    }

    slice->next = pool->slice_head;
    pool->slice_head = slice;

#ifdef MPOOL_MT
    block = MBLOCK(addr, -MBLOCK_OVERHEAD + sizeof(struct mslice *));
#else
    block = MBLOCK(addr, -MBLOCK_OVERHEAD);
#endif
    MBLOCK_SET_SIZE(block, available); 
    MBLOCK_SET_FREE(block);
    MBLOCK_PREV_SET_USED(block);
    mblock_insert(pool, block);

    next = mblock_link_next(block);
    MBLOCK_SET_SIZE(next, 0);
    MBLOCK_SET_USED(next); 
    MBLOCK_PREV_SET_FREE(next);

#ifdef MPOOL_MT
    slice->owner = pool;
    slice->free_list = NULL;
    block->owner = slice;
    next->owner = slice;
#endif

    MDBG("block %p, prev %p", block, block->prev_block); 
    return 0;
}

static inline void mblock_free(struct mpool* pool, struct mblock* block)
{
    mblock_mark_free(block);
    block = mblock_join_prev(pool, block);
    block = mblock_join_next(pool, block);
    mblock_insert(pool, block);
    MDBG("block %p, prev %p", block, block->prev_block);
}

#ifdef MPOOL_MT
void mpool_cleanup_mt(void)
{
    mpool_release(tpool);
}

static size_t mpool_getsize(void)
{
    char*   env = getenv(MPOOL_SIZE_ENV);
    long    size;

    if (!env) {
        return MPOOL_MT_DEFAULT_SIZE;
    }
    size = atol(env);
    if (size <= 0) {
        return MPOOL_MT_DEFAULT_SIZE;
    }
    
    return (size_t)size;
}

void* mpool_alloc_mt(size_t size)
{
    struct mblock*  block = NULL;
    size_t          req_size;
    size_t          slice_size;

    if (!tpool) {
        slice_size = mpool_getsize();
        tpool = mpool_create(slice_size);
        if (!tpool) {
            MERR("mpool creat failed");
            return NULL;
        }
        MDBG("slice_size %lu, tpool %p", slice_size, tpool);
    }
    req_size = mblock_request_size(size, MALIGN_SIZE);
    block = mblock_find_free(tpool, req_size);
    if (!block) {
        MWARN("slow path, collect free blocks for req_size %lu", req_size);
        if (mblock_free_collect() >= req_size) {
            block = mblock_find_free(tpool, req_size);
            if (block) {
                goto MBLOCK_READY_USED; 
            }
        }

        slice_size = tpool->slice_size;
        if (slice_size < req_size) {
            slice_size = req_size << 1;

        }
        MWARN("slow path, mpool extend size %lu", slice_size);
        struct mslice* slice = mslice_create(slice_size);
        if (!slice) {
            return NULL;
        }
        mslice_add(tpool, slice);
        block = mblock_find_free(tpool, req_size);
    }

MBLOCK_READY_USED:
    return mblock_ready_used(tpool, block, req_size);
}

void mpool_free_mt(void* ptr)
{
    struct mblock* block;
    struct mslice* slice;

    if (ptr) {
        block = MBLOCK_FROM_ADDR(ptr);
        slice = block->owner;
        if (slice->owner == tpool) {
            MDBG("free local block %p", block);
            mblock_free(tpool, block);
        } else {
            MDBG("push remote free list block %p", block);
            mblock_push_remote(block);
        }
    }
}

void* mpool_alloc_align_mt(size_t size, size_t align)
{
    struct mblock*  block = NULL;
    struct mslice*  slice = NULL;
    size_t          req_size;
    size_t          total_size;
    size_t          aligned_size;
    size_t          act_size;
    size_t          gap;
    size_t          gap_remain;
    size_t          offset;
    size_t          slice_size;
    void*           ptr;
    void*           aligned;
    void*           next_aligned;

    if (!tpool) {
        slice_size = mpool_getsize();
        tpool = mpool_create(slice_size);
        if (!tpool) {
            MERR("mpool creat failed");
            return NULL;
        }
    }

    req_size = mblock_request_size(size, MALIGN_SIZE);
    total_size = req_size + align + sizeof(struct mblock);
    aligned_size = mblock_request_size(total_size, align);
    act_size = (req_size && align > MALIGN_SIZE) ?  aligned_size : req_size;

    block = mblock_find_free(tpool, act_size);
    if (!block) {
        if (mblock_free_collect() >= req_size) {
            block = mblock_find_free(tpool, req_size);
            if (block) {
                goto MBLOCK_READY_USED; 
            }
        }
        slice_size = tpool->slice_size;
        if (slice_size < act_size) {
            slice_size = act_size << 1;
        }
        MWARN("slow path, mpool extend size %lu", slice_size);
        slice = mslice_create(slice_size);
        if (!slice) {
            return NULL;
        }
        mslice_add(tpool, slice);
        block = mblock_find_free(tpool, act_size);
    }

MBLOCK_READY_USED:
    ptr = MBLOCK_TO_ADDR(block);    
    aligned = (void *)MSIZE_ALIGN_UP(ptr, align);
    gap = aligned - ptr;
    if (gap && gap < sizeof(struct mblock)) {
        gap_remain = sizeof(struct mblock) - gap; 
        offset = gap_remain > align ? gap_remain : align;
        next_aligned = aligned + offset;
        aligned = (void *)MSIZE_ALIGN_UP(next_aligned, align);
        gap = aligned - ptr;
    }
    if (gap) {
        block = mblock_trim_free(tpool, block, gap);
     }

    return mblock_ready_used(tpool, block, req_size);
}

void* mpool_calloc_mt(size_t size)
{
    void* addr = mpool_alloc_mt(size);

    if (addr) {
        memset(addr, 0, size);
    }

    return addr;
}

void* mpool_calloc_align_mt(size_t size, size_t align)
{
    void* addr = mpool_alloc_align_mt(size, align);
    if (addr) {
        memset(addr, 0, size);
    }

    return addr;
}
#endif

void* mpool_alloc(struct mpool* pool, size_t size)
{
    struct mblock*  block = NULL;
    size_t          req_size;
    size_t          slice_size;

    req_size = mblock_request_size(size, MALIGN_SIZE);
    block = mblock_find_free(pool, req_size);
    if (!block) {
        slice_size = pool->slice_size;
        if (slice_size < req_size) {
            slice_size = req_size << 1;

        }
        MWARN("slow path, mpool extend size %lu", slice_size);
        struct mslice* slice = mslice_create(slice_size);
        if (!slice) {
            return NULL;
        }
        mslice_add(pool, slice);
        MDBG("call mblock_find_free after extend");
        block = mblock_find_free(pool, req_size);
    }
    return mblock_ready_used(pool, block, req_size);
}

void mpool_free(struct mpool* pool, void* ptr)
{
    struct mblock* block;

    if (ptr) {
        block = MBLOCK_FROM_ADDR(ptr);
        mblock_free(pool, block);
    }
}

void* mpool_alloc_align(struct mpool* pool, size_t size, size_t align)
{
    struct mblock*  block = NULL;
    struct mslice*  slice = NULL;
    size_t          req_size;
    size_t          total_size;
    size_t          aligned_size;
    size_t          act_size;
    size_t          gap;
    size_t          gap_remain;
    size_t          offset;
    size_t          slice_size;
    void*           ptr;
    void*           aligned;
    void*           next_aligned;

    req_size = mblock_request_size(size, MALIGN_SIZE);
    total_size = req_size + align + sizeof(struct mblock);
    aligned_size = mblock_request_size(total_size, align);
    act_size = (req_size && align > MALIGN_SIZE) ?  aligned_size : req_size;

    block = mblock_find_free(pool, act_size);
    if (!block) {
        slice_size = pool->slice_size;
        if (slice_size < act_size) {
            slice_size = act_size << 1;
        }
        MWARN("slow path, mpool extend size %lu", slice_size);
        slice = mslice_create(slice_size);
        if (!slice) {
            return NULL;
        }
        mslice_add(pool, slice);
        block = mblock_find_free(pool, act_size);
    }

    ptr = MBLOCK_TO_ADDR(block);    
    aligned = (void *)MSIZE_ALIGN_UP(ptr, align);
    gap = aligned - ptr;
    if (gap && gap < sizeof(struct mblock)) {
        gap_remain = sizeof(struct mblock) - gap; 
        offset = gap_remain > align ? gap_remain : align;
        next_aligned = aligned + offset;
        aligned = (void *)MSIZE_ALIGN_UP(next_aligned, align);
        gap = aligned - ptr;
    }
    if (gap) {
        block = mblock_trim_free(pool, block, gap);
     }

    MDBG("block %p, prev %p", block, block->prev_block);
    return mblock_ready_used(pool, block, req_size);
}

void* mpool_calloc(struct mpool* pool, size_t size)
{
    void* addr = mpool_alloc(pool, size);
    if (!addr) {
        return NULL;
    }
    memset(addr, 0, size);

    return addr;
}

void* mpool_calloc_align(struct mpool* pool, size_t size, size_t align)
{
    void* addr = mpool_alloc_align(pool, size, align);
    if (!addr) {
        return 0;
    }
    memset(addr, 0, size);
    return addr;
}

static int mprepare_hugepage(size_t* size)
{
    char            path[MPOOL_PATH_MAX];
    char*           env;
    int             fd = -1;
    size_t          page_size = 0;
    struct statfs   sfs;

    env = getenv(MPOOL_HUGEPAGE_ENV);
    if (!env) {
        MWARN("env %s is not set", MPOOL_HUGEPAGE_ENV);
        return -1;
    }

    snprintf(path, MPOOL_PATH_MAX, "%s/%s.XXXXXX", env, MPOOL_HUGEPAGE_FILE); 
    fd = mkstemp(path);
    if (fd < 0) {
        MERR("mkstemp %s failed, error %s", path, strerror(errno));
        return -1;
    }

    if (unlink(path) < 0) {
        MWARN("unlink %s failed, error %s", path, strerror(errno));
    }
    
    if (fstatfs(fd, &sfs) < 0) {
        MERR("fstatfs %s failed, error %s", path, strerror(errno));
        close(fd);
        return -1;
    }

    page_size = (size_t)sfs.f_bsize;
    *size = MSIZE_ALIGN_UP(*size, page_size);
    MDBG("path %s size %lu, page_size %lu", path, *size, page_size);
    if (ftruncate(fd, *size) < 0) {
        MERR("ftruncate %s failed, error %s", path, strerror(errno));
        close(fd);
        return -1;
    }
    
    return fd;
}


#if GCC_VER >= 30400

static inline int m_ffs(unsigned int word)
{
    return __builtin_ffs(word) - 1;
}

static inline int m_fls(unsigned int word)
{
    const int bit = word ? 32 - __builtin_clz(word) : 0;
    return bit - 1;
}

#else

static inline int m_fls_generic(unsigned int word)
{
    int bit = 32;

    if (!word) {
        bit -= 1;
    }
    if (!(word & 0xffff0000)) {
        word <<= 16; bit -= 16;
    }
    if (!(word & 0xff000000)) {
        word <<= 8; bit -= 8;
    }
    if (!(word & 0xf0000000)) {
        word <<= 4; bit -= 4;
    }
    if (!(word & 0xc0000000)) {
        word <<= 2; bit -= 2;
    }
    if (!(word & 0x80000000)) {
        word <<= 1; bit -= 1;
    }

    return bit;
}

static inline int m_ffs(unsigned int word)
{
    return m_fls_generic(word & (~word + 1)) - 1;
}

static inline int m_fls(unsigned int word)
{
    return m_fls_generic(word) - 1;
}
#endif

#if defined (MPOOL_64BIT)
static inline int m_fls_sizet(size_t size)
{
    int high = size >> 32; 
    int bits = 0;

    if (high) {
        bits = 32 + m_fls(high);
    }   else {
        bits = m_fls((int)size & 0xffffffff);
    }
    return bits;
}
#else
#   define m_fls_sizet m_fls
#endif 

static void mblock_get_index(size_t size, size_t* el, size_t* ll)
{       
    if (size < MBLOCK_SIZE_SMALL) {
        *el = 0;
        *ll = size >> MALIGN_LOG2;
    } else {
        *el = m_fls_sizet(size);
        *ll = (size >> (*el - MLL_IDX_CNT_LOG2)) ^ (1 << MLL_IDX_CNT_LOG2);
        *el -= (MEL_SHIFT - 1);
    }
}

static void mblock_search(size_t size, size_t* el, size_t* ll)
{
    if (size >= MBLOCK_SIZE_SMALL) {
        const size_t round = (1 << (m_fls_sizet(size) - MLL_IDX_CNT_LOG2)) - 1;
        size += round;
    }
    mblock_get_index(size, el, ll);
    MDBG("get index size %lu, el %lu, ll %lu", size, *el, *ll);
}

static void mblock_insert_free(struct mpool* pool, struct mblock* block,
    size_t el, size_t ll)
{
    struct mblock *curr = pool->blocks[el][ll];
    block->next_free = curr;
    block->prev_free = &pool->empty_block;
    curr->prev_free = block;

    pool->blocks[el][ll] = block;
    pool->el_bitmap |= (1U << el);
    pool->ll_bitmap[el] |= (1U << ll);
    MDBG("insert free el %lu, ll %lu", el, ll);
}

static struct mblock* mblock_link_next(struct mblock* block)
{   
    struct mblock* next = MBLOCK_NEXT(block);
    next->prev_block = block;
    return next;
}

static void mblock_insert(struct mpool* pool, struct mblock* block)
{
    size_t el;
    size_t ll;

    mblock_get_index(MBLOCK_GET_SIZE(block), &el, &ll);
    MDBG("get index el %lu, ll %lu", el, ll);
    mblock_insert_free(pool, block, el, ll);
}

static size_t mblock_request_size(size_t size, size_t align)
{   
    size_t  adjust = 0; 
    size_t  aligned;
    if (size) {
        aligned = MSIZE_ALIGN_UP(size, align);
        if (aligned < MBLOCK_SIZE_MAX) {
            adjust = aligned > MBLOCK_SIZE_MIN ? aligned : MBLOCK_SIZE_MIN;
        }
    }
    return adjust;
}

static struct mblock* mblock_find_suitable(struct mpool* pool, size_t* el, size_t* ll)
{   
    unsigned int ll_map;
    unsigned int el_map;

    MDBG("el %lu, ll %lu", *el, *ll);
    ll_map = pool->ll_bitmap[*el] & (~0U << *ll);
    if (!ll_map) {
        el_map = pool->el_bitmap & (~0U << (*el + 1));
        if (!el_map) {
            return NULL;
        } 

        *el = m_ffs(el_map);
        ll_map = pool->ll_bitmap[*el];
    }

    *ll = m_ffs(ll_map);

    return pool->blocks[*el][*ll];
}

static void mblock_remove(struct mpool* pool, struct mblock* block)
{
    size_t  el;
    size_t  ll;

    mblock_get_index(MBLOCK_GET_SIZE(block), &el, &ll);
    mblock_remove_free(pool, block, el, ll);
} 

static void mblock_remove_free(struct mpool* pool, struct mblock* block,
    size_t el, size_t ll)
{
    struct mblock* prev = block->prev_free;
    struct mblock* next = block->next_free;

    next->prev_free = prev;
    prev->next_free = next;

    if (pool->blocks[el][ll] == block) {
        pool->blocks[el][ll] = next;
        if (next == &pool->empty_block) {
            pool->ll_bitmap[el] &= ~(1U << ll);
            if (!pool->ll_bitmap[el]) {
                pool->el_bitmap &= ~(1U << el);
            }
        }
    }
}

static struct mblock* mblock_find_free(struct mpool* pool, size_t size)
{
    struct mblock*  block = NULL; 
    size_t          el = 0; 
    size_t          ll = 0;
    
    if (size) {
        mblock_search(size, &el, &ll);
        if (el < MEL_IDX_CNT) {
            block = mblock_find_suitable(pool, &el, &ll);
            MDBG("find suitable el %lu, ll %lu", el, ll);
        }
    }
        
    if (block) {
        assert(MBLOCK_GET_SIZE(block) >= size);
        mblock_remove_free(pool, block, el, ll);
    }

    return block;
}

static void mblock_mark_free(struct mblock* block)
{   
    struct mblock* next = mblock_link_next(block);
    MBLOCK_PREV_SET_FREE(next);
    MBLOCK_SET_FREE(block);
}

static void mblock_mark_used(struct mblock* block)
{
    struct mblock* next = MBLOCK_NEXT(block);
    MBLOCK_PREV_SET_USED(next);
    MBLOCK_SET_USED(block);
}

static struct mblock* mblock_split(struct mblock* block, size_t size)
{
    struct mblock*  remain_block;
    size_t          remain_size;

    remain_block = MBLOCK(MBLOCK_TO_ADDR(block), size - MBLOCK_OVERHEAD);
    remain_size = MBLOCK_GET_SIZE(block) - (size + MBLOCK_OVERHEAD);
    MBLOCK_SET_SIZE(remain_block, remain_size);
    MBLOCK_SET_SIZE(block, size);
    mblock_mark_free(remain_block);

#ifdef MPOOL_MT
    remain_block->owner = block->owner;
#endif
    return remain_block;
}

static void mblock_trim(struct mpool* pool, struct mblock* block,
    size_t size)
{
    struct mblock* remain;
    MDBG("block %p, prev %p", block, block->prev_block);
    if (MBLOCK_CAN_SPLIT(block, size)) {
        remain = mblock_split(block, size);
        mblock_link_next(block);
        MBLOCK_PREV_SET_FREE(remain);
        mblock_insert(pool, remain);
    }
} 

static struct mblock* mblock_trim_free(struct mpool* pool,
    struct mblock* block, size_t size)
{
    struct mblock* remain = block;
    if (MBLOCK_CAN_SPLIT(block, size)) {
        remain = mblock_split(block, size - MBLOCK_OVERHEAD);
        MBLOCK_PREV_SET_FREE(remain);
        mblock_link_next(block); 
        mblock_insert(pool, block);
    }   
        
    return remain;
}

static void* mblock_ready_used(struct mpool* pool, struct mblock* block,
    size_t size)
{
    void* p = NULL;

    if (block) {
        mblock_trim(pool, block, size);
        mblock_mark_used(block);
        p = MBLOCK_TO_ADDR(block);
    }

    return p;
}

static struct mblock* mblock_join(struct mblock* prev, struct mblock* block)
{
    prev->size += MBLOCK_GET_SIZE(block) + MBLOCK_OVERHEAD;
    mblock_link_next(prev); 
    return prev;
}

static struct mblock* mblock_join_prev(struct mpool* pool, struct mblock* block)
{
    struct mblock* prev;
    if (MBLOCK_PREV_IS_FREE(block)) {
        prev = MBLOCK_PREV(block);
        mblock_remove(pool, prev);
        block = mblock_join(prev, block);
    }

    return block;
}

static struct mblock* mblock_join_next(struct mpool* pool, struct mblock* block)
{
    struct mblock* next = MBLOCK_NEXT(block);

    if (MBLOCK_IS_FREE(next)) {
        mblock_remove(pool, next);
        block = mblock_join(block, next);
    }

    return block;
}

#ifdef MPOOL_MT
#   if GCC_VER >= 40700
#       define mpool_atomic_exchange __atomic_exchange_n
#       define mpool_atomic_cas __atomic_compare_exchange_n
#   else  //GCC_VER
#       if defined(__x86_64__) || defined(__i386__)
static inline void* mpool_atomic_exchange(void **addr, void *newv, int MPOOL_UNUSED(morder))
{
    void* old;
    __asm__ __volatile__ (
        "xchgq %0, %1"
        : "=r" (old), "+m" (*addr)
        : "0" (newv)
        : "memory"
    );
    return old;
}

static inline int mpool_atomic_cas(void* volatile* ptr, void** expected,
    void* desired, int MPOOL_UNUSED(flag), int MPOOL_UNUSED(success_morder),
    int MPOOL_UNUSED(fail_morder))
{
    unsigned char success;

    __asm__ __volatile__(
        "lock cmpxchgq %3, %1\n"
        "sete %0\n"
        : "=r"(success),
          "+m"(*ptr),
          "+a"(*expected)
        : "r"(desired)
        : "memory", "cc"
    );

    return !!success;
}

#       elif defined(__aarch64__) // __x86_64__
static inline void* mpool_atomic_exchange(void **addr, void *newv, int MPOOL_UNUSED(morder))
{
    void *old;
    unsigned int tmp;
    __asm__ __volatile__ (
        "0:\n"
        "ldaxr   %0, [%2]\n"
        "stlxr   %w1, %3, [%2]\n"
        "cbnz    %w1, 0b\n"
        : "=&r" (old), "=&r" (tmp)
        : "r" (addr), "r" (newv)
        : "memory"
    );
    return old;
}
static inline int mpool_atomic_cas(void* volatile* ptr, void** expected, 
    void* desired, int MPOOL_UNUSED(flag), int MPOOL_UNUSED(success_morder),
    int MPOOL_UNUSED(fail_morder))
{
    uint64_t old;
    uint32_t fail;

    __asm__ __volatile__(
        "ldxr    %0, [%2]\n"
        "cmp     %0, %3\n"
        "b.ne    1f\n"
        "stxr    %w1, %4, [%2]\n"
        "b       2f\n"
        "1:\n"
        "mov     %w1, #1\n"
        "2:\n"
        : "=&r"(old), "=&r"(fail)
        : "r"(ptr), "r"(*expected), "r"(desired)
        : "memory", "cc"
    );

    if (fail != 0) {
        *expected = (void*)old;
        return 0;
    }

    return 1;
}

#       else //__x86_64__
#           error "atomic_exchange_ptr: unsupported architecture"
#       endif //_x86_64__

#   endif //GCC_VER

static inline size_t mblock_free_collect(void)
{
    struct mslice*  slice;
    struct mblock*  head;
    struct mblock*  block;
    struct mblock*  next;
    size_t          size = 0;
    
    for (slice = tpool->slice_head; slice; slice = slice->next) {
        head = (struct mblock *)mpool_atomic_exchange(&slice->free_list, NULL,
            __ATOMIC_ACQ_REL);
        for (block = head; block; block = next) {
            next = block->next_free;
            size += MBLOCK_GET_SIZE(block);
            mblock_free(tpool, block);    
        }
    }
   
    return size; 
}

static inline void mblock_push_remote(struct mblock* block)
{
    struct mslice* slice = block->owner;
    struct mblock* head;
    
    MDBG("push remote block %p", block);
    do {
        head = (struct mblock *)slice->free_list;
        block->next_free = (struct mblock *)slice->free_list;
    } while (!mpool_atomic_cas(&slice->free_list, &head, block,
        1, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
}
#endif //MPOOL_MT

#ifdef __cplusplus
}
#endif

