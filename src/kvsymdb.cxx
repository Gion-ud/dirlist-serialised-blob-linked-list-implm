#include "_kvsymdb.inl.hpp"

using namespace cxx_kvsymdb;
using ErrorCode = _intrnl::error_code::_ErrorCodeEnum;
extern "C" const int KVSYMDB_OK = ErrorCode::NOERROR;




extern "C" kvsymdb_t *
create_kvsymdb(uint32_t entc, uint32_t arena_size, int *out_errno_p) {
    if (!out_errno_p) return nullptr;
    *out_errno_p = ErrorCode::NOERROR;

    if (entc < kvsymdb::INIT_ENTC) entc = kvsymdb::INIT_ENTC;
    if (arena_size < kvsymdb::INIT_BUFSIZE)
        arena_size = kvsymdb::INIT_BUFSIZE;

    arena_size = _intrnl::align_u32(arena_size);

    size_t _db_buf_size = _intrnl::ckvsymdb::calc_buf_size(entc, arena_size);
    void *_db_buf = ::operator new(_db_buf_size, std::nothrow);

    if (!_db_buf) {
        *out_errno_p = ErrorCode::ERR_OPNEW;
        dbg_log_msg("");
        dbg_print("create_kvsymdb failed: %s", _intrnl::strerror(*out_errno_p));
        ::operator delete(_db_buf);
        return nullptr;
    }
    memset(_db_buf, 0, _db_buf_size);

    kvsymdb_t *c_dbp = static_cast<kvsymdb_t*>(_db_buf);

    c_dbp->_entrycnt     = 0u;
    c_dbp->_entrycap     = entc;
    c_dbp->_buf_len      = 0u;
    c_dbp->_buf_size     = arena_size;
    c_dbp->_err_code     = ErrorCode::NOERROR;
    c_dbp->_state_arr    =
        static_cast<uint8_t*>(_db_buf) + sizeof(kvsymdb_t) + arena_size;

    return c_dbp;
}

extern "C" void destroy_kvsymdb(kvsymdb_t *symdb_p) {
    if (symdb_p) ::operator delete(symdb_p);
}

extern "C" int kvsymdb_get_intrnl_state(
    const kvsymdb_t    *symdb_p,
    kvsymdb_state_t    *out_view_p
) {
    if (!symdb_p || !out_view_p) return ErrorCode::ERR_NULLPTR;

    _intrnl::assert_intrnl_state(symdb_p);    
    out_view_p->ent_count       = symdb_p->_entrycnt;
    out_view_p->ent_capacity    = symdb_p->_entrycap;
    out_view_p->buf_len         = symdb_p->_buf_len;
    out_view_p->buf_size        = symdb_p->_buf_size;
    out_view_p->_state_arr      = symdb_p->_state_arr;
    out_view_p->arena_buf       = symdb_p->_arena_buf;
    
    return KVSYMDB_OK;
}

extern "C" uint32_t kvsymdb_entcnt(const kvsymdb_t *symdb_p) {
    return (!symdb_p) ? 0u : symdb_p->_entrycnt;
}


extern "C" int kvsymdb_reserve(
    kvsymdb_t **symdb_pp,
    uint32_t    new_entc
) {
    if (!symdb_pp || !*symdb_pp) return ErrorCode::ERR_NULLPTR;
    (*symdb_pp)->_err_code = ErrorCode::NOERROR;
    _intrnl::assert_intrnl_state(*symdb_pp);

    // explicit check required for already enough cap
    if ((*symdb_pp)->_entrycap >= new_entc) return KVSYMDB_OK;

    int rc = _intrnl::reserve(symdb_pp, new_entc);
    if (rc != KVSYMDB_OK) {
        (*symdb_pp)->_err_code = ErrorCode::ERR_RESIZE;
        return (*symdb_pp)->_err_code;
    }

    return KVSYMDB_OK;
}

