#include "_kvsymdb.inl.hpp"

using namespace cxx_kvsymdb;

#define RETURN_FAILED_STAT_WITH_ERRNO(out_errno_p, errno)   \
    do {                                                    \
        *(out_errno_p) = (errno);                           \
        return KVSYMDB_FAILED;                              \
    } while(0)                                              \


extern "C" kvsymdb_t *
create_kvsymdb(uint32_t entc, uint32_t bufsize, int *out_errno_p) {
    if (!out_errno_p) return nullptr;
    *out_errno_p = _intrnl::error_code::NOERROR;

    if (entc < kvsymdb::INIT_ENTC)
        entc = kvsymdb::INIT_ENTC;

    if (bufsize < kvsymdb::INIT_BUFSIZE)
        bufsize = kvsymdb::INIT_BUFSIZE;

    kvsymdb_t *_symdb_p =
        static_cast<kvsymdb_t*>(
            operator new(sizeof(kvsymdb_t) + bufsize, std::nothrow)
        );

    if (!_symdb_p) {
        *out_errno_p = _intrnl::error_code::ERR_OPNEW;
        dbg_log_msg("");
        dbg_print(
            "create_kvsymdb failed: %s",
            _intrnl::strerror(*out_errno_p)
        );
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
        *out_errno_p = _intrnl::error_code::ERR_OPNEWARR;
        dbg_log_msg("");
        dbg_print(
            "create_kvsymdb failed: %s",
            _intrnl::strerror(*out_errno_p)
        );
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
    *out_errno_p = _intrnl::error_code::NOERROR;

    if (!symdb_p || !out_view_p)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _intrnl::error_code::ERR_NULLPTR
        );

    _intrnl::assert_intrnl_state(symdb_p);
    
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
    *out_errno_p = _intrnl::error_code::NOERROR;

    if (!symdb_p)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _intrnl::error_code::ERR_NULLPTR
        );

    _intrnl::assert_intrnl_state(symdb_p);

    // explicit check required for already enough cap
    if (symdb_p->_entrycap >= new_entc) return KVSYMDB_SUCCESS;

    int rc = _intrnl::reserve(symdb_p, new_entc, out_errno_p);
    if (rc < 0)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _intrnl::error_code::ERR_RESIZE
        );

    return KVSYMDB_SUCCESS;
}

extern "C" int kvsymdb_reserve_arenabuf(
    kvsymdb_t **symdb_pp,
    uint32_t    new_bufsize,
    int        *out_errno_p
) {
    if (!out_errno_p) return KVSYMDB_FAILED;
    *out_errno_p = _intrnl::error_code::NOERROR;

    if (!symdb_pp || !*symdb_pp)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _intrnl::error_code::ERR_NULLPTR
        );

    _intrnl::assert_intrnl_state(*symdb_pp);

    // no need to resize if its already enough
    if ((*symdb_pp)->_buf_size >= new_bufsize) return KVSYMDB_SUCCESS;
    int rc = _intrnl::\
        reserve_arenabuf(symdb_pp, new_bufsize, out_errno_p);

    if (rc < 0)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _intrnl::error_code::ERR_RESIZEBUF
        );

    return KVSYMDB_SUCCESS;
}

