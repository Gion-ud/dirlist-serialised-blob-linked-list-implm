#include <kvsymdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <fnv1a_hash.h>
#include "dbg_print.h"

typedef struct cstr_cstr_kv {
    const char *name;
    const char *data;
} cstr_cstr_kv;

static cstr_cstr_kv kv_arr[] = {
    {"open", "libc fcntl.h: open"},
    {"close", "libc unistd.h: close"},
    {"read", "libc unistd.h: read"},
    {"write", "libc unistd.h: write"},
    {"lseek", "libc unistd.h: lseek"},
    {"mmap", "libc sys/mman.h: mmap"},
    {"munmap", "libc sys/mman.h: munmap"},
    {"opendir", "libc dirent.h: opendir"},
    {"closedir", "libc dirent.h: closedir"},
    {"readdir", "libc dirent.h: readdir"},
    {"seekdir", "libc dirent.h: seekdir"},
    {"telldir", "libc dirent.h: telldir"},
    {"malloc", "libc stdlib.h: malloc"},
    {"calloc", "libc stdlib.h: calloc"},
    {"realloc", "libc stdlib.h: realloc"},
    {"free", "libc stdlib.h: free"},
    {"getdents64", "(x86-64_linux, GNU libc) sys/syscall.h: getdents64"},
    {"db_open", "(BSD) libdb db.h: db_open"},
    {"vsnprintf", "libc stdio.h: vsnprintf"},
    {"new", "libstdc++ new: operator new"},
    {"delete", "libstdc++ new: operator delete"},
};

#define c_arr_len(arr) sizeof(arr) / sizeof(*arr)

static const uint32_t kv_cnt = c_arr_len(kv_arr);

