#include <kvsymdb.h>
#include <iostream>
#include <new>
#include <utility>
#include <memory>
#include <assert.h>
#include <stdint.h>
#include <array>
#include <fnv1a_hash.h>
#include <string.h>
#include <stdio.h>
#include "dbg_print.h"

constexpr uint32_t ENT_TYPE = 0xE10F;

struct cstr_kv {
    const char *name;
    const char *data;
};

template <typename T, size_t N>
constexpr size_t c_array_length(T (&)[N]){
    return N;
}

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


namespace cxx_class_utils {
    namespace memory {
        template<typename T>
        struct cleanup {
            void operator()(T *obj_p) {
                if (obj_p) obj_p->~T();
            }
        };

        template<typename T>
        struct buffer {
        private:
            alignas(T) uint8_t _buf[sizeof(T)];
        public:
            buffer() = default;
            ~buffer() = default; // does NOT call T::~T()
            void *mem() noexcept {
                return this->_buf;
            }
            template<typename... Args>
            T *obj_init(Args&&... ctor_args) {
                return ::new (this->_buf) T(std::forward<Args>(ctor_args)...);
            }
        };
    }
}


namespace kvsymdb_utils {
    inline void print_entry_view(cxx_kvsymdb::kvsymdb::entry_view &entview_r) {
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
            entview_r.id, entview_r.hash, entview_r.type,
            entview_r.name_len,
            static_cast<const char*>(entview_r.name),
            entview_r.data_len,
            static_cast<int>(entview_r.data_len),
            static_cast<const char*>(entview_r.data),
            entview_r._record
        );
    }
}