extern "C" int kvsymdb_compact(
    kvsymdb_t **symdb_pp,
    int        *out_errno_p
) {
    using iterator      = kvsymdb_iterator_t;
    using entry_view    = kvsymdb::entry_view;
    using buffer_view   = kvsymdb::buffer_view;

    if (!out_errno_p) return KVSYMDB_FAILED;
    *out_errno_p = _intrnl::error_code::NOERROR;

    if (!symdb_pp || !*symdb_pp)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _intrnl::error_code::ERR_NULLPTR
        );

    _intrnl::assert_intrnl_state(*symdb_pp);

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
            _intrnl::error_code::ERR_CREATE
        );

    entry_view entview{};
    buffer_view key{}, val{};

    iterator it   = reinterpret_cast<iterator>(old_symdb_p->_arena_buf);
    iterator end  = reinterpret_cast<iterator>(old_symdb_p->_arena_buf + old_symdb_p->_buf_len);
    while (it != end) {
        if (
            old_symdb_p->_state_arr[it->id] == _intrnl::state::ST_DEAD
        ) {
            it = reinterpret_cast<iterator>(
                reinterpret_cast<uint8_t*>(it) + it->record_len
            );
            continue;
        }
        _intrnl::get_entry_view(
            it,
            &entview
        );

        key.set(key.length(), key.data());
        val.set(val.length(), val.data());

        int rc = kvsymdb_insert(
            &new_symdb_p,
            &key._base(),
            &val._base(),
            entview.hash,
            entview.type,
            out_errno_p
        );
        
        if (rc) {
            *out_errno_p = _intrnl::error_code::ERR_INSERT;
            dbg_log_msg("");
            dbg_print(
                "create_kvsymdb failed: %s",
                _intrnl::strerror(*out_errno_p)
            );
            destroy_kvsymdb(new_symdb_p);
            return KVSYMDB_FAILED;
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
    *out_errno_p = _intrnl::error_code::NOERROR;

    if (!symdb_pp || !*symdb_pp || !key_p || !val_p)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _intrnl::error_code::ERR_NULLPTR
        );

    if (
        !key_p->buf_data || !key_p->buf_size ||
        !val_p->buf_data || !val_p->buf_size
    )
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _intrnl::error_code::ERR_BUFVIEW
        );

    kvsymdb_t *symdb_p = *symdb_pp;
    if (symdb_p->_entrycnt == symdb_p->_entrycap)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _intrnl::error_code::ERR_FULL
        );

    _intrnl::assert_intrnl_state(symdb_p);

    uint32_t entsize = _intrnl::align::\
        entsize_required<kvsymdb::ALIGN_SIZE>(key_p, val_p);

    bool is_aligned = align_utils::\
        is_aligned_off<uint32_t, kvsymdb::ALIGN_SIZE>(entsize);
    assert(is_aligned);

    if (symdb_p->_buf_len + entsize > symdb_p->_buf_size) {
        uint32_t new_bufsize = (entsize > symdb_p->_buf_size)
            ? entsize * 2 : symdb_p->_buf_size * 2;
        dbg_print(
            "symdb_p->_buf_size: %u; new_bufsize: %u",
            symdb_p->_buf_size, new_bufsize
        );

        int rc = _intrnl::\
            reserve_arenabuf(&symdb_p, new_bufsize, out_errno_p);
        if (rc < 0)
            RETURN_FAILED_STAT_WITH_ERRNO(
                out_errno_p,
                _intrnl::error_code::ERR_RESIZEBUF
            );

        *symdb_pp = symdb_p;
        assert(symdb_p->_buf_size == new_bufsize);
    }

    uint32_t ent_off = symdb_p->_buf_len;

    is_aligned = align_utils::\
        is_aligned_off<uint32_t, kvsymdb::ALIGN_SIZE>(ent_off);
    assert(is_aligned);

    kvsymdb_record_header_t *ent_header_p = 
        reinterpret_cast<kvsymdb_record_header_t*>(symdb_p->_arena_buf + ent_off);

    ent_header_p->id            = symdb_p->_entrycnt;
    ent_header_p->hash          = key_hash;
    ent_header_p->type          = type;
    ent_header_p->name_len      = key_p->buf_size;
    ent_header_p->data_len      = val_p->buf_size;
    ent_header_p->record_len    = entsize;
    
    
    uint32_t key_size_aligned = _intrnl::align::\
        cstr_size_aligned<kvsymdb::ALIGN_SIZE>(key_p->buf_size);

    uint8_t *dest_p = ent_header_p->payload;
    memcpy(dest_p, key_p->buf_data, key_p->buf_size);
    dest_p[key_p->buf_size] = '\0'; // zero termination

    uint32_t data_size_aligned =
        _intrnl::align::blob_size_aligned<kvsymdb::ALIGN_SIZE>(val_p->buf_size);
    dest_p = ent_header_p->payload + key_size_aligned;
    memcpy(dest_p, val_p->buf_data, val_p->buf_size);
    symdb_p->_buf_len += entsize;

    assert(
        entsize ==
            sizeof(kvsymdb_record_header_t) +
            key_size_aligned + data_size_aligned
    );
    assert(symdb_p->_buf_len == ent_off + entsize);

    symdb_p->_state_arr[symdb_p->_entrycnt] = _intrnl::state::ST_ALIVE;

    ++symdb_p->_entrycnt;
    return KVSYMDB_SUCCESS;
}


