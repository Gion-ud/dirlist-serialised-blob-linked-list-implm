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
        uint8_t    *_state_arr;     // [4]
        uint8_t     _arena_buf[];   // [5]
    };
}


namespace cxx_kvsymdb {
namespace _intrnl {

constexpr uint32_t FILE_MAGIC   = 0x4244564Bu;
constexpr uint16_t FILE_VERSION = 0x0101;
constexpr uint32_t FILE_ALIGN   = kvsymdb::ALIGN_SIZE;

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

inline const char *strerror(int db_errno) {
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
            cstr_size_aligned<align_size>(key_p->size) +
            blob_size_aligned<align_size>(val_p->size);
    }
} // namespace align

inline void assert_intrnl_state(const kvsymdb_t *symdb_p) {
    assert(symdb_p->_buf_size >= kvsymdb::INIT_BUFSIZE);
    assert(symdb_p->_entrycap >= kvsymdb::INIT_ENTC);
    assert(symdb_p->_buf_len <= symdb_p->_buf_size);
    bool is_aligned_buf_len = align_utils::\
        is_aligned_off<uint32_t, kvsymdb::ALIGN_SIZE>(symdb_p->_buf_len);
    assert(is_aligned_buf_len);
    assert(symdb_p->_entrycnt <= symdb_p->_entrycap);
    assert(symdb_p->_state_arr);
}

inline bool is_valid_entoff(
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
        align_utils::is_aligned_off<uint32_t, kvsymdb::ALIGN_SIZE>(off) &&
        align_utils::is_aligned_off<uint32_t, kvsymdb::ALIGN_SIZE>(ent_p->record_len)
    );
}

inline uint32_t record_size(const kvsymdb_entry_t *ent_p) {
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
) {
    using namespace align_utils;

    assert(arena_buf && ent_p);
    auto record_len = record_size(ent_p);

    auto record_begin   = reinterpret_cast<const uint8_t*>(ent_p);
    auto record_end     = reinterpret_cast<const uint8_t*>(ent_p) + record_len;
    auto arena_begin    = static_cast<const uint8_t*>(arena_buf);
    auto arena_end      = static_cast<const uint8_t*>(arena_buf) + arena_buf_len;

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
) {
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
) {
    assert(arena_view_p && ent_p);

    return _is_valid_entry(
        arena_view_p->data,
        arena_view_p->size,
        entc,
        ent_p
    );
}