int main() {
    using namespace cxx_kvsymdb;
    using namespace cxx_class_utils;

    memory::buffer<cxx_kvsymdb::kvsymdb> _db_buf{};
    std::unique_ptr<kvsymdb, memory::cleanup<kvsymdb>> 
        dbp(new (_db_buf.mem()) cxx_kvsymdb::kvsymdb(1u));

    assert(dbp->is_init());

    int rc = dbp->reserve(32u);
    assert(!rc);
    rc = dbp->reserve_buffer(128u);
    assert(!rc);

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

    std::cout << 
        "2. -- get all entries (via raw buffer) --\n"
        "(DONT CRASH HERE!!!!!!)\n";
    { // unsafe; might crash
        kvsymdb::self_state dbv{};
        int rc = dbp->get_self_state(&dbv);
        if (!rc) {
            uint32_t pos = 0u;
            while (pos < dbv.buf_len) {
                auto ent_p =
                    reinterpret_cast<const kvsymdb::entry*>(dbv.arena_buf + pos);
                kvsymdb::entry_view ev{};
                int rc = dbp->get_entview(ent_p, &ev);
                if (rc) {
                    std::cerr << "get_entview failed: " << dbp->errmsg() << "\n";
                    dbp->clearerr();
                    continue;
                }
                kvsymdb_utils::print_entry_view(ev);
                pos += ent_p->record_len;
            }
            assert(pos == dbv.buf_len);
        } else {
            std::cerr << "dbp->insert failed: " << dbp->errmsg() << "\n";
            dbp->clearerr();
        }
    }
    std::cout << 
        "(IT DIDNT CRASH!!)\n\n";

    std::cout << "2. -- get all entries --\n";
    for (auto it = dbp->begin(); it != dbp->end(); ++it) {
        std::cout << "it\n";
        kvsymdb::entry &ent_ref = *it;
        kvsymdb::entry_view ev{};
        int rc = dbp->get_entview(
            std::addressof(ent_ref),
            std::addressof(ev)
        );
        if (rc) {
            std::cerr << "get_entview failed: " << dbp->errmsg() << "\n";
            dbp->clearerr();
            continue;
        }
        kvsymdb_utils::print_entry_view(ev);
    }
    std::cout << "\n";

    std::cout << "2.a. -- get all entries with for each loop --\n";
    for (kvsymdb::entry &ent_ref : *dbp) {
        std::cout << "it\n";
        kvsymdb::entry_view ev{};
        int rc = dbp->get_entview(
            std::addressof(ent_ref),
            std::addressof(ev)
        );
        if (rc) {
            std::cerr << "get_entview failed: " << dbp->errmsg() << "\n";
            dbp->clearerr();
            continue;
        }
        kvsymdb_utils::print_entry_view(ev);
    }
    std::cout << "\n";

    std::cout << "2.b. -- get all entries offset info --\n";
    for (kvsymdb::entry &ent_ref : *dbp) {
        kvsymdb::self_state st{};
        dbg_log_msg("#0");
        dbp->get_self_state(&st);
        std::cout << "Current state: \n\t"
            "(" << st.ent_count <<  ", " << st.ent_capacity << 
            ", " << st.buf_len << ", " << st.buf_size << ")\n";

        std::cout << "ent info: (" << 
            ent_ref.id
            << ", "
            << reinterpret_cast<uint8_t*>(&ent_ref) - st.arena_buf
            << ", "
            << reinterpret_cast<uint8_t*>(&ent_ref) - st.arena_buf + ent_ref.record_len
            << ")\n";
    }
    std::cout << "\n";

    std::cout << "3. -- unalive all entries logically --\n";
    for (auto &ent_ref : *dbp) {
        assert(dbp->is_valid_entry(std::addressof(ent_ref)));

        dbg_log_msg("#2");
        int rc = dbp->mark_dead(std::addressof(ent_ref));
    
        // this is safe because mark_dead does NOT move memory
        // it only toggles state flag
        dbg_log_msg("#3");
        assert(!dbp->is_valid_entry(std::addressof(ent_ref)));

        dbg_log_msg("#4");
        if (rc) {
            std::cerr << "dbp->mark_dead failed: " << dbp->errmsg() << "\n";
            dbp->clearerr();
            continue;
        }
    }
    std::cout << "\n";


    std::cout << "3. -- double mark dead to make sure it doesnt crash --\n";
    for (auto it = dbp->begin(); it != dbp->end(); it++) {
        kvsymdb::self_state st{};
        dbp->get_self_state(&st);
        kvsymdb::entry &ent_ref = dbp->deref(it);
        assert(
            static_cast<ptrdiff_t>(st.buf_len) ==
            reinterpret_cast<const uint8_t*>(
                kvsymdb_iterator_end(dbp->_raw())
            ) - st.arena_buf
        );

        dbg_log_msg("#1");
        int rc = dbp->mark_dead(std::addressof(ent_ref));
    
        // this is safe because mark_dead does NOT move memory
        // it only toggles state flag
        dbg_log_msg("#2");
        assert(!dbp->is_valid_entry(std::addressof(ent_ref)));

        dbg_log_msg("#3");
        if (rc) {
            std::cerr << "dbp->mark_dead failed: " << dbp->errmsg() << "\n";
            dbp->clearerr();
            continue;
        }
    }
    std::cout << "\n";


    std::cout << "???" << std::endl;
    {
        kvsymdb::self_state dbst{};
        dbp->get_self_state(&dbst);
        dbp->reserve(dbst.ent_capacity - 1u);
        dbp->reserve_buffer(dbst.buf_len - 128u);
    }
    std::cout << "???" << std::endl;


    std::cout << "4. -- get all DEAD BEEF entries --\n";
    for (auto it = dbp->begin(); it != dbp->end(); it = dbp->next(it)) {
        std::cout << "it\n";
        kvsymdb::entry &ent_ref = dbp->deref(it);
        // this is safe because mark_dead does NOT move memory
        // it only toggles state flag
        assert(!dbp->is_valid_entry(std::addressof(ent_ref)));
        std::cout << 
            "#DEADBEEF entry!!!\n";
        kvsymdb::entry_view ev{};
        int rc = dbp->get_entview(
            std::addressof(ent_ref),
            std::addressof(ev)
        );
        if (rc) {
            std::cerr << "get_entview failed: " << dbp->errmsg() << "\n";
            dbp->clearerr();
            continue;
        }
        kvsymdb_utils::print_entry_view(ev);
    }
    std::cout << "\n";

    std::cout << "1. -- insert all entries (again) --\n";
    for (auto i = 0u; i < rectbl_len; ++i) {
        std::cout << "it @ " << i << " \n";
        kvsymdb_strview key(rectbl[rectbl_len - i - 1].name), val(rectbl[rectbl_len - i - 1].data);
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

    // test move && gc
    auto dbp1(std::move(dbp));
    std::cout << "5. -- compaction --\n";
    rc = dbp1->compact();
    if (rc) {
        std::cerr << "dbp->compact failed: " << dbp1->errmsg() << '\n';
        return -1;
    }
    dbp = std::move(dbp1);

    //dbp.reset(); //it must crash

    std::cout << "6. -- get all DEAD BEEF entries --\n";
    for (auto it = dbp->begin(); it != dbp->end(); it = dbp->next(it)) {
        std::cout << "it\n";
        kvsymdb::entry &ent_ref = dbp->deref(it);
        // this is safe because mark_dead does NOT move memory
        // it only toggles state flag
        // assert(!dbp->is_valid_entry(std::addressof(ent_ref)));
        // std::cout << "#DEADBEEF entry!!!\n";
        kvsymdb::entry_view ev{};
        int rc = dbp->get_entview(
            std::addressof(ent_ref),
            std::addressof(ev)
        );
        if (rc) {
            std::cerr << "get_entview failed: " << dbp->errmsg() << "\n";
            dbp->clearerr();
            continue;
        }
        kvsymdb_utils::print_entry_view(ev);
    }
    std::cout << "\n";


    //dbp->~kvsymdb(); 

    return 0;
}
