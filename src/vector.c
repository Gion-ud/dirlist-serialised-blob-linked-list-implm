#include <vector.h>
#include <stdlib.h>
#include <string.h>

typedef struct _VectorHeader {
    ushort_t    elem_size;
    uint_t      vec_length;
    uint_t      vec_capacity;
    byte_t      data[];
} _VectorHeader;

#define VECTOR_INIT_CAPACITY 1
_VectorHandle _c_vector_create(ushort_t elem_size) {
    __auto_type vhdr_p = (_VectorHeader*)malloc(sizeof(_VectorHeader));
    if (!vhdr_p) return VECTOR_INVALID_HANDLE;
    vhdr_p->elem_size       = elem_size;
    vhdr_p->vec_length      = 0;
    vhdr_p->vec_capacity    = 0;

    return vhdr_p->data;
}

int_t _c_vector_push_back(_VectorHandle *vec_p, void *data) {
    if (!vec_p || !*vec_p || !data) goto failed;
    __auto_type vhdr_p = ((_VectorHeader*)*vec_p) - 1;

    if (vhdr_p->vec_length >= vhdr_p->vec_capacity) {
        uint_t new_capacity = (vhdr_p->vec_capacity)
            ? vhdr_p->vec_capacity * 2 : VECTOR_INIT_CAPACITY;
        __auto_type new_vhdr_p =
            (_VectorHeader*)realloc(
                vhdr_p, sizeof(_VectorHeader) + new_capacity * vhdr_p->elem_size
            );
        if (!new_vhdr_p) goto failed;
        vhdr_p = new_vhdr_p;
        *vec_p = vhdr_p->data;
        vhdr_p->vec_capacity = new_capacity;
    };

    int_t idx = vhdr_p->vec_length;
    void *ent_p = vhdr_p->data + idx * vhdr_p->elem_size;
    memcpy(ent_p, data, vhdr_p->elem_size);
    ++vhdr_p->vec_length;

    return idx;
failed:
    return -1;
}

int _c_vector_reserve(_VectorHandle *vec_p, uint_t n) {
    if (!vec_p || !*vec_p) goto failed;
    __auto_type vhdr_p = ((_VectorHeader*)*vec_p) - 1;

    if (vhdr_p->vec_capacity < n) {
        __auto_type new_vhdr_p =
            (_VectorHeader*)realloc(
                vhdr_p,
                sizeof(_VectorHeader) + n * vhdr_p->elem_size
            );
        if (!new_vhdr_p) goto failed;
        vhdr_p = new_vhdr_p;
        *vec_p = vhdr_p->data;
        vhdr_p->vec_capacity = n;
    };

    return 0;
failed:
    return -1;
}

int_t _c_vector_pop_back(_VectorHandle vec) {
    if (!vec) goto failed;
    __auto_type vhdr_p = ((_VectorHeader*)vec) - 1;
    if (!vhdr_p->vec_length) goto failed;
    --vhdr_p->vec_length;
    return vhdr_p->vec_length - 1;
failed:
    return -1;
}

void *_c_vector_get(const _VectorHandle vec, uint_t idx) {
    if (!vec) goto failed;
    __auto_type vhdr_p = ((_VectorHeader*)vec) - 1;
    if (idx >= vhdr_p->vec_length) goto failed;
    return vhdr_p->data + idx * vhdr_p->elem_size;
failed:
    return NULL;
}

uint_t _c_vector_size(const _VectorHandle vec) {
    if (!vec) goto failed;
    __auto_type vhdr_p = ((_VectorHeader*)vec) - 1;
    return vhdr_p->vec_length;
failed:
    return 0;
}

void *_c_vector_iterator_begin(const _VectorHandle vec) {
    if (!vec) goto failed;
    __auto_type vhdr_p = ((_VectorHeader*)vec) - 1;
    return vhdr_p->data;
failed:
    return 0;
}

void *_c_vector_iterator_end(const _VectorHandle vec) {
    if (!vec) goto failed;
    __auto_type vhdr_p = ((_VectorHeader*)vec) - 1;
    return vhdr_p->data + vhdr_p->vec_length * vhdr_p->elem_size;
failed:
    return 0;
}

void *_c_vector_iterator_next(const _VectorHandle vec, const void *it) {
    if (!vec) goto failed;
    __auto_type vhdr_p = ((_VectorHeader*)vec) - 1;
    return (byte_t*)it + vhdr_p->elem_size;
failed:
    return 0;
}


uint_t _c_vector_capacity(const _VectorHandle vec) {
    if (!vec) goto failed;
    __auto_type vhdr_p = ((_VectorHeader*)vec) - 1;
    return vhdr_p->vec_capacity;
failed:
    return 0;
}

void _c_vector_destroy(_VectorHandle vec) {
    if (!vec) return;
    __auto_type vhdr_p = ((_VectorHeader*)vec) - 1;
    free(vhdr_p);
}

void _c_vector_clear(_VectorHandle vec) {
    if (!vec) return;
    __auto_type vhdr_p = ((_VectorHeader*)vec) - 1;
    vhdr_p->vec_length = 0;
    memset(vhdr_p->data, 0, vhdr_p->elem_size * vhdr_p->vec_capacity);
}
