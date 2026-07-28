#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct _c_kvsymdb kvsymdb_t;
typedef struct _c_kvsymdb_reader kvsymdb_reader_t;

typedef struct _kvsymdb_view {
    uint32_t        ent_count;      // [0]
    uint32_t        ent_capacity;   // [1]
    uint32_t        buf_len;        // [2]
    uint32_t        buf_size;       // [3]
    const void     *_state_arr;     // [4]
    const uint8_t  *arena_buf;      // [5]
} kvsymdb_state_t;

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
    size_t          size;
    const void     *data;
} kvsymdb_bufview_t;

typedef kvsymdb_entry_t *kvsymdb_iterator_t;


struct _c_kvsymdb_reader {
    const kvsymdb_t    *_symdb_p;   // [0]
    uint32_t            _pos;       // [1]
};

// newly introduced: hash index table

typedef struct _kvsymdb_hidx_slot kvsymdb_hidx_slot_t;
struct _kvsymdb_hidx_slot {
    const kvsymdb_entry_t      *ent_p;          // non owning reference
    struct _kvsymdb_hidx_slot  *prev_slot_p;    // prev
    struct _kvsymdb_hidx_slot  *next_slot_p;    // next
};

typedef struct _c_kvsymdb_hidx kvsymdb_hash_index_t;

extern kvsymdb_t *create_kvsymdb(uint32_t entc, uint32_t bufsize, int *out_errno_p);
extern void destroy_kvsymdb(kvsymdb_t *symdb_p);
extern int kvsymdb_get_intrnl_state_view(
    const kvsymdb_t    *symdb_p,
    kvsymdb_state_t    *out_view_p,
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
extern int kvsymdb_get_entview(
    const kvsymdb_t        *symdb_p,
    const kvsymdb_entry_t  *ent_p,
    kvsymdb_entview_t      *out_entview_p,
    int                    *out_errno_p
);
extern int kvsymdb_compact(
    kvsymdb_t **symdb_pp,
    int        *out_errno_p
);

extern kvsymdb_iterator_t kvsymdb_iterator_begin(kvsymdb_t *symdb_p);
extern kvsymdb_iterator_t kvsymdb_iterator_end(kvsymdb_t *symdb_p);
extern kvsymdb_iterator_t kvsymdb_iterator_next(
    kvsymdb_t          *symdb_p,
    kvsymdb_iterator_t  iter
);
extern const char *kvsymdb_strerror(int kvsymdb_errno);

// New APIs since 13/07/2026
extern int kvsymdb_reader_bind(
    kvsymdb_reader_t   *reader_p,
    const kvsymdb_t    *symdb_p,
    int                *out_errno_p
);
extern const kvsymdb_entry_t *kvsymdb_reader_read(
    kvsymdb_reader_t   *reader_p,
    int                *out_errno_p
);
extern int kvsymdb_reader_rewind(
    kvsymdb_reader_t   *reader_p
);
extern void kvsymdb_reader_unbind(
    kvsymdb_reader_t   *reader_p
);



extern kvsymdb_hash_index_t *
create_kvsymdb_hash_index(
    const kvsymdb_t    *symdb_p,
    int                *out_errno_p
);
extern void destroy_kvsymdb_hash_index(
    kvsymdb_hash_index_t *c_hidx_p
);
extern int kvsymdb_hidx_insert(
    kvsymdb_hash_index_t   *c_hidx_p,
    const kvsymdb_entry_t  *ent_p,
    int                    *out_errno_p
);
extern const kvsymdb_entry_t *
kvsymdb_hidx_lookup(
    kvsymdb_hash_index_t       *c_hidx_p,
    const kvsymdb_bufview_t    *key_p,
    int                        *out_errno_p
);
extern int kvsymdb_hidx_remove(
    kvsymdb_hash_index_t   *c_hidx_p,
    const kvsymdb_entry_t  *ent_p,
    int                    *out_errno_p
);



enum kvsymdb_return_code {
    KVSYMDB_SUCCESS = 0,
    KVSYMDB_FAILED  = -1,
};

typedef struct _c_kvsymdb_file_header {
    uint32_t    fh_magic;   // [0]
    uint16_t    fh_version; // [1]
    uint16_t    fh_align;   // [2]
    uint32_t    fh_entcnt;  // [3]
    uint32_t    fh_buflen;  // [4]
    uint32_t    fh_crc32;   // [5]
    uint8_t     fh_data[];  // [6]
} kvsymdb_file_header_t;

#include <stdio.h>
typedef struct _c_kvsymdb_file_reader {
    int                 fileno;
    kvsymdb_bufview_t   view;
} kvsymdb_file_reader_t;

/*
typedef struct _c_kvsymdb_file_builder {
    uint8_t    *file_buf;
    uint32_t    file_size;
} kvsymdb_file_builder_t;
*/

// questionable api
kvsymdb_t *create_kvsymdb_file_reader(const char *filename, int *out_errno_p);


#ifdef __cplusplus
}
#endif /*__cplusplus*/


#ifdef __cplusplus
#include <_cxx_kvsymdb.hpp>
#endif /* __cplusplus */

