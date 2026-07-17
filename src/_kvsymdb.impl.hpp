#include <assert.h>
#include <string.h>
#include <new>
#include <kvsymdb.h>

extern "C" {
#include <fnv1a_hash.h>
}

#if defined(_DEBUG) && !defined(_NO_DBG_PRINT)
#include <stdio.h>
#define dbg_print(...) \
    do { \
        fprintf(stderr, __VA_ARGS__);putc('\n', stderr);fflush(stderr); \
    } while(0)

#define dbg_log_msg(msg) \
    do {\
        fprintf(stderr, "<%s@file '%s' line%d> %s\n", __func__, __FILE__, __LINE__, msg);\
    } while(0)

#else
#define dbg_print(...)
#define dbg_log_msg(msg)
#endif


namespace align_utils {
    template<typename T, T align_size>
    inline T align_off(T base_off) {
        return (base_off + align_size - 1) &~ (align_size - 1);
    }
    template<typename T, T align_size>
    inline bool is_aligned_off(T base_off) {
        return align_off<T, align_size>(base_off) == base_off;
    }
    template<typename T>
    inline bool is_pow2(T num) {
        return (num > 0) && ((num & (num - 1)) == 0);
    }
    template<typename T>
    inline T num_next_pow2(T num) {
        if (!num) return 1u;
        return (sizeof(T) == sizeof(uint64_t)) ?
            1ULL << (64ULL - __builtin_clzll(static_cast<unsigned long long>(num - 1))) :
            1u << (32u - __builtin_clz(static_cast<unsigned int>(num - 1)));
    }
}

namespace _kvsymdb_intrnl {
    namespace error_code {
        enum _errno {
            NOERROR,
            ERR_OPNEW,
            ERR_OPNEWARR,
            ERR_NULLPTR,
            ERR_BUFVIEW,
            ERR_BUFFULL,
            ERR_FULL,
            ERR_RESIZE,
            ERR_RESIZEBUF,
            ERR_ENTVIEW,
            ERR_EMPTY,
            ERR_BADIDX,
            ERR_BADOFF,
            ERR_BADENT,     // 1
            ERR_GC,
            ERR_CREATE,
            ERR_INSERT,
            ERR_RDEOF,      // 2
            ERR_BADALIGN,
            ERR_HT_INIT,    // 3
            ERR_POOL_FULL,  // 4
            ERR_ADDR_OUT_OF_RANGE,
            ERR_PTR_UNALIGNED,
            ERR_POOL_ALLOC,
            ERR_KEY_NOT_FOUND,  // 5
            ERR_BAD_HTLURES,    // 6
        };
    };

    namespace state {
        enum _state {
            ST_EMPTY    = 0,
            ST_ALIVE    = 1,
            ST_DEAD     = 2,
        };
    }

    namespace align {
        template<uint32_t align_size>
        inline uint32_t cstr_size_aligned(uint32_t len) {
            return align_utils::align_off<uint32_t, align_size>(len + 1);
        }

        template<uint32_t align_size>
        inline uint32_t blob_size_aligned(uint32_t size) {
            return align_utils::align_off<uint32_t, align_size>(size);
        }

        template<uint32_t align_size>
        inline uint32_t entsize_required(
            const kvsymdb_bufview_t    *key_p,
            const kvsymdb_bufview_t    *val_p
        ) {
            assert(key_p && val_p);

            return
                sizeof(kvsymdb_record_header_t) + 
                cstr_size_aligned<align_size>(key_p->size) +
                blob_size_aligned<align_size>(val_p->size);
        }
    }


    static inline int reserve(
        kvsymdb_t  *symdb_p,
        uint32_t    new_entc,
        int        *out_errno_p
    );
    static inline void assert_intrnl_state(const kvsymdb_t *symdb_p);
    static inline bool is_valid_entoff(
        const kvsymdb_t    *symdb_p,
        uint32_t            off
    );
    static inline bool is_valid_entry(
        const kvsymdb_t        *symdb_p,
        const kvsymdb_entry_t  *ent_p
    );
    static inline int reserve_arenabuf(
        kvsymdb_t **symdb_pp,
        uint32_t    new_bufsize,
        int        *out_errno_p
    );
    static inline uint32_t record_size(
        const kvsymdb_entry_t  *ent_p 
    );
    static inline void get_entview(
        const kvsymdb_t        *symdb_p,
        const kvsymdb_entry_t  *ent_p,
        kvsymdb_entview_t      *out_entview_p
    );
    static inline const char *strerror(int db_errno);
}

