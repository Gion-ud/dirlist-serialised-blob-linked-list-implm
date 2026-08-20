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
#include <keylist_print.h>



int main() {
    KeyListArena *kla_p = kla_create_key_list_arena();
    assert(kla_p);

    kla_put_key(&kla_p, "open");
    kla_put_key(&kla_p, "close");
    kla_put_key(&kla_p, "read");
    kla_put_key(&kla_p, "write");
    kla_put_key(&kla_p, "lseek");
    kla_put_key(&kla_p, "tell");
    kla_put_key(&kla_p, "mmap");
    kla_put_key(&kla_p, "munmap");
    kla_put_key(&kla_p, "malloc");
    kla_put_key(&kla_p, "calloc");
    kla_put_key(&kla_p, "realloc");
    kla_put_key(&kla_p, "free");
    kla_put_key(&kla_p, "alloca");
    kla_put_key(&kla_p, "ioctl");
    kla_put_key(&kla_p, "opendir");
    kla_put_key(&kla_p, "readdir");
    kla_put_key(&kla_p, "writedir");
    kla_put_key(&kla_p, "seekdir");
    kla_put_key(&kla_p, "telldir");
    kla_put_key(&kla_p, "closedir");



    KeyListIterator klit = {0};
    kla_iterator_init(&klit, kla_p);

    Vector(KeyListEntry*) ep_vec = vector_create(KeyListEntry*);

    KeyListEntry *ent_p = NULL;
    while ((ent_p = kla_iterator_read(&klit)) != NULL) {
        vector_push_back(&ep_vec, &ent_p);
        kla_print_entry(ent_p);
    }

    kla_mark_dead(kla_p, ep_vec[5]);
    //kla_mark_dead(kla_p, ep_vec[9]);
    //kla_mark_dead(kla_p, ep_vec[10]);
    kla_mark_dead(kla_p, ep_vec[12]);
    kla_mark_dead(kla_p, ep_vec[13]);
    kla_mark_dead(kla_p, ep_vec[16]);

    puts("");
    VectorIterator(KeyListEntry*) it = {0};
    for (
        it = vector_begin(ep_vec);
        it != vector_end(ep_vec);
        it = vector_next(ep_vec, it)
    ) {
        if (kla_is_dead_entry(kla_p, *it)) continue;
        kla_print_entry(*it);
    }
    
    vector_clear(ep_vec);
    kla_iterator_reset(&klit);
    kla_compact(&kla_p);

    kla_iterator_init(&klit, kla_p);
    ent_p = NULL;
    while ((ent_p = kla_iterator_read(&klit)) != NULL) {
        vector_push_back(&ep_vec, &ent_p);
        kla_print_entry(ent_p);
    }

    puts("");
    for (
        it = vector_begin(ep_vec);
        it != vector_end(ep_vec);
        it = vector_next(ep_vec, it)
    ) {
        if (kla_is_dead_entry(kla_p, *it)) continue;
        kla_print_entry(*it);
    }

    kla_put_key(&kla_p, "std::vector::iterator");
    kla_compact(&kla_p);

    vector_destroy(ep_vec);
    kla_iterator_reset(&klit);


    int fd = open("keylist.bin", O_CREAT | O_RDWR | O_BINARY, 0644);
    assert(fd != -1);
    FILE *fp = fdopen(dup(fd), "wb+");

    fwrite(
        (byte_t*)kla_p + offsetof(KeyListArena, entrycnt),
        1,
        kla_p->buf_len + sizeof(uint32_t),
        fp
    );

    fclose(fp);
    close(fd);


    kla_destroy_key_list_arena(kla_p);
    return 0;
}