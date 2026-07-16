#pragma once
#include <kvsymdb.h>
#include <iterator>
#include <fnv1a_hash.h>
#include <new>
#include <string.h>


namespace cxx_kvsymdb {
    static constexpr uint32_t ALIGN_SIZE    = alignof(kvsymdb_entry_t);
    static constexpr uint32_t INIT_BUFSIZE  = 32u;
    static constexpr uint32_t INIT_ENTC     = 1u;

    struct kvsymdb {
        using entry         = kvsymdb_entry_t;
        using buffer_view   = kvsymdb_bufview_t;
        using self_state    = kvsymdb_state_t;
        using entry_view    = kvsymdb_entview_t;

        struct string_view : public kvsymdb_bufview_t {
            string_view() noexcept
            {
                this->data = nullptr;
                this->size = 0ul;
            }
            string_view(const char *data, size_t len) noexcept {
                this->data = data;
                this->size = len;
            }
            string_view(const char *cstr) noexcept {
                this->data = cstr;
                this->size = strlen(cstr);
            }
            string_view(buffer_view &buf_view) noexcept {
                this->data = buf_view.data;
                this->size = buf_view.size;
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
            ~string_view() noexcept = default;
            string_view(const string_view &other_ref) noexcept = default;
            string_view(string_view &&other_rref) noexcept = default;
            string_view& operator=(const string_view &other_ref) noexcept = default;
            string_view& operator=(string_view &&other_rref) noexcept = default;
        };

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
            if (this != &other_rref) this->_cleanup();  // destroy this if is_init to avoid leak

            this->m_symdb_p      = other_rref.m_symdb_p;
            this->m_errno        = other_rref.m_errno;
            other_rref.m_symdb_p = nullptr;
            other_rref.m_errno   = 0;
            return *this;
        }

        void clearerr() noexcept {
            this->m_errno = 0;
        }
        const char *errmsg() const noexcept {
            return kvsymdb_strerror(this->m_errno);
        }
        bool is_init() const noexcept {
            return (!!this->m_symdb_p);
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
        int get_entview(
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
        struct hash_table;
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
            ) noexcept : m_parent_p(parent_p), m_ent_p(ent_p)
            {
            }
            iterator& operator++() noexcept {   // ++it; next(it)
                this->m_ent_p = this->_next();
                return *this;
            }
            iterator operator++(int) noexcept { // it++
                iterator ret = *this;
                this->m_ent_p = this->_next();
                return ret;
            }
            bool operator==(const iterator& other_ref) const noexcept { // it1 == it2
                return (this->m_ent_p == other_ref.m_ent_p);
            }
            bool operator!=(const iterator& other_ref) const noexcept { // it1 != it2
                return (this->m_ent_p != other_ref.m_ent_p);
            }
            reference operator*() const noexcept {  // *it; deref(it)
                return *this->m_ent_p;
            };
        };

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
        iterator next(iterator& it_ref) const noexcept {
            return ++it_ref;
        }
        entry& deref(const iterator& it_ref) const noexcept {
            return *it_ref;
        }
    };
}

struct cxx_kvsymdb::kvsymdb::reader : private kvsymdb_reader_t { // inherited from c abi struct
private:
    kvsymdb    *m_parent_p;
public:
    reader(kvsymdb& symdb_ref) noexcept :
        m_parent_p(&symdb_ref)
    {
        int rc = kvsymdb_reader_bind(
            this,
            symdb_ref.m_symdb_p,
            &symdb_ref.m_errno
        );
        if (rc != KVSYMDB_SUCCESS) {
            this->_symdb_p      = nullptr;
            this->_pos          = 0u;
            this->m_parent_p    = nullptr;
        }
    }
    ~reader() noexcept {
        kvsymdb_reader_unbind(this);
        this->m_parent_p = nullptr;
    }
    reader(const reader &other_ref) = delete;
    reader& operator=(const reader &other_ref) = delete;


    bool is_init() const noexcept {
        return (!!this->m_parent_p);
    }
    const entry *read() noexcept {
        return kvsymdb_reader_read(this, &this->m_parent_p->m_errno);
    }
    int rewind() noexcept {
        return kvsymdb_reader_rewind(this);
    }
};


struct cxx_kvsymdb::kvsymdb::hash_table {
    using slot  = kvsymdb_hidx_slot_t;
    struct bucket {
        slot   *chain_head_p;
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
        slot *alloc() noexcept;
        int free(slot *slot_p) noexcept;

        pool(const pool &other_ref) = delete;
        pool& operator=(const pool &other_ref) = delete;
        ~pool() noexcept {
            this->_cleanup();
        };
    };

    const kvsymdb_t    *m_c_symdb_p;    // [0]
    pool                m_pool;         // [1]
    bucket             *m_bucket_arr;   // [2]
    uint32_t            m_bucket_cnt;   // [3]
    int                 m_errno;        // [4]

    void _cleanup() noexcept;

public:
    int _init(
        const kvsymdb_t    *c_symdb_p,
        uint32_t            bucket_cnt,
        uint32_t            slot_cnt
    ) noexcept;

    bool is_init() const noexcept {
        return (!!this->m_c_symdb_p);
    }
    inline int init(
        kvsymdb    &symdb_ref,
        uint32_t    capacity
    ) noexcept {
        return this->_init(
            symdb_ref.m_symdb_p,
            capacity,
            capacity
        );
    }

    int insert(const entry *ent_p) noexcept;
    const entry *lookup(const buffer_view &key_ref, uint32_t hash) noexcept;
    int remove(const entry *ent_p) noexcept;

    hash_table() noexcept :
        m_c_symdb_p(nullptr),
        m_pool{},
        m_bucket_arr(nullptr),
        m_bucket_cnt(0u),
        m_errno(0)
    {
    }
    ~hash_table() noexcept {
        this->_cleanup();
    }
    hash_table(const hash_table &other_ref) = delete;
    hash_table& operator=(const hash_table &other_ref) = delete;
};

