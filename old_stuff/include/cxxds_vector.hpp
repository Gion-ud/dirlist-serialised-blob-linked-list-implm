extern "C" {
#include "vector.h"
#include <string.h>
}


/*
 * my own rule of five:
 * - ctor and dtor (preferably ctor only zero init 
 *      with separate obj.init() to be able to get error stat code)
 * - dtor MUST be double destruction safe
 * - c++ copy ctor, copy/move assignment deleted; no std::move()
 * - replaced by obj.move_in(&src_obj), obj.move_out(&dest_obj), obj.copy_from(&src_obj)
 * - minimise the use of T&

*/

typedef struct _VectorHeader {
    ushort_t    elem_size;
    uint_t      vec_length;
    uint_t      vec_capacity;
    byte_t      data[];
} _VectorHeader;

namespace cxxds {

template<typename T>
struct vector {
private:
    T  *_vec;
public:
    vector() : _vec(nullptr) { this->_init(); }
    ~vector() { this->_destroy(); }
    inline bool is_valid() {
        return !!(this->_vec);
    };
    inline void clear() {
        _c_vector_clear(this->_vec);
    }

    vector(const vector&) = delete;
    vector& operator=(const vector&) = delete;

    inline const T *_init() {
        auto *vec = static_cast<T*>(_c_vector_create(sizeof(T)));
        this->_vec = vec;
        return vec;
    }
    inline void _destroy() {
        _c_vector_destroy(this->_vec);
        this->_vec = nullptr;
    }
    inline int reserve(uint_t n) {
        return _c_vector_reserve(
            reinterpret_cast<_VectorHandle*>(&this->_vec), n
        );
    }
    inline int_t push_back(const T &item_ref) {
        return _c_vector_push_back(
            reinterpret_cast<_VectorHandle*>(&this->_vec),
            const_cast<T*>(&item_ref)
        );
    }
    inline int_t pop_back() {
        return _c_vector_pop_back(this->_vec);
    }
    inline const T *get(uint_t idx) {
        return static_cast<T*>(_c_vector_get(this->_vec, idx));
    }
    inline const T *data() {
        return this->_vec;
    }
    inline T &operator[](uint_t idx) {
        return this->_vec[idx];
    }
    inline uint_t size() {
        return _c_vector_size(this->_vec);
    }
    inline uint_t capacity() {
        return _c_vector_capacity(this->_vec);
    }
    using iterator = T*;
    inline iterator begin() {
        return static_cast<iterator>(_c_vector_iterator_begin(this->_vec));
    }
    inline iterator end() {
        return static_cast<iterator>(_c_vector_iterator_end(this->_vec));
    }
    inline iterator next(iterator it) {
        return static_cast<iterator>(_c_vector_iterator_next(this->_vec, static_cast<T*>(it)));
    }
    inline iterator rbegin() {
        return this->end() - 1;
    }
    inline iterator rend() {
        return this->begin() - 1;
    }
    inline iterator rnext(iterator it) {
        return it - 1;
    }
    inline const T &deref(iterator it) {
        return *it;
    }
    inline const T &front() {
        return *(this->begin());
    }
    inline const T &back() {
        return *(this->end() - 1);
    }
    inline void move_in(vector *in_p) {
        this->_destroy();
        this->_vec = in_p->_vec;
        in_p->_vec = nullptr;
    }
    inline void move_out(vector *out_p) {
        out_p->_vec = this->_vec;
        this->_vec = nullptr;
    }
    inline void copy_from(vector *in_p) {
        this->_destroy();
        this->_init();
        for (auto it = in_p->begin(); it != in_p->end(); it = in_p->next(it)) {
            this->push_back(*it);
        }
    }
};

template<typename T, uint_t N>
struct static_vector {
private:
    byte_t          _buf[sizeof(_VectorHeader) + sizeof(T) * N];
    _VectorHeader  *_vhdr_p;
    T              *_data;
    inline _VectorHeader *_get_header() {
        return reinterpret_cast<_VectorHeader*>(this->_buf);
    }
    inline T *_get_data(_VectorHeader *vhdr_p) {
        return reinterpret_cast<T*>(vhdr_p->data);
    }
public:
    static_vector() : _buf{0}, _vhdr_p(nullptr), _data(nullptr) {
        this->_vhdr_p   = this->_get_header();
        this->_data     = this->_get_data(this->_vhdr_p);

        this->_vhdr_p->elem_size    = sizeof(T);
        this->_vhdr_p->vec_length   = 0;
        this->_vhdr_p->vec_capacity = N;
    }
    ~static_vector() {
        this->_vhdr_p   = nullptr;
        this->_data     = nullptr;
    }
    inline void clear() {
        this->_vhdr_p->vec_length = 0;
    }

    static_vector(const static_vector&) = delete;
    static_vector& operator=(const static_vector&) = delete;

    int_t push_back(const T &item_ref) {
        uint_t idx = this->_vhdr_p->vec_length;
        if (idx >= this->_vhdr_p->vec_capacity) return -1;
        memcpy(&this->_data[idx], &item_ref, sizeof(T));
        ++this->_vhdr_p->vec_length;
        return idx;
    }
    inline int_t pop_back() {
        if (!this->_vhdr_p->vec_length) return -1;
        return --this->_vhdr_p->vec_length;
    }
    inline const T *get(uint_t idx) {
        return (idx >= this->_vhdr_p->vec_length)
            ? nullptr : this->_data[idx];
    }
    inline T &operator[](uint_t idx) {
        return this->_data[idx];
    }
    inline uint_t size() {
        return this->_vhdr_p->vec_length;
    }
    inline uint_t capacity() {
        return this->_vhdr_p->vec_capacity;
    }
    using iterator = T*;
    inline iterator begin() {
        return this->_data;
    }
    inline iterator end() {
        return this->_data + this->_vhdr_p->vec_length;
    }
    inline iterator next(iterator it) {
        return it + 1;
    }
    inline iterator rbegin() {
        return this->end() - 1;
    }
    inline iterator rend() {
        return this->begin() - 1;
    }
    inline iterator rnext(iterator it) {
        return it - 1;
    }
    inline const T &deref(iterator it) {
        return *it;
    }
    inline const T &front() {
        return *(this->begin());
    }
    inline const T &back() {
        return *(this->end() - 1);
    }
};


};

