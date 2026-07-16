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
        int        *out_errno_p,
        uint32_t    new_entc
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
    int        *out_errno_p,
    uint32_t    new_entc
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


#define RETURN_FAILED_STAT_WITH_ERRNO(out_errno_p, errno)   \
    do {                                                    \
        *(out_errno_p) = (errno);                           \
        return KVSYMDB_FAILED;                              \
    } while(0)                                              \

#define RETURN_SET_ERRNO(out_errno_p, errno, ret)   \
    do {                                            \
        *(out_errno_p) = (errno);                   \
        return (ret);                               \
    } while(0)                                      \


extern "C" kvsymdb_t *
create_kvsymdb(uint32_t entc, uint32_t bufsize, int *out_errno_p) {
    if (!out_errno_p) return nullptr;
    *out_errno_p = _kvsymdb_intrnl::error_code::NOERROR;

    if (entc < cxx_kvsymdb::INIT_ENTC)
        entc = cxx_kvsymdb::INIT_ENTC;

    if (bufsize < cxx_kvsymdb::INIT_BUFSIZE)
        bufsize = cxx_kvsymdb::INIT_BUFSIZE;

    kvsymdb_t *_symdb_p =
        static_cast<kvsymdb_t*>(
            operator new(sizeof(kvsymdb_t) + bufsize, std::nothrow)
        );

    if (!_symdb_p) {
        *out_errno_p = _kvsymdb_intrnl::error_code::ERR_OPNEW;
        dbg_log_msg("");
        dbg_print(
            "create_kvsymdb failed: %s",
            _kvsymdb_intrnl::strerror(*out_errno_p)
        );
        goto failed_ret;
    }
    memset(_symdb_p, 0, sizeof(kvsymdb_t) + bufsize);

    _symdb_p->_entrycnt     = 0u;
    _symdb_p->_entrycap     = entc;
    _symdb_p->_buf_len      = 0u;
    _symdb_p->_buf_size     = bufsize;

    _symdb_p->_state_arr    =
        static_cast<uint8_t*>(
            operator new[](entc * sizeof(uint8_t), std::nothrow)
        );

    if (!_symdb_p->_state_arr) {
        *out_errno_p = _kvsymdb_intrnl::error_code::ERR_OPNEWARR;
        dbg_log_msg("");
        dbg_print(
            "create_kvsymdb failed: %s",
            _kvsymdb_intrnl::strerror(*out_errno_p)
        );
        goto failed;
    }
    memset(_symdb_p->_state_arr, 0, entc * sizeof(uint8_t));

    return _symdb_p;
failed:
    destroy_kvsymdb(_symdb_p);
failed_ret:
    return nullptr;
}

extern "C" void destroy_kvsymdb(kvsymdb_t *symdb_p) {
    if (!symdb_p) return;
    if (symdb_p->_state_arr)
        operator delete[](symdb_p->_state_arr);
    operator delete(symdb_p);
}

extern "C" int kvsymdb_get_intrnl_state_view(
    const kvsymdb_t    *symdb_p,
    kvsymdb_state_t    *out_view_p,
    int                *out_errno_p
) {
    if (!out_errno_p) return KVSYMDB_FAILED;
    *out_errno_p = _kvsymdb_intrnl::error_code::NOERROR;

    if (!symdb_p || !out_view_p)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_NULLPTR
        );

    _kvsymdb_intrnl::assert_intrnl_state(symdb_p);
    
    out_view_p->ent_count       = symdb_p->_entrycnt;
    out_view_p->ent_capacity    = symdb_p->_entrycap;
    out_view_p->buf_len         = symdb_p->_buf_len;
    out_view_p->buf_size        = symdb_p->_buf_size;
    out_view_p->_state_arr      = symdb_p->_state_arr;
    out_view_p->arena_buf       = symdb_p->_arena_buf;
    
    return KVSYMDB_SUCCESS;
}

extern "C" int kvsymdb_reserve(
    kvsymdb_t  *symdb_p,
    uint32_t    new_entc,
    int        *out_errno_p
) {
    if (!out_errno_p) return KVSYMDB_FAILED;
    *out_errno_p = _kvsymdb_intrnl::error_code::NOERROR;

    if (!symdb_p)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_NULLPTR
        );

    _kvsymdb_intrnl::assert_intrnl_state(symdb_p);

    // explicit check required for already enough cap
    if (symdb_p->_entrycap >= new_entc) return KVSYMDB_SUCCESS;

    int rc = _kvsymdb_intrnl::reserve(symdb_p, out_errno_p, new_entc);
    if (rc < 0)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_RESIZE
        );

    return KVSYMDB_SUCCESS;
}

