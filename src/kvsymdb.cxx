#include <assert.h>
#include <string.h>
#include <new>
#include <kvsymdb.h>

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
            ERR_BADENT, // 1
            ERR_GC,
            ERR_CREATE,
            ERR_INSERT,
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
    if (!new_state_arr) return -1;

    memset(new_state_arr, 0, new_entc * sizeof(uint8_t));
    memcpy(
        new_state_arr,
        symdb_p->_state_arr,
        symdb_p->_entrycnt * sizeof(uint8_t)
    );

    operator delete[](symdb_p->_state_arr);
    symdb_p->_state_arr = new_state_arr;
    symdb_p->_entrycap = new_entc;

    return 0;
}

static inline void
_kvsymdb_intrnl::assert_intrnl_state(const kvsymdb_t *symdb_p) {
    assert(symdb_p->_buf_size >= cxx_kvsymdb::INIT_BUFSIZE);
    assert(symdb_p->_entrycap >= cxx_kvsymdb::INIT_ENTC);
    assert(symdb_p->_buf_len <= symdb_p->_buf_size);
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

    return (off + ent_p->record_len <= symdb_p->_buf_len);
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
        entry_size == ent_p->record_len
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
            destroy_kvsymdb(new_symdb_p);
            RETURN_FAILED_STAT_WITH_ERRNO(
                out_errno_p,
                _kvsymdb_intrnl::error_code::ERR_INSERT
            );
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

    if (symdb_p->_buf_len + entsize >= symdb_p->_buf_size) {
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
        dbg_print(
            "symdb_p->_buf_size: %u; new_bufsize: %u",
            symdb_p->_buf_size, new_bufsize
        );
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

extern "C"
const char *kvsymdb_strerror(int kvsymdb_errno) {
    using namespace _kvsymdb_intrnl::error_code;

    switch (kvsymdb_errno) {
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

        default:
            break;

    }

    return "no error";
}
