#include <keylist.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <intdef.h>
#include <vector.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>


const char *keytbl[] = {
    "new",
    "delete",
    "key",
    "value",
    "std::vector",
    "std::array",
    "begin",
    "end",
    "next",
    "prev",
    "malloc",
    "free",
    "std::vector::iterator",
};

uint_t keytbl_len = sizeof(keytbl) / sizeof(*keytbl);

int main() {
    KeyListArena *kla_p = kla_create_key_list_arena();
    assert(kla_p);

    for (uint_t i = 0; i < keytbl_len; ++i) {
        puts("it");
        __auto_type ep = kla_put_key(&kla_p, keytbl[i]);
        assert(ep);
    }

    __auto_type klit_p = kla_create_iterator(kla_p);
    assert(klit_p);
    Vector(KeyListEntry*) v = vector_create(KeyListEntry*);
    assert(v);

    KeyListEntry *ent_p = NULL;
    puts("");
    while ((ent_p = kla_iterator_read(klit_p)) != NULL) {
        vector_push_back(&v, &ent_p);
        printf(
            "(%u, %u, 0x%.4hx, %hu %s)\n",
            ent_p->id, ent_p->next_off, ent_p->ent_info, ent_p->name_len, ent_p->name
        );
    }
    puts("");
    uint32_t off_cur = 0;
    for (uint32_t i = 0u; i < kla_p->entrycnt; ++i) {
        __auto_type ent_p = kla_read(kla_p, off_cur);
        assert(ent_p);
        printf(
            "(%u, %u, 0x%.4hx, %hu, %s)\n",
            ent_p->id, ent_p->next_off, ent_p->ent_info, ent_p->name_len, ent_p->name
        );
        off_cur = ent_p->next_off;
    }

    kla_mark_dead(kla_p, v[0]);
    kla_mark_dead(kla_p, v[1]);
    kla_mark_dead(kla_p, v[2]);
    kla_mark_dead(kla_p, v[3]);
    kla_mark_dead(kla_p, v[5]);
    kla_mark_dead(kla_p, v[7]);
    kla_mark_dead(kla_p, v[11]);

    kla_iterator_rewind(klit_p);

    puts("");
    while ((ent_p = kla_iterator_read(klit_p)) != NULL) {
        //if (kla_is_dead_entry(kla_p, ent_p)) continue;
        printf(
            "(%u, %u, 0x%.4hx, %hu, %s)\n",
            ent_p->id, ent_p->next_off, ent_p->ent_info, ent_p->name_len, ent_p->name
        );
    }
    puts("");
    for (
    VectorIterator(__typeof__(*v))
        it = vector_begin(v);
        it != vector_end(v);
        it = vector_next(v, it)
    ) {
        if (kla_is_dead_entry(kla_p, *it)) continue;
        printf("[%u]: %s\n", (*it)->id, (*it)->name);
    }
    vector_clear(v);
    kla_iterator_reset(klit_p);
    kla_put_key(&kla_p, "std::vector::iterator");

    int ret = kla_compact(&kla_p);
    assert(ret != -1);
    off_cur = 0;
    for (uint32_t i = 0u; i < kla_p->entrycnt; ++i) {
        __auto_type ent_p = kla_read(kla_p, off_cur);
        assert(ent_p);
        printf(
            "(%u, %u, 0x%.4hx, %hu, %s)\n",
            ent_p->id, ent_p->next_off, ent_p->ent_info, ent_p->name_len, ent_p->name
        );
        off_cur = ent_p->next_off;
    }

    kla_iterator_init(klit_p, kla_p);
    puts("");
    while ((ent_p = kla_iterator_read(klit_p)) != NULL) {
        if (kla_is_dead_entry(kla_p, ent_p)) continue;
        printf(
            "(%u, %u, 0x%.4hx, %hu, %s)\n",
            ent_p->id, ent_p->next_off, ent_p->ent_info, ent_p->name_len, ent_p->name
        );
    }
    puts("");

    kla_clear(kla_p);

    for (uint_t i = 0; i < keytbl_len; ++i) {
        puts("it");
        __auto_type ep = kla_put_key(&kla_p, keytbl[i]);
        assert(ep);
    }

    puts("");
    off_cur = 0;
    for (uint32_t i = 0u; i < kla_p->entrycnt; ++i) {
        __auto_type ent_p = kla_read(kla_p, off_cur);
        assert(ent_p);
        vector_push_back(&v, &ent_p);
        printf(
            "(%u, %u, 0x%.4hx, %hu, %s)\n",
            ent_p->id, ent_p->next_off, ent_p->ent_info, ent_p->name_len, ent_p->name
        );
        off_cur = ent_p->next_off;
    }
    puts("");
    kla_iterator_seek(klit_p, v[6]->next_off);

    ent_p = kla_iterator_read(klit_p);
    printf(
        "(%u, %u, 0x%.4hx, %hu, %s)\n",
        ent_p->id, ent_p->next_off, ent_p->ent_info, ent_p->name_len, ent_p->name
    );

    for (
    VectorIterator(KeyListEntry*)
        it = vector_begin(v);
        it != vector_end(v);
        it = vector_next(v, it)
    ) {
        printf("%s\n", (*it)->name);
    }

    kla_destroy_iterator(klit_p);


    int fd = open("keylist.bin", O_CREAT | O_RDWR | O_BINARY, 0644);
    assert(fd != -1);
    FILE *fp = fdopen(dup(fd), "wb+");

    fwrite(
        (byte_t*)kla_p + offsetof(KeyListArena, entrycnt),
        1,
        kla_p->buf_len + sizeof(uint32_t),
        fp
    );

    vector_destroy(v);

    fclose(fp);
    close(fd);
    kla_destroy_key_list_arena(kla_p);
    return 0;
}