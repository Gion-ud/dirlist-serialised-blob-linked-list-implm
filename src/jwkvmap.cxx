#include "jwkvmap.hpp"
#include <cstring>
#include <cassert>
#include <new>
#include <iostream>
#include <cerrno>
//#include <algorithm>
//#include <iterator>

//#include "dbg_print.h"

#define LOG_ERROR "[ERROR] "

namespace jwkvmap {

namespace _priv {
    #include "alignoff.h"
    using namespace align_utils;
    constexpr std::uint32_t ALIGN_SIZE = sizeof(std::max_align_t);

    inline std::uint32_t entry_length_required(
        const buffer_view  &key_view_ref,
        const buffer_view  &val_view_ref
    ) noexcept {
        return
            sizeof(KVMap::EntryHeader) +
            align_off<uint32_t, ALIGN_SIZE>(key_view_ref.length()) +
            align_off<uint32_t, ALIGN_SIZE>(val_view_ref.length());
    }

    inline KVMap::EntryHeader *
    slot_get_entry_header(
        KVMap::Slot    *slot_p
    ) noexcept {
        return &slot_p->hs_ehdr;
    }

    inline char *
    entry_get_key_payload(KVMap::EntryHeader *ent_p) {
        return reinterpret_cast<char*>(ent_p->eh_payload);
    }

    inline void *
    entry_get_val_payload(KVMap::EntryHeader *ent_p) {
        return ent_p->eh_payload + align_off<uint32_t, ALIGN_SIZE>(ent_p->eh_key_len);
    }

    inline const char *
    entry_get_key_payload(const KVMap::EntryHeader *ent_p) {
        return reinterpret_cast<const char*>(ent_p->eh_payload);
    }

