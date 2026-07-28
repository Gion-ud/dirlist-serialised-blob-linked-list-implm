#pragma once
#include <kvsymdb.h>
#include <iterator>
#include <fnv1a_hash.h>
#include <string.h>
#include <unistd.h>


namespace cxx_kvsymdb {

struct kvsymdb {
    static constexpr uint32_t ALIGN_SIZE    = alignof(kvsymdb_entry_t);
    static constexpr uint32_t INIT_BUFSIZE  = 32u;
    static constexpr uint32_t INIT_ENTC     = 1u;

    using entry         = kvsymdb_entry_t;
    using buffer_view   = kvsymdb_bufview_t;
    using self_state    = kvsymdb_state_t;
    struct entry_view;

    struct string_view : public kvsymdb_bufview_t {
        string_view() noexcept
        {
            this->data = nullptr;
            this->size = 0ul;
        }
        string_view(const char *data, size_t len) noexcept {
            this->set(data, len);
        }
        string_view(const char *cstr) noexcept {
            this->set(cstr);
        }
        void set(const char *data, size_t len) noexcept {
            this->data = data;
            this->size = len;
        }
        void set(const char *cstr) noexcept {
            this->data = cstr;
            this->size = strlen(cstr);
        }
        uint32_t hash32() const noexcept {
            return fnv_1a_hash32(this->data, this->size);
        }
        string_view &operator=(const char *cstr) noexcept {
            this->set(cstr);
            return *this;
        }
        ~string_view() noexcept = default;
        string_view(const string_view &other_ref) noexcept = default;
        string_view(string_view &&other_rref) noexcept = default;
        string_view &operator=(const string_view &other_ref) noexcept = default;
        string_view &operator=(string_view &&other_rref) noexcept = default;
    }; // struct string_view

private:
    kvsymdb_t  *m_symdb_p;
    int         m_errno;

    void _cleanup() noexcept {
        if (this->m_symdb_p) {
            destroy_kvsymdb(this->m_symdb_p);
            this->m_symdb_p = nullptr;
        }
    }

public:
    kvsymdb(uint32_t entc) noexcept
        : m_symdb_p(nullptr), m_errno(0)
    {
        this->m_symdb_p = create_kvsymdb(entc, INIT_BUFSIZE, &this->m_errno);
    }
    ~kvsymdb() noexcept {
        this->_cleanup();
    }
    kvsymdb(const kvsymdb &other_ref) = delete;             // copy constructor: GONE
    kvsymdb& operator=(const kvsymdb &other_ref) = delete;  // copy assignment: GONE
    // I will consider impl a proper deep copy

    kvsymdb(kvsymdb &&other_rref) noexcept                  // move consturctor
        : m_symdb_p(other_rref.m_symdb_p), m_errno(other_rref.m_errno)
    {
        other_rref.m_symdb_p = nullptr;
        other_rref.m_errno   = 0;
    }
    kvsymdb& operator=(kvsymdb &&other_rref) noexcept {     // move assignment
        if (this == &other_rref) return *this;

        this->_cleanup();  // destroy this if is_init to avoid leak

        this->m_symdb_p      = other_rref.m_symdb_p;
        this->m_errno        = other_rref.m_errno;
        other_rref.m_symdb_p = nullptr;
        other_rref.m_errno   = 0;
        return *this;
    }

    bool is_init() const noexcept {
        return (!!this->m_symdb_p);
    }
    void clearerr() noexcept {
        this->m_errno = 0;
    }
    const char *errmsg() const noexcept {
        return kvsymdb_strerror(this->m_errno);
    }

    int reserve(uint32_t entc) noexcept {
        return kvsymdb_reserve(this->m_symdb_p, entc, &this->m_errno);
    }
    int reserve_buffer(uint32_t new_bufsize) noexcept {
        return kvsymdb_reserve_arenabuf(
            &this->m_symdb_p, new_bufsize, &this->m_errno
        );
    }
    int get_self_state(self_state *out_view_p) noexcept {
        return kvsymdb_get_intrnl_state_view(
            this->m_symdb_p, out_view_p, &this->m_errno
        );
    }
    int insert(
        const string_view  &key_ref,
        const buffer_view  &val_ref,
        uint16_t            type
    ) noexcept {
        return kvsymdb_insert(
            &this->m_symdb_p,
            &key_ref,
            &val_ref,
            key_ref.hash32(),
            type,
            &this->m_errno
        );
    }
    int mark_dead(entry *ent_p) noexcept {
        return kvsymdb_mark_dead(
            this->m_symdb_p,
            ent_p,
            &this->m_errno
        );
    }
    int get_entry_view(
        const entry    *ent_p,
        entry_view     *out_entview_p
    ) noexcept {
        return kvsymdb_get_entview(
            this->m_symdb_p,
            ent_p,
            out_entview_p,
            &this->m_errno
        );
    }
    bool is_valid_entry(const entry *ent_p) const noexcept {
        return kvsymdb_is_valid_entry(this->m_symdb_p, ent_p);
    }
    kvsymdb_t *_raw() const noexcept {
        return this->m_symdb_p;
    };
    int compact() noexcept {
        return kvsymdb_compact(&this->m_symdb_p, &this->m_errno);
    };