extern "C" int kvsymdb_reserve_arenabuf(
    kvsymdb_t **symdb_pp,
    uint32_t    new_bufsize,
    int        *out_errno_p
) {
    *out_errno_p = _kvsymdb_intrnl::error_code::NOERROR;
    if (!out_errno_p) return KVSYMDB_FAILED;

    if (!symdb_pp || !*symdb_pp)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_NULLPTR
        );

    _kvsymdb_intrnl::assert_intrnl_state(*symdb_pp);

    // no need to resize if its already enough
    if ((*symdb_pp)->_buf_size >= new_bufsize) return KVSYMDB_SUCCESS;
    int rc = _kvsymdb_intrnl::\
        reserve_arenabuf(symdb_pp, new_bufsize, out_errno_p);

    if (rc < 0)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_RESIZEBUF
        );

    return KVSYMDB_SUCCESS;
}

extern "C" int kvsymdb_compact(
    kvsymdb_t **symdb_pp,
    int        *out_errno_p
) {
    using iterator      = kvsymdb_iterator_t;
    using entry_view    = kvsymdb_entview_t;
    using buffer_view   = kvsymdb_bufview_t;

    *out_errno_p = _kvsymdb_intrnl::error_code::NOERROR;
    if (!out_errno_p) return KVSYMDB_FAILED;

    if (!symdb_pp || !*symdb_pp)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_NULLPTR
        );

    _kvsymdb_intrnl::assert_intrnl_state(*symdb_pp);

    kvsymdb_t *old_symdb_p  = *symdb_pp;
    kvsymdb_t *new_symdb_p  =
        create_kvsymdb(
            old_symdb_p->_entrycap,
            old_symdb_p->_buf_size,
            out_errno_p
        );
     
    if (!new_symdb_p)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_CREATE
        );

    entry_view entview{};
    buffer_view key{}, val{};

    iterator it   = reinterpret_cast<iterator>(old_symdb_p->_arena_buf);
    iterator end  = reinterpret_cast<iterator>(old_symdb_p->_arena_buf + old_symdb_p->_buf_len);
    while (it != end) {
        if (
            old_symdb_p->_state_arr[it->id] == _kvsymdb_intrnl::state::ST_DEAD
        ) {
            it = reinterpret_cast<iterator>(
                reinterpret_cast<uint8_t*>(it) + it->record_len
            );
            continue;
        }
        _kvsymdb_intrnl::get_entview(
            old_symdb_p,
            it,
            &entview
        );

        key.data    = entview.name;
        key.size    = entview.name_len;
        val.data    = entview.data;
        val.size    = entview.data_len;

        int rc = kvsymdb_insert(
            &new_symdb_p,
            &key,
            &val,
            entview.hash,
            entview.type,
            out_errno_p
        );
        
        if (rc) {
            *out_errno_p = _kvsymdb_intrnl::error_code::ERR_INSERT;
            dbg_log_msg("");
            dbg_print(
                "create_kvsymdb failed: %s",
                _kvsymdb_intrnl::strerror(*out_errno_p)
            );
            destroy_kvsymdb(new_symdb_p);
            return KVSYMDB_FAILED;
        }

        it = reinterpret_cast<iterator>(
            reinterpret_cast<uint8_t*>(it) + it->record_len
        );
    }

    destroy_kvsymdb(old_symdb_p);

    *symdb_pp = new_symdb_p;

    return KVSYMDB_SUCCESS;
}