extern "C" { // c abi structs; implmtation detail
    struct _c_kvsymdb {
        uint32_t    _entrycnt;      // [0]
        uint32_t    _entrycap;      // [1]
        uint32_t    _buf_len;       // [2]
        uint32_t    _buf_size;      // [3]
        uint8_t    *_state_arr;     // [4]
        uint8_t     _arena_buf[];   // [5]
    };
}


static inline void _kvsymdb_intrnl::get_entview(
    const kvsymdb_t        *symdb_p,
    const kvsymdb_entry_t  *ent_p,
    kvsymdb_entview_t      *out_entview_p
) {
    assert(symdb_p);
    assert(ent_p);
    assert(out_entview_p);

    out_entview_p->id       = ent_p->id;
    out_entview_p->hash     = ent_p->hash;
    out_entview_p->type     = ent_p->type;
    out_entview_p->name     = reinterpret_cast<const char*>(ent_p->payload); 
    out_entview_p->name_len = ent_p->name_len;
    out_entview_p->data     = \
        reinterpret_cast<const char*>(
            ent_p->payload +
            _kvsymdb_intrnl::align::\
                cstr_size_aligned<cxx_kvsymdb::ALIGN_SIZE>(ent_p->name_len)
        );

    out_entview_p->data_len = ent_p->data_len;
    out_entview_p->_record  = static_cast<const void*>(ent_p);
}


static inline int _kvsymdb_intrnl::reserve_arenabuf(
    kvsymdb_t **symdb_pp,
    uint32_t    new_bufsize,
    int        *out_errno_p
) {
    assert(symdb_pp && *symdb_pp && out_errno_p);
    assert((*symdb_pp)->_state_arr);
    assert(new_bufsize >= (*symdb_pp)->_buf_len);

    kvsymdb_t *old_symdb_p = *symdb_pp;
    kvsymdb_t *new_symdb_p = static_cast<kvsymdb_t*>(
        operator new(sizeof(kvsymdb_t) + new_bufsize, std::nothrow)
    );
    if (!new_symdb_p) {
        *out_errno_p = _kvsymdb_intrnl::error_code::ERR_OPNEW;
        dbg_log_msg("");
        dbg_print(
            "_kvsymdb_intrnl::reserve_arenabuf failed: %s\n",
            _kvsymdb_intrnl::strerror(*out_errno_p)
        );
        goto failed_ret;
    }
    memset(new_symdb_p, 0, sizeof(kvsymdb_t) + new_bufsize);

    new_symdb_p->_entrycnt  = old_symdb_p->_entrycnt;
    new_symdb_p->_entrycap  = old_symdb_p->_entrycap;
    new_symdb_p->_buf_len   = old_symdb_p->_buf_len;
    new_symdb_p->_buf_size  = new_bufsize;
    new_symdb_p->_state_arr = old_symdb_p->_state_arr; // ptr swap

    memcpy(
        new_symdb_p->_arena_buf,
        old_symdb_p->_arena_buf,
        old_symdb_p->_buf_len
    );
    operator delete(old_symdb_p);
    *symdb_pp = new_symdb_p;

    return KVSYMDB_SUCCESS;
failed_ret:
    return KVSYMDB_FAILED;
}

static inline int
_kvsymdb_intrnl::reserve(
    kvsymdb_t  *symdb_p,
    uint32_t    new_entc,
    int        *out_errno_p
) {
    assert(symdb_p && out_errno_p);
    assert(new_entc >= symdb_p->_entrycnt);
    assert(symdb_p->_state_arr);

    uint8_t *new_state_arr = static_cast<uint8_t*>(
        operator new[](new_entc * sizeof(uint8_t), std::nothrow)
    );
    if (!new_state_arr) {
        *out_errno_p = _kvsymdb_intrnl::error_code::ERR_OPNEWARR;
        dbg_log_msg("");
        dbg_print(
            "_kvsymdb_intrnl::reserve failed: %s\n",
            _kvsymdb_intrnl::strerror(*out_errno_p)
        );
        return -1;
    }

    memset(new_state_arr, 0, new_entc * sizeof(uint8_t));
    memcpy(
        new_state_arr,
        symdb_p->_state_arr,
        symdb_p->_entrycnt * sizeof(uint8_t)
    );

    operator delete[](symdb_p->_state_arr);
    symdb_p->_state_arr = new_state_arr;
    symdb_p->_entrycap = new_entc;

    return KVSYMDB_SUCCESS;
}

