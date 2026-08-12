#include <cstddef>
#include <cstdint>
#include <memory>
#include <cstring>
#include <cassert>
#include <cstdio>
#include "string_utils.hpp"

#include "dbg_print.h"

extern "C" {

typedef struct _c_jwkvmap_link {
    struct _c_jwkvmap_link *ln_prev_p;
    struct _c_jwkvmap_link *ln_next_p;
} jwkvmap_link_t;

typedef struct _c_jwkvmap_entry_header {
    uint32_t    eh_type;
    uint32_t    eh_hash;
    uint32_t    eh_key_len;
    uint32_t    eh_val_len;
    uint32_t    eh_ent_len;
    uint8_t     eh_payload[];
} jwkvmap_entry_header_t;

typedef struct _c_jwkvmap_hash_slot {
    jwkvmap_link_t          hs_link;
    jwkvmap_entry_header_t  hs_ehdr;
} jwkvmap_hash_slot_t;

typedef struct _c_jwkvmap_entry_view {
    const jwkvmap_entry_header_t   *ev_ehdr_p;
    const char                     *ev_key_p;
    const void                     *ev_val_p;
} jwkvmap_entry_view_t;


typedef struct _c_jwkvmap_bufview {
    const void *buf_data;
    size_t      buf_length;
} jwkvmap_bufview_t;

enum {
    JWKVMAP_OK      = 0,
    JWKVMAP_FAILED  = -1,
};


inline uint32_t jwkvmap_hash32(
    const jwkvmap_bufview_t *buf_view_p
) {
    return fnv_1a_hash32(buf_view_p->buf_data, buf_view_p->buf_length);
}


} // extern "C"

#include <iostream>

namespace jwkvmap {

using namespace string_utils;
using hash32_t = uint32_t;

struct buffer_view : private jwkvmap_bufview_t {
    buffer_view(const void *data, size_t length) noexcept :
        jwkvmap_bufview_t{ data, length }
    {
    }
    buffer_view(const char *cstr) noexcept :
        jwkvmap_bufview_t{}
    {
        if (!cstr) return;
        this->buf_data      = cstr;
        this->buf_length    = std::strlen(cstr);
    }
    const void *data() const noexcept {
        return this->buf_data;
    }
    std::size_t length() const noexcept {
        return this->buf_length;
    }
    const jwkvmap_bufview_t &_base() const noexcept {
        return *this;
    }
}; // struct buffer_view


inline hash32_t hash32(const buffer_view &bufview_ref) {
    return jwkvmap_hash32(&bufview_ref._base());
}

enum class KVErrorCode : std::uint8_t {
    ERR_NOERROR = 0,
    ERR_OPNEWARR,
    ERR_ALREADY_INIT,
    ERR_UNINIT,
    ERR_NULLPTR,
    ERR_RESIZE,
    ERR_OUT_OF_BOUND,
    ERR_OPNEW,
    ERR_BUCPUSH,
    ERR_NOT_FOUND,
    ERR_UNLINK,
    ERR_INIT,
    ERR_INSERT,
    ERR_CREATE_ENT,
    ERR_LOOKUP,
    ERR_BUCINIT,
    ERR_DUPKEY,
};

struct KVError {
private:
    KVErrorCode m_errno;

public:
    KVError(KVErrorCode kv_err) noexcept : m_errno(kv_err) {
    }

    KVErrorCode geterror() const noexcept {
        return this->m_errno;
    }