inline int reserve(
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
        *out_errno_p = error_code::ERR_OPNEWARR;
        dbg_log_msg("");
        dbg_print(
            "reserve failed: %s\n",
            strerror(*out_errno_p)
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

inline int reserve_arenabuf(
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
        *out_errno_p = error_code::ERR_OPNEW;
        dbg_log_msg("");
        dbg_print(
            "reserve_arenabuf failed: %s\n",
            strerror(*out_errno_p)
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
        fhdr_p->fh_buflen   == filesize - sizeof(kvsymdb_file_header_t)
    );
}



// hash_index::pool impl
inline int hash_index::pool::init(uint32_t pool_size) {
    assert(pool_size);
    this->m_errno = error_code::NOERROR;

    this->m_entry_arr = static_cast<entry*>(
        operator new[](pool_size * sizeof(entry), std::nothrow)
    );
    if (!this->m_entry_arr) {
        this->m_errno = error_code::ERR_OPNEWARR;
        goto failed_ret;
    }

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

inline void hash_index::pool::_cleanup() {
    if (this->m_entry_arr)
        operator delete[](this->m_entry_arr);

    this->m_entry_arr   = nullptr;
    this->m_free_head_p = nullptr;
    this->m_size        = 0u;
    this->m_capacity    = 0u;
    this->m_errno       = 0;
}

inline hash_index::slot *hash_index::pool::new_slot() {
    this->m_errno = error_code::NOERROR;
    if (this->m_size == this->m_capacity) {
        this->m_errno = error_code::ERR_POOL_FULL;
        return nullptr;
    }

    assert(this->m_size <= this->m_capacity);
    assert(this->m_free_head_p);

    slot *slot_p = reinterpret_cast<slot*>(this->m_free_head_p);
    this->m_free_head_p = reinterpret_cast<entry*>(
        this->m_free_head_p->next_free_p
    );
    ++this->m_size;

    memset(slot_p, 0, sizeof(*slot_p));

    return slot_p;
}

inline int hash_index::pool::free_slot(slot *slot_p) {
    this->m_errno = error_code::NOERROR;
    if (!slot_p) {
        this->m_errno = error_code::ERR_NULLPTR;
        return KVSYMDB_FAILED;
    }

    memset(slot_p, 0, sizeof(*slot_p));
    entry *pool_ent_p = reinterpret_cast<entry*>(slot_p);
    ptrdiff_t ent_idx = pool_ent_p - this->m_entry_arr;
    if (
        ent_idx < 0L ||
        ent_idx >= static_cast<ptrdiff_t>(this->m_capacity)
    ) {
        this->m_errno = error_code::ERR_ADDR_OUT_OF_RANGE;
        return KVSYMDB_FAILED;
    }

    ptrdiff_t ent_off =
        reinterpret_cast<uint8_t*>(pool_ent_p) - 
        reinterpret_cast<uint8_t*>(this->m_entry_arr);
    
    if (ent_off % sizeof(entry) != 0L) {
        this->m_errno = error_code::ERR_PTR_UNALIGNED;
        return KVSYMDB_FAILED;
    }

    assert(this->m_size <= this->m_capacity);
    assert(this->m_size);

    pool_ent_p->next_free_p = reinterpret_cast<slot*>(this->m_free_head_p);
    this->m_free_head_p = pool_ent_p;
    --this->m_size;

    return KVSYMDB_SUCCESS;
}



// hash_index impl
inline int hash_index::init(
    const kvsymdb_t    *symdb_p,
    uint32_t            bucket_cnt,
    uint32_t            slot_cnt
) {
    assert(bucket_cnt && slot_cnt);
    this->m_c_symdb_p = symdb_p;
    this->m_errno = error_code::NOERROR;

    // init pool
    int rc = this->m_pool.init(slot_cnt);
    if (rc) goto failed_ret;

    // alloc && init bucket arr
    this->m_bucket_arr = static_cast<bucket*>(
        operator new[](bucket_cnt * sizeof(bucket), std::nothrow)
    );
    if (!this->m_bucket_arr){
        this->m_errno = error_code::ERR_OPNEWARR;
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

inline void hash_index::_cleanup() {
    if (this->m_bucket_arr)
        operator delete[](this->m_bucket_arr);

    this->m_pool._cleanup();
    this->m_c_symdb_p   = nullptr;
    this->m_bucket_arr  = nullptr; 
    this->m_bucket_cnt  = 0u;
    this->m_errno       = 0;
}

inline int hash_index::insert(const kvsymdb::entry *ent_p) {
    this->m_errno = error_code::NOERROR;
    if (!ent_p) {
        this->m_errno = error_code::ERR_NULLPTR;
        return KVSYMDB_FAILED;
    }

    slot *slot_p = this->m_pool.new_slot();
    if (!slot_p) {
        this->m_errno = error_code::ERR_POOL_ALLOC;
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

inline int hash_index::lookup(
    const kvsymdb::buffer_view &key_ref,
    uint32_t                    hash,
    lookup_result              *out_res_p
) {
    this->m_errno = error_code::NOERROR;
    if (!out_res_p) {
        this->m_errno = error_code::ERR_NULLPTR;
        return KVSYMDB_FAILED;
    }
    if (!key_ref.data || !key_ref.size) {
        this->m_errno = error_code::ERR_ENTVIEW;
        return KVSYMDB_FAILED;
    }

    out_res_p->bucket_p = nullptr;
    out_res_p->slot_p   = nullptr;

    bucket *bucket_p = &this->m_bucket_arr[hash % this->m_bucket_cnt];
    slot *slot_p = bucket_p->chain_head_p;

    uint32_t probc = 0u;
    while (probc < this->m_pool.m_size && slot_p) {
        const kvsymdb::entry *ent_p = slot_p->ent_p;
        if (ent_p->hash == hash) {
            kvsymdb::entry_view view{};
            get_entry_view(
                ent_p,
                &view
            );
            if (
                key_ref.size == view.name_len &&
                memcmp(key_ref.data, view.name, view.name_len) == 0
            ) {
                out_res_p->bucket_p = bucket_p;
                out_res_p->slot_p   = slot_p;
                return KVSYMDB_SUCCESS;
            }
        }

        slot_p = slot_p->next_slot_p;
        ++probc;
    }

    assert(!slot_p);

    this->m_errno = error_code::ERR_KEY_NOT_FOUND;
    return KVSYMDB_FAILED;
}

inline int hash_index::remove(lookup_result *lu_res_p) {
    if (
        !lu_res_p ||
        !lu_res_p->bucket_p ||
        !lu_res_p->slot_p ||
        !lu_res_p->slot_p->ent_p
    ) {
        this->m_errno = error_code::ERR_BAD_HTLURES;
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

    int rc = this->m_pool.free_slot(old_slot_p);
    assert(!rc);

    return KVSYMDB_SUCCESS;
}

} // namespace _intrnl

} // namespace cxx_kvsymdb 