extern "C" int kvsymdb_insert(
    kvsymdb_t                 **symdb_pp,
    const kvsymdb_bufview_t    *key_p,
    const kvsymdb_bufview_t    *val_p,
    uint32_t                    key_hash,
    uint16_t                    type,
    int                        *out_errno_p
) {
    if (!out_errno_p) return KVSYMDB_FAILED;
    *out_errno_p = _kvsymdb_intrnl::error_code::NOERROR;

    if (!symdb_pp || !*symdb_pp || !key_p || !val_p)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_NULLPTR
        );

    if (
        !key_p->data || !key_p->size ||
        !val_p->data || !val_p->size
    )
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_BUFVIEW
        );

    kvsymdb_t *symdb_p = *symdb_pp;
    if (symdb_p->_entrycnt == symdb_p->_entrycap)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_FULL
        );

    _kvsymdb_intrnl::assert_intrnl_state(symdb_p);

    uint32_t entsize = _kvsymdb_intrnl::align::\
        entsize_required<cxx_kvsymdb::ALIGN_SIZE>(key_p, val_p);

    bool is_aligned = align_utils::\
        is_aligned_off<uint32_t, cxx_kvsymdb::ALIGN_SIZE>(entsize);
    assert(is_aligned);

    if (symdb_p->_buf_len + entsize > symdb_p->_buf_size) {
        uint32_t new_bufsize = (entsize > symdb_p->_buf_size)
            ? entsize * 2 : symdb_p->_buf_size * 2;
        dbg_print(
            "symdb_p->_buf_size: %u; new_bufsize: %u",
            symdb_p->_buf_size, new_bufsize
        );

        int rc = _kvsymdb_intrnl::\
            reserve_arenabuf(&symdb_p, new_bufsize, out_errno_p);
        if (rc < 0)
            RETURN_FAILED_STAT_WITH_ERRNO(
                out_errno_p,
                _kvsymdb_intrnl::error_code::ERR_RESIZEBUF
            );

        *symdb_pp = symdb_p;
        assert(symdb_p->_buf_size == new_bufsize);
    }

    uint32_t ent_off = symdb_p->_buf_len;

    is_aligned = align_utils::\
        is_aligned_off<uint32_t, cxx_kvsymdb::ALIGN_SIZE>(ent_off);
    assert(is_aligned);

    kvsymdb_record_header_t *ent_header_p = 
        reinterpret_cast<kvsymdb_record_header_t*>(symdb_p->_arena_buf + ent_off);

    ent_header_p->id            = symdb_p->_entrycnt;
    ent_header_p->hash          = key_hash;
    ent_header_p->type          = type;
    ent_header_p->name_len      = key_p->size;
    ent_header_p->data_len      = val_p->size;
    ent_header_p->record_len    = entsize;
    
    
    uint32_t key_size_aligned = _kvsymdb_intrnl::align::\
        cstr_size_aligned<cxx_kvsymdb::ALIGN_SIZE>(key_p->size);

    uint8_t *dest_p = ent_header_p->payload;
    memcpy(dest_p, key_p->data, key_p->size);
    dest_p[key_p->size] = '\0'; // zero termination

    uint32_t data_size_aligned =
        _kvsymdb_intrnl::align::blob_size_aligned<cxx_kvsymdb::ALIGN_SIZE>(val_p->size);
    dest_p = ent_header_p->payload + key_size_aligned;
    memcpy(dest_p, val_p->data, val_p->size);
    symdb_p->_buf_len += entsize;

    assert(
        entsize ==
            sizeof(kvsymdb_record_header_t) +
            key_size_aligned + data_size_aligned
    );
    assert(symdb_p->_buf_len == ent_off + entsize);

    symdb_p->_state_arr[symdb_p->_entrycnt] = _kvsymdb_intrnl::state::ST_ALIVE;

    ++symdb_p->_entrycnt;
    return KVSYMDB_SUCCESS;
}


extern "C" int kvsymdb_mark_dead(
    kvsymdb_t          *symdb_p,
    kvsymdb_entry_t    *ent_p,
    int                *out_errno_p
) {
    if (!out_errno_p) return KVSYMDB_FAILED;
    *out_errno_p = _kvsymdb_intrnl::error_code::NOERROR;

    if (!symdb_p || !ent_p)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_NULLPTR
        );

    if (!_kvsymdb_intrnl::is_valid_entry(symdb_p, ent_p))
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_BADENT
        );

    _kvsymdb_intrnl::assert_intrnl_state(symdb_p);

    symdb_p->_state_arr[ent_p->id] = _kvsymdb_intrnl::state::ST_DEAD;

    return KVSYMDB_SUCCESS;
}