    struct reader;
    struct hash_index;
    struct iterator :
        public std::iterator<
            std::input_iterator_tag,    // iterator_category 
            kvsymdb_entry_t,            // value_type
            ptrdiff_t,                  // difference_type
            kvsymdb_entry_t*,           // Pointer
            kvsymdb_entry_t&            // Reference
        >
    {
    private:
        kvsymdb         *m_parent_p;
        kvsymdb_entry_t *m_ent_p;
        kvsymdb_entry_t *_next() noexcept {
            return kvsymdb_iterator_next(
                this->m_parent_p->m_symdb_p,
                this->m_ent_p
            );
        }
    public:
        explicit iterator(
            kvsymdb            *parent_p,
            kvsymdb_entry_t    *ent_p
        ) noexcept
            : m_parent_p(parent_p), m_ent_p(ent_p)
        {
        }
        iterator &operator++() noexcept {   // ++it; next(it)
            this->m_ent_p = this->_next();
            return *this;
        }
        iterator operator++(int) noexcept { // it++
            iterator ret = *this;
            this->m_ent_p = this->_next();
            return ret;
        }
        bool operator==(const iterator  &other_ref) const noexcept { // it1 == it2
            return (this->m_ent_p == other_ref.m_ent_p);
        }
        bool operator!=(const iterator &other_ref) const noexcept { // it1 != it2
            return (this->m_ent_p != other_ref.m_ent_p);
        }
        reference operator*() const noexcept {  // *it; deref(it)
            return *this->m_ent_p;
        };
    }; // struct iterator

    iterator begin() noexcept {
        return iterator(
            this,
            kvsymdb_iterator_begin(this->m_symdb_p)
        );
    }
    iterator end() noexcept {
        return iterator(
            this,
            kvsymdb_iterator_end(this->m_symdb_p)
        );
    }
    iterator next(iterator &it_ref) const noexcept {
        return ++it_ref;
    }
    entry &deref(const iterator &it_ref) const noexcept {
        return *it_ref;
    }

    struct entry_view : public kvsymdb_entview_t {
    private:
        kvsymdb    *m_parent_symdb_p;

        void _zero_init_base() noexcept {
            memset(this, 0, sizeof(kvsymdb_entview_t));
        }
    public:
        entry_view() noexcept :
            m_parent_symdb_p(nullptr)
        {
            this->_zero_init_base();
        }
        entry_view(
            kvsymdb        &symdb_ref,
            const entry    &ent_ref
        ) noexcept :
            m_parent_symdb_p(&symdb_ref)
        {
            this->_zero_init_base();
            this->from_entry(symdb_ref, ent_ref);
        }

        int from_entry(
            kvsymdb        &symdb_ref,
            const entry    &ent_ref
        ) noexcept {
            int rc = symdb_ref.get_entry_view(&ent_ref, this);
            if (!rc) this->m_parent_symdb_p = &symdb_ref;
            return rc;
        }

        bool is_init() const noexcept {
            return (!!this->m_parent_symdb_p);
        }
    };

    using file_header = kvsymdb_file_header_t;

    struct file_builder {
    private:
        kvsymdb    *m_parent_symdb_p;
        int         m_errno;

    public:
        file_builder(kvsymdb &symdb_ref) noexcept :
            m_parent_symdb_p(&symdb_ref),
            m_errno(0)
        {
        }

        int dump(const char *filename) noexcept;

        ~file_builder() noexcept {
            this->m_parent_symdb_p  = nullptr;
            this->m_errno           = 0;
        }

        inline const char *errmsg() const noexcept {
            return kvsymdb_strerror(this->m_errno);
        }

        inline void clearerr() noexcept {
            this->m_errno = 0;
        }
    };

    struct file_reader;

}; // struct kvsymdb

namespace _intrnl {
struct hash_index;
} // namespace _intrnl



struct kvsymdb::reader : private kvsymdb_reader_t { // inherited from c abi struct
private:
    int m_errno;
public:
    reader(kvsymdb &symdb_ref) noexcept : m_errno(0) {
        int rc = ::kvsymdb_reader_bind(
            this,
            symdb_ref.m_symdb_p,
            &this->m_errno
        );
        if (rc != KVSYMDB_SUCCESS) {
            this->_symdb_p = nullptr;
        }
    }
    reader(kvsymdb_t *c_symdb_p) noexcept : m_errno(0) {
        int rc = ::kvsymdb_reader_bind(
            this,
            c_symdb_p,
            &this->m_errno
        );
        if (rc != KVSYMDB_SUCCESS) {
            this->_symdb_p = nullptr;
        }
    }
    ~reader() noexcept {
        kvsymdb_reader_unbind(this);
    }
    //reader(const reader &other_ref) = delete;
    //reader& operator=(const reader &other_ref) = delete;

    void clearerr() noexcept {
        this->m_errno = 0;
    }
    const char *errmsg() const noexcept {
        return kvsymdb_strerror(this->m_errno);
    }

