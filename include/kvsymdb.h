#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct _c_kvsymdb kvsymdb_t;

typedef struct _kvsymdb_view {
    uint32_t        ent_count;      // [0]
    uint32_t        ent_capacity;   // [1]
    uint32_t        buf_len;        // [2]
    uint32_t        buf_size;       // [3]
    const void     *_state_arr;     // [4]
    const uint8_t  *arena_buf;      // [5]
} kvsymdb_view_t;

typedef struct _kvsymdb_record_header {
    uint32_t    id;         // [0]; entry idx
    uint32_t    hash;       // [1]
    uint16_t    type;       // [2]
    uint16_t    name_len;   // [3]; must be 0 terminated cstr
    uint32_t    data_len;   // [4]
    uint32_t    record_len; // [5]
    uint8_t     payload[];  // [6]; [name]['\0'][data]
} kvsymdb_record_header_t;
typedef kvsymdb_record_header_t kvsymdb_entry_t;

typedef struct _kvsymdb_entview {
    uint32_t    id;         // [0]
    uint32_t    hash;       // [1]
    uint16_t    type;       // [2]
    const char *name;       // [3]
    uint32_t    name_len;   // [4]
    const void *data;       // [5]
    uint32_t    data_len;   // [6]
    const void *_record;    // [7] ; opaque
} kvsymdb_entview_t;

typedef struct _kvsymdb_bufview {
    size_t  size;
    void   *data;
} kvsymdb_bufview_t;

typedef struct _kvsymdb_iterator {
    kvsymdb_entry_t    *_ent_p;
} kvsymdb_iterator_t;

typedef struct _c_kvsymdb_reader kvsymdb_reader_t;

extern kvsymdb_t *create_kvsymdb(uint32_t entc, uint32_t bufsize, int *out_errno_p);
extern void destroy_kvsymdb(kvsymdb_t *symdb_p);
extern int kvsymdb_get_view(
    const kvsymdb_t    *symdb_p,
    kvsymdb_view_t     *out_view_p,
    int                *out_errno_p
);
extern int kvsymdb_reserve(
    kvsymdb_t  *symdb_p,
    uint32_t    new_entc,
    int        *out_errno_p
);
extern int kvsymdb_reserve_arenabuf(
    kvsymdb_t **symdb_pp,
    uint32_t    new_bufsize,
    int        *out_errno_p
);
extern int kvsymdb_insert(
    kvsymdb_t                 **symdb_pp,
    const kvsymdb_bufview_t    *key_p,
    const kvsymdb_bufview_t    *val_p,
    uint32_t                    key_hash,
    uint16_t                    type,
    int                        *out_errno_p
);
extern int kvsymdb_mark_dead(
    kvsymdb_t          *symdb_p,
    kvsymdb_entry_t    *ent_p,
    int                *out_errno_p
);
extern bool kvsymdb_is_valid_entry(
    const kvsymdb_t        *symdb_p,
    const kvsymdb_entry_t  *ent_p
);
/*
extern const kvsymdb_entry_t *kvsymdb_read_entry(
    kvsymdb_t  *symdb_p,
    uint32_t    off,
    int        *out_errno_p
);    
*/
extern uint32_t _kvsymdb_read_buf(
    kvsymdb_t  *symdb_p,
    uint32_t    off,
    void       *dest_buf,
    uint32_t    buf_size,
    int        *out_errno_p
);
extern kvsymdb_iterator_t kvsymdb_iterator_begin(kvsymdb_t *symdb_p);
extern kvsymdb_iterator_t kvsymdb_iterator_end(kvsymdb_t *symdb_p);
extern kvsymdb_iterator_t kvsymdb_iterator_next(
    kvsymdb_t          *symdb_p,
    kvsymdb_iterator_t  iter
);
extern const char *kvsymdb_strerror(int kvsymdb_errno);

enum kvsymdb_return_code {
    KVSYMDB_SUCCESS = 0,
    KVSYMDB_FAILED  = -1,
};


#ifdef __cplusplus
}
#endif /*__cplusplus*/


#ifdef __cplusplus
namespace cxx_kvsymdb {
    static constexpr uint32_t ALIGN_SIZE    = 4u;
    static constexpr uint32_t INIT_BUFSIZE  = 32u;
    static constexpr uint32_t INIT_ENTC     = 1u;

    struct kvsymdb {
    private:
        kvsymdb_t  *_symdb_p;
        int         _errno;
    public:
        inline kvsymdb(uint32_t entc) noexcept
            : _symdb_p(nullptr), _errno(0)
        {
            this->_symdb_p = create_kvsymdb(entc, INIT_BUFSIZE, &this->_errno);
        }
        inline ~kvsymdb() noexcept {
            if (!this->_symdb_p) return;
            destroy_kvsymdb(this->_symdb_p);
            this->_symdb_p = nullptr;
        }

        void clearerr() noexcept {
            this->_errno = 0;
        }
        const char *errmsg() noexcept {
            return kvsymdb_strerror(this->_errno);
        }
        bool is_init() noexcept {
            return (!!this->_symdb_p);
        }

        int reserve(uint32_t entc) noexcept {
            return kvsymdb_reserve(this->_symdb_p, entc, &this->_errno);
        }
        int reserve_buffer(uint32_t new_bufsize) noexcept {
            return kvsymdb_reserve_arenabuf(
                &this->_symdb_p, new_bufsize, &this->_errno
            );
        }
        int get_self_view(kvsymdb_view_t *out_view_p) noexcept {
            return kvsymdb_get_view(this->_symdb_p, out_view_p, &this->_errno);
        }
        int insert (
            const kvsymdb_bufview_t    *key_p,
            const kvsymdb_bufview_t    *val_p,
            uint32_t                    key_hash,
            uint16_t                    type
        ) noexcept {
            return kvsymdb_insert(
                &this->_symdb_p,
                key_p,
                val_p,
                key_hash,
                type,
                &this->_errno
            );
        }
        int mark_dead(kvsymdb_entry_t *ent_p) noexcept {
            return kvsymdb_mark_dead(
                this->_symdb_p,
                ent_p,
                &this->_errno
            );
        }
        bool is_valid_entry(const kvsymdb_entry_t *ent_p) noexcept {
            return kvsymdb_is_valid_entry(this->_symdb_p, ent_p);
        }
        struct iterator {
            kvsymdb_entry_t *_ent_p;
            iterator(kvsymdb_entry_t *ent_p) noexcept {
                this->_ent_p = ent_p;
            }
        };

        iterator begin() noexcept {
            return iterator(
                kvsymdb_iterator_begin(this->_symdb_p)._ent_p
            );
        }
        iterator end() noexcept {
            return iterator(
                kvsymdb_iterator_end(this->_symdb_p)._ent_p
            );
        }
        iterator next(iterator &it) noexcept {
            return iterator(
                kvsymdb_iterator_next(
                    this->_symdb_p,
                    (kvsymdb_iterator_t) { it._ent_p }
                )._ent_p
            );
        }
        const kvsymdb_entry_t &deref(iterator &it) noexcept {
            return *it._ent_p;
        }

        kvsymdb(const kvsymdb&) = delete;
        kvsymdb& operator=(const kvsymdb&) = delete;
    };
}

#endif /* __cplusplus */