static inline void
_kvsymdb_intrnl::assert_intrnl_state(const kvsymdb_t *symdb_p) {
    assert(symdb_p->_buf_size >= cxx_kvsymdb::INIT_BUFSIZE);
    assert(symdb_p->_entrycap >= cxx_kvsymdb::INIT_ENTC);
    assert(symdb_p->_buf_len <= symdb_p->_buf_size);
    bool is_aligned_buf_len = align_utils::\
        is_aligned_off<uint32_t, cxx_kvsymdb::ALIGN_SIZE>(symdb_p->_buf_len);
    assert(is_aligned_buf_len);
    assert(symdb_p->_entrycnt <= symdb_p->_entrycap);
    assert(symdb_p->_state_arr);
}

static inline bool
_kvsymdb_intrnl::is_valid_entoff(
    const kvsymdb_t    *symdb_p,
    uint32_t            off
) {
    assert(symdb_p);
    if (off + sizeof(kvsymdb_record_header_t) > symdb_p->_buf_len)
        return false;

    const kvsymdb_entry_t *ent_p =
        reinterpret_cast<const kvsymdb_entry_t*>(symdb_p->_arena_buf + off);

    return (
        off + ent_p->record_len <= symdb_p->_buf_len &&
        align_utils::is_aligned_off<uint32_t, cxx_kvsymdb::ALIGN_SIZE>(off) &&
        align_utils::is_aligned_off<uint32_t, cxx_kvsymdb::ALIGN_SIZE>(ent_p->record_len)
    );
}

static inline bool _kvsymdb_intrnl::is_valid_entry(
    const kvsymdb_t        *symdb_p,
    const kvsymdb_entry_t  *ent_p
) {
    assert(symdb_p && ent_p);
    auto entry_size = _kvsymdb_intrnl::record_size(ent_p);

    auto record_begin = 
        reinterpret_cast<const uint8_t*>(ent_p);

    auto record_end =
        reinterpret_cast<const uint8_t*>(ent_p) + entry_size;

    auto arenabuf_begin = symdb_p->_arena_buf;
    auto arenabuf_end = symdb_p->_arena_buf + symdb_p->_buf_len;

    return (
        ent_p &&
        record_begin >= arenabuf_begin &&
        record_end <= arenabuf_end &&
        ent_p->id < symdb_p->_entrycnt &&
        entry_size == ent_p->record_len &&
        align_utils::is_aligned_off<uint32_t, cxx_kvsymdb::ALIGN_SIZE>(ent_p->record_len)
    );
}

static inline uint32_t
_kvsymdb_intrnl::record_size(const kvsymdb_entry_t *ent_p) {
    return
        sizeof(*ent_p) +
        _kvsymdb_intrnl::align::\
            cstr_size_aligned<cxx_kvsymdb::ALIGN_SIZE>(ent_p->name_len) +
        _kvsymdb_intrnl::align::\
            blob_size_aligned<cxx_kvsymdb::ALIGN_SIZE>(ent_p->data_len);
}

static inline const char *_kvsymdb_intrnl::strerror(int db_errno) {
    using namespace error_code;

    switch (db_errno) {
        case (ERR_OPNEW):
            return "operator new: out of memory (ERR_OPNEW)";
        case (ERR_OPNEWARR):
            return "operator new[]: out of memory (ERR_OPNEWARR)";
        case (ERR_NULLPTR):
            return "passing nullptr";
        case (ERR_BUFVIEW):
            return "invalid buffer view";
        case (ERR_BUFFULL):
            return "arena buffer full";
        case (ERR_FULL):
            return "capacity full";
        case (ERR_RESIZE):
            return "resize failed";
        case (ERR_RESIZEBUF):
            return "resize buffer failed";
        case (ERR_ENTVIEW):
            return "invalid entry view";
        case (ERR_EMPTY):
            return "capacity empty";
        case (ERR_BADIDX):
            return "invalid entry idx";
        case (ERR_BADOFF):
            return "invalid offset";
        case (ERR_BADENT):
            return "invalid entry";
        case (ERR_GC):
            return "compaction failure";
        case (ERR_CREATE):
            return "failed to create db";
        case (ERR_INSERT):
            return "failed to insert entry";
        case (ERR_RDEOF):
            return "arena EOF reached";
        case (ERR_BADALIGN):
            return "unaligned offset";
        case (ERR_HT_INIT):
            return "hash table init failed";
        case (ERR_POOL_FULL) :
            return "pool allocator full";
        case (ERR_ADDR_OUT_OF_RANGE):
            return "ptr addr out of valid range";
        case (ERR_PTR_UNALIGNED):
            return "unaligned ptr for given type";
        case (ERR_POOL_ALLOC):
            return "pool allocation failed";
        case (ERR_KEY_NOT_FOUND):
            return "key not found";

        default:
            break;
    }

    return "no error";   
}

