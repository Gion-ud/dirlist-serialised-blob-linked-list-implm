#pragma once
#include <assert.h>
#include <intdef.h>
#include <string.h>

typedef struct _cds_stack {
    ushort_t    elem_size;
    uint_t      size;
    uint_t      capacity;
    void       *data_buf;
} _cds_stack;


static inline void _cds_stack_init(
    _cds_stack *sp,
    uint_t      elem_size,
    uint_t      buf_size,
    void       *data_buf
) {
    if (!sp || !elem_size || !buf_size || !data_buf)
        return;
    sp->elem_size   = elem_size;
    sp->size        = 0;
    sp->capacity    = buf_size / elem_size;
    sp->data_buf    = data_buf;
}

static inline int _cds_stack_push(
    _cds_stack *sp,
    void       *elem_p
) {
    if (!sp || !elem_p || sp->size >= sp->capacity)
        return -1;
    assert(
        sp->elem_size && sp->capacity &&
        sp->data_buf && sp->size <= sp->capacity
    );
    memcpy(
        (unsigned char*)sp->data_buf + sp->size * sp->elem_size,
        elem_p,
        sp->elem_size
    );
    ++sp->size;
    return 0;
}

static inline int _cds_stack_pop(
    _cds_stack *sp,
    void       *elem_p
) {
    if (!sp || !elem_p || !sp->size) return -1;
    assert(
        sp->elem_size && sp->capacity &&
        sp->data_buf && sp->size <= sp->capacity
    );
    --sp->size;
    return 0;
}

static inline void *_cds_stack_top( _cds_stack *sp) {
    if (!sp) return NULL;
    assert(
        sp->elem_size && sp->capacity &&
        sp->data_buf && sp->size <= sp->capacity
    );
    return (unsigned char*)
        sp->data_buf + (sp->size - 1) * sp->elem_size;
}

static inline void _cds_stack_reset(_cds_stack *sp) {
    if (!sp) return;
    sp->size = 0;
}

#define stack(T) _cds_stack
#define stack_init(T, sp, buf, buf_size) \
    _cds_stack_init(sp, sizeof(T), buf_size, buf)

#define stack_init_from_arr(sp, arr, arr_len) \
    _cds_stack_init(sp, sizeof(*arr), arr_len, arr)

#define stack_reset(sp) _cds_stack_reset(sp)
#define stack_push(sp, elem_p) _cds_stack_push(sp, elem_p)
#define stack_pop(sp) _cds_stack_pop(sp)
#define stack_top(sp) _cds_stack_top(sp)


