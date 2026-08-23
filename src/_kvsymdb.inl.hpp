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

extern "C" { // c abi structs; implmtation detail
    struct _c_kvsymdb {
        uint32_t    _entrycnt;      // [0]
        uint32_t    _entrycap;      // [1]
        uint32_t    _buf_len;       // [2]
        uint32_t    _buf_size;      // [3]
        int         _err_code;      // [4]
        uint8_t    *_state_arr;     // [5]
        uint8_t     _arena_buf[];   // [6]
    };
}


namespace cxx_kvsymdb {
namespace _intrnl {

constexpr uint32_t FILE_MAGIC   = 0x4244564Bu;
constexpr uint16_t FILE_VERSION = 0x0101;
constexpr uint32_t FILE_ALIGN   = kvsymdb::ALIGN_SIZE;

namespace error_code {
    enum _ErrorCodeEnum {
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
        ERR_FOPEN,          // 7
        ERR_DBST,
        ERR_IOFREAD,
        ERR_IOFWRITE,
        ERR_FSTAT,
        ERR_BADDBF,
        ERR_MMAP,
        ERR_BADFHDR,
        ERR_CRC,
        ERR_DUP,
    };
}; // namespace error_code

inline const char *strerror(int db_errno) noexcept {
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
        case (ERR_BAD_HTLURES):
            return "ERR_BAD_HTLURES";
        case (ERR_FOPEN):
            return "fopen failed";
        case (ERR_DBST):
            return "failed to get self state view";
        case (ERR_IOFREAD):
            return "fread failed";
        case (ERR_IOFWRITE):
            return "fwrite failed";
        case (ERR_FSTAT):
            return "fstat failed";
        case (ERR_BADDBF):
            return "invalid db file";
        case (ERR_MMAP):
            return "mmap failed";
        case (ERR_BADFHDR):
            return "bad file header";
        case (ERR_CRC):
            return "crc mismatch";
        case (ERR_DUP):
            return "dup failed";

        default:
            break;
    }

    return "no error";   
}

namespace state {
    enum _state {
        ST_EMPTY    = 0,
        ST_ALIVE    = 1,
        ST_DEAD     = 2,
    };
} // namespace state

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
            cstr_size_aligned<align_size>(key_p->buf_size) +
            blob_size_aligned<align_size>(val_p->buf_size);
    }
} // namespace align

inline void assert_intrnl_state(const kvsymdb_t *symdb_p) noexcept {
    assert(symdb_p->_buf_size >= kvsymdb::INIT_BUFSIZE);
    assert(symdb_p->_entrycap >= kvsymdb::INIT_ENTC);
    assert(symdb_p->_buf_len <= symdb_p->_buf_size);
    bool is_aligned_buf_len = align_utils::
        is_aligned_off<uint32_t, kvsymdb::ALIGN_SIZE>(symdb_p->_buf_len);
    assert(is_aligned_buf_len);
    bool is_aligned_buf_size = align_utils::
        is_aligned_off<uint32_t, kvsymdb::ALIGN_SIZE>(symdb_p->_buf_size);
    assert(is_aligned_buf_size);
    assert(symdb_p->_entrycnt <= symdb_p->_entrycap);
    assert(symdb_p->_state_arr);
}

inline bool is_valid_entoff(
    const kvsymdb_t    *symdb_p,
    uint32_t            off
) noexcept {
    assert(symdb_p);
    if (off + sizeof(kvsymdb_record_header_t) > symdb_p->_buf_len)
        return false;

    const kvsymdb_entry_t *ent_p =
        reinterpret_cast<const kvsymdb_entry_t*>(symdb_p->_arena_buf + off);

    return (
        off + ent_p->record_len <= symdb_p->_buf_len &&
        align_utils::is_aligned_off<uint32_t, kvsymdb::ALIGN_SIZE>(off) &&
        align_utils::is_aligned_off<uint32_t, kvsymdb::ALIGN_SIZE>(ent_p->record_len)
    );
}

