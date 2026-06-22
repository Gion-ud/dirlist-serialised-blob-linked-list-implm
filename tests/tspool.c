//#define _PA_INTRNL_IMPLM

#include <pool_alloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <intdef.h>
#include <vector.h>
#include <assert.h>

#define log_msg(msg) \
    fprintf(stderr, "<%s@file '%s' line%d> %s\n", __func__, __FILE__, __LINE__ , msg)

int main() {
    log_msg("");
    pool_alloc *pap = pa_new_pool(int, 8);
    assert(pap);
    log_msg("");
    Vector(int) v = vector_create(int);
    assert(v);
    log_msg("");
    vector_reserve(&v, 8u);

    log_msg("");
    for (__auto_type i = 0u; i < 8; ++i) {
        log_msg("");
        int item = - (int)i * 4;
        int_t item_idx = pa_dup_chunk(pap, &item);
        assert(item_idx == (int_t)i);
        vector_push_back(&v, &item_idx);
        printf("%d\n", pa_deref(int, pap, item_idx));
    }
    puts("");

    int_t item_idx = pa_alloc_chunk(pap);
    assert(item_idx == PA_NIL_IDX);

    log_msg("");
    for (__auto_type i = 0u; i < vector_size(v); ++i) {
        printf("%d\n", pa_deref(int, pap, v[i]));
        pa_free_chunk(pap, v[i]);
    }
    puts("");
    log_msg("");
    vector_clear(v);

    for (__auto_type i = 0u; i < 8; ++i) {
        log_msg("");
        int item = - (int)i * 4;
        int_t item_idx = pa_dup_chunk(pap, &item);
        assert(item_idx != PA_NIL_IDX);
        vector_push_back(&v, &item_idx);
        printf("%d\n", pa_deref(int, pap, item_idx));
    }
    puts("");

    log_msg("");
    for (__auto_type i = 0u; i < vector_size(v); ++i) {
        printf("%d\n", *pa_get_ptr(int, pap, v[i]));
        pa_free_chunk(pap, v[i]);
    }
    puts("");
    log_msg("");


    vector_destroy(v);
    pa_free_pool(pap);
    return 0;
}