    const char *strerror() const noexcept {
        using Err = KVErrorCode;
        switch (this->m_errno) {
            case (Err::ERR_OPNEWARR):
                return "operator new[] failed";
            case (Err::ERR_ALREADY_INIT):
                return "object already initialized";
            case (Err::ERR_UNINIT):
                return "object uninitialized";
            case (Err::ERR_NULLPTR):
                return "nullptr arg";
            case (Err::ERR_RESIZE):
                return "resize failed";
            case (Err::ERR_OUT_OF_BOUND):
                return "out of bound access";
            case (Err::ERR_OPNEW):
                return "operator new failed";
            case (Err::ERR_BUCPUSH):
                return "bucket push slot failed";
            case (Err::ERR_NOT_FOUND):
                return "key not found";
            case (Err::ERR_UNLINK):
                return "failed to unlink slot";
            case (Err::ERR_INIT):
                return "object construction failed";
            case (Err::ERR_INSERT):
                return "failed to insert entry";
            case (Err::ERR_CREATE_ENT):
                return "failed to create entry";
            case (Err::ERR_LOOKUP):
                return "lookup failed";
            case (Err::ERR_BUCINIT):
                return "bucket init failed";
            case (Err::ERR_DUPKEY):
                return "key already exists";
            default:
                return "no error";
        }
    }
}; // struct KVError


namespace fmt {
constexpr const char U32_HEX_UPPER_FMT[] = "0x%.8X";
constexpr const char U32_HEX_LOWER_FMT[] = "0x%.8x";

template<const char *FMT>
struct u32_hex_fmt {
private:
    static constexpr auto BUFSIZE = 16u;
    char    m_buf[BUFSIZE];
public:
    u32_hex_fmt(std::uint32_t value) noexcept :
        m_buf{}
    {
        std::snprintf(this->m_buf, BUFSIZE, FMT, value);
    }
    const char *data() const noexcept {
        return this->m_buf;
    }
};

using u32_upper_hex_fmt = u32_hex_fmt<U32_HEX_UPPER_FMT>;
using u32_lower_hex_fmt = u32_hex_fmt<U32_HEX_LOWER_FMT>;


inline std::ostream &operator<<(std::ostream &os_ref, const buffer_view &bufview_ref) noexcept {
    if (!bufview_ref.data()) {
        std::cout << "(null)";
    } else {
        os_ref.write(
            static_cast<const char*>(bufview_ref.data()),
            bufview_ref.length()
        );
    }
    return os_ref;
}

inline std::ostream &operator<<(std::ostream &os_ref, const KVError &err_ref) noexcept {
    os_ref << err_ref.strerror();
    return os_ref;
}


template<const char *FMT>
inline std::ostream &operator<<(std::ostream &os_ref, const u32_hex_fmt<FMT>&fmt_ref) noexcept {
    os_ref << fmt_ref.data();
    return os_ref;
}


} // namespace fmt


constexpr std::ptrdiff_t NULL_IDX = -1L;

struct KVMap {
    using Slot          = jwkvmap_hash_slot_t;
    using SlotLink      = jwkvmap_link_t;
    using EntryHeader   = jwkvmap_entry_header_t;
    using c_EntryView   = jwkvmap_entry_view_t;

    struct SlotHandle {
    private:
        Slot   *m_slot_p;
    public:
        SlotHandle(Slot *slot_p) noexcept :
            m_slot_p(slot_p) 
        {
        }
        SlotHandle(SlotLink *slot_link_p) noexcept :
            m_slot_p(reinterpret_cast<Slot*>(slot_link_p)) 
        {
        }
        SlotHandle() noexcept :
            m_slot_p(nullptr) 
        {
        }
        void set(Slot *slot_p) noexcept {
            this->m_slot_p = slot_p;
        }
        void set(SlotLink *slot_link_p) noexcept {
            this->m_slot_p = reinterpret_cast<Slot*>(slot_link_p);
        }
        bool is_good() const noexcept {
            return (!!this->m_slot_p);
        }
        SlotLink *link() noexcept {
            return &this->m_slot_p->hs_link;
        }
        Slot *get_slot() noexcept {
            return this->m_slot_p;
        }
        Slot *prev_slot() noexcept {
            return reinterpret_cast<Slot*>(this->m_slot_p->hs_link.ln_prev_p);
        }
        Slot *next_slot() noexcept {
            return reinterpret_cast<Slot*>(this->m_slot_p->hs_link.ln_next_p);
        }
        SlotHandle prev() noexcept {
            return SlotHandle(this->prev_slot());
        }
        SlotHandle next() noexcept {
            return SlotHandle(this->next_slot());
        }
        EntryHeader *header() noexcept {
            return &this->m_slot_p->hs_ehdr;
        }
        Slot *unlink() noexcept {
            this->link()->ln_prev_p->ln_next_p = this->link()->ln_next_p;
            this->link()->ln_next_p->ln_prev_p = this->link()->ln_prev_p;
            this->link()->ln_prev_p = this->link();
            this->link()->ln_next_p = this->link();

            return this->get_slot();
        }
        Slot *push_head(SlotHandle slot_h) noexcept {
            SlotLink *root_p = this->link();
            SlotLink *head_p = this->next().link();

            slot_h.link()->ln_prev_p = root_p;
            slot_h.link()->ln_next_p = head_p;
            root_p->ln_next_p = slot_h.link();
            head_p->ln_prev_p = slot_h.link();

            return slot_h.get_slot();
        }
        Slot *push_tail(SlotHandle slot_h) noexcept {
            SlotLink *root_p = this->link();
            SlotLink *tail_p = this->prev().link();

            slot_h.link()->ln_prev_p = tail_p;
            slot_h.link()->ln_next_p = root_p;
            root_p->ln_prev_p = slot_h.link();
            tail_p->ln_next_p = slot_h.link();

            return slot_h.get_slot();
        }
    };

private:
    struct Bucket {
        friend struct KVMap;
    private:
        SlotLink    m_chain_root;
        std::size_t m_slot_cnt;

