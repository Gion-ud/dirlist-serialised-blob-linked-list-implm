#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct KeyListEntry {
    uint32_t    id;
    uint32_t    next_off;
    uint16_t    ent_info;
    uint16_t    name_len;
    char        name[];
} KeyListEntry;

typedef struct Blob {
    uint32_t    data_len;
    uint8_t     data[];
} Blob;

typedef struct KeyListArena {
    uint32_t    buf_len;
    uint32_t    buf_size;
    uint32_t    entrycnt;
    uint8_t     buf[];
} KeyListArena;

typedef struct StringView {
    size_t      len;
    const char *data;
} StringView;

typedef struct KeyListIterator {
    const KeyListArena *kla_p;
    uint32_t            pos;
} KeyListIterator;

extern KeyListArena *Create_KeyListArena();
extern void Destroy_KeyListArena(KeyListArena *kla_p);
extern KeyListEntry *KeyListArena_WriteEntry(
    KeyListArena  **kla_pp,
    StringView     *key_sv_p,
    uint32_t       *out_entoff_p
);
extern int KeyListArena_MarkEntryDead(
    KeyListArena   *kla_p,
    KeyListEntry   *ent_p
);
extern KeyListEntry *KeyListArena_GetEntry(
    const KeyListArena *kla_p,
    uint32_t            entoff
);



extern KeyListIterator *Create_KeyListIterator(const KeyListArena *kla_p);
extern void Destroy_KeyListIterator(KeyListIterator *it_p);
extern KeyListEntry *KeyListIterator_ReadEntry(KeyListIterator *it_p);
extern int KeyListIterator_Rewind(KeyListIterator *it_p);
extern int KeyListArena_IsDeadEntry(
    KeyListArena   *kla_p,
    KeyListEntry   *ent_p
);
extern int Compact_KeyListArena(
    KeyListArena  **kla_pp
);
extern void KeyListArena_Clear(KeyListArena *kla_p);

#include <string.h>
static const KeyListEntry *KeyListArena_PutCstrKey(
    KeyListArena  **kla_pp,
    const char      cstr[]
) {
    if (!cstr) return NULL;
    StringView sv = {
        .len = strlen(cstr),
        .data = cstr
    };
    return KeyListArena_WriteEntry(kla_pp, &sv, NULL);
}

static void KeyListIterator_Init(
    KeyListIterator    *klit_p,
    const KeyListArena *kla_p
) {
    if (!klit_p || !kla_p) return;
    klit_p->kla_p   = kla_p;
    klit_p->pos     = 0u;
}

static void KeyListIterator_Reset(
    KeyListIterator    *klit_p
) {
    if (!klit_p) return;
    klit_p->kla_p   = NULL;
    klit_p->pos     = 0u;
}

extern int32_t KeyListIterator_Seek(KeyListIterator *it_p, uint32_t pos);
extern int32_t KeyListIterator_Tell(KeyListIterator *it_p);


#define kla_create_key_list_arena()                 Create_KeyListArena()
#define kla_destroy_key_list_arena(kla_p)           Destroy_KeyListArena(kla_p)
#define kla_write(kla_pp, key_sv_p, out_entoff_p)   KeyListArena_WriteEntry(kla_pp, key_sv_p, out_entoff_p)
#define kla_mark_dead(kla_p, ent_p)                 KeyListArena_MarkEntryDead(kla_p, ent_p)
#define kla_read(kla_p, entoff)                     KeyListArena_GetEntry(kla_p, entoff)
#define kla_create_iterator(kla_p)                  Create_KeyListIterator(kla_p)
#define kla_destroy_iterator(it_p)                  Destroy_KeyListIterator(it_p)
#define kla_iterator_read(it_p)                     KeyListIterator_ReadEntry(it_p)
#define kla_iterator_rewind(it_p)                   KeyListIterator_Rewind(it_p)
#define kla_is_dead_entry(kla_p, ent_p)             KeyListArena_IsDeadEntry(kla_p, ent_p)
#define kla_compact(kla_pp)                         Compact_KeyListArena(kla_pp)
#define kla_clear(kla_p)                            KeyListArena_Clear(kla_p)
#define kla_put_key(kla_pp, cstr)                   KeyListArena_PutCstrKey(kla_pp, cstr)
#define kla_iterator_init(klit_p, kla_p)            KeyListIterator_Init(klit_p, kla_p)
#define kla_iterator_reset(klit_p)                  KeyListIterator_Reset(klit_p)
#define kla_iterator_seek(klit_p, pos)              KeyListIterator_Seek(klit_p, pos)
#define kla_iterator_tell(klit_p, pos)              KeyListIterator_Tell(klit_p)