inline uint32_t record_size(const kvsymdb_entry_t *ent_p) noexcept {
    return
        sizeof(*ent_p) +
        align::cstr_size_aligned<kvsymdb::ALIGN_SIZE>(ent_p->name_len) +
        align::blob_size_aligned<kvsymdb::ALIGN_SIZE>(ent_p->data_len);
}

inline bool _is_valid_entry(
    const void             *arena_buf,
    uint32_t                arena_buf_len, 
    uint32_t                entc,
    const kvsymdb_entry_t  *ent_p
) noexcept {
    using namespace align_utils;

    assert(arena_buf && ent_p);
    auto record_len = record_size(ent_p);

    auto record_begin   = reinterpret_cast<const uint8_t*>(ent_p);
    auto record_end     = reinterpret_cast<const uint8_t*>(ent_p) + record_len;
    auto arena_begin    = static_cast<const uint8_t*>(arena_buf);
    auto arena_end      = static_cast<const uint8_t*>(arena_buf) + arena_buf_len;

    assert(ent_p);
    assert(record_begin >= arena_begin);
    assert(record_end <= arena_end);
    bool b = is_aligned_off<uint32_t, kvsymdb::ALIGN_SIZE>(record_begin - arena_begin);
    assert(b);
    assert(ent_p->id < entc);
    assert(record_len == ent_p->record_len);


    return (
        ent_p &&
        record_begin >= arena_begin &&
        record_end <= arena_end &&
        is_aligned_off<uint32_t, kvsymdb::ALIGN_SIZE>(record_begin - arena_begin) &&
        ent_p->id < entc &&
        record_len == ent_p->record_len
    );
}

inline bool is_valid_entry(
    const kvsymdb_t        *symdb_p,
    const kvsymdb_entry_t  *ent_p
) noexcept {
    assert(symdb_p && ent_p);
    
    return _is_valid_entry(
        symdb_p->_arena_buf,
        symdb_p->_buf_len,
        symdb_p->_entrycnt,
        ent_p
    );
}

inline bool is_valid_entry(
    const kvsymdb_bufview_t    *arena_view_p,
    uint32_t                    entc,
    const kvsymdb_entry_t      *ent_p
) noexcept {
    assert(arena_view_p && ent_p);
    return _is_valid_entry(
        arena_view_p->buf_data,
        arena_view_p->buf_size,
        entc,
        ent_p
    );
}

static auto align_u32 = [](uint32_t size) -> uint32_t {
    return align_utils::align_off<uint32_t, kvsymdb::ALIGN_SIZE>(size);
};


namespace ckvsymdb {
    static auto calc_buf_size = [](uint32_t entc, uint32_t arena_size) -> uint32_t {
        return
            sizeof(kvsymdb_t)                       // header
            + align_u32(arena_size)                 // arena size
            + align_u32(entc * sizeof(uint8_t));    // state arr
    };
    void *alloc_cdb_mem(
        uint32_t    max_entc,
        uint32_t    max_arena_size,
        kvsymdb_t **out_cdb_pp,
        uint8_t   **out_state_arr_p
    ) noexcept {
        size_t _db_buf_size = calc_buf_size(max_entc, max_arena_size);
        void *_db_buf = ::operator new(_db_buf_size, std::nothrow);

        if (!_db_buf) return nullptr;
        memset(_db_buf, 0, _db_buf_size);

        if (out_cdb_pp)
            *out_cdb_pp = static_cast<kvsymdb_t*>(_db_buf);

        if (out_state_arr_p)
            *out_state_arr_p =
                static_cast<uint8_t*>(_db_buf) +
                sizeof(kvsymdb_t) +
                _intrnl::align_u32(max_arena_size);

        return _db_buf;
    }
}