int main() {
    int err;
    kvsymdb_iter_t it;
    kvsymdb_iter_t it_end;

    kvsymdb_t *dbp = create_kvsymdb(32u, 0u, &err);
    assert(dbp);

    dbg_log_msg("#1");
    for (uint32_t i = 0u; i < kv_cnt; ++i) {
        kvsymdb_bufview_t
            key_view = {
                .buf_data = kv_arr[i].name,
                .buf_size = strlen(kv_arr[i].name)
            },
            val_view = {
                .buf_data = kv_arr[i].data,
                .buf_size = strlen(kv_arr[i].data)
            };

        int rc = kvsymdb_insert(
            &dbp,
            &key_view,
            &val_view,
            fnv_1a_hash32(key_view.buf_data, key_view.buf_size),
            0x0001
        );
        assert(rc == KVSYMDB_OK);
    }

    dbg_log_msg("#2");    
    it = kvsymdb_iter_begin(dbp);
    it_end = kvsymdb_iter_end(dbp);
    while (it != it_end) {
        kvsymdb_entview_t ev;
        int rc = kvsymdb_get_entview(dbp, it, &ev);
        if (rc != KVSYMDB_OK) {
            continue;
            dbg_print("%s", kvsymdb_strerror(rc));
            assert(rc == KVSYMDB_OK);
        }

        printf(
            "[%u] (0x%.8x, \"%.*s\", \"%.*s\")\n",
            ev.id,
            ev.hash,
            ev.name_len, ev.name,
            ev.data_len, (char*)ev.data
        );

        it = kvsymdb_iter_next(dbp, it);
    }
    puts("");

    dbg_log_msg("#2");    
    it = kvsymdb_iter_begin(dbp);
    it_end = kvsymdb_iter_end(dbp);
    while (it != it_end) {
        printf(
            "[%u] (\"%.*s\")\n",
            it->id,
            it->name_len, (char*)it->payload
        );

        it = kvsymdb_iter_next(dbp, it);
    }
    puts("");

    dbg_log_msg("#3");
    it = kvsymdb_iter_begin(dbp);
    it_end = kvsymdb_iter_end(dbp);
    while (it != it_end) {
        int rc = kvsymdb_mark_dead(dbp, it);
        assert(rc == KVSYMDB_OK);

        it = kvsymdb_iter_next(dbp, it);
    }
    puts("");

    int _rc = kvsymdb_reserve(&dbp, 64u);
    assert(_rc == KVSYMDB_OK);

    for (uint32_t i = 0u; i < kv_cnt; ++i) {
        kvsymdb_bufview_t
            key_view = {
                .buf_data = kv_arr[i].name,
                .buf_size = strlen(kv_arr[i].name)
            },
            val_view = {
                .buf_data = kv_arr[i].data,
                .buf_size = strlen(kv_arr[i].data)
            };

        int rc = kvsymdb_insert(
            &dbp,
            &key_view,
            &val_view,
            fnv_1a_hash32(key_view.buf_data, key_view.buf_size),
            0x0001
        );
        assert(rc == KVSYMDB_OK);
    }

    dbg_log_msg("#4");
    it = kvsymdb_iter_begin(dbp);
    it_end = kvsymdb_iter_end(dbp);
    while (it != it_end) {
        if (!kvsymdb_is_valid_entry(dbp, it))
            goto next_iter;

        kvsymdb_entview_t ev;
        int rc = kvsymdb_get_entview(dbp, it, &ev);
        assert(rc == KVSYMDB_OK);
    
        printf(
            "[%u] (0x%.8x, \"%.*s\", \"%.*s\")\n",
            ev.id,
            ev.hash,
            ev.name_len, ev.name,
            ev.data_len, (char*)ev.data
        );

    next_iter:
        it = kvsymdb_iter_next(dbp, it);
    }
    puts("");

    _rc = kvsymdb_compact(&dbp);
    assert(!_rc);

    dbg_log_msg("#5");
    it = kvsymdb_iter_begin(dbp);
    it_end = kvsymdb_iter_end(dbp);
    while (it != it_end) {
        kvsymdb_entview_t ev;
        int rc = kvsymdb_get_entview(dbp, it, &ev);
        assert(rc == KVSYMDB_OK);

        printf(
            "[%u] (0x%.8x, \"%.*s\", \"%.*s\")\n",
            ev.id,
            ev.hash,
            ev.name_len, ev.name,
            ev.data_len, (char*)ev.data
        );

        it = kvsymdb_iter_next(dbp, it);
    }
    puts("");

    kvsymdb_reader_t rdr;
    _rc = kvsymdb_reader_bind_db(&rdr, dbp);
    assert(!_rc);

    dbg_log_msg("#6");
    const kvsymdb_entry_t *ent_p = NULL;
    while ((ent_p = kvsymdb_reader_read(&rdr)) != NULL) {
        kvsymdb_entview_t ev;
        int rc = kvsymdb_get_entview(dbp, ent_p, &ev);
        assert(rc == KVSYMDB_OK);

        printf(
            "[%u] (0x%.8x, \"%.*s\", \"%.*s\")\n",
            ev.id,
            ev.hash,
            ev.name_len, ev.name,
            ev.data_len, (char*)ev.data
        );
    }
    kvsymdb_reader_unbind(&rdr);

    _rc = kvsymdb_file_builder_dump(dbp, "tscsym.bin");
    assert(_rc == KVSYMDB_OK);

    kvsymdb_file_mapper_t mpr;
    _rc = kvsymdb_file_mapper_init(&mpr, "tscsym.bin");
    assert(!_rc);

    const kvsymdb_file_header_t *hdr_p = kvsymdb_file_mapper_get_file_header(&mpr);
    _rc = kvsymdb_reader_bind(
        &rdr,
        hdr_p->fh_data,
        hdr_p->fh_buflen,
        hdr_p->fh_entcnt
    );
    assert(!_rc);

    if (!kvsymdb_reader_read(&rdr)) {
        printf("reader: %s\n", kvsymdb_reader_errmsg(&rdr));
    }
    kvsymdb_reader_rewind(&rdr);

    kvsymdb_bufview_t arena_view = {
        .buf_data = hdr_p->fh_data,
        .buf_size = hdr_p->fh_buflen
    };

    dbg_log_msg("#67");
    ent_p = NULL;
    while ((ent_p = kvsymdb_reader_read(&rdr)) != NULL) {
        kvsymdb_entview_t ev;
        int rc = kvsymdb_entry_to_view(&arena_view, hdr_p->fh_entcnt, ent_p, &ev);
        if (rc) {
            printf("[ERR] kvsymdb_entry_to_view: %s\n", kvsymdb_strerror(rc));
            continue;
        }

        printf(
            "[%u] (0x%.8x, \"%.*s\", \"%.*s\")\n",
            ev.id,
            ev.hash,
            ev.name_len, ev.name,
            ev.data_len, (char*)ev.data
        );
    }
    kvsymdb_reader_unbind(&rdr);
    kvsymdb_file_mapper_cleanup(&mpr);

    destroy_kvsymdb(dbp);
    return 0;
}