extern "C" int kvsymdb_get_entview(
    const kvsymdb_t        *symdb_p,
    const kvsymdb_entry_t  *ent_p,
    kvsymdb_entview_t      *out_entview_p,
    int                    *out_errno_p
) {
    if (!out_errno_p) return KVSYMDB_FAILED;
    *out_errno_p = _kvsymdb_intrnl::error_code::NOERROR;

    if (!symdb_p || !ent_p || !out_entview_p)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_NULLPTR
        );

    if (!_kvsymdb_intrnl::is_valid_entry(symdb_p, ent_p))
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_BADENT
        );

    _kvsymdb_intrnl::assert_intrnl_state(symdb_p);

    _kvsymdb_intrnl::get_entview(symdb_p, ent_p, out_entview_p);

    return KVSYMDB_SUCCESS;
}


extern "C" bool kvsymdb_is_valid_entry(
    const kvsymdb_t        *symdb_p,
    const kvsymdb_entry_t  *ent_p
) {
    if (
        !symdb_p || !ent_p ||
        !_kvsymdb_intrnl::is_valid_entry(symdb_p, ent_p)
    ) return false;
    _kvsymdb_intrnl::assert_intrnl_state(symdb_p);
    return (symdb_p->_state_arr[ent_p->id] == _kvsymdb_intrnl::state::ST_ALIVE);
}

// read raw buf; like linux getdents/getdents64
extern "C" uint32_t _kvsymdb_read_buf(
    kvsymdb_t  *symdb_p,
    uint32_t    off,
    void       *dest_buf,
    uint32_t    count,
    int        *out_errno_p
) {
    if (!out_errno_p) return 0;
    *out_errno_p = _kvsymdb_intrnl::error_code::NOERROR;

    if (!symdb_p || !dest_buf)
        RETURN_SET_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_NULLPTR,
            0u
        );

    if (!_kvsymdb_intrnl::is_valid_entoff(symdb_p, off))
        RETURN_SET_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_BADOFF,
            0u
        );

    _kvsymdb_intrnl::assert_intrnl_state(symdb_p);

    if (symdb_p->_buf_len <= off) return 0u;

    uint32_t rem_len = symdb_p->_buf_len - off;
    uint32_t nread = (rem_len < count) ? rem_len : count;

    memcpy(
        dest_buf,
        symdb_p->_arena_buf + off,
        nread
    );

    return nread;
}

extern "C"
kvsymdb_iterator_t kvsymdb_iterator_begin(
    kvsymdb_t  *symdb_p
) {
    dbg_log_msg("");
    return (!symdb_p) ? nullptr : 
        reinterpret_cast<kvsymdb_entry_t*>(symdb_p->_arena_buf);
}

extern "C"
kvsymdb_iterator_t kvsymdb_iterator_end(
    kvsymdb_t  *symdb_p
) {
    dbg_log_msg("");
    return (!symdb_p) ? nullptr :
        reinterpret_cast<kvsymdb_entry_t*>(
            symdb_p->_arena_buf + symdb_p->_buf_len
        );
}

extern "C"
kvsymdb_iterator_t kvsymdb_iterator_next(
    kvsymdb_t          *symdb_p,
    kvsymdb_iterator_t  iter
) {
    dbg_log_msg("");
    if (!symdb_p || !iter) return nullptr;
    ptrdiff_t ent_off =
        reinterpret_cast<uint8_t*>(iter) - symdb_p->_arena_buf;
    return
        (ent_off < static_cast<ptrdiff_t>(symdb_p->_buf_len))
            ? reinterpret_cast<kvsymdb_entry_t*>(
                reinterpret_cast<uint8_t*>(iter) + iter->record_len
            )
            : reinterpret_cast<kvsymdb_entry_t*>(
                reinterpret_cast<uint8_t*>(symdb_p->_arena_buf) + symdb_p->_buf_len
            );
}

// reader methods:
// reader.open() # reader(), init
// reader.read()
// reader.rewind()
// reader.seek()
// reader.close() # reader.~reader(), cleanup

// init
extern "C"
int kvsymdb_reader_bind(
    kvsymdb_reader_t   *reader_p,
    const kvsymdb_t    *symdb_p,
    int                *out_errno_p
) {
    // kvsymdb_t verification required
    dbg_log_msg("");
    if (!out_errno_p) return KVSYMDB_FAILED;
    *out_errno_p = _kvsymdb_intrnl::error_code::NOERROR;
    if (!reader_p || !symdb_p)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_NULLPTR
        );

    reader_p->_symdb_p  = symdb_p;
    reader_p->_pos      = 0u;

    return KVSYMDB_SUCCESS;
}