extern "C" int kvsymdb_mark_dead(
    kvsymdb_t          *symdb_p,
    kvsymdb_entry_t    *ent_p,
    int                *out_errno_p
) {
    if (!out_errno_p) return KVSYMDB_FAILED;
    *out_errno_p = _intrnl::error_code::NOERROR;

    if (!symdb_p || !ent_p)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _intrnl::error_code::ERR_NULLPTR
        );

    if (!_intrnl::is_valid_entry(symdb_p, ent_p))
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _intrnl::error_code::ERR_BADENT
        );

    _intrnl::assert_intrnl_state(symdb_p);

    symdb_p->_state_arr[ent_p->id] = _intrnl::state::ST_DEAD;

    return KVSYMDB_SUCCESS;
}


extern "C" int kvsymdb_get_entview(
    const kvsymdb_t        *symdb_p,
    const kvsymdb_entry_t  *ent_p,
    kvsymdb_entview_t      *out_entview_p,
    int                    *out_errno_p
) {
    if (!out_errno_p) return KVSYMDB_FAILED;
    *out_errno_p = _intrnl::error_code::NOERROR;

    if (!symdb_p || !ent_p || !out_entview_p)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _intrnl::error_code::ERR_NULLPTR
        );

    if (!_intrnl::is_valid_entry(symdb_p, ent_p))
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _intrnl::error_code::ERR_BADENT
        );

    _intrnl::assert_intrnl_state(symdb_p);

    _intrnl::get_entry_view(ent_p, out_entview_p);

    return KVSYMDB_SUCCESS;
}

extern "C" int kvsymdb_entry_to_view(
    const kvsymdb_bufview_t    *arena_view_p,
    uint32_t                    entc,
    const kvsymdb_entry_t      *ent_p,
    kvsymdb_entview_t          *out_entview_p,
    int                        *out_errno_p
) {
    if (!out_errno_p) return KVSYMDB_FAILED;
    *out_errno_p = _intrnl::error_code::NOERROR;

    if (!ent_p || !out_entview_p)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _intrnl::error_code::ERR_NULLPTR
        );

    if (!_intrnl::is_valid_entry(arena_view_p, entc, ent_p))
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _intrnl::error_code::ERR_BADENT
        );

    _intrnl::get_entry_view(ent_p, out_entview_p);

    return KVSYMDB_SUCCESS;
}


extern "C" bool kvsymdb_is_valid_entry(
    const kvsymdb_t        *symdb_p,
    const kvsymdb_entry_t  *ent_p
) {
    if (
        !symdb_p || !ent_p ||
        !_intrnl::is_valid_entry(symdb_p, ent_p)
    ) return false;
    _intrnl::assert_intrnl_state(symdb_p);
    return (symdb_p->_state_arr[ent_p->id] == _intrnl::state::ST_ALIVE);
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
    *out_errno_p = _intrnl::error_code::NOERROR;

    if (!symdb_p || !dest_buf) {
        *out_errno_p = _intrnl::error_code::ERR_NULLPTR;
        return 0u;
    }

    if (!_intrnl::is_valid_entoff(symdb_p, off)) {
        *out_errno_p = _intrnl::error_code::ERR_BADOFF;
        return 0u;
    }

    _intrnl::assert_intrnl_state(symdb_p);

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
    return (!symdb_p) ? nullptr : 
        reinterpret_cast<kvsymdb_entry_t*>(symdb_p->_arena_buf);
}