    bool is_init() const noexcept {
        return (!!this->_symdb_p);
    }
    const entry *read() noexcept {
        return kvsymdb_reader_read(this, &this->m_errno);
    }
    // lseek
    int rewind() noexcept {
        return kvsymdb_reader_rewind(this);
    }
};

struct _intrnl::hash_index {
    using slot  = kvsymdb_hidx_slot_t;
    struct bucket {
        slot   *chain_head_p;
    };
    struct lookup_result {
        bucket *bucket_p;
        slot   *slot_p;
    };

private:
    struct pool {
        union entry {
            slot   *next_free_p;    // free list ptr
            slot    slot;           // slot
        };

        entry      *m_entry_arr;    // [0]; slot arr; union
        entry      *m_free_head_p;  // [1]; free head
        uint32_t    m_size;         // [2]; pool size
        uint32_t    m_capacity;     // [3]; pool capacity
        int         m_errno;        // [4]; errno

        inline pool() noexcept :
            m_entry_arr(nullptr),
            m_free_head_p(nullptr),
            m_size(0u),
            m_capacity(0u),
            m_errno(0)
        {
        }

        int init(uint32_t pool_size) noexcept;
        void _cleanup() noexcept;
        slot *new_slot() noexcept;
        int free_slot(slot *slot_p) noexcept;

        pool(const pool &other_ref) = delete;
        pool& operator=(const pool &other_ref) = delete;
        ~pool() noexcept = default;
    };

public:
    const kvsymdb_t    *m_c_symdb_p;    // [0]
    pool                m_pool;         // [1]
    bucket             *m_bucket_arr;   // [2]
    uint32_t            m_bucket_cnt;   // [3]
    int                 m_errno;        // [4]

    int init(
        const kvsymdb_t    *c_symdb_p,
        uint32_t            bucket_cnt,
        uint32_t            slot_cnt
    ) noexcept;
    void _cleanup() noexcept;

    int insert(const kvsymdb::entry *ent_p) noexcept;
    int lookup(
        const kvsymdb::buffer_view &key_ref,
        uint32_t                    key_hash,
        lookup_result              *out_res_p
    ) noexcept;
    int remove(lookup_result *lu_res_p) noexcept;

    inline hash_index() noexcept :
        m_c_symdb_p(nullptr),
        m_pool{},
        m_bucket_arr(nullptr),
        m_bucket_cnt(0u),
        m_errno(0)
    {
    }
    inline ~hash_index() noexcept {
        this->_cleanup();
    }
    inline hash_index(const hash_index &other_ref) = delete;
    inline hash_index& operator=(const hash_index &other_ref) = delete;
};

struct kvsymdb::hash_index {
private:
    using lookup_result = _intrnl::hash_index::lookup_result;

    _intrnl::hash_index m_base;
public:
    const char *errmsg() const noexcept {
        return kvsymdb_strerror(this->m_base.m_errno);
    }
    int geterror() {
        return this->m_base.m_errno;
    }
    void clearerr() {
        this->m_base.m_errno = 0;
    }
    bool is_init() const noexcept {
        return (!!this->m_base.m_c_symdb_p);
    }

    int init(
        kvsymdb    &symdb_ref,
        uint32_t    capacity
    ) noexcept;
    int _init(
        const kvsymdb_t    *symdb_p,
        uint32_t            capacity
    ) noexcept;
    int insert(const entry *ent_p) noexcept;
    const entry *get(const string_view &key_ref) noexcept;
    const entry *get(const char *key_cstr) noexcept {
        return this->get(string_view(key_cstr));
    }
    const entry *operator[](const string_view &key_ref) noexcept {
        return this->get(key_ref);
    }
    const entry *operator[](const char *key_cstr) noexcept {
        return this->get(key_cstr);
    }

    int remove(const entry *ent_p) noexcept;

    hash_index() noexcept : m_base{} {
    }

    ~hash_index() noexcept = default;
    hash_index(const hash_index &other_ref) = delete;
    hash_index& operator=(const hash_index &other_ref) = delete;
};

struct kvsymdb::file_reader {
private:
    kvsymdb_t  *m_c_symdb_p;
    int         m_errno;

public:
    file_reader(const char *filename) noexcept :
        m_c_symdb_p(nullptr), m_errno(0)
    {
        m_c_symdb_p = ::create_kvsymdb_file_reader(
            filename,
            &this->m_errno
        );
    }

    bool is_init() const noexcept {
        return (!!this->m_c_symdb_p);
    }
    const char *errmsg() const noexcept {
        return ::kvsymdb_strerror(this->m_errno);
    }
    void clearerr() noexcept {
        this->m_errno = 0;
    }

    reader make_reader() noexcept {
        return reader(this->m_c_symdb_p);
    }

    int get_entry_view(
        const entry    *ent_p,
        entry_view     *out_entview_p
    ) noexcept {
        return kvsymdb_get_entview(
            this->m_c_symdb_p,
            ent_p,
            out_entview_p,
            &this->m_errno
        );
    }

    ~file_reader() {
        ::destroy_kvsymdb(this->m_c_symdb_p);
        this->m_errno = 0;
    }
};


} // namespace cxx_kvsymdb 