const kvsymdb_entry_t *kvsymdb_reader_read(
    kvsymdb_reader_t   *reader_p,
    int                *out_errno_p
) {
    // kvsymdb_t verification required
    dbg_log_msg("");
    if (!out_errno_p) return nullptr;
    *out_errno_p = _kvsymdb_intrnl::error_code::NOERROR;

    dbg_log_msg("#1");
    if (!reader_p || !reader_p->_symdb_p)
        RETURN_SET_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_NULLPTR,
            nullptr
        );

    dbg_log_msg("#2");
    // check alignment
    if (
        reader_p->_pos &&
        !align_utils::\
            align_off<uint32_t, cxx_kvsymdb::ALIGN_SIZE>(reader_p->_pos)
    ) {
        RETURN_SET_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_BADALIGN,
            nullptr
        );
    }

    dbg_log_msg("#3");
    // checking is _pos + min_reclen <= than buf_len so 
    // the rec header is safe for deref
    uint32_t min_reclen = sizeof(kvsymdb_record_header_t);
    if (reader_p->_pos + min_reclen > reader_p->_symdb_p->_buf_len) {
        reader_p->_pos = reader_p->_symdb_p->_buf_len; // EOF
        RETURN_SET_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_RDEOF,
            nullptr
        );
    }

    dbg_log_msg("#4");
    const kvsymdb_entry_t *ent_p =
        reinterpret_cast<const kvsymdb_entry_t*>(
            reader_p->_symdb_p->_arena_buf + reader_p->_pos
        );

    dbg_log_msg("#5");
    if (!_kvsymdb_intrnl::is_valid_entry(reader_p->_symdb_p, ent_p)) {
        // It is usually bc _pos + sizeof(header) >= buf_len
        // so (Header*)((byte*)base + _pos) is safe to deref
        // but _pos + header.rec_len_aligned > buf_len
        // i.e. The payload is out of bound / truncated
        // to be safe we set _pos to _buf_len (guaranteed EOF)
        reader_p->_pos = reader_p->_symdb_p->_buf_len;
        RETURN_SET_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_BADENT,
            nullptr
        );
    }

    _kvsymdb_intrnl::assert_intrnl_state(reader_p->_symdb_p); 
   
    reader_p->_pos += ent_p->record_len;    
    assert(reader_p->_pos <= reader_p->_symdb_p->_buf_len);

    return ent_p;
}

int kvsymdb_reader_rewind(
    kvsymdb_reader_t   *reader_p
) {
    dbg_log_msg("");
    if (!reader_p || !reader_p->_symdb_p)
        return KVSYMDB_FAILED;

    reader_p->_pos = 0u;

    return KVSYMDB_SUCCESS;
}

// I will rethink about seek impl because it might not worth it
// due to safety concerns

// cleanup
extern "C"
void kvsymdb_reader_unbind(
    kvsymdb_reader_t   *reader_p
) {
    dbg_log_msg("");
    if (reader_p) {
        reader_p->_symdb_p  = nullptr;
        reader_p->_pos      = 0u;
    }
}


extern "C"
const char *kvsymdb_strerror(int kvsymdb_errno) {
    return _kvsymdb_intrnl::strerror(kvsymdb_errno);
}


namespace _hidx_intrnl {
    
}


// impl of hash table
// this should be priv ctor
int cxx_kvsymdb::kvsymdb::hash_table::pool::\
init(uint32_t pool_size) {
    assert(pool_size);
    this->m_errno = _kvsymdb_intrnl::error_code::NOERROR;

    this->m_entry_arr = static_cast<entry*>(
        operator new[](pool_size * sizeof(entry), std::nothrow)
    );
    if (!this->m_entry_arr) {
        this->m_errno = _kvsymdb_intrnl::error_code::ERR_OPNEWARR;
        goto failed_ret;
    }
    memset(this->m_entry_arr, 0, pool_size * sizeof(entry));

    for (uint32_t i = 0u; i < pool_size - 1; ++i) {
        this->m_entry_arr[i].next_free_p =
            reinterpret_cast<decltype(entry::next_free_p)>(
                &this->m_entry_arr[i + 1]
            );
    }
    this->m_entry_arr[pool_size - 1].next_free_p = nullptr;

    this->m_free_head_p = &this->m_entry_arr[0];
    this->m_size        = 0u;
    this->m_capacity    = pool_size;

    return KVSYMDB_SUCCESS;
failed_ret:
    return KVSYMDB_FAILED;
}