extern "C"
kvsymdb_iterator_t kvsymdb_iterator_end(
    kvsymdb_t  *symdb_p
) {
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

// reader methods:
// reader.open() # reader(), init
// reader.read()
// reader.rewind()
// reader.seek()
// reader.close() # reader.~reader(), cleanup

// init
extern "C"
int kvsymdb_reader_bind(
    kvsymdb_reader_t           *reader_p,
    const kvsymdb_bufview_t    *arena_view_p,
    uint32_t                    entry_count,
    int                        *out_errno_p
) {
    // kvsymdb_t verification required
    if (!out_errno_p) return KVSYMDB_FAILED;
    *out_errno_p = _intrnl::error_code::NOERROR;
    if (!reader_p || !arena_view_p || !arena_view_p->buf_data)
        RETURN_FAILED_STAT_WITH_ERRNO(
            out_errno_p,
            _intrnl::error_code::ERR_NULLPTR
        );

    reader_p->_arena_view.buf_data  = arena_view_p->buf_data;
    reader_p->_arena_view.buf_size  = arena_view_p->buf_size;
    reader_p->_entc = entry_count; 
    reader_p->_pos  = 0u;


    return KVSYMDB_SUCCESS;
}

const kvsymdb_entry_t *kvsymdb_reader_read(
    kvsymdb_reader_t   *reader_p,
    int                *out_errno_p
) {
    // kvsymdb_t verification required
    if (!out_errno_p) return nullptr;
    *out_errno_p = _intrnl::error_code::NOERROR;

    const kvsymdb_entry_t *ent_p = nullptr;
    uint32_t min_reclen{};

    if (!reader_p || !reader_p->_arena_view.buf_data) {
        *out_errno_p = _intrnl::error_code::ERR_NULLPTR;
        goto failed;
    }

    // check alignment
    if (
        reader_p->_pos &&
        !align_utils::align_off<uint32_t, kvsymdb::ALIGN_SIZE>(reader_p->_pos)
    ) {
        *out_errno_p = _intrnl::error_code::ERR_BADALIGN;
        goto failed;
    }

    // checking is _pos + min_reclen <= than buf_len so 
    // the rec header is safe for deref (not guaranteed for payload yet)
    min_reclen = sizeof(kvsymdb_record_header_t);
    if (reader_p->_pos + min_reclen > reader_p->_arena_view.buf_size) {
        reader_p->_pos = reader_p->_arena_view.buf_size; // EOF
        *out_errno_p = _intrnl::error_code::ERR_RDEOF;
        goto failed;
    }

    ent_p = reinterpret_cast<const kvsymdb_entry_t*>(
        static_cast<const uint8_t*>(reader_p->_arena_view.buf_data) + reader_p->_pos
    );

    if (
        !_intrnl::is_valid_entry(
            &reader_p->_arena_view,
            reader_p->_entc,
            ent_p
        )
    ) {
        // It is usually bc _pos + sizeof(header) >= buf_len
        // so (Header*)((byte*)base + _pos) is safe to deref
        // but _pos + header.rec_len_aligned > buf_len
        // i.e. The payload is out of bound / truncated
        // to be safe we set _pos to _buf_len (guaranteed EOF)

        reader_p->_pos = reader_p->_arena_view.buf_size;
        *out_errno_p = _intrnl::error_code::ERR_BADENT;
        goto failed;
    }
   
    reader_p->_pos += ent_p->record_len;    
    assert(reader_p->_pos <= reader_p->_arena_view.buf_size);

    return ent_p;
failed:
    return nullptr;
}

int kvsymdb_reader_rewind(
    kvsymdb_reader_t   *reader_p
) {
    if (!reader_p) return KVSYMDB_FAILED;
    reader_p->_pos = 0u;
    return KVSYMDB_SUCCESS;
}

// I will rethink about seek impl because it might not worth it
// due to safety concerns

// cleanup
extern "C"
void kvsymdb_reader_unbind(
    kvsymdb_reader_t   *reader_p
) {
    if (reader_p) {
        reader_p->_arena_view.buf_data = nullptr;
        reader_p->_arena_view.buf_size = 0UL;
        reader_p->_entc = 0u;
        reader_p->_pos  = 0u;
    }
}


extern "C"
const char *kvsymdb_strerror(int kvsymdb_errno) {
    return _intrnl::strerror(kvsymdb_errno);
}


int kvsymdb::hash_index::init(
    const kvsymdb  &symdb_ref,
    uint32_t        capacity
) {
    return this->m_base.init(
        symdb_ref.m_symdb_p,
        capacity,
        capacity
    );
}

int kvsymdb::hash_index::_init(
    const kvsymdb_t    *symdb_p,
    uint32_t            capacity
) {
    return this->m_base.init(
        symdb_p,
        capacity,
        capacity
    );
}

int kvsymdb::hash_index::insert(const entry *ent_p) {
    return this->m_base.insert(ent_p);
}

const kvsymdb::entry *kvsymdb::hash_index::get(
    const string_view  &key_ref
) {
    lookup_result res{};
    int rc = this->m_base.lookup(
        key_ref,
        key_ref.hash32(),
        &res
    );
    return (rc == KVSYMDB_FAILED) ? nullptr : res.slot_p->ent_p;
}



int kvsymdb::hash_index::remove(const entry *ent_p) {
    if (!_intrnl::is_valid_entry(this->m_base.m_c_symdb_p, ent_p)) {
        this->m_base.m_errno = _intrnl::error_code::ERR_BADENT;
        return KVSYMDB_FAILED;
    }

    entry_view ent_view{};
    _intrnl::get_entry_view(ent_p, &ent_view);

    string_view key_view(ent_view.name_len, ent_view.name);
    lookup_result lu_res{};
    int rc = this->m_base.lookup(key_view, key_view.hash32(), &lu_res);
    if (rc == KVSYMDB_FAILED) return rc;

    return this->m_base.remove(&lu_res);
}


// opaque c abi type; typedef struct _c_kvsymdb_hidx kvsymdb_hash_index_t;
struct _c_kvsymdb_hidx {
    kvsymdb::hash_index _kvsymdb_hidx;
};

extern "C" kvsymdb_hash_index_t *
create_kvsymdb_hash_index(
    const kvsymdb_t    *symdb_p,
    int                *out_errno_p
) {
    if (!out_errno_p) return nullptr;
    *out_errno_p = _intrnl::error_code::NOERROR;

    if (!symdb_p) {
        *out_errno_p = _intrnl::error_code::ERR_NULLPTR;
        return nullptr;
    }

    _intrnl::assert_intrnl_state(symdb_p);

    int rc = 0;
    auto *c_hidx_p = new (std::nothrow) _c_kvsymdb_hidx();
    if (!c_hidx_p) {
        *out_errno_p = _intrnl::error_code::ERR_OPNEW;
        goto failed_ret;
    }

    rc = c_hidx_p->_kvsymdb_hidx._init(
        symdb_p, symdb_p->_entrycap
    );
    if (rc) goto failed_cleanup;

    return c_hidx_p;
failed_cleanup:
    delete c_hidx_p;
failed_ret:
    return nullptr;
}

extern "C" void
destroy_kvsymdb_hash_index(
    kvsymdb_hash_index_t *c_hidx_p
) {
    if (!c_hidx_p) return;
    delete c_hidx_p; // dtor automatically called
}

extern "C" int
kvsymdb_hidx_insert(
    kvsymdb_hash_index_t   *c_hidx_p,
    const kvsymdb_entry_t  *ent_p,
    int                    *out_errno_p
) {
    if (!out_errno_p) return KVSYMDB_FAILED;
    if (!c_hidx_p || !ent_p) {
        *out_errno_p = _intrnl::error_code::ERR_NULLPTR;
        return KVSYMDB_FAILED;
    }

    int rc = c_hidx_p->_kvsymdb_hidx.insert(ent_p);
    *out_errno_p = c_hidx_p->_kvsymdb_hidx.geterror();

    return rc;
}

extern "C"
const kvsymdb_entry_t *
kvsymdb_hidx_lookup(
    kvsymdb_hash_index_t       *c_hidx_p,
    const kvsymdb_bufview_t    *key_p,
    int                        *out_errno_p
) {
    if (!out_errno_p) return nullptr;
    if (!c_hidx_p || !key_p) {
        *out_errno_p = _intrnl::error_code::ERR_NULLPTR;
        return nullptr;
    }

    using string_view = kvsymdb::kvsymdb::string_view;

    const kvsymdb_entry_t *ent_p = 
        c_hidx_p->_kvsymdb_hidx.get(string_view(*key_p));

    *out_errno_p = c_hidx_p->_kvsymdb_hidx.geterror();

    return ent_p;
}

extern "C"
int kvsymdb_hidx_remove(
    kvsymdb_hash_index_t   *c_hidx_p,
    const kvsymdb_entry_t  *ent_p,
    int                    *out_errno_p
) {
    if (!out_errno_p) return KVSYMDB_FAILED;
    if (!c_hidx_p || !ent_p) {
        *out_errno_p = _intrnl::error_code::ERR_NULLPTR;
        return KVSYMDB_FAILED;
    }

    int rc = c_hidx_p->_kvsymdb_hidx.remove(ent_p);
    *out_errno_p = c_hidx_p->_kvsymdb_hidx.geterror();

    return rc;
}

extern "C" {
#include <stdio.h>
#include <zlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
}


struct CXX_FILE {
private:
    FILE   *m_fp;
public:
    inline CXX_FILE() noexcept : m_fp(nullptr) {
    }
    inline CXX_FILE(FILE *fp) noexcept : m_fp(fp) {
    }

    CXX_FILE(const CXX_FILE &other_ref) = delete;
    CXX_FILE &operator=(const CXX_FILE &other_ref) = delete;

    inline ~CXX_FILE() noexcept {
        if (this->m_fp) {
            fclose(this->m_fp);
            this->m_fp = nullptr;
        }
    }

    inline bool is_open() noexcept {
        return (!!this->m_fp);
    }

    inline FILE *get() noexcept {
        return this->m_fp;
    }

    inline int fileno() noexcept {
        fflush(this->get());
        return ::fileno(this->get());
    }

    inline CXX_FILE &operator=(FILE *fp) noexcept {
        if (this->is_open()) this->~CXX_FILE();
        return *::new (this) CXX_FILE(fp);
    }
};

constexpr int INVALID_FILENO = -1;
struct FileDescHandle {
private:
    int m_fileno;

public:
    inline FileDescHandle() noexcept : m_fileno(INVALID_FILENO) {
    }
    inline FileDescHandle(int fd) noexcept : m_fileno(fd) {
    }
    FileDescHandle(const FileDescHandle &other_ref) = delete;
    FileDescHandle &operator=(const FileDescHandle &other_ref) = delete;

    inline bool is_valid() const noexcept {
        return (this->m_fileno != INVALID_FILENO);
    }

    inline int get() noexcept {
        return this->m_fileno;
    }

    inline void move_out(int *out_fd_p) noexcept {
        *out_fd_p       = this->m_fileno;
        this->m_fileno  = INVALID_FILENO;
    }

    inline ~FileDescHandle() noexcept {
        if (this->m_fileno != INVALID_FILENO) {
            close(this->m_fileno);
            this->m_fileno = INVALID_FILENO;
        }
    }

    FileDescHandle &operator=(int fd) noexcept {
        if (this->is_valid()) this->~FileDescHandle();
        return *::new (this) FileDescHandle(fd);
    }
};

struct MMapHandle {
private:
    void   *m_base;
    size_t  m_size;
public: 
    inline MMapHandle(void *buf_base, size_t buf_size) noexcept :
        m_base(buf_base), m_size(buf_size)
    {
    }

    MMapHandle(const MMapHandle &other_ref) = delete;
    MMapHandle &operator=(const MMapHandle &other_ref) = delete;

    inline bool is_good() {
        return (this->m_base != MAP_FAILED && this->m_base);
    }

    inline size_t length() const noexcept {
        return this->m_size;
    }

    inline void *data() const noexcept {
        return this->m_base;
    }

    inline void move_out(void **out_buf_p, size_t *out_size_p) noexcept {
        *out_buf_p      = this->m_base;
        *out_size_p     = this->m_size; 
        this->m_base    = nullptr;
        this->m_size    = 0UL;
    }

    inline ~MMapHandle() noexcept {
        if (this->m_base != MAP_FAILED && this->m_base) {
            ::munmap(this->m_base, this->m_size);
            this->m_base = nullptr;
            this->m_size = 0UL;
        }
    }
};

extern "C"
int kvsymdb_file_builder_dump(
    const kvsymdb_t    *symdb_p,
    const char         *filename,
    int                *out_errno_p
) {
    if (!out_errno_p) return KVSYMDB_FAILED;
    *out_errno_p = _intrnl::error_code::NOERROR;

    if (!filename) {
        *out_errno_p = _intrnl::error_code::ERR_NULLPTR;
        return KVSYMDB_FAILED;
    }

    CXX_FILE db_file(fopen(filename, "wb"));
    if (!db_file.is_open()) {
        dbg_log_msg("");
        dbg_print("fopen failed: %s", strerror(errno));
        *out_errno_p = _intrnl::error_code::ERR_FOPEN;
        return KVSYMDB_FAILED;
    }

    kvsymdb::self_state dbst{};
    int rc = kvsymdb_get_intrnl_state_view(symdb_p, &dbst, out_errno_p);
    if (rc) {
        dbg_log_msg("");
        dbg_print(
            "kvsymdb_get_intrnl_state_view failed: %s",
            _intrnl::strerror(*out_errno_p)
        );
        *out_errno_p = _intrnl::error_code::ERR_DBST;
        return KVSYMDB_FAILED;
    }

    fseek(db_file.get(), sizeof(kvsymdb::file_header), SEEK_SET);
    if (
        fwrite(
            dbst.arena_buf,
            sizeof(uint8_t),
            dbst.buf_len,
            db_file.get()
        ) != dbst.buf_len
    ) {
        dbg_log_msg("fwrite: failed to write payload");
        *out_errno_p = _intrnl::error_code::ERR_IOFWRITE;
        return KVSYMDB_FAILED;
    }

    auto calc_crc32 = [](const void *buf, uint32_t buf_len) {
        uint32_t crc = static_cast<uint32_t>(::crc32(0UL, Z_NULL, 0u));
        return static_cast<uint32_t>(
            ::crc32(crc, static_cast<const uint8_t*>(buf), buf_len)
        );
    };

    kvsymdb::file_header fhdr = {
        .fh_magic   = _intrnl::FILE_MAGIC,
        .fh_version = _intrnl::FILE_VERSION,
        .fh_align   = _intrnl::FILE_ALIGN,
        .fh_entcnt  = dbst.ent_count,
        .fh_buflen  = dbst.buf_len,
        .fh_crc32   = calc_crc32(dbst.arena_buf, dbst.buf_len),
    };

    fseek(db_file.get(), 0L, SEEK_SET);
    if (
        fwrite(
            &fhdr,
            sizeof(uint8_t),
            sizeof(kvsymdb::file_header),
            db_file.get()
        ) != sizeof(kvsymdb::file_header)
    ) {
        dbg_log_msg("fwrite: failed to write header");
        *out_errno_p = _intrnl::error_code::ERR_IOFWRITE;
        return KVSYMDB_FAILED;
    }

    return KVSYMDB_SUCCESS;
}


/*
int kvsymdb::file_builder::dump(const char *filename) {
    CXX_FILE db_file(fopen(filename, "wb"));

    if (!db_file.get()) {
        this->m_errno = _intrnl::error_code::ERR_FOPEN;
        return KVSYMDB_FAILED;
    }

    self_state dbst{};
    int rc = this->m_parent_symdb_p->get_self_state(&dbst);
    if (rc) {
        dbg_log_msg("");
        dbg_print(
            "this->m_parent_symdb_p->get_self_state failed: %s",
            this->m_parent_symdb_p->errmsg()
        );
        this->m_parent_symdb_p->clearerr();
        this->m_errno = _intrnl::error_code::ERR_DBST;
        return KVSYMDB_FAILED;
    }

    fseek(db_file.get(), sizeof(file_header), SEEK_SET);
    if (
        fwrite(
            dbst.arena_buf,
            sizeof(uint8_t),
            dbst.buf_len,
            db_file.get()
        ) != dbst.buf_len
    ) {
        dbg_log_msg("failed to write payload");
        this->m_errno = _intrnl::error_code::ERR_IOFWRITE;
        return KVSYMDB_FAILED;
    }

    auto calc_crc32 = [](const void *buf, uint32_t buf_len) {
        uint32_t crc = static_cast<uint32_t>(::crc32(0UL, Z_NULL, 0u));
        return static_cast<uint32_t>(
            ::crc32(crc, static_cast<const uint8_t*>(buf), buf_len)
        );
    };

    file_header fhdr = {
        .fh_magic   = _intrnl::FILE_MAGIC,
        .fh_version = _intrnl::FILE_VERSION,
        .fh_align   = _intrnl::FILE_ALIGN,
        .fh_entcnt  = dbst.ent_count,
        .fh_buflen  = dbst.buf_len,
        .fh_crc32   = calc_crc32(dbst.arena_buf, dbst.buf_len),
    };

    fseek(db_file.get(), 0L, SEEK_SET);
    if (
        fwrite(
            &fhdr,
            sizeof(uint8_t),
            sizeof(file_header),
            db_file.get()
        ) != sizeof(file_header)
    ) {
        dbg_log_msg("failed to write header");
        this->m_errno = _intrnl::error_code::ERR_IOFWRITE;
        return KVSYMDB_FAILED;
    }

    return KVSYMDB_SUCCESS;
}
*/

extern "C"
kvsymdb_file_reader_t *
kvsymdb_file_reader_init(
    void       *_obj_mem,
    const char *filename,
    int        *out_errno_p
) {
    if (!out_errno_p) return nullptr;
    *out_errno_p = _intrnl::error_code::NOERROR;

    if (!_obj_mem || !filename) {
        *out_errno_p = _intrnl::error_code::ERR_NULLPTR;
        return nullptr;
    }

    auto *reader_p = static_cast<kvsymdb_file_reader_t*>(_obj_mem);

    CXX_FILE dbif(fopen(filename, "rb"));
    if (!dbif.is_open()) {
        dbg_log_msg("");
        fprintf(stderr, "fopen failed: %s\n", ::strerror(errno));
        *out_errno_p = _intrnl::error_code::ERR_FOPEN;
        return nullptr;
    }

    FileDescHandle fd(dup(dbif.fileno()));
    if (!fd.is_valid()) {
        dbg_log_msg("");
        fprintf(stderr, "dup failed: %s\n", ::strerror(errno));
        *out_errno_p = _intrnl::error_code::ERR_DUP;
        return nullptr;
    }

    struct ::stat st{};
    if (::fstat(fd.get(), &st) < 0) {
        dbg_log_msg("");
        fprintf(stderr, "fstat failed: %s\n", ::strerror(errno));
        *out_errno_p = _intrnl::error_code::ERR_FSTAT;
        return nullptr;
    }

    size_t filesize = st.st_size;

    MMapHandle dbif_map(
        mmap(
            nullptr,
            filesize,
            PROT_READ,
            MAP_SHARED | MAP_FILE,
            fd.get(),
            0L
        ),
        filesize
    );

    if (!dbif_map.is_good()) {
        dbg_log_msg("");
        fprintf(stderr, "mmap failed: %s\n", ::strerror(errno));
        *out_errno_p = _intrnl::error_code::ERR_MMAP;
        return nullptr;
    }

    auto *hdr_p =
        static_cast<const kvsymdb::file_header*>(dbif_map.data());

    if (!_intrnl::is_valid_file_header(hdr_p, filesize)) {
        *out_errno_p = _intrnl::error_code::ERR_BADFHDR;
        return nullptr;
    }

    auto arena_buf = reinterpret_cast<const uint8_t*>(hdr_p + 1);

    auto calc_crc32 = [](const void *buf, uint32_t buf_len) -> uint32_t {
        return static_cast<uint32_t>(
            ::crc32(0UL, static_cast<const uint8_t*>(buf), buf_len)
        );
    };

    if (calc_crc32(arena_buf, hdr_p->fh_buflen) != hdr_p->fh_crc32) {
        *out_errno_p = _intrnl::error_code::ERR_CRC;
        return nullptr;
    }

    // ownership transfer out of RAII for c api
    fd.move_out(&reader_p->_fileno);
    dbif_map.move_out(&reader_p->_base, &reader_p->_size);    

    return reader_p;
}

extern "C"
void kvsymdb_file_reader_cleanup(
    kvsymdb_file_reader_t  *reader_p
) {
    if (!reader_p) return;

    // using RAII for cleanup
    (void)FileDescHandle(reader_p->_fileno);
    (void)MMapHandle(reader_p->_base, reader_p->_size);

    reader_p->_fileno   = INVALID_FILENO;
    reader_p->_base     = nullptr;
    reader_p->_size     = 0UL;
}
