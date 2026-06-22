#pragma once
// poop_alloc.h
#include <intdef.h>

typedef struct pool_alloc pool_alloc;
#define PA_NIL_IDX -1

#ifdef _PA_INTRNL_IMPLM
struct pool_alloc {
    uint_t  elem_size;
    uint_t  count;
    uint_t  capacity;
    int_t   free_head_idx;
    byte_t  buf[];
};

#define _pa_get_chunk_ptr(T, pool_p, chunk_idx) \
    (T*)(pool_p)->buf + (chunk_idx)

#define _pa_deref(T, pool_p, chunk_idx) \
    *((T*)(pool_p)->buf + (chunk_idx))

#endif

extern pool_alloc *pa_create_pool(
    uint_t  elem_size,
    uint_t  capacity
);
extern int_t    pa_alloc_chunk(pool_alloc *alloc_p);
extern int      pa_free_chunk(pool_alloc *alloc_p, int_t chunk_off);
extern void     pa_destroy_pool(pool_alloc *alloc_p);
extern int_t    pa_dup_chunk(pool_alloc *alloc_p, void *chunk_p);
extern void    *pa_get_chunk_chked(pool_alloc *alloc_p, int_t chunk_idx);
extern void    *pa_get_chunk(pool_alloc *alloc_p, int_t chunk_idx);

#define pa_new_pool(T, capacity) pa_create_pool(sizeof(T), capacity)
#define pa_free_pool(pool_p) pa_destroy_pool(pool_p)
#define pa_get_ptr(T, pool_p, chunk_idx) (T*)pa_get_chunk(pool_p, chunk_idx)
#define pa_deref(T, pool_p, chunk_idx) (*pa_get_ptr(T, pool_p, chunk_idx))

