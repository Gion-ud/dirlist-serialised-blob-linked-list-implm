#pragma once
extern "C" {
#include <keylist.h>
}

namespace cxx_kla {

struct key_list_arena {
    KeyListArena       *_kla_p;
    key_list_arena() : _kla_p(nullptr) {
        this->_kla_p = kla_create_key_list_arena();
        if (!this->_kla_p) return;
    }
    ~key_list_arena() {
        kla_destroy_key_list_arena(this->_kla_p);
        this->_kla_p = nullptr;
    }
    bool is_init() { return (!!this->_kla_p); }
    const KeyListEntry *put_key(const char cstr[]) {
        return kla_put_key(&this->_kla_p, cstr);
    }
    const KeyListEntry *write(const char cstr[]) {
        return kla_put_key(&this->_kla_p, cstr);
    }
    struct iterator {
        KeyListIterator _it;
        iterator(key_list_arena &kla) : _it{} {
        }
    };
    key_list_arena(const key_list_arena &) = delete;
    key_list_arena &operator=(const key_list_arena &) = delete;
};


}