        SlotLink *chain_begin() noexcept {
            return m_chain_root.ln_next_p;
        }
        SlotLink *chain_end() noexcept {
            return &m_chain_root;
        }

    public:
        const SlotLink *list_root() const noexcept {
            return &m_chain_root;
        }
        std::size_t size() const noexcept {
            return this->m_slot_cnt;
        }

        Bucket() noexcept :
            m_chain_root{ &m_chain_root, &m_chain_root },
            m_slot_cnt(0UL)
        {
        }

        ~Bucket() noexcept {
            m_chain_root.ln_prev_p = &m_chain_root;
            m_chain_root.ln_next_p = &m_chain_root;
            this->m_slot_cnt = 0UL;
        }

        Bucket(const Bucket &) = delete;
        Bucket &operator=(const Bucket &) = delete;

    private:
        Slot *push_slot(Slot *slot_p) noexcept {
            assert(slot_p);
            ++this->m_slot_cnt;

            auto root = SlotHandle(&m_chain_root);
            auto ret = root.push_head(slot_p);
            return ret;
        }
        Slot *unlink_slot(Slot *slot_p) noexcept {
            assert(slot_p && this->size());
            --this->m_slot_cnt;
            return SlotHandle(slot_p).unlink();
        }
    };

public:
    struct LookupHandle {
        Bucket *bucket_p;
        Slot   *slot_p;
    }; // struct LookupHandle

private:
    std::unique_ptr<Bucket[]>   m_bucket_arr;   // [0]
    std::size_t                 m_bucket_cnt;   // [1]
    std::size_t                 m_slot_cnt;     // [2]
    KVErrorCode                 m_errno;        // [3]

    Slot *_create_slot(
        const buffer_view  &key_view_ref,
        const buffer_view  &val_view_ref,
        hash32_t            key_hash,
        std::uint32_t       type
    ) noexcept;
    void _destroy_slot(Slot *slot_p) noexcept;


    int _init() noexcept;
    void _cleanup() noexcept;

    int _link_slot(Slot *slot_p) noexcept;
    int _lookup_slot(
        const buffer_view  &key_view_ref,
        hash32_t            key_hash,
        LookupHandle       *out_luh_p
    ) noexcept;
    Slot *_unlink_slot(
        Bucket *bucket_p,
        Slot   *slot_p
    ) noexcept;

    Bucket *_bucket_arr_begin() noexcept {
        return this->m_bucket_arr.get();
    }
    Bucket *_bucket_arr_end() noexcept {
        return this->_bucket_arr_begin() + this->m_bucket_cnt;
    }

    int _rehash(std::size_t new_ht_size) noexcept;

public:
    KVMap() noexcept :
        m_bucket_arr(nullptr),
        m_bucket_cnt(0UL),
        m_slot_cnt(0UL),
        m_errno(KVErrorCode::ERR_NOERROR)
    {
    }

    const Bucket *bucket_arr_begin() noexcept {
        return this->m_bucket_arr.get();
    }
    const Bucket *bucket_arr_end() noexcept {
        return this->_bucket_arr_begin() + this->m_bucket_cnt;
    }

    bool is_init() const noexcept {
        return (!!this->m_bucket_arr);
    }

    ~KVMap() {
        if (this->is_init()) {
            this->_cleanup();
        }
    }

    KVMap(const KVMap &) = delete;
    KVMap &operator=(const KVMap &) = delete;
    KVMap(KVMap &&other_rref) noexcept :
        m_bucket_arr(std::move(other_rref.m_bucket_arr)),
        m_bucket_cnt(other_rref.m_bucket_cnt),
        m_slot_cnt(other_rref.m_slot_cnt),
        m_errno(other_rref.m_errno)
    {
        other_rref.m_bucket_cnt = 0UL;
        other_rref.m_slot_cnt   = 0UL;
        other_rref.m_errno      = KVErrorCode::ERR_NOERROR;
    }
    KVMap &operator=(KVMap &&other_rref) noexcept {
        if (this->is_init()) this->~KVMap();
        return *::new (this) KVMap(std::move(other_rref));
    }

