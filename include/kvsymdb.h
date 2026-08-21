#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct _c_kvsymdb kvsymdb_t;
typedef struct _c_kvsymdb_reader kvsymdb_reader_t;

typedef struct _c_kvsymdb_state {
    uint32_t        ent_count;      // [0]
    uint32_t        ent_capacity;   // [1]
    uint32_t        buf_len;        // [2]
    uint32_t        buf_size;       // [3]
    int             err_code;       // [4]
    const void     *_state_arr;     // [5]
    const uint8_t  *arena_buf;      // [6]
} kvsymdb_state_t;

typedef struct _c_kvsymdb_record_header {
    uint32_t    id;         // [0]; entry idx
    uint32_t    hash;       // [1]
    uint16_t    type;       // [2]
    uint16_t    name_len;   // [3]; must be 0 terminated cstr
    uint32_t    data_len;   // [4]
    uint32_t    record_len; // [5]
    uint8_t     payload[];  // [6]; [name]['\0'][data]
} kvsymdb_record_header_t;
typedef kvsymdb_record_header_t kvsymdb_entry_t;

typedef struct _c_kvsymdb_entview {
    uint32_t    id;         // [0]
    uint32_t    hash;       // [1]
    uint16_t    type;       // [2]
    const char *name;       // [3]
    uint32_t    name_len;   // [4]
    const void *data;       // [5]
    uint32_t    data_len;   // [6]
    const void *_record;    // [7] ; opaque
} kvsymdb_entview_t;

typedef struct _c_kvsymdb_bufview {
    size_t      buf_size;
    const void *buf_data;
} kvsymdb_bufview_t;

typedef kvsymdb_entry_t *kvsymdb_iter_t;

// new error code
struct _c_kvsymdb_reader {
    kvsymdb_bufview_t   _arena_view;    // [0]
    uint32_t            _entc;          // [1]
    uint32_t            _pos;           // [2]
    int                 _err_code;      // [3]
};

extern kvsymdb_t *create_kvsymdb(
    uint32_t    entc,
    uint32_t    bufsize,
    int        *out_errno_p
);
extern void destroy_kvsymdb(kvsymdb_t *symdb_p);

extern int kvsymdb_get_intrnl_state(
    const kvsymdb_t    *symdb_p,
    kvsymdb_state_t    *out_view_p
);
extern int kvsymdb_reserve(
    kvsymdb_t  *symdb_p,
    uint32_t    new_entc
);
extern int kvsymdb_reserve_arenabuf(
    kvsymdb_t **symdb_pp,
    uint32_t    new_bufsize
);
extern int kvsymdb_insert(
    kvsymdb_t                 **symdb_pp,
    const kvsymdb_bufview_t    *key_p,
    const kvsymdb_bufview_t    *val_p,
    uint32_t                    key_hash,
    uint16_t                    type
);
extern int kvsymdb_mark_dead(
    kvsymdb_t          *symdb_p,
    kvsymdb_entry_t    *ent_p
);
extern int kvsymdb_get_entview(
    const kvsymdb_t        *symdb_p,
    const kvsymdb_entry_t  *ent_p,
    kvsymdb_entview_t      *out_entview_p
);
extern int kvsymdb_entry_to_view(
    const kvsymdb_bufview_t    *arena_view_p,
    uint32_t                    entc,
    const kvsymdb_entry_t      *ent_p,
    kvsymdb_entview_t          *out_entview_p
);
extern bool kvsymdb_is_valid_entry(
    const kvsymdb_t        *symdb_p,
    const kvsymdb_entry_t  *ent_p
);
/*
extern uint32_t _kvsymdb_read_buf(
    kvsymdb_t  *symdb_p,
    uint32_t    off,
    void       *dest_buf,
    uint32_t    buf_size
);
*/

extern int kvsymdb_compact(kvsymdb_t **symdb_pp);

extern kvsymdb_iter_t kvsymdb_iter_begin(kvsymdb_t *symdb_p);
extern kvsymdb_iter_t kvsymdb_iter_end(kvsymdb_t *symdb_p);
extern kvsymdb_iter_t kvsymdb_iter_next(
    kvsymdb_t      *symdb_p,
    kvsymdb_iter_t  iter
);


extern const char *kvsymdb_strerror(int err);
extern int kvsymdb_geterror(const kvsymdb_t *symdb_p);
extern const char *kvsymdb_errmsg(const kvsymdb_t *symdb_p);
extern void kvsymdb_clearerr(kvsymdb_t *symdb_p);

// New APIs since 13/07/2026
extern int kvsymdb_reader_bind(
    kvsymdb_reader_t           *reader_p,
    const kvsymdb_bufview_t    *arena_view_p,
    uint32_t                    entry_count
);
extern const kvsymdb_entry_t *
    kvsymdb_reader_read(kvsymdb_reader_t *reader_p);

extern int kvsymdb_reader_rewind(kvsymdb_reader_t *reader_p);
extern void kvsymdb_reader_unbind(kvsymdb_reader_t *reader_p);

extern int kvsymdb_file_builder_dump(
    const kvsymdb_t    *symdb_p,
    const char         *filename
);

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
typedef struct _c_kvsymdb_file_mapper {
    int         _fileno;
    void       *_base;
    size_t      _size;
} kvsymdb_file_mapper_t;

extern int kvsymdb_file_mapper_init(
    kvsymdb_file_mapper_t  *mapper_p,
    const char             *filename
);
extern void kvsymdb_file_mapper_cleanup(kvsymdb_file_mapper_t *mapper_p);


extern const int KVSYMDB_OK;

#ifdef __cplusplus
}
#endif /*__cplusplus*/


#ifdef __cplusplus
#include <_cxx_kvsymdb.hpp>
#endif /* __cplusplus */

