#pragma once
#include <kvsymdb.h>
#include <iterator>
#include <new>


namespace cxx_kvsymdb {
    static constexpr uint32_t ALIGN_SIZE    = 4u;
    static constexpr uint32_t INIT_BUFSIZE  = 32u;
    static constexpr uint32_t INIT_ENTC     = 1u;

    struct kvsymdb {
    private:
        kvsymdb_t  *_symdb_p;
        int         _errno;
    public:
        kvsymdb(uint32_t entc) noexcept
            : _symdb_p(nullptr), _errno(0)
        {
            this->_symdb_p = create_kvsymdb(entc, INIT_BUFSIZE, &this->_errno);
        }
        void _cleanup() noexcept {
            if (this->_symdb_p) {
                destroy_kvsymdb(this->_symdb_p);
                this->_symdb_p = nullptr;
            }
        }
        ~kvsymdb() noexcept {
            this->_cleanup();
        }

        void clearerr() noexcept {
            this->_errno = 0;
        }
        const char *errmsg() noexcept {
            return kvsymdb_strerror(this->_errno);
        }
        bool is_init() noexcept {
            return (!!this->_symdb_p);
        }

        using entry         = kvsymdb_entry_t;
        using buffer_view   = kvsymdb_bufview_t;
        using self_state    = kvsymdb_state_t;
        using entry_view    = kvsymdb_entview_t;


        int reserve(uint32_t entc) noexcept {
            return kvsymdb_reserve(this->_symdb_p, entc, &this->_errno);
        }
        int reserve_buffer(uint32_t new_bufsize) noexcept {
            return kvsymdb_reserve_arenabuf(
                &this->_symdb_p, new_bufsize, &this->_errno
            );
        }
        int get_self_state(self_state *out_view_p) noexcept {
            return kvsymdb_get_intrnl_state_view(
                this->_symdb_p, out_view_p, &this->_errno
            );
        }
        int insert(
            const buffer_view  *key_p,
            const buffer_view  *val_p,
            uint32_t            key_hash,
            uint16_t            type
        ) noexcept {
            return kvsymdb_insert(
                &this->_symdb_p,
                key_p,
                val_p,
                key_hash,
                type,
                &this->_errno
            );
        }
        int mark_dead(entry *ent_p) noexcept {
            return kvsymdb_mark_dead(
                this->_symdb_p,
                ent_p,
                &this->_errno
            );
        }
        int get_entview(
            const entry    *ent_p,
            entry_view     *out_entview_p
        ) noexcept {
            return kvsymdb_get_entview(
                this->_symdb_p,
                ent_p,
                out_entview_p,
                &this->_errno
            );
        }
        bool is_valid_entry(const entry *ent_p) const noexcept {
            return kvsymdb_is_valid_entry(this->_symdb_p, ent_p);
        }
        
        struct iterator :
            public std::iterator<
                std::input_iterator_tag,    // iterator_category 
                kvsymdb_entry_t,            // value_type
                ptrdiff_t,                  // difference_type
                kvsymdb_entry_t *,          // Pointer
                kvsymdb_entry_t &           // Reference
            >
        {
        private:
            kvsymdb         *_iter_parent_p;
            kvsymdb_entry_t *_iter_ent_p;
            kvsymdb_entry_t *_next() noexcept {
                return kvsymdb_iterator_next(
                    this->_iter_parent_p->_symdb_p,
                    this->_iter_ent_p
                );
            }
        public:
            explicit iterator(
                kvsymdb            *parent_p,
                kvsymdb_entry_t    *ent_p
            ) noexcept
                : _iter_parent_p(parent_p), _iter_ent_p(ent_p) {}
            iterator &operator++() noexcept {   // ++it; next(it)
                this->_iter_ent_p = this->_next();
                return *this;
            }
            iterator operator++(int) noexcept { // it++
                iterator ret = *this;
                this->_iter_ent_p = this->_next();
                return ret;
            }
            bool operator==(iterator other) const noexcept {    // it1 == it2
                return (this->_iter_ent_p == other._iter_ent_p);
            }
            bool operator!=(iterator other) const noexcept {    // it1 != it2
                return (this->_iter_ent_p != other._iter_ent_p);
            }
            reference operator*() const noexcept {  // *it; deref(it)
                return *this->_iter_ent_p;
            };
        };

        iterator begin() noexcept {
            return iterator(
                this,
                kvsymdb_iterator_begin(this->_symdb_p)
            );
        }
        iterator end() noexcept {
            return iterator(
                this,
                kvsymdb_iterator_end(this->_symdb_p)
            );
        }
        iterator next(iterator &it_ref) noexcept {
            return ++it_ref;
        }
        entry &deref(iterator &it_ref) noexcept {
            return *it_ref;
        }
        kvsymdb_t *_raw() const noexcept {
            return this->_symdb_p;
        };

        int compact() noexcept {
            return kvsymdb_compact(&this->_symdb_p, &this->_errno);
        };

        kvsymdb(const kvsymdb &other_r) = delete;               // copy constructor: GONE
        kvsymdb& operator=(const kvsymdb &other_r) = delete;    // copy assignment: GONE
        kvsymdb(kvsymdb &&other_rr) noexcept                    // move consturctor
            : _symdb_p(other_rr._symdb_p), _errno(other_rr._errno)
        {
            other_rr._symdb_p   = nullptr;
            other_rr._errno     = 0;
        }
    };
}
