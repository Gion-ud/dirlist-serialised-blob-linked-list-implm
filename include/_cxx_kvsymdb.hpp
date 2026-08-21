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
    using self_state    = kvsymdb_state_t;

    struct entry_view;

    struct buffer_view : private kvsymdb_bufview_t {
        buffer_view() noexcept : kvsymdb_bufview_t{} {
        }
        buffer_view(size_t len, const void *data) noexcept :
            kvsymdb_bufview_t{len, data}
        {
        }
        buffer_view(const kvsymdb_bufview_t &view_ref) noexcept {
            ::new (this) buffer_view(view_ref.buf_size, view_ref.buf_data);
        }
        void set(size_t len, const void *data) noexcept {
            ::new (this) buffer_view(len, data);
        }
        size_t length() const noexcept {
            return this->buf_size;
        }
        const void *data() const noexcept {
            return this->buf_data;
        }
        const kvsymdb_bufview_t &_base() const noexcept {
            return *this;
        }
    };

    struct string_view : public buffer_view {
        string_view() noexcept : buffer_view{} {
        }
        string_view(size_t len, const char *data) noexcept :
            buffer_view(len, data)
        {
        }
        string_view(const kvsymdb_bufview_t &view_ref) noexcept :
            buffer_view(view_ref.buf_size, view_ref.buf_data)
        {
        }
        string_view(const char *cstr) noexcept :
            buffer_view(
                (cstr) ? strlen(cstr) : 0UL,
                cstr
            )
        {
        }
        void set(size_t len, const char *data) noexcept {
            ::new (this) string_view(len, data);
        }
        void set(const char *cstr) noexcept {
            ::new (this) string_view(cstr);
        }
        size_t length() const noexcept {
            return this->buffer_view::length();
        }
        const char *data() const noexcept {
            return static_cast<const char*>(this->buffer_view::data());
        }        
        uint32_t hash32() const noexcept {
            return fnv_1a_hash32(this->data(), this->length());
        }
        string_view &operator=(const char *cstr) noexcept {
            this->set(cstr);
            return *this;
        }
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
        kvsymdb_clearerr(this->m_symdb_p);
    }
    const char *errmsg() const noexcept {
        return kvsymdb_errmsg(this->m_symdb_p);
    }

    int reserve(uint32_t entc) noexcept {
        return kvsymdb_reserve(this->m_symdb_p, entc);
    }
    int reserve_buffer(uint32_t new_bufsize) noexcept {
        return kvsymdb_reserve_arenabuf(&this->m_symdb_p, new_bufsize);
    }
    int get_intrnl_state(self_state *out_view_p) noexcept {
        return kvsymdb_get_intrnl_state(this->m_symdb_p, out_view_p);
    }
    int insert(
        const string_view  &key_ref,
        const buffer_view  &val_ref,
        uint16_t            type
    ) noexcept {
        return kvsymdb_insert(
            &this->m_symdb_p,
            &key_ref._base(),
            &val_ref._base(),
            key_ref.hash32(),
            type
        );
    }
    int insert(const entry_view &entview_ref) noexcept {
        return this->insert(
            string_view(entview_ref.name_len, entview_ref.name),
            buffer_view(entview_ref.data_len, entview_ref.data),
            entview_ref.type
        );
    }
    int mark_dead(entry *ent_p) noexcept {
        return kvsymdb_mark_dead(this->m_symdb_p, ent_p);
    }
    int get_entry_view(
        const entry    *ent_p,
        entry_view     *out_entview_p
    ) const noexcept {
        return kvsymdb_get_entview(
            this->m_symdb_p,
            ent_p,
            out_entview_p
        );
    }
    bool is_valid_entry(const entry *ent_p) const noexcept {
        return kvsymdb_is_valid_entry(this->m_symdb_p, ent_p);
    }
    kvsymdb_t *_raw() const noexcept {
        return this->m_symdb_p;
    };
    int compact() noexcept {
        return kvsymdb_compact(&this->m_symdb_p);
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
            return kvsymdb_iter_next(
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
            kvsymdb_iter_begin(this->m_symdb_p)
        );
    }
    iterator end() noexcept {
        return iterator(
            this,
            kvsymdb_iter_end(this->m_symdb_p)
        );
    }

    struct entry_view : public kvsymdb_entview_t {
    private:
        const kvsymdb  *m_parent_symdb_p;

    public:
        entry_view() noexcept :
            kvsymdb_entview_t{},
            m_parent_symdb_p(nullptr)
        {
        }
        entry_view(
            const kvsymdb  &symdb_ref,
            const entry    *ent_p
        ) noexcept :
            kvsymdb_entview_t{},
            m_parent_symdb_p(&symdb_ref)
        {
            this->from_entry(symdb_ref, ent_p);
        }

        int from_entry(
            const kvsymdb  &symdb_ref,
            const entry    *ent_p
        ) noexcept {
            int rc = symdb_ref.get_entry_view(ent_p, this);
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

        //int dump(const char *filename) noexcept;

        int dump(const char *filename) const noexcept {
            return kvsymdb_file_builder_dump(
                this->m_parent_symdb_p->_raw(),
                filename
            );
        }

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

    struct file_mapper;
}; // struct kvsymdb



namespace _intrnl {
struct hash_index;
} // namespace _intrnl

#include <assert.h>

struct kvsymdb::file_mapper : private kvsymdb_file_mapper_t {
private:
    int         m_errno;
    buffer_view m_bufview;
public:
    file_mapper(const char *filename) noexcept :
        kvsymdb_file_mapper_t{},
        m_errno(0),
        m_bufview{}
    {
        int rc = kvsymdb_file_mapper_init(this, filename);
        if (rc != KVSYMDB_OK) return;

        this->m_bufview.set(
            this->kvsymdb_file_mapper_t::_size,
            this->kvsymdb_file_mapper_t::_base
        );
    }

    bool is_init() const noexcept {
        return (!!this->_base);
    }
    const char *errmsg() const noexcept {
        return kvsymdb_strerror(this->m_errno);
    }
    void clearerr() noexcept {
        this->m_errno = 0;
    }

    const file_header *get_file_header() const noexcept {
        return static_cast<const file_header*>(this->_base);
    }

    const buffer_view &get_file_view() const noexcept {
        return this->m_bufview;
    }

    uint32_t entc() const noexcept {
        return this->get_file_header()->fh_entcnt;
    }

    int get_entry_view(
        const entry    *ent_p,
        entry_view     *out_view_p
    ) const noexcept {
        return kvsymdb_entry_to_view(
            &this->get_file_view()._base(),
            this->entc(),
            ent_p,
            out_view_p
        );
    };

    ~file_mapper() noexcept {
        kvsymdb_file_mapper_cleanup(this);
    }
};

struct kvsymdb::reader : private kvsymdb_reader_t { // inherited from c abi struct
private:
    int m_errno;

public:
    reader(
        const buffer_view  &arena_view_ref,
        uint32_t            entry_count
    ) noexcept :
        kvsymdb_reader_t{}, m_errno(0)
    {
        int rc = ::kvsymdb_reader_bind(
            this,
            &arena_view_ref._base(),
            entry_count
        );
        if (rc) {
            this->_arena_view.buf_data = nullptr;
        }
    }

    reader(const kvsymdb &symdb_ref) noexcept :
        kvsymdb_reader_t{}, m_errno(0)
    {
        kvsymdb::self_state dbst{};
        int rc = kvsymdb_get_intrnl_state(symdb_ref._raw(), &dbst);
        if (rc != KVSYMDB_OK) return;

        ::new (this) reader(
            kvsymdb::buffer_view(dbst.buf_len, dbst.arena_buf),
            dbst.ent_count
        );
    }

    reader(file_mapper &dbif_ref) noexcept :
        kvsymdb_reader_t{}, m_errno(0)
    {
        auto *fhdr_p = dbif_ref.get_file_header();
        ::new (this) reader(
            buffer_view(fhdr_p->fh_buflen, fhdr_p->fh_data),
            fhdr_p->fh_entcnt
        );
    }

    ~reader() noexcept {
        kvsymdb_reader_unbind(this);
    }

    reader(const reader &other_ref) = delete;
    reader& operator=(const reader &other_ref) = delete;

    void clearerr() noexcept {
        this->m_errno = 0;
    }
    const char *errmsg() const noexcept {
        return kvsymdb_strerror(this->kvsymdb_reader_t::_err_code);
    }

    bool is_init() const noexcept {
        return (!!this->_arena_view.buf_data);
    }
    const entry *read() noexcept {
        return kvsymdb_reader_read(this);
    }

    // lseek
    int rewind() noexcept {
        return kvsymdb_reader_rewind(this);
    }
};

} // namespace cxx_kvsymdb 

