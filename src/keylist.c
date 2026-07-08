#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include "dbg_print.h"
#include "alignoff.h"
#include <keylist.h>

#define _ent_info_u16_get_flags_u8(info) (((info) >> 8u) & 0xFF)
#define _ent_info_u16_get_type_u8(info) (((info) >> 0u) & 0xFF)
#define _ent_info_u16_set_flags_u8(info, flags) (((info) & 0x00FF) | ((flags) << 8u))
#define _ent_info_u16_set_type_u8(info, type) (((info) & 0xFF00) | ((type) << 0u))
#define _ent_info_u16_set_type_and_flags(info, type, flags) (((flags) << 8u) | ((type) << 0u))
#define INIT_BUF_SIZE 32u
#define KEY_LIST_ARENA_ALIGN 4u

enum KeyListEntryState {
    KLES_EMPTY  = 0x00,
    KLES_INUSE  = 0x01,
    KLES_DEAD   = 0x02,
};

typedef struct ArrayChunk {
    uint16_t    elem_info;
    uint16_t    length;
    uint8_t     data[];
} ArrayChunk;


KeyListArena *Create_KeyListArena() {
    _dbg_log_msg("0");
    uint32_t buf_size = INIT_BUF_SIZE;
    __auto_type kla_p = (KeyListArena*)malloc(sizeof(KeyListArena) + buf_size);
    if (!kla_p) goto failed;
    kla_p->buf_len  = 0;
    kla_p->buf_size = buf_size;
    kla_p->entrycnt = 0;
    memset(kla_p->buf, 0, buf_size);
    _dbg_log_msg("0.ret");
    return kla_p;
failed:
    _dbg_log_msg("-1.ret");
    return NULL;
}

void Destroy_KeyListArena(KeyListArena *kla_p) {
    _dbg_log_msg("");
    if (kla_p) free(kla_p);
}

KeyListEntry *KeyListArena_WriteEntry(
    KeyListArena  **kla_pp,
    StringView     *key_sv_p,
    uint32_t       *out_entoff_p
) {
    _dbg_log_msg("0");
    if (!kla_pp || !*kla_pp || !(*kla_pp)->buf_size) goto failed;
    if (!key_sv_p || !key_sv_p->len || !key_sv_p->data) goto failed;

    KeyListArena *kla_p = *kla_pp;

    _dbg_print("len=%u;size=%u;", kla_p->buf_len, kla_p->buf_size);
    assert(kla_p->buf_len <= kla_p->buf_size);
    assert(is_aligned_off(kla_p->buf_len, KEY_LIST_ARENA_ALIGN));

    uint32_t ent_len_aligned =
        align_off(sizeof(KeyListEntry) + key_sv_p->len + 1, KEY_LIST_ARENA_ALIGN);
    uint32_t next_entoff = kla_p->buf_len + ent_len_aligned;

    _dbg_log_msg("1");
    if (next_entoff >= kla_p->buf_size) {
        uint32_t new_buf_size =
            (next_entoff <= kla_p->buf_size * 2)
            ? kla_p->buf_size * 2 : next_entoff * 2;
        _dbg_log_msg("1.a");
        __auto_type new_kla_p =
            (KeyListArena*)realloc(*kla_pp, sizeof(KeyListArena) + new_buf_size);
        if (!new_kla_p) goto failed;
        kla_p = new_kla_p;
        *kla_pp = new_kla_p;
        kla_p->buf_size = new_buf_size;
    }

    __auto_type ent_p = (KeyListEntry*)(kla_p->buf + kla_p->buf_len);

    _dbg_log_msg("2");
    ent_p->id       = kla_p->entrycnt;
    ent_p->next_off = next_entoff;
    ent_p->ent_info = _ent_info_u16_set_type_and_flags(ent_p->ent_info, 0xFF, KLES_INUSE);
    ent_p->name_len = key_sv_p->len;
    memcpy(ent_p->name, key_sv_p->data, key_sv_p->len);
    ent_p->name[ent_p->name_len] = '\0';

    _dbg_log_msg("3");
    if (out_entoff_p) *out_entoff_p = kla_p->buf_len;
    kla_p->buf_len = next_entoff;
    ++kla_p->entrycnt;

    _dbg_log_msg("0.ret\n");
    return ent_p;
failed:
    _dbg_log_msg("-1.ret\n");
    return NULL;
}

int KeyListArena_MarkEntryDead(
    KeyListArena   *kla_p,
    KeyListEntry   *ent_p
) {
    _dbg_log_msg("0");
    if (!kla_p || !kla_p->buf_size) goto failed;
    if (!ent_p) goto failed;

    _dbg_log_msg("1");
    assert(kla_p->buf_len <= kla_p->buf_size);
    assert(is_aligned_off(kla_p->buf_len, KEY_LIST_ARENA_ALIGN));

    _dbg_log_msg("2");
    ent_p->ent_info = _ent_info_u16_set_flags_u8(ent_p->ent_info, KLES_DEAD);

    _dbg_log_msg("0.ret\n");
    return 0;
failed:
    _dbg_log_msg("-1.ret\n");
    return -1;
}