extern "C" int kvsymdb_reserve_arenabuf(
    kvsymdb_t **symdb_pp,
    uint32_t    new_bufsize
) {
    if (!symdb_pp || !*symdb_pp)
        return ErrorCode::ERR_NULLPTR;

    (*symdb_pp)->_err_code = ErrorCode::NOERROR;
    _intrnl::assert_intrnl_state(*symdb_pp);

    // no need to resize if its already enough
    if ((*symdb_pp)->_buf_size >= new_bufsize) return KVSYMDB_OK;
    int rc = _intrnl::reserve_arenabuf(symdb_pp, new_bufsize);

    if (rc != KVSYMDB_OK) {
        (*symdb_pp)->_err_code = ErrorCode::ERR_RESIZEBUF;
        return (*symdb_pp)->_err_code;
    }

    return KVSYMDB_OK;
}

extern "C" int kvsymdb_compact(kvsymdb_t **symdb_pp) {
    using iterator      = kvsymdb_iter_t;
    using entry_view    = kvsymdb::entry_view;
    using buffer_view   = kvsymdb::buffer_view;

    if (!symdb_pp || !*symdb_pp)
        return ErrorCode::ERR_NULLPTR;

    (*symdb_pp)->_err_code = ErrorCode::NOERROR;
    _intrnl::assert_intrnl_state(*symdb_pp);

    int ret_err = ErrorCode::NOERROR;
    kvsymdb_t *old_symdb_p  = *symdb_pp;
    kvsymdb_t *new_symdb_p  =
        create_kvsymdb(
            old_symdb_p->_entrycap,
            old_symdb_p->_buf_size,
            &ret_err
        );

    if (!new_symdb_p) {
        old_symdb_p->_err_code = ErrorCode::ERR_CREATE;
        return old_symdb_p->_err_code;
    }

    entry_view entview{};
    buffer_view key{}, val{};

    iterator it   = reinterpret_cast<iterator>(old_symdb_p->_arena_buf);
    iterator end  = reinterpret_cast<iterator>(old_symdb_p->_arena_buf + old_symdb_p->_buf_len);
    while (it != end) {
        assert(it->id < old_symdb_p->_entrycnt);
        if (
            old_symdb_p->_state_arr[it->id] == _intrnl::state::ST_DEAD
        ) {
            it = reinterpret_cast<iterator>(
                reinterpret_cast<uint8_t*>(it) + it->record_len
            );
            continue; // skip dead entry
        }
        _intrnl::get_entry_view(it, &entview);

        key.set(entview.name_len, entview.name);
        val.set(entview.data_len, entview.data);

        int rc = kvsymdb_insert(
            &new_symdb_p,
            &key._base(),
            &val._base(),
            entview.hash,
            entview.type
        );
        
        if (rc) {
            dbg_log_msg("");
            dbg_print(
                "kvsymdb_insert failed: %s",
                _intrnl::strerror(rc)
            );
            destroy_kvsymdb(new_symdb_p);
            old_symdb_p->_err_code = ErrorCode::ERR_INSERT;
            return old_symdb_p->_err_code;
        }

        it = reinterpret_cast<iterator>(
            reinterpret_cast<uint8_t*>(it) + it->record_len
        ); // next record
    }

    destroy_kvsymdb(old_symdb_p);
    *symdb_pp = new_symdb_p;

    return KVSYMDB_OK;
}