    inline const void *
    entry_get_val_payload(const KVMap::EntryHeader *ent_p) {
        return ent_p->eh_payload + align_off<uint32_t, ALIGN_SIZE>(ent_p->eh_key_len);
    }

}

KVMap::Slot *KVMap::_create_slot(
    const buffer_view  &key_view_ref,
    const buffer_view  &val_view_ref,
    hash32_t            key_hash,
    std::uint32_t       type
) {
    this->m_errno = KVErrorCode::ERR_NOERROR;

    std::uint32_t ent_len   = _priv::entry_length_required(key_view_ref, val_view_ref);
    std::uint32_t slot_len  = ent_len + sizeof(SlotLink);

    void *_ent_buf = ::operator new(slot_len, std::nothrow);
    if (!_ent_buf) {
        std::cerr
            << LOG_ERROR "KVMap::_create_slot: operator new failed: "
            << std::strerror(errno) << std::endl;
        this->m_errno = KVErrorCode::ERR_OPNEW;
        return nullptr;
    }
    std::memset(_ent_buf, 0, slot_len);

    Slot *slot_p = ::new (_ent_buf) Slot();

    slot_p->hs_link.ln_prev_p = &slot_p->hs_link;
    slot_p->hs_link.ln_next_p = &slot_p->hs_link;
    auto *ehdr_p = _priv::slot_get_entry_header(slot_p);

    ehdr_p->eh_type     = type;
    ehdr_p->eh_hash     = key_hash;
    ehdr_p->eh_key_len  = key_view_ref.length();
    ehdr_p->eh_val_len  = val_view_ref.length();
    ehdr_p->eh_ent_len  = ent_len;

    std::memcpy(
        _priv::entry_get_key_payload(ehdr_p),
        key_view_ref.data(),
        key_view_ref.length()
    );
    std::memcpy(
        _priv::entry_get_val_payload(ehdr_p),
        val_view_ref.data(),
        val_view_ref.length()
    );

    return slot_p;
}

void KVMap::_destroy_slot(Slot *slot_p) noexcept {
    if (slot_p) {
        slot_p->~Slot();
        ::operator delete(slot_p);
    }
}

void KVMap::_cleanup() {
    if (!this->is_init()) return;

    for (
        auto it = this->_bucket_arr_begin();
        it != this->_bucket_arr_end();
        ++it
    ) {
        if (it->m_slot_cnt) {
            for (
                auto it1 = it->chain_begin();
                it1 != it->chain_end();
            ) {
                auto next_slot = it1->ln_next_p;
                Slot *slot_p = SlotHandle(it1).unlink();
                this->_destroy_slot(slot_p);
                it1 = next_slot;
            }
        }
    }

    this->m_bucket_arr.reset();
    this->m_bucket_cnt  = 0UL;
    this->m_slot_cnt    = 0UL;
    this->m_errno       = KVErrorCode::ERR_NOERROR;
}


int KVMap::_link_slot(Slot *slot_p) {
    assert(this->is_init());
    assert(slot_p);
    assert(this->bucket_count());

    std::uint32_t bucket_idx = slot_p->hs_ehdr.eh_hash % this->bucket_count();
    auto *bucket_p = this->_bucket_arr_begin() + bucket_idx;

    Slot *ret = bucket_p->push_slot(slot_p);
    assert(ret);
    ++this->m_slot_cnt;

    return JWKVMAP_OK;
}


int KVMap::_lookup_slot(
    const buffer_view  &key_view_ref,
    hash32_t            key_hash,
    LookupHandle       *out_luh_p
) {
    assert(this->is_init());
    assert(out_luh_p);

    auto bucket_idx = key_hash % this->bucket_count();
    auto *bucket_p  = this->_bucket_arr_begin() + bucket_idx;

    for (
    auto
        slot_ln = bucket_p->chain_begin();
        slot_ln != bucket_p->chain_end();
        slot_ln = slot_ln->ln_next_p
    ) {
        auto *slot_p   = SlotHandle(slot_ln).get_slot();
        auto *ent_p    = &slot_p->hs_ehdr;
        if (
            ent_p->eh_hash == key_hash &&
            ent_p->eh_key_len == key_view_ref.length() &&
            std::memcmp(
                _priv::entry_get_key_payload(ent_p),
                key_view_ref.data(),
                key_view_ref.length()
            ) == 0
        ) {
            out_luh_p->bucket_p = bucket_p;
            out_luh_p->slot_p   = slot_p;

            return JWKVMAP_OK;
        }
    }

    this->m_errno = KVErrorCode::ERR_NOT_FOUND;
    return JWKVMAP_FAILED;
}

KVMap::Slot *KVMap::_unlink_slot(
    Bucket *bucket_p,
    Slot   *slot_p
) {
    assert(this->is_init());
    assert(bucket_p >= this->_bucket_arr_begin());
    assert(bucket_p < this->_bucket_arr_end());
    assert(slot_p);

    Slot *ret_slot_p = bucket_p->unlink_slot(slot_p);

    if (!ret_slot_p) {
        std::cerr <<
            LOG_ERROR "KVMap::_unlink_slot: bucket_p->unlink_slot failed"
            << std::endl;
        this->m_errno = KVErrorCode::ERR_UNLINK;
        return nullptr;
    }

    --this->m_slot_cnt;
    return slot_p;
}

int KVMap::_rehash(std::size_t new_ht_size) {
    if (new_ht_size <= this->bucket_count())
        new_ht_size = this->bucket_count();
    if (!new_ht_size) new_ht_size = 2UL;
    {
        KVMap tmp_kvmap{};
        tmp_kvmap.m_bucket_arr = std::unique_ptr<Bucket[]>(
            ::new (std::nothrow) Bucket[new_ht_size]{}
        );

        if (!tmp_kvmap.m_bucket_arr) {
            std::cerr <<
                "KVMap::_rehash: operator new[] failed" << std::endl;
            this->m_errno = KVErrorCode::ERR_OPNEWARR;
            return JWKVMAP_FAILED;
        }
        tmp_kvmap.m_bucket_cnt = new_ht_size;

        for (
        auto
            row_it = this->_bucket_arr_begin();
            row_it != this->_bucket_arr_end();
            ++row_it
        ) {
            if (!row_it->size()) continue;
            for (
                auto it = row_it->m_chain_root.ln_next_p;
                it != &row_it->m_chain_root;
            ) {
                auto next_it = SlotHandle(it).next().link();
                int rc = tmp_kvmap._link_slot(SlotHandle(it).unlink());
                assert(!rc);
                it = next_it;
            }
        }

        *this = std::move(tmp_kvmap);
    }

    return JWKVMAP_OK;
}

int KVMap::insert(
    const buffer_view  &key_view_ref,
    const buffer_view  &val_view_ref,
    uint32_t            type
) {
    if (!this->is_init()) {
        std::cerr
            << LOG_ERROR "KVMap::insert failed: ht uninitialised"
            << std::endl;
        this->m_errno = KVErrorCode::ERR_UNINIT;
        return JWKVMAP_FAILED;
    }
    assert(this->bucket_count());

    LookupHandle luh{};
    int rc = this->_lookup_slot(
        key_view_ref,
        hash32(key_view_ref),
        &luh
    );

    if (rc == JWKVMAP_OK) {
        std::cerr
            << LOG_ERROR "KVMap::insert: _lookup_slot: key already exists"
            << std::endl;
        this->m_errno = KVErrorCode::ERR_DUPKEY;
        return JWKVMAP_FAILED;
    }

    Slot *slot_p = this->_create_slot(
        key_view_ref,
        val_view_ref,
        hash32(key_view_ref),
        type        
    );
    if (!slot_p) {
        std::cerr
            << LOG_ERROR "KVMap::insert: this->_create_entry failed: "
            << KVError(this->m_errno).strerror()
            << std::endl;
        this->m_errno = KVErrorCode::ERR_CREATE_ENT;
        return JWKVMAP_FAILED;
    }

    rc = this->_link_slot(slot_p);
    assert(!rc);

    return JWKVMAP_OK;
}

const KVMap::EntryHeader *KVMap::find(const buffer_view &key_view_ref) {
    if (!this->is_init()) {
        std::cerr
            << LOG_ERROR "KVMap::find failed: ht uninitialised"
            << std::endl;
        this->m_errno = KVErrorCode::ERR_UNINIT;
        return nullptr;
    }

    LookupHandle luh{};
    int rc = this->_lookup_slot(
        key_view_ref,
        hash32(key_view_ref),
        &luh
    );

    if (rc != JWKVMAP_OK) {
        std::cerr
            << LOG_ERROR "KVMap::find: this->_lookup_slot failed: "
            << KVError(this->m_errno).strerror() << std::endl;
        this->m_errno = KVErrorCode::ERR_LOOKUP;
        return nullptr;
    }

    return &luh.slot_p->hs_ehdr;
}

int KVMap::remove(const buffer_view &key_view_ref) {
    if (!this->is_init()) {
        std::cerr
            << LOG_ERROR "KVMap::remove failed: ht uninitialised"
            << std::endl;
        this->m_errno = KVErrorCode::ERR_UNINIT;
        return JWKVMAP_FAILED;
    }

    LookupHandle luh{};
    int rc = this->_lookup_slot(
        key_view_ref,
        hash32(key_view_ref),
        &luh
    );

    if (rc != JWKVMAP_OK) {
        std::cerr
            << LOG_ERROR "KVMap::remove: this->_lookup_slot failed: "
            << KVError(this->m_errno).strerror() << std::endl;
        this->m_errno = KVErrorCode::ERR_LOOKUP;
        return JWKVMAP_FAILED;
    }
   
    auto *slot_p = this->_unlink_slot(luh.bucket_p, luh.slot_p);
    if (!slot_p) {
        std::cerr
            << LOG_ERROR "KVMap::remove: this->_unlink_slot failed: "
            << KVError(this->m_errno).strerror() << std::endl;
        this->m_errno = KVErrorCode::ERR_UNLINK;
        return JWKVMAP_FAILED;
    }

    this->_destroy_slot(slot_p);
    return JWKVMAP_OK;
}


int KVMap::rehash(std::size_t new_ht_size) {
    return this->_rehash(new_ht_size);
}

int _kvmap_get_entry_view(
    const KVMap::EntryHeader   *ehdr_p,
    KVMap::c_EntryView         *out_entview_p
) {
    if (!ehdr_p || !out_entview_p) return JWKVMAP_FAILED;
    out_entview_p->ev_ehdr_p    = ehdr_p;
    out_entview_p->ev_key_p     = _priv::entry_get_key_payload(ehdr_p);
    out_entview_p->ev_val_p     = _priv::entry_get_val_payload(ehdr_p);

    return JWKVMAP_OK;
}



} // namespace jwkvmap