#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <fnv1a_hash.h>
#include "dbg_print.h"

#define JW_NULL_IDX UINT32_MAX

typedef struct {
    char   *e_name;
    void   *e_data;
} jwkv_entry_t;

typedef struct {
    uint32_t    s_hash;
    uint32_t    s_idx;
} jwkv_hidx_slot_t;

typedef struct {
    const jwkv_entry_t *h_kv_arr;
    jwkv_hidx_slot_t   *h_slot_arr;
    unsigned int        h_kvc;
    unsigned int        h_slotc;
} jwkv_hidx_t;


extern void jwkv_hidx_cleanup(jwkv_hidx_t *hidx_p);
jwkv_hidx_t *jwkv_hidx_init(
    void               *hidx_mem,
    const jwkv_entry_t *kv_arr,
    unsigned int        kvc
) {
    if (!hidx_mem || !kv_arr || !kvc) return NULL;
    memset(hidx_mem, 0, sizeof(jwkv_hidx_t));

    jwkv_hidx_t *hidx_p = (jwkv_hidx_t*)hidx_mem;
    hidx_p->h_kvc       = kvc;
    hidx_p->h_slotc     = kvc * 2;
    hidx_p->h_kv_arr    = kv_arr;
    hidx_p->h_slot_arr  = 
        (jwkv_hidx_slot_t*)malloc(sizeof(jwkv_hidx_slot_t) * hidx_p->h_slotc);

    if (!hidx_p->h_slot_arr) goto failed;
    for (unsigned int i = 0u; i < hidx_p->h_slotc; ++i) {
        hidx_p->h_slot_arr[i].s_hash    = 0u;
        hidx_p->h_slot_arr[i].s_idx     = JW_NULL_IDX;
    }

    for (unsigned int i = 0u; i < kvc; ++i) {
        if (!kv_arr[i].e_name || !*kv_arr[i].e_name) continue;
        uint32_t hash = fnv_1a_hash32(
            kv_arr[i].e_name,
            strlen(kv_arr[i].e_name)
        );
        int slot_idx = hash % hidx_p->h_slotc;

        unsigned int probc = 0;
        while (probc <= hidx_p->h_slotc) {
            jwkv_hidx_slot_t *slot_p = &hidx_p->h_slot_arr[slot_idx];
            if (slot_p->s_idx == JW_NULL_IDX) {
                slot_p->s_hash  = hash;
                slot_p->s_idx   = i;
                ++probc;
                break;
            }

            slot_idx = (slot_idx + 1) % hidx_p->h_slotc;
            ++probc;
        }
    }

    return hidx_p;
failed:
    jwkv_hidx_cleanup(hidx_p);
    return NULL;
}

const jwkv_entry_t *jwkv_hidx_find(
    const jwkv_hidx_t  *hidx_p,
    const char         *name
) {
    if (!hidx_p || !name || !*name) return NULL;

    uint32_t hash = fnv_1a_hash32(name, strlen(name));
    int slot_idx = hash % hidx_p->h_slotc;

    unsigned int probc = 0;
    while (probc <= hidx_p->h_slotc) {
        const jwkv_hidx_slot_t *slot_p = &hidx_p->h_slot_arr[slot_idx];
        if (slot_p->s_idx == JW_NULL_IDX) break;

        const jwkv_entry_t *ent_p = &hidx_p->h_kv_arr[slot_p->s_idx];
        if (
            slot_p->s_hash == hash &&
            strcmp(name, ent_p->e_name) == 0
        ) return ent_p;

        slot_idx = (slot_idx + 1) % hidx_p->h_slotc;
        ++probc;
    }

    return NULL;
}


void jwkv_hidx_cleanup(jwkv_hidx_t *hidx_p) {
    if (!hidx_p) return;
    if (hidx_p->h_slot_arr) free(hidx_p->h_slot_arr);
    memset(hidx_p, 0, sizeof(*hidx_p));
}

#define jwkv_hidx_length(hidx_p) (hidx_p)->h_kvc
#define jwkv_entry_key(ent_p) (ent_p)->e_name
#define jwkv_entry_value(ent_p) (ent_p)->e_data

int main() {
    static const jwkv_entry_t kvtbl[] = {
        {"open", "fcntl.h: open"},
        {"close", "unistd.h: close"},
        {"read", "unistd.h: read"},
        {"write", "unistd.h: write"},
        {"lseek", "unistd.h: lseek"},
        {"mmap", "sys/mman.h: mmap"},
        {"munmap", "sys/mman.h: munmap"},
        {"fopen", "stdio.h: fopen"},
        {"fclose", "stdio.h: fclose"},
        {"fmemopen", "stdio.h: fmemopen"},
        {"open_memstream", "stdio.h: open_memstream"},
        {"getline", "stdio.h: getline"},
        {"malloc", "stdlib.h: malloc"},
        {"free", "stdlib.h: free"},
    };

    const unsigned int kvc = sizeof(kvtbl) / sizeof(*kvtbl);

    jwkv_hidx_t _hidx;
    jwkv_hidx_t *hidx_p = jwkv_hidx_init(&_hidx, kvtbl, kvc);
    assert(hidx_p);

    for (unsigned int i = 0u; i < kvc; ++i) {
        const jwkv_entry_t *ent_p = jwkv_hidx_find(hidx_p, kvtbl[i].e_name);
        assert(ent_p);
        printf("[%u] %s -> %s\n", i, ent_p->e_name, (char*)ent_p->e_data);
    }

    for (unsigned int i = 0; i < hidx_p->h_slotc; ++i) {
        printf("[%u] %d\n", i, hidx_p->h_slot_arr[i].s_idx);
    }

    assert(!jwkv_hidx_find(hidx_p, "_name"));
    assert(!jwkv_hidx_find(hidx_p, "name_"));
    assert(!jwkv_hidx_find(hidx_p, "alloca"));
    assert(!jwkv_hidx_find(hidx_p, "calloc"));

    jwkv_hidx_cleanup(hidx_p);

    return 0;
}