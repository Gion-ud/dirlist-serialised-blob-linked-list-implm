#pragma once
#include <kvsymdb.h>


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
        void _kvsymdb_cleanup() noexcept {
            if (!this->_symdb_p) return;
            destroy_kvsymdb(this->_symdb_p);
            this->_symdb_p = nullptr;
        }
        ~kvsymdb() noexcept {
            this->_kvsymdb_cleanup();
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

        int reserve(uint32_t entc) noexcept {
            return kvsymdb_reserve(this->_symdb_p, entc, &this->_errno);
        }
        int reserve_buffer(uint32_t new_bufsize) noexcept {
            return kvsymdb_reserve_arenabuf(
                &this->_symdb_p, new_bufsize, &this->_errno
            );
        }
        int get_self_state_view(kvsymdb_view_t *out_view_p) noexcept {
            return kvsymdb_get_view(this->_symdb_p, out_view_p, &this->_errno);
        }
        int insert(
            const kvsymdb_bufview_t    *key_p,
            const kvsymdb_bufview_t    *val_p,
            uint32_t                    key_hash,
            uint16_t                    type
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
        int mark_dead(kvsymdb_entry_t *ent_p) noexcept {
            return kvsymdb_mark_dead(
                this->_symdb_p,
                ent_p,
                &this->_errno
            );
        }
        int get_entview(
            const kvsymdb_entry_t  *ent_p,
            kvsymdb_entview_t      *out_entview_p
        ) noexcept {
            return kvsymdb_get_entview(
                this->_symdb_p,
                ent_p,
                out_entview_p,
                &this->_errno
            );
        }
        bool is_valid_entry(const kvsymdb_entry_t *ent_p) noexcept {
            return kvsymdb_is_valid_entry(this->_symdb_p, ent_p);
        }
        
        using iterator = kvsymdb_entry_t *;

        iterator begin() noexcept {
            return iterator(
                kvsymdb_iterator_begin(this->_symdb_p)._ent_p
            );
        }
        iterator end() noexcept {
            return iterator(
                kvsymdb_iterator_end(this->_symdb_p)._ent_p
            );
        }
        iterator next(iterator &it_ref) noexcept {
            return iterator(
                kvsymdb_iterator_next(
                    this->_symdb_p,
                    (kvsymdb_iterator_t) { it_ref }
                )._ent_p
            );
        }
        const kvsymdb_entry_t &deref(iterator &it_ref) noexcept {
            return *it_ref;
        }

        kvsymdb(const kvsymdb&) = delete;
        kvsymdb& operator=(const kvsymdb&) = delete;
    };
}
