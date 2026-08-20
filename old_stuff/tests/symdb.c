#include <kvsymdb.h>
#include <stdint.h>
#include <fnv1a_hash.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "dbg_print.h"


typedef struct cstr_kv {
    const char *name;
    const char *data;
} cstr_kv;

const cstr_kv rectbl[] = {
    { "operator new", "libstdc++: new; new" },
    { "operator new[]", "libstdc++: new; new[]" },
    { "operator delete", "libstdc++: new; delete" },
    { "operator delete[]", "libstdc++: new; delete[]" },
    { "new (mem) Type()", "libstdc++; new (placement)" },
    { "Type::~Type()", "destructor" },
    { "Type::Type()", "constructor" },
    { "T *obj_p = new T()", "operator new(sizeof(T)), new (obj_p) T()" },
    { "delete obj_p", "obj_p->~T(), operator delete(obj_p)" },
    { "std::vector<T>", "libstdc++: vector (STL, dynamic array)" },
    { "std::array<T, N>", "libstdc++: array (STL)" },
    { "std::addressof(obj)", "libstdc++: &obj" },
    { "std::unique_ptr<T>", "libstdc++: memory; unique_ptr" },
    { "open", "fcntl.h: open" },
    { "close", "unistd.h: close" },
    { "read", "unistd.read" },
    { "write", "unistd.h: write" },
    { "lseek", "unistd.h: lseek" },
};

const uint32_t rectbl_len = sizeof(rectbl) / sizeof(*rectbl);


static inline void print_entry_view(const kvsymdb_entview_t *entview_p) {
    printf(
        "{\n"
        "\tid=%u,\n"
        "\thash=0x%.8X,\n"
        "\ttype=0x%.4X,\n"
        "\tname_len=%u,\n"
        "\tname='%s',\n"
        "\tdata_len=%u,\n"
        "\tdata='%.*s',\n"
        "\taddr=%p\n"
        "}\n",
        entview_p->id, entview_p->hash, entview_p->type,
        entview_p->name_len,
        (char*)entview_p->name,
        entview_p->data_len,
        entview_p->data_len,
        (char*)entview_p->data,
        entview_p->_record
    );
}

static inline void print_ent_kv(const kvsymdb_entview_t *entview_p) {
    printf(
        "[%u] ('%s', '%.*s')\n",
        entview_p->id,
        (char*)entview_p->name,
        entview_p->data_len,
        (char*)entview_p->data
    );
}


const uint32_t STDIO_BUFSIZE = 8192u;

#define TYPE_MAGIC 0xEF0A