extern "C" int kvsymdb_insert(
    kvsymdb_t                 **symdb_pp,
    const kvsymdb_bufview_t    *key_p,
    const kvsymdb_bufview_t    *val_p,
    uint32_t                    key_hash,
    uint16_t                    type
) {
    if (!symdb_pp || !*symdb_pp || !key_p || !val_p)
        return ErrorCode::ERR_NULLPTR;

    (*symdb_pp)->_err_code = ErrorCode::NOERROR;

    if (
        !key_p->buf_data || !key_p->buf_size ||
        !val_p->buf_data || !val_p->buf_size
    ) {
        (*symdb_pp)->_err_code = ErrorCode::ERR_BUFVIEW;
        return (*symdb_pp)->_err_code;
    }

    kvsymdb_t *symdb_p = *symdb_pp;
    if (symdb_p->_entrycnt == symdb_p->_entrycap) {
        symdb_p->_err_code = ErrorCode::ERR_FULL;
        return symdb_p->_err_code;
    }
    _intrnl::assert_intrnl_state(symdb_p);
    uint32_t entsize = _intrnl::align::
        entsize_required<kvsymdb::ALIGN_SIZE>(key_p, val_p);

    bool is_aligned = 
        align_utils::is_aligned_off<uint32_t, kvsymdb::ALIGN_SIZE>(entsize);

    assert(is_aligned);
    if (symdb_p->_buf_len + entsize > symdb_p->_buf_size) {
        uint32_t new_bufsize = _intrnl::align_u32(
            (entsize > symdb_p->_buf_size)
                ? entsize : symdb_p->_buf_size * 2
        );

        dbg_print(
            "symdb_p->_buf_size: %u; new_bufsize: %u",
            symdb_p->_buf_size, new_bufsize
        );

        int rc = _intrnl::reserve_arenabuf(&symdb_p, new_bufsize);
        if (rc < 0) {
            symdb_p->_err_code = ErrorCode::ERR_RESIZEBUF;
            return symdb_p->_err_code;
        }

        *symdb_pp = symdb_p;
        assert(symdb_p->_buf_size == new_bufsize);
    }

    uint32_t ent_off = symdb_p->_buf_len;
    is_aligned = align_utils::
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

    uint32_t key_size_aligned = _intrnl::align::
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

    return KVSYMDB_OK;
}

extern "C" int kvsymdb_mark_dead(
    kvsymdb_t          *symdb_p,
    kvsymdb_entry_t    *ent_p
) {
    if (!symdb_p || !ent_p) return ErrorCode::ERR_NULLPTR;

    symdb_p->_err_code = ErrorCode::NOERROR;
    if (!_intrnl::is_valid_entry(symdb_p, ent_p)) {
        symdb_p->_err_code = ErrorCode::ERR_BADENT;
        return symdb_p->_err_code;
    }
    _intrnl::assert_intrnl_state(symdb_p);
    symdb_p->_state_arr[ent_p->id] = _intrnl::state::ST_DEAD;

    return KVSYMDB_OK;
}


extern "C" int kvsymdb_get_entview(
    const kvsymdb_t        *symdb_p,
    const kvsymdb_entry_t  *ent_p,
    kvsymdb_entview_t      *out_entview_p
) {

    if (!symdb_p || !ent_p || !out_entview_p)
        return ErrorCode::ERR_NULLPTR;

    if (!_intrnl::is_valid_entry(symdb_p, ent_p))
        return ErrorCode::ERR_BADENT;

    _intrnl::assert_intrnl_state(symdb_p);
    _intrnl::get_entry_view(ent_p, out_entview_p);

    return KVSYMDB_OK;
}