int KeyListArena_IsDeadEntry(
    KeyListArena   *kla_p,
    KeyListEntry   *ent_p
) {
    (void)kla_p;

    return (
        ent_p &&
        _ent_info_u16_get_flags_u8(
            ent_p->ent_info
        ) == (uint16_t)KLES_DEAD
    );
}

void KeyListArena_Clear(KeyListArena *kla_p) {
    if (!kla_p || !kla_p->buf_size) return;
    kla_p->buf_len = 0;
    kla_p->entrycnt = 0;
    memset(kla_p->buf, 0, kla_p->buf_size);
}

KeyListEntry *KeyListArena_GetEntry(
    const KeyListArena *kla_p,
    uint32_t            entoff
) {
    _dbg_log_msg("0");
    if (!kla_p || !kla_p->buf_size) goto failed;
    if (entoff >= kla_p->buf_len) goto failed;

    _dbg_log_msg("1");
    assert(kla_p->buf_len <= kla_p->buf_size);
    assert(is_aligned_off(kla_p->buf_len, KEY_LIST_ARENA_ALIGN));

    _dbg_log_msg("2");
    __auto_type ent_p = (KeyListEntry*)(kla_p->buf + entoff);
    assert(ent_p->id < kla_p->entrycnt);
    assert(ent_p->next_off <= kla_p->buf_len);
    uint32_t ent_len_aligned =
        align_off(sizeof(KeyListEntry) + ent_p->name_len + 1, KEY_LIST_ARENA_ALIGN);
    assert(ent_p->next_off == entoff + ent_len_aligned);

    _dbg_log_msg("0.ret");
    return ent_p;
failed:
    _dbg_log_msg("-1.ret");
    return NULL;
}

int Compact_KeyListArena(
    KeyListArena  **kla_pp
) {
    _dbg_log_msg("0");
    if (!kla_pp || !*kla_pp || !(*kla_pp)->buf_size) goto failed_ret;

    KeyListArena *kla_p = *kla_pp;

    _dbg_log_msg("1");
    KeyListArena *new_kla_p = Create_KeyListArena();
    if (!new_kla_p) goto failed_ret;

    _dbg_log_msg("2 # loop");
    uint32_t off_cur = 0;
    for (uint32_t i = 0u; i < kla_p->entrycnt; ++i) {
        __auto_type ent_p = KeyListArena_GetEntry(kla_p, off_cur);
        if (_ent_info_u16_get_flags_u8(ent_p->ent_info) != KLES_INUSE)
            goto loop_next_entry;
        _dbg_print(
            "(%u, %u, 0x%.4hx, %hu, %s)",
            ent_p->id, ent_p->next_off, ent_p->ent_info, ent_p->name_len, ent_p->name
        );
        StringView sv = {
            .len = ent_p->name_len,
            .data = ent_p->name
        };
        __auto_type __ep = KeyListArena_WriteEntry(&new_kla_p, &sv, NULL);
        if (!__ep) goto failed;
    loop_next_entry:
        off_cur = ent_p->next_off;
    }

    _dbg_log_msg("3");
    Destroy_KeyListArena(kla_p);
    assert(*kla_pp != new_kla_p);
    *kla_pp = new_kla_p;

    _dbg_log_msg("0.ret\n");
    return 0;
failed:
    Destroy_KeyListArena(new_kla_p);
failed_ret:
    _dbg_log_msg("-1.ret\n");
    return -1;
}


KeyListIterator *Create_KeyListIterator(const KeyListArena *kla_p) {
    _dbg_log_msg("0");
    if (!kla_p) goto failed;
    __auto_type it_p = (KeyListIterator*)malloc(sizeof(KeyListIterator));
    if (!it_p) goto failed;
    it_p->kla_p = kla_p;
    it_p->pos   = 0u;
    return it_p;
    _dbg_log_msg("0.ret");
failed:
    _dbg_log_msg("-1.ret");
    return NULL;
}

void Destroy_KeyListIterator(KeyListIterator *it_p) {
    _dbg_log_msg("");
    if (it_p) free(it_p);
}

KeyListEntry *KeyListIterator_ReadEntry(KeyListIterator *it_p) {
    _dbg_log_msg("");
    if (!it_p || !it_p->kla_p) return NULL;
    __auto_type ent_p = KeyListArena_GetEntry(it_p->kla_p, it_p->pos);
    if (!ent_p) return NULL;
    it_p->pos = ent_p->next_off;
    return ent_p;
}

int KeyListIterator_Rewind(KeyListIterator *it_p) {
    if (!it_p || !it_p->kla_p) return -1;
    it_p->pos = 0u;
    return 0;
}

int KeyListIterator_Seek(KeyListIterator *it_p, uint32_t pos) {
    if (!it_p || !it_p->kla_p) return -1;
    it_p->pos = pos;
    return 0;
}
int32_t KeyListIterator_Tell(KeyListIterator *it_p) {
    if (!it_p || !it_p->kla_p) return -1;
    return (int32_t)it_p->pos;
}
