#pragma once
#include <kvsymdb.h>
#include <iterator>
#include <new>


namespace cxx_kvsymdb {
    static constexpr uint32_t ALIGN_SIZE    = alignof(kvsymdb_entry_t);
    static constexpr uint32_t INIT_BUFSIZE  = 32u;
    static constexpr uint32_t INIT_ENTC     = 1u;

    struct kvsymdb {
    private:
        kvsymdb_t  *m_symdb_p;
        int         m_errno;
    public:
        kvsymdb(uint32_t entc) noexcept
            : m_symdb_p(nullptr), m_errno(0)
        {
            this->m_symdb_p = create_kvsymdb(entc, INIT_BUFSIZE, &this->m_errno);
        }
        void _cleanup() noexcept {
            if (this->m_symdb_p) {
                destroy_kvsymdb(this->m_symdb_p);
                this->m_symdb_p = nullptr;
            }
        }
        ~kvsymdb() noexcept {
            this->_cleanup();
        }

        void clearerr() noexcept {
            this->m_errno = 0;
        }
        const char *errmsg() noexcept {
            return kvsymdb_strerror(this->m_errno);
        }
        bool is_init() noexcept {
            return (!!this->m_symdb_p);
        }

        using entry         = kvsymdb_entry_t;
        using buffer_view   = kvsymdb_bufview_t;
        using self_state    = kvsymdb_state_t;
        using entry_view    = kvsymdb_entview_t;


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
            const buffer_view  *key_p,
            const buffer_view  *val_p,
            uint32_t            key_hash,
            uint16_t            type
        ) noexcept {
            return kvsymdb_insert(
                &this->m_symdb_p,
                key_p,
                val_p,
                key_hash,
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
            ) noexcept : m_parent_p(parent_p), m_ent_p(ent_p) {}
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

        struct reader : private kvsymdb_reader_t { // inherited from c abi struct
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
            bool is_init() noexcept {
                return (!!this->m_parent_p);
            }
            const entry *read() noexcept {
                return kvsymdb_reader_read(this, &this->m_parent_p->m_errno);
            }
            int rewind() noexcept {
                return kvsymdb_reader_rewind(this);
            }
            ~reader() noexcept {
                kvsymdb_reader_unbind(this);
                this->m_parent_p = nullptr;
            }
            reader(const reader &other_ref) = delete;
            reader& operator=(const reader &other_ref) = delete;
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
        iterator next(iterator& it_ref) noexcept {
            return ++it_ref;
        }
        entry& deref(iterator& it_ref) noexcept {
            return *it_ref;
        }
        kvsymdb_t *_raw() const noexcept {
            return this->m_symdb_p;
        };

        int compact() noexcept {
            return kvsymdb_compact(&this->m_symdb_p, &this->m_errno);
        };

        kvsymdb(const kvsymdb& other_ref) = delete;             // copy constructor: GONE
        kvsymdb& operator=(const kvsymdb &other_ref) = delete;  // copy assignment: GONE
        // I will consider impl a proper deep copy

        kvsymdb(kvsymdb &&other_rref) noexcept                  // move consturctor
            : m_symdb_p(other_rref.m_symdb_p), m_errno(other_rref.m_errno)
        {
            other_rref.m_symdb_p = nullptr;
            other_rref.m_errno   = 0;
        }
        kvsymdb& operator=(kvsymdb&& other_rref) noexcept {     // move assignment
            if (this != &other_rref) this->_cleanup();  // destroy this if is_init to avoid leak

            this->m_symdb_p      = other_rref.m_symdb_p;
            this->m_errno        = other_rref.m_errno;
            other_rref.m_symdb_p = nullptr;
            other_rref.m_errno   = 0;
            return *this;
        }
    };
}