extern "C" int kvsymdb_entry_to_view(
    const kvsymdb_bufview_t    *arena_view_p,
    uint32_t                    entc,
    const kvsymdb_entry_t      *ent_p,
    kvsymdb_entview_t          *out_entview_p
) {

    if (!ent_p || !out_entview_p)
        return ErrorCode::ERR_NULLPTR;

    if (!_intrnl::is_valid_entry(arena_view_p, entc, ent_p))
        return ErrorCode::ERR_BADENT;

    _intrnl::get_entry_view(ent_p, out_entview_p);

    return KVSYMDB_OK;
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

extern "C" kvsymdb_iter_t
kvsymdb_iter_begin(kvsymdb_t *symdb_p) {
    return
        (!symdb_p) ? nullptr : 
        reinterpret_cast<kvsymdb_entry_t*>(symdb_p->_arena_buf);
}

extern "C" kvsymdb_iter_t
kvsymdb_iter_end(kvsymdb_t *symdb_p) {
    return
        (!symdb_p) ? nullptr :
        reinterpret_cast<kvsymdb_entry_t*>(
            symdb_p->_arena_buf + symdb_p->_buf_len
        );
}

extern "C" kvsymdb_iter_t
kvsymdb_iter_next(
    kvsymdb_t      *symdb_p,
    kvsymdb_iter_t  iter
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


inline void _kvsymdb_reader_bind(
    kvsymdb_reader_t   *reader_p,
    const void         *arena_buf,
    uint32_t            arena_len,
    uint32_t            entcnt
) {

    reader_p->_arena_view.buf_data  = arena_buf;
    reader_p->_arena_view.buf_size  = arena_len;
    reader_p->_entc     = entcnt; 
    reader_p->_pos      = 0u;
    reader_p->_err_code = ErrorCode::NOERROR;
}

extern "C"
int kvsymdb_reader_bind(
    kvsymdb_reader_t   *reader_p,
    const void         *arena_buf,
    uint32_t            arena_len,
    uint32_t            entcnt
) {
    if (!reader_p || !arena_buf) return ErrorCode::ERR_NULLPTR;

    _kvsymdb_reader_bind(reader_p, arena_buf, arena_len, entcnt);

    return KVSYMDB_OK;
}

extern "C"
int kvsymdb_reader_bind_db(
    kvsymdb_reader_t   *reader_p,
    const kvsymdb_t    *dbp
) {
    if (!reader_p || !dbp) return ErrorCode::ERR_NULLPTR;

    _kvsymdb_reader_bind(
        reader_p,
        dbp->_arena_buf,
        dbp->_buf_len,
        dbp->_entrycnt
    );

    return KVSYMDB_OK;
}

extern "C"
int kvsymdb_reader_bind_fhdr(
    kvsymdb_reader_t               *reader_p,
    const kvsymdb_file_header_t    *fhdr_p  
) {
    if (!reader_p || !fhdr_p) return ErrorCode::ERR_NULLPTR;

    _kvsymdb_reader_bind(
        reader_p,
        fhdr_p->fh_data,
        fhdr_p->fh_buflen,
        fhdr_p->fh_entcnt
    );

    return KVSYMDB_OK;
}

extern "C" const kvsymdb_entry_t *
kvsymdb_reader_read(kvsymdb_reader_t *reader_p) {
    const kvsymdb_entry_t *ent_p = nullptr;
    uint32_t min_reclen{};

    if (!reader_p || !reader_p->_arena_view.buf_data) {
        reader_p->_err_code = ErrorCode::ERR_NULLPTR;
        goto failed;
    }

    // check alignment
    if (
        reader_p->_pos &&
        !align_utils::align_off<uint32_t, kvsymdb::ALIGN_SIZE>(reader_p->_pos)
    ) {
        reader_p->_err_code = ErrorCode::ERR_BADALIGN;
        goto failed;
    }

    // checking is _pos + min_reclen <= than buf_len so 
    // the rec header is safe for deref (not guaranteed for payload yet)
    min_reclen = sizeof(kvsymdb_record_header_t);
    if (reader_p->_pos + min_reclen > reader_p->_arena_view.buf_size) {
        reader_p->_pos = reader_p->_arena_view.buf_size; // EOF
        reader_p->_err_code = ErrorCode::ERR_RDEOF;
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
        reader_p->_err_code = ErrorCode::ERR_BADENT;
        goto failed;
    }
   
    reader_p->_pos += ent_p->record_len;    
    assert(reader_p->_pos <= reader_p->_arena_view.buf_size);

    return ent_p;
failed:
    return nullptr;
}

extern "C" int kvsymdb_reader_rewind(kvsymdb_reader_t *reader_p) {
    if (!reader_p) return ErrorCode::ERR_NULLPTR;
    reader_p->_pos = 0u;
    return KVSYMDB_OK;
}

// I will rethink about seek impl because it might not worth it
// due to safety concerns

// cleanup
extern "C"
void kvsymdb_reader_unbind(kvsymdb_reader_t *reader_p) {
    if (reader_p) {
        reader_p->_arena_view.buf_data = nullptr;
        reader_p->_arena_view.buf_size = 0UL;
        reader_p->_entc = 0u;
        reader_p->_pos  = 0u;
    }
}


extern "C" const char *kvsymdb_strerror(int err_code) {
    return _intrnl::strerror(err_code);
}
extern "C" int kvsymdb_geterror(const kvsymdb_t *symdb_p) {
    return (symdb_p) ? symdb_p->_err_code : -1;
}
extern "C" void kvsymdb_clearerr(kvsymdb_t *symdb_p) {
    if (symdb_p) symdb_p->_err_code = KVSYMDB_OK;
}
extern "C" int kvsymdb_reader_geterror(const kvsymdb_reader_t *rdr_p) {
    return (rdr_p) ? rdr_p->_err_code : -1;
}
extern "C" void kvsymdb_reader_clearerr(kvsymdb_reader_t *rdr_p) {
    rdr_p->_err_code = 0;
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
    const char         *filename
) {
    if (!filename) return ErrorCode::ERR_NULLPTR;

    CXX_FILE db_file(fopen(filename, "wb"));
    if (!db_file.is_open()) {
        dbg_log_msg("");
        dbg_print("fopen failed: %s", strerror(errno));
        return ErrorCode::ERR_FOPEN;
    }

    kvsymdb::self_state dbst{};
    int rc = kvsymdb_get_intrnl_state(symdb_p, &dbst);
    if (rc) {
        dbg_log_msg("");
        dbg_print(
            "kvsymdb_get_intrnl_state_view failed: %s",
            _intrnl::strerror(rc)
        );
        return ErrorCode::ERR_DBST;
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
        return ErrorCode::ERR_IOFWRITE;
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
        return ErrorCode::ERR_IOFWRITE;
    }

    return KVSYMDB_OK;
}

extern "C"
int kvsymdb_file_mapper_init(
    kvsymdb_file_mapper_t  *mapper_p,
    const char             *filename
) {
    if (!mapper_p || !filename) return ErrorCode::ERR_NULLPTR;

    CXX_FILE dbif(fopen(filename, "rb"));
    if (!dbif.is_open()) {
        dbg_log_msg("");
        fprintf(stderr, "fopen failed: %s\n", ::strerror(errno));
        return ErrorCode::ERR_FOPEN;
    }

    FileDescHandle fd(dup(dbif.fileno()));
    if (!fd.is_valid()) {
        dbg_log_msg("");
        fprintf(stderr, "dup failed: %s\n", ::strerror(errno));
        return ErrorCode::ERR_DUP;
    }

    struct ::stat st{};
    if (::fstat(fd.get(), &st) < 0) {
        dbg_log_msg("");
        fprintf(stderr, "fstat failed: %s\n", ::strerror(errno));
        return ErrorCode::ERR_FSTAT;
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
        return ErrorCode::ERR_MMAP;
    }

    auto *hdr_p = static_cast<const kvsymdb::file_header*>(dbif_map.data());

    if (!_intrnl::is_valid_file_header(hdr_p, filesize))
        return ErrorCode::ERR_BADFHDR;

    auto arena_buf = reinterpret_cast<const uint8_t*>(hdr_p + 1);

    auto calc_crc32 = [](const void *buf, uint32_t buf_len) -> uint32_t {
        return static_cast<uint32_t>(
            ::crc32(0UL, static_cast<const uint8_t*>(buf), buf_len)
        );
    };

    if (calc_crc32(arena_buf, hdr_p->fh_buflen) != hdr_p->fh_crc32)
        return ErrorCode::ERR_CRC;

    // ownership transfer out of RAII for c api
    fd.move_out(&mapper_p->_fileno);
    dbif_map.move_out(&mapper_p->_base, &mapper_p->_size);    

    return KVSYMDB_OK;
}

extern "C"
void kvsymdb_file_mapper_cleanup(
    kvsymdb_file_mapper_t  *mapper_p
) {
    if (!mapper_p) return;

    // using RAII for cleanup
    (void)FileDescHandle(mapper_p->_fileno);
    (void)MMapHandle(mapper_p->_base, mapper_p->_size);

    mapper_p->_fileno   = INVALID_FILENO;
    mapper_p->_base     = nullptr;
    mapper_p->_size     = 0UL;
}

extern "C"
const kvsymdb_file_header_t *
kvsymdb_file_mapper_get_file_header(
    const kvsymdb_file_mapper_t  *mapper_p
) {
    if (!mapper_p) return nullptr;

    return static_cast<const kvsymdb_file_header_t*>(mapper_p->_base);
}