inline int reserve(
    kvsymdb_t **cdb_pp,
    uint32_t    new_entc
) {
    assert(cdb_pp);
    assert(new_entc >= (*cdb_pp)->_entrycnt);

    kvsymdb_t *old_cdbp = *cdb_pp;
    kvsymdb_t *new_cdbp = nullptr;
    uint8_t *new_state_arr = nullptr;    

    void *new_cdb_buf = ckvsymdb::alloc_cdb_mem(
        new_entc,
        old_cdbp->_buf_size,
        &new_cdbp,
        &new_state_arr
    );

    if (!new_cdb_buf) {
        old_cdbp->_err_code = error_code::ERR_OPNEW;
        dbg_log_msg("");
        dbg_print(
            "cxx_kvsymdb::_intrnl::reserve failed: %s",
            _intrnl::strerror(old_cdbp->_err_code)
        );
        return old_cdbp->_err_code;
    }

    new_cdbp->_entrycnt     = old_cdbp->_entrycnt;
    new_cdbp->_entrycap     = new_entc;
    new_cdbp->_buf_len      = old_cdbp->_buf_len;
    new_cdbp->_buf_size     = old_cdbp->_buf_size;
    new_cdbp->_err_code     = old_cdbp->_err_code;
    new_cdbp->_state_arr    = new_state_arr;

    memcpy(
        new_cdbp->_state_arr,
        old_cdbp->_state_arr,
        old_cdbp->_entrycnt * sizeof(uint8_t)
    );
    memcpy(
        new_cdbp->_arena_buf,
        old_cdbp->_arena_buf,
        old_cdbp->_buf_len
    );

    operator delete[](old_cdbp);
    *cdb_pp = new_cdbp;

    return KVSYMDB_OK;
}

inline int reserve_arenabuf(
    kvsymdb_t **cdb_pp,
    uint32_t    new_arena_size
) {
    assert(cdb_pp && *cdb_pp);
    assert((*cdb_pp)->_state_arr);
    assert(new_arena_size >= (*cdb_pp)->_buf_len);
    assert(new_arena_size == align_u32(new_arena_size));

    kvsymdb_t *old_cdbp = *cdb_pp;
    kvsymdb_t *new_cdbp = nullptr;
    uint8_t *new_state_arr = nullptr;    

    void *new_cdb_buf = ckvsymdb::alloc_cdb_mem(
        old_cdbp->_entrycap,
        new_arena_size,
        &new_cdbp,
        &new_state_arr
    );

    if (!new_cdb_buf) {
        old_cdbp->_err_code = error_code::ERR_OPNEW;
        dbg_log_msg("");
        dbg_print(
            "cxx_kvsymdb::_intrnl::reserve_arenabuf failed: %s\n",
            strerror(old_cdbp->_err_code)
        );
        return old_cdbp->_err_code;
    }

    new_cdbp->_entrycnt     = old_cdbp->_entrycnt;
    new_cdbp->_entrycap     = old_cdbp->_entrycap;
    new_cdbp->_buf_len      = old_cdbp->_buf_len;
    new_cdbp->_buf_size     = new_arena_size;
    new_cdbp->_err_code     = old_cdbp->_err_code;
    new_cdbp->_state_arr    = new_state_arr;

    memcpy(
        new_cdbp->_state_arr,
        old_cdbp->_state_arr,
        old_cdbp->_entrycnt * sizeof(uint8_t)
    );
    memcpy(
        new_cdbp->_arena_buf,
        old_cdbp->_arena_buf,
        old_cdbp->_buf_len
    );

    operator delete(old_cdbp);
    *cdb_pp = new_cdbp;

    return KVSYMDB_OK;
}


inline void get_entry_view(
    const kvsymdb_entry_t  *ent_p,
    kvsymdb_entview_t      *out_entview_p
) {
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
            align::cstr_size_aligned<kvsymdb::ALIGN_SIZE>(ent_p->name_len)
        );

    out_entview_p->data_len = ent_p->data_len;
    out_entview_p->_record  = static_cast<const void*>(ent_p);
}


inline bool is_valid_file_header(
    const kvsymdb_file_header_t    *fhdr_p,
    size_t                          filesize
) {
    return (
        fhdr_p->fh_magic    == FILE_MAGIC &&
        fhdr_p->fh_version  == FILE_VERSION &&
        fhdr_p->fh_align    == FILE_ALIGN &&
        fhdr_p->fh_buflen   <= filesize - sizeof(kvsymdb_file_header_t)
    );

    // og: fhdr_p->fh_buflen == filesize - sizeof(kvsymdb_file_header_t)
}

} // namespace _intrnl

} // namespace cxx_kvsymdb 