#pragma once

#include <intdef.h>

typedef void *_VectorHandle;
#define VECTOR_INVALID_HANDLE NULL




extern _VectorHandle _c_vector_create(ushort_t elem_size);
extern int_t _c_vector_push_back(_VectorHandle *vec_p, void *data);
extern int_t _c_vector_pop_back(_VectorHandle vec);
extern void *_c_vector_get(const _VectorHandle vec, uint_t idx);
extern void _c_vector_destroy(_VectorHandle vec);
extern uint_t _c_vector_size(const _VectorHandle vec);
extern uint_t _c_vector_capacity(const _VectorHandle vec);
extern void *_c_vector_iterator_begin(const _VectorHandle vec);
extern void *_c_vector_iterator_end(const _VectorHandle vec);
extern void *_c_vector_iterator_next(const _VectorHandle vec, const void *it);
extern void _c_vector_clear(_VectorHandle vec);
extern int _c_vector_reserve(_VectorHandle *vec_p, uint_t n);


#ifndef __cplusplus

#define Vector(T) T*
#define VectorIterator(T) T*
#define VecElemPtrType(T) T*
#define vector_create(T) _c_vector_create(sizeof(T))
#define vector_destroy(vec) _c_vector_destroy((_VectorHandle)vec)
#define vector_push_back(vec_p, elem_p) _c_vector_push_back((_VectorHandle*)vec_p, elem_p) 
#define vector_pop_back(vec) _c_vector_pop_back((_VectorHandle)vec)
#define vector_get(T, vec, idx) (T*)_c_vector_get((_VectorHandle)vec, idx)
#define vector_size(vec) _c_vector_size((_VectorHandle)vec)
#define vector_capacity(vec) _c_vector_capacity((_VectorHandle)vec)
#define vector_begin(vec) _c_vector_iterator_begin((_VectorHandle)vec)
#define vector_end(vec) _c_vector_iterator_end((_VectorHandle)vec)
#define vector_next(vec, it) _c_vector_iterator_next((_VectorHandle)vec, it)
#define vector_clear(vec) _c_vector_clear((_VectorHandle)vec)
#define vector_reserve(vec_p, n) _c_vector_reserve((_VectorHandle*)vec_p, n)

#endif /* __cplusplus */
