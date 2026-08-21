#pragma once
#include <keylist.h>
#include <stdio.h>

static inline void kla_print_entry(KeyListEntry *ent_p) {
    printf(
        "(%u, %u, 0x%.4hx, %hu, %s)\n",
        ent_p->id, ent_p->next_off, ent_p->ent_info, ent_p->name_len, ent_p->name
    );
}