int main() {
    //setvbuf(stdout, NULL, _IOFBF, STDIO_BUFSIZE);
    //setvbuf(stderr, NULL, _IOFBF, STDIO_BUFSIZE);

    int err = 0;

    kvsymdb_t *dbp = create_kvsymdb(32, 1024u, &err);
    if (!dbp) {
        dbg_print("create_kvsymdb failed: %s\n", kvsymdb_strerror(err));
        return -1;
    }

    for (uint32_t i = 0u; i < rectbl_len; ++i) {
        kvsymdb_bufview_t
            key = { strlen(rectbl[i].name), rectbl[i].name },
            val = { strlen(rectbl[i].data), rectbl[i].data };

        int rc = kvsymdb_insert(
            &dbp,
            &key,
            &val,
            fnv_1a_hash32(key.data, key.size),
            TYPE_MAGIC,
            &err
        );
        if (rc) {
            dbg_print("kvsymdb_insert failed: %s\n", kvsymdb_strerror(err));
            goto failed;
        }
    }


    kvsymdb_iterator_t it = kvsymdb_iterator_begin(dbp);
    kvsymdb_iterator_t end = kvsymdb_iterator_end(dbp);
    while (it != end) {
        kvsymdb_entview_t ev = {0};
        int rc = kvsymdb_get_entview(dbp, it, &ev, &err);
        if (rc) {
            dbg_print("kvsymdb_get_entview failed: %s\n", kvsymdb_strerror(err));
            goto failed;
        }

        print_entry_view(&ev);
        it = kvsymdb_iterator_next(dbp, it);
    }

    puts("");

    it = kvsymdb_iterator_begin(dbp);
    end = kvsymdb_iterator_end(dbp);
    while (it != end) {
        kvsymdb_entview_t ev = {0};
        int rc = kvsymdb_get_entview(dbp, it, &ev, &err);
        if (rc) {
            dbg_print("kvsymdb_get_entview failed: %s\n", kvsymdb_strerror(err));
            continue;
        }

        print_ent_kv(&ev);
        rc = kvsymdb_mark_dead(dbp, it, &err);
        if (rc) {
            dbg_print("kvsymdb_mark_dead failed: %s\n", kvsymdb_strerror(err));
            continue;
        }

        it = kvsymdb_iterator_next(dbp, it);
    }

    puts("");
    it = kvsymdb_iterator_begin(dbp);
    end = kvsymdb_iterator_end(dbp);
    while (it != end) {
        kvsymdb_entview_t ev = {0};
        int rc = kvsymdb_get_entview(dbp, it, &ev, &err);
        if (rc) {
            dbg_print("kvsymdb_get_entview failed: %s\n", kvsymdb_strerror(err));
            continue;
        }

        if (!kvsymdb_is_valid_entry(dbp, it)) {
            fputs("[DEADBEEF] ", stdout);
        }

        print_ent_kv(&ev);
        rc = kvsymdb_mark_dead(dbp, it, &err);

        it = kvsymdb_iterator_next(dbp, it);
    }


    int rc = kvsymdb_compact(&dbp, &err);
    if (rc) {
        dbg_print("kvsymdb_compact failed: %s\n", kvsymdb_strerror(err));
        goto failed;
    }

    puts("---- AFTER COMPACTION ----");
    it = kvsymdb_iterator_begin(dbp);
    end = kvsymdb_iterator_end(dbp);
    while (it != end) {
        kvsymdb_entview_t ev = {0};
        int rc = kvsymdb_get_entview(dbp, it, &ev, &err);
        dbg_log_msg("");
        if (rc) {
            dbg_print("kvsymdb_get_entview failed: %s\n", kvsymdb_strerror(err));
            continue;
        }

        if (!kvsymdb_is_valid_entry(dbp, it)) {
            fputs("[DEADBEEF] ", stdout);
        }

        print_ent_kv(&ev);
        rc = kvsymdb_mark_dead(dbp, it, &err);

        it = kvsymdb_iterator_next(dbp, it);
    }

    for (uint32_t i = 0u; i < rectbl_len; ++i) {
        kvsymdb_bufview_t
            key = { strlen(rectbl[i].name), rectbl[i].name },
            val = { strlen(rectbl[i].data), rectbl[i].data };

        dbg_log_msg("");
        int rc = kvsymdb_insert(
            &dbp,
            &key,
            &val,
            fnv_1a_hash32(key.data, key.size),
            TYPE_MAGIC,
            &err
        );
        if (rc) {
            dbg_print("kvsymdb_insert failed: %s\n", kvsymdb_strerror(err));
            goto failed;
        }
    }

    puts("AGAIN:");

    it = kvsymdb_iterator_begin(dbp);
    end = kvsymdb_iterator_end(dbp);
    while (it != end) {
        kvsymdb_entview_t ev = {0};
        int rc = kvsymdb_get_entview(dbp, it, &ev, &err);
        dbg_log_msg("");
        if (rc) {
            dbg_print("kvsymdb_get_entview failed: %s\n", kvsymdb_strerror(err));
            goto failed;
        }

        print_ent_kv(&ev);
        it = kvsymdb_iterator_next(dbp, it);
    }

    kvsymdb_hash_index_t *hidxp =
        create_kvsymdb_hash_index(dbp, &err);

    if (!hidxp) {
        dbg_print("create_kvsymdb_hash_index failed: %s\n", kvsymdb_strerror(err));
        goto failed;
    }


    puts("AGAIN 2:");
    it = kvsymdb_iterator_begin(dbp);
    end = kvsymdb_iterator_end(dbp);
    while (it != end) {
        int rc = kvsymdb_hidx_insert(hidxp, it, &err);
        if (rc) {
            dbg_print("kvsymdb_hidx_insert failed: %s\n", kvsymdb_strerror(err));
            continue;
        }

        it = kvsymdb_iterator_next(dbp, it);
    }


    puts("AGAIN 3:");
    for (uint32_t i = 0u; i < rectbl_len; ++i) {
        kvsymdb_bufview_t key = { strlen(rectbl[i].name), rectbl[i].name };
        const kvsymdb_entry_t *ent_p = kvsymdb_hidx_lookup(
            hidxp,
            &key,
            &err
        );
        if (!ent_p) {
            dbg_print("kvsymdb_insert failed: %s\n", kvsymdb_strerror(err));
            continue;
        }
        kvsymdb_entview_t ev = {0};
        int rc = kvsymdb_get_entview(dbp, ent_p, &ev, &err);
        if (rc) {
            dbg_print("kvsymdb_get_entview failed: %s\n", kvsymdb_strerror(err));
            continue;
        }
        print_ent_kv(&ev);
    }


    destroy_kvsymdb_hash_index(hidxp);
    destroy_kvsymdb(dbp);
    return 0;

failed:
    destroy_kvsymdb(dbp);
    return -1;
}