void cxx_kvsymdb::kvsymdb::hash_table::pool::_cleanup() {
    if (this->m_entry_arr)
        operator delete[](this->m_entry_arr);

    this->m_entry_arr   = nullptr;
    this->m_free_head_p = nullptr;
    this->m_size        = 0u;
    this->m_capacity    = 0u;
    this->m_errno       = 0;
}

int cxx_kvsymdb::kvsymdb::hash_table::\
_init(const kvsymdb_t *symdb_p, uint32_t bucket_cnt, uint32_t slot_cnt) {
    assert(bucket_cnt && slot_cnt);
    this->m_c_symdb_p = symdb_p;
    this->m_errno = _kvsymdb_intrnl::error_code::NOERROR;

    // init pool
    int rc = this->m_pool.init(slot_cnt);
    if (rc) goto failed_ret;

    // alloc && init bucket arr
    this->m_bucket_arr = static_cast<bucket*>(
        operator new[](bucket_cnt * sizeof(bucket), std::nothrow)
    );
    if (!this->m_bucket_arr){
        this->m_errno = _kvsymdb_intrnl::error_code::ERR_OPNEWARR;
        goto failed_cleanup;
    }

    for (uint32_t i = 0u; i < bucket_cnt; ++i) {
        this->m_bucket_arr[i].chain_head_p = nullptr;
    }
    this->m_bucket_cnt = bucket_cnt;

    return KVSYMDB_SUCCESS;
failed_cleanup:
    this->_cleanup();   
failed_ret:
    return KVSYMDB_FAILED;
}

void cxx_kvsymdb::kvsymdb::hash_table::_cleanup() {
    this->m_pool._cleanup();
    if (this->m_bucket_arr)
        operator delete[](this->m_bucket_arr);

    this->m_c_symdb_p   = nullptr;
    this->m_bucket_arr  = nullptr; 
    this->m_bucket_cnt  = 0u;
    this->m_errno       = 0;
}

cxx_kvsymdb::kvsymdb::hash_table::slot *
cxx_kvsymdb::kvsymdb::hash_table::pool::alloc() {
    this->m_errno = _kvsymdb_intrnl::error_code::NOERROR;
    if (this->m_size == this->m_capacity) {
        this->m_errno = _kvsymdb_intrnl::error_code::ERR_POOL_FULL;
        return nullptr;
    }

    assert(this->m_size <= this->m_capacity);
    assert(this->m_free_head_p);

    slot *slot_p = reinterpret_cast<slot*>(this->m_free_head_p);
    this->m_free_head_p = reinterpret_cast<entry*>(
        this->m_free_head_p->next_free_p
    );
    ++this->m_size;

    return slot_p;
}

int cxx_kvsymdb::kvsymdb::hash_table::pool::free(slot *slot_p) {
    this->m_errno = _kvsymdb_intrnl::error_code::NOERROR;
    if (!slot_p) {
        this->m_errno = _kvsymdb_intrnl::error_code::ERR_NULLPTR;
        return KVSYMDB_FAILED;
    }

    entry *pool_ent_p = reinterpret_cast<entry*>(slot_p);
    ptrdiff_t ent_idx = pool_ent_p - this->m_entry_arr;
    if (
        ent_idx < 0L ||
        ent_idx >= static_cast<ptrdiff_t>(this->m_capacity)
    ) {
        this->m_errno = _kvsymdb_intrnl::error_code::ERR_ADDR_OUT_OF_RANGE;
        return KVSYMDB_FAILED;
    }

    ptrdiff_t ent_off =
        reinterpret_cast<uint8_t*>(pool_ent_p) - 
        reinterpret_cast<uint8_t*>(this->m_entry_arr);
    
    if (ent_off % sizeof(entry) != 0L) {
        this->m_errno = _kvsymdb_intrnl::error_code::ERR_PTR_UNALIGNED;
        return KVSYMDB_FAILED;
    }

    assert(this->m_size <= this->m_capacity);
    assert(this->m_size);

    pool_ent_p->next_free_p = reinterpret_cast<slot*>(this->m_free_head_p);
    this->m_free_head_p = pool_ent_p;
    --this->m_size;

    return KVSYMDB_SUCCESS;
}