    int insert(
        const buffer_view  &key_view_ref,
        const buffer_view  &val_view_ref,
        uint32_t            type
    ) noexcept;
    int upsert(
        const buffer_view  &key_view_ref,
        const buffer_view  &val_view_ref,
        uint32_t            type
    ) noexcept;
    const EntryHeader *find(const buffer_view &key_view_ref) noexcept;
    int erase(LookupHandle &luh_ref) noexcept;
    int remove(const buffer_view &key_view_ref) noexcept;
    int rehash(std::size_t new_ht_size) noexcept;

    int reserve(std::size_t new_ht_size) noexcept {
        return rehash(new_ht_size);
    }

    const EntryHeader *operator[](const buffer_view &key_view_ref) noexcept {
        return this->find(key_view_ref);
    }

    std::size_t bucket_count() const noexcept {
        return this->m_bucket_cnt;
    }

    std::size_t slot_count() const noexcept {
        return this->m_slot_cnt;
    }

    int upsert_auto_rehash(
        const buffer_view  &key_view_ref,
        const buffer_view  &val_view_ref,
        uint32_t            type
    ) noexcept {
        if (this->slot_count() + 1 > this->bucket_count()) {
            std::size_t new_slotc = (this->bucket_count())
                ? this->bucket_count() * 2 : 2UL;

            int rc = this->rehash(new_slotc);
            if (rc != JWKVMAP_OK) return JWKVMAP_FAILED;
        }
        return this->upsert(
            key_view_ref,
            val_view_ref,
            type
        );
    }
    KVErrorCode geterror() const noexcept {
        return this->m_errno;
    }

    struct BucketArrIter;

    struct BucketChainIter {
    private:
        Bucket     *m_bucket_p;
        SlotLink   *m_slot_ln_p;
    public:
        BucketChainIter(Bucket *bucket_p) noexcept :
            m_bucket_p((bucket_p->size()) ? bucket_p : nullptr),
            m_slot_ln_p(nullptr)
        {
        }

        const EntryHeader *next_entry() noexcept {
            if (!m_bucket_p) return nullptr;
            if (!m_slot_ln_p) {
                m_slot_ln_p = m_bucket_p->chain_begin();
                return SlotHandle(m_slot_ln_p).header();
            }
            m_slot_ln_p = m_slot_ln_p->ln_next_p;

            return (m_slot_ln_p == m_bucket_p->chain_end())
                ? nullptr : SlotHandle(m_slot_ln_p).header();
        }
    };

    struct BucketArrIter {
    private:
        KVMap  *m_kvm_p;
        Bucket *m_bucket_p;
    public:
        BucketArrIter(KVMap &kvm_ref) noexcept :
            m_kvm_p(&kvm_ref),
            m_bucket_p()
        {
        }
        Bucket *next_bucket() {
            if (!m_bucket_p) {
                m_bucket_p = m_kvm_p->_bucket_arr_begin();
                return m_bucket_p;
            }
            ++m_bucket_p;
            return (m_bucket_p != m_kvm_p->_bucket_arr_end())
                ? m_bucket_p : nullptr;
        }

    };


    struct EntryView;
}; // KVMap

int _kvmap_get_entry_view(
    const KVMap::EntryHeader   *ehdr_p,
    KVMap::c_EntryView         *out_entview_p
) noexcept;

struct KVMap::EntryView : private KVMap::c_EntryView {
    EntryView() noexcept : c_EntryView{} {
    }
    EntryView(const EntryHeader *ehdr_p) noexcept : 
        c_EntryView{ ehdr_p, nullptr, nullptr }
    {
        int rc = _kvmap_get_entry_view(this->ev_ehdr_p, this);
        if (rc != JWKVMAP_OK) {
            this->ev_ehdr_p = nullptr;
            this->ev_key_p  = nullptr;
            this->ev_val_p  = nullptr;
        }
    }
    void init_from(const EntryHeader *ehdr_p) noexcept {
        ::new (this) EntryView(ehdr_p);
    }
    const c_EntryView *_base() const noexcept {
        return this;
    }
    const buffer_view key() const noexcept {
        return buffer_view(this->ev_key_p, ev_ehdr_p->eh_key_len);
    }
    const buffer_view value() const noexcept {
        return buffer_view(this->ev_val_p, ev_ehdr_p->eh_val_len);
    }
    uint32_t hash() const noexcept {
        return this->ev_ehdr_p->eh_hash;
    }
    uint32_t type() const noexcept {
        return this->ev_ehdr_p->eh_type;
    }
    bool is_init() const noexcept {
        return (!!this->ev_ehdr_p);
    }
};



} // namespace jwkvmap