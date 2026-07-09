#include <kvsymdb.h>
#include <iostream>
#include <new>
#include <memory>
#include <assert.h>
#include <stdint.h>
#include <array>
#include <fnv1a_hash.h>
#include <string.h>
#include <stdio.h>

constexpr uint32_t ENT_TYPE = 0xE10F;

struct cstr_kv {
    const char *name;
    const char *data;
};

template <typename T, size_t N>
constexpr size_t c_array_length(T (&)[N]){
    return N;
}

template <typename T>
struct object_buffer {
private:
    alignas(T) uint8_t _buf[sizeof(T)];
public:
    object_buffer() = default;
    ~object_buffer() = default;
    void *base() noexcept {
        return this->_buf;
    }
};

struct kvsymdb_strview : kvsymdb_bufview_t {
    kvsymdb_strview(const char *cstr) noexcept {
        this->data = static_cast<const void*>(cstr);
        this->size = strlen(cstr);
    }
    kvsymdb_bufview_t &get_base() noexcept {
        return *this;
    }
    uint32_t hash32() noexcept {
        return fnv_1a_hash32(this->data, this->size);   
    }
};


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
};
auto rectbl_len = c_array_length(rectbl);

int main() {
    object_buffer<cxx_kvsymdb::kvsymdb> _kvsymdb_buf;
    auto dbp = new (_kvsymdb_buf.base()) cxx_kvsymdb::kvsymdb(32u);
    assert(dbp->is_init());

    std::cout << "1. -- insert all entries --\n";
    for (auto i = 0u; i < rectbl_len; ++i) {
        std::cout << "it @ " << i << " \n";
        kvsymdb_strview key(rectbl[i].name), val(rectbl[i].data);
        int rc = dbp->insert(
            std::addressof(key),
            std::addressof(val),
            key.hash32(),
            ENT_TYPE
        );
        if (rc) {
            std::cerr << "dbp->insert failed: " << dbp->errmsg() << "\n";
            dbp->clearerr();
        }
    }
    std::cout << "\n";


    for (auto it = dbp->begin(); it != dbp->end(); it = dbp->next(it)) {
        std::cout << "it\n";
        const kvsymdb_entry_t &ent_ref = dbp->deref(it);
        kvsymdb_entview_t ev{};
        int rc = dbp->get_entview(
            std::addressof(ent_ref),
            std::addressof(ev)
        );
        if (rc) {
            std::cerr << "get_entview failed: " << dbp->errmsg() << "\n";
            dbp->clearerr();
            continue;
        }
        printf(
            "(\n\t%u,\n\t0x%.8X,\n\t0x%.4X,\n\t'%.*s',\n\t'%.*s',\n\t%p\n)\n",
            ev.id, ev.hash, ev.type,
            (int)ev.name_len, ev.name,
            (int)ev.data_len, (char*)ev.data,
            ev._record
        );
    }


    dbp->_kvsymdb_cleanup(); 

    return 0;
}