int cxx_kvsymdb::kvsymdb::hash_table::insert(const entry *ent_p) {
    this->m_errno = _kvsymdb_intrnl::error_code::NOERROR;
    if (!ent_p) {
        this->m_errno = _kvsymdb_intrnl::error_code::ERR_NULLPTR;
        return KVSYMDB_FAILED;
    }

    slot *slot_p = this->m_pool.alloc();
    if (!slot_p) {
        this->m_errno = _kvsymdb_intrnl::error_code::ERR_POOL_ALLOC;
        return KVSYMDB_FAILED;
    }

    slot_p->ent_p       = ent_p;
    slot_p->prev_slot_p = nullptr;
    slot_p->next_slot_p = nullptr;    

    uint32_t bucket_idx = ent_p->hash % this->m_bucket_cnt;
    bucket *bucket_p = &this->m_bucket_arr[bucket_idx];

    slot_p->next_slot_p = bucket_p->chain_head_p;
    if (bucket_p->chain_head_p)
        bucket_p->chain_head_p->prev_slot_p = slot_p;

    bucket_p->chain_head_p = slot_p;

    return KVSYMDB_SUCCESS;
}

int cxx_kvsymdb::kvsymdb::hash_table::_lookup(
    const buffer_view  *key_p,
    uint32_t            hash,
    lookup_result      *out_res_p
) {
    this->m_errno = _kvsymdb_intrnl::error_code::NOERROR;
    if (!key_p || !out_res_p) {
        this->m_errno = _kvsymdb_intrnl::error_code::ERR_NULLPTR;
        return KVSYMDB_FAILED;
    }
    if (!key_p->data || !key_p->size) {
        this->m_errno = _kvsymdb_intrnl::error_code::ERR_ENTVIEW;
        return KVSYMDB_FAILED;
    }

    out_res_p->bucket_p = nullptr;
    out_res_p->slot_p   = nullptr;

    bucket *bucket_p = &this->m_bucket_arr[hash % this->m_bucket_cnt];
    slot *slot_p = bucket_p->chain_head_p;

    uint32_t probc = 0u;
    while (probc < this->m_pool.m_size && slot_p) {
        const entry *ent_p = slot_p->ent_p;
        if (ent_p->hash == hash) {
            entry_view view{};
            _kvsymdb_intrnl::get_entview(
                this->m_c_symdb_p,
                ent_p,
                &view
            );
            if (
                key_p->size == view.name_len &&
                memcmp(key_p->data, view.name, view.name_len) == 0
            ) {
                out_res_p->bucket_p = bucket_p;
                out_res_p->slot_p   = slot_p;
                return KVSYMDB_SUCCESS;
            }
        }
        slot_p = slot_p->next_slot_p;
    }

    assert(!slot_p);

    this->m_errno = _kvsymdb_intrnl::error_code::ERR_KEY_NOT_FOUND;
    return KVSYMDB_FAILED;
}

int cxx_kvsymdb::kvsymdb::hash_table::_remove(lookup_result *lu_res_p) {
    if (
        !lu_res_p ||
        !lu_res_p->bucket_p ||
        !lu_res_p->slot_p ||
        !lu_res_p->slot_p->ent_p
    ) {
        this->m_errno = _kvsymdb_intrnl::error_code::ERR_BAD_HTLURES;
        return KVSYMDB_FAILED;
    }

    assert(!lu_res_p->bucket_p->chain_head_p->prev_slot_p);
    slot *old_slot_p = lu_res_p->slot_p;
    bucket *bucket_p = lu_res_p->bucket_p;

    if (old_slot_p->prev_slot_p) {
        old_slot_p->prev_slot_p->next_slot_p = old_slot_p->next_slot_p;
    } else {
        assert(bucket_p->chain_head_p == old_slot_p);
        bucket_p->chain_head_p = old_slot_p->next_slot_p;
    }
    if (old_slot_p->next_slot_p) {
        old_slot_p->next_slot_p->prev_slot_p = old_slot_p->prev_slot_p;
    }

    int rc = this->m_pool.free(old_slot_p);
    assert(!rc);

    return KVSYMDB_SUCCESS;
}

