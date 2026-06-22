// poop_alloc.c
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <intdef.h>
#define _PA_INTRNL_IMPLM
#include <pool_alloc.h>
#include "dbg_print.h"
#include "alignoff.h"


#define _PA_ALIGN_MIN sizeof(_free_list_chunk)
typedef struct _free_list_chunk {
    int_t   next_free_idx;
} _free_list_chunk;

pool_alloc *pa_create_pool(
    uint_t  elem_size,
    uint_t  capacity
) {
    __auto_type pap =
        (pool_alloc*)malloc(sizeof(pool_alloc) + elem_size * capacity);
    if (!pap) return NULL;
    memset(pap->buf, 0xFF, elem_size * capacity);


    for (uint_t i = 0u; i < capacity - 1; ++i) {
        __auto_type flc_p = (_free_list_chunk*)(pap->buf + i * elem_size);
        flc_p->next_free_idx = i + 1;
    }

    __auto_type bflc_p = (_free_list_chunk*)(pap->buf + (capacity - 1) * elem_size);
    bflc_p->next_free_idx = PA_NIL_IDX;


    pap->elem_size      = elem_size;
    pap->count          = 0;
    pap->capacity       = capacity;
    pap->free_head_idx  = 0;

    return pap;
}

int_t pa_alloc_chunk(pool_alloc *alloc_p) {
    if (!alloc_p || alloc_p->count >= alloc_p->capacity)
        return PA_NIL_IDX;
    assert(alloc_p->free_head_idx != PA_NIL_IDX);
    int_t chunk_idx = alloc_p->free_head_idx;
    _free_list_chunk *chunk_p = (_free_list_chunk*)(
        alloc_p->buf + chunk_idx * sizeof(alloc_p->elem_size)
    );
    alloc_p->free_head_idx = chunk_p->next_free_idx;
    ++alloc_p->count;
    return chunk_idx;
}

int_t pa_dup_chunk(pool_alloc *alloc_p, void *chunk_p) {
    if (!chunk_p) return PA_NIL_IDX;
    int_t chunk_idx = pa_alloc_chunk(alloc_p);
    if (chunk_idx == PA_NIL_IDX) return PA_NIL_IDX;
    byte_t *__chunk_p =
        alloc_p->buf + chunk_idx * alloc_p->elem_size;
    memcpy(__chunk_p, chunk_p, alloc_p->elem_size);
    return chunk_idx;
}

int pa_free_chunk(pool_alloc *alloc_p, int_t chunk_idx) {
    if (!alloc_p || !alloc_p->count) return -1;
    if (
        chunk_idx < 0 ||
        chunk_idx > (int_t)alloc_p->capacity
    ) return -1;
    _free_list_chunk *flc_p =
        (_free_list_chunk*)(alloc_p->buf + chunk_idx * alloc_p->elem_size);
    flc_p->next_free_idx    = alloc_p->free_head_idx;
    alloc_p->free_head_idx  = chunk_idx;
    --alloc_p->count;
    return 0;
}

void *pa_get_chunk_chked(pool_alloc *alloc_p, int_t chunk_idx) {
    if (!alloc_p || !alloc_p->count) return NULL;
    if (
        chunk_idx < 0 ||
        chunk_idx > (int_t)alloc_p->capacity
    ) return NULL;
    assert(alloc_p->free_head_idx != PA_NIL_IDX);
    return alloc_p->buf + chunk_idx * alloc_p->elem_size;
}

void *pa_get_chunk(pool_alloc *alloc_p, int_t chunk_idx) {
    return alloc_p->buf + chunk_idx * alloc_p->elem_size;
}

void pa_destroy_pool(pool_alloc *alloc_p) {
    if (alloc_p) free(alloc_p);
}