int cxx_kvsymdb::kvsymdb::hash_table::remove(const entry *ent_p) {
    if (!_kvsymdb_intrnl::is_valid_entry(this->m_c_symdb_p, ent_p)) {
        this->m_errno = _kvsymdb_intrnl::error_code::ERR_BADENT;
        return KVSYMDB_FAILED;
    }

    entry_view ent_view{};
    _kvsymdb_intrnl::get_entview(this->m_c_symdb_p, ent_p, &ent_view);

    string_view key_view(ent_view.name, ent_view.name_len);
    lookup_result lu_res{};
    int rc = this->_lookup(&key_view, key_view.hash32(), &lu_res);
    if (rc == KVSYMDB_FAILED) return rc;

    return this->_remove(&lu_res);
}

// opaque c abi type; typedef struct _c_kvsymdb_hidx kvsymdb_hash_index_t;
struct _c_kvsymdb_hidx {
    cxx_kvsymdb::kvsymdb::hash_table    _cxx_kvsymdb_hidx;
};

extern "C" kvsymdb_hash_index_t *
create_kvsymdb_hash_index(
    const kvsymdb_t    *symdb_p,
    int                *out_errno_p
) {
    if (!out_errno_p) return nullptr;
    *out_errno_p = _kvsymdb_intrnl::error_code::NOERROR;

    if (!symdb_p)
        RETURN_SET_ERRNO(
            out_errno_p,
            _kvsymdb_intrnl::error_code::ERR_NULLPTR,
            nullptr
        );

    _kvsymdb_intrnl::assert_intrnl_state(symdb_p);

    int rc = 0;
    auto *c_hidx_p = new (std::nothrow) _c_kvsymdb_hidx();
    if (!c_hidx_p) {
        *out_errno_p = _kvsymdb_intrnl::error_code::ERR_OPNEW;
        goto failed_ret;
    }

    rc = c_hidx_p->_cxx_kvsymdb_hidx._init(
        symdb_p, symdb_p->_entrycap, symdb_p->_entrycap
    );
    if (rc) goto failed_cleanup;

    return c_hidx_p;
failed_cleanup:
    delete c_hidx_p;
failed_ret:
    return nullptr;
}

extern "C" void
destroy_kvsymdb_hash_index(
    kvsymdb_hash_index_t *c_hidx_p
) {
    if (!c_hidx_p) return;
    delete c_hidx_p; // dtor automatically called
}

extern "C" int
kvsymdb_hidx_insert(
    kvsymdb_hash_index_t   *c_hidx_p,
    const kvsymdb_entry_t  *ent_p,
    int                    *out_errno_p
) {
    if (!out_errno_p) return KVSYMDB_FAILED;
    if (!c_hidx_p || !ent_p) {
        *out_errno_p = _kvsymdb_intrnl::error_code::ERR_NULLPTR;
        return KVSYMDB_FAILED;
    }

    int rc = c_hidx_p->_cxx_kvsymdb_hidx.insert(ent_p);
    *out_errno_p = c_hidx_p->_cxx_kvsymdb_hidx.m_errno;

    return rc;
}

extern "C"
const kvsymdb_entry_t *
kvsymdb_hidx_lookup(
    kvsymdb_hash_index_t       *c_hidx_p,
    const kvsymdb_bufview_t    *key_p,
    int                        *out_errno_p
) {
    if (!out_errno_p) return nullptr;
    if (!c_hidx_p || !key_p) {
        *out_errno_p = _kvsymdb_intrnl::error_code::ERR_NULLPTR;
        return nullptr;
    }

    const kvsymdb_entry_t *ent_p = 
        c_hidx_p->_cxx_kvsymdb_hidx.lookup(*key_p);
    *out_errno_p = c_hidx_p->_cxx_kvsymdb_hidx.m_errno;

    return ent_p;
}

extern "C"
int kvsymdb_hidx_remove(
    kvsymdb_hash_index_t   *c_hidx_p,
    const kvsymdb_entry_t  *ent_p,
    int                    *out_errno_p
) {
    if (!out_errno_p) return KVSYMDB_FAILED;
    if (!c_hidx_p || !ent_p) {
        *out_errno_p = _kvsymdb_intrnl::error_code::ERR_NULLPTR;
        return KVSYMDB_FAILED;
    }

    int rc = c_hidx_p->_cxx_kvsymdb_hidx.remove(ent_p);
    *out_errno_p = c_hidx_p->_cxx_kvsymdb_hidx.m_errno;

    return rc;
}


