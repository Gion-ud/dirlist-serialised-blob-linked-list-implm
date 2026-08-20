#include <kvsymdb.h>
#include <iostream>
#include <new>
#include <utility>
#include <memory>
#include <assert.h>
#include <stdint.h>
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
            T *obj_init(Args&&... ctor_args_rref) {
                return ::new (this->_buf) T(std::forward<Args>(ctor_args_rref)...);
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
    inline void iterate_and_print_via_raw_ptr(cxx_kvsymdb::kvsymdb &symdb_ref) {
        using namespace cxx_kvsymdb;
        std::cout << 
            "-- cxx_kvsymdb::kvsymdb: get all entries (via raw buffer) --\n"
            "(DONT CRASH HERE!!!!!!)\n";
        { // unsafe; might crash
            kvsymdb::self_state db_view{};
            int rc = symdb_ref.get_self_state(&db_view);
            if (!rc) {
                uint32_t pos = 0u;
                while (pos < db_view.buf_len) {
                    auto ent_p = reinterpret_cast<const kvsymdb::entry*>(db_view.arena_buf + pos);
                    kvsymdb::entry_view ev{};
                    int rc = symdb_ref.get_entry_view(ent_p, &ev);
                    if (rc) {
                        std::cerr << "get_entry_view failed: " << symdb_ref.errmsg() << "\n";
                        symdb_ref.clearerr();
                        continue;
                    }
                    kvsymdb_utils::print_entry_view(ev);
                    pos += ent_p->record_len;
                }
                assert(pos == db_view.buf_len);
            } else {
                std::cerr << "dbp->insert failed: " << symdb_ref.errmsg() << "\n";
                symdb_ref.clearerr();
            }
        }
        std::cout <<  "(IT DIDNT CRASH!!)\n\n";
    }
}

constexpr uint32_t STDIO_BUFSIZE = 8192u;

int main() {
    //std::ios::sync_with_stdio(false);
    //std::cin.tie(nullptr);
    //std::cout.tie(nullptr);
    //std::cerr.tie(nullptr);
    setvbuf(stdout, nullptr, _IOFBF, STDIO_BUFSIZE);
    setvbuf(stderr, nullptr, _IOFBF, STDIO_BUFSIZE);

    using namespace cxx_kvsymdb;
    using namespace cxx_class_utils;

    memory::buffer<kvsymdb> _db_buf{};
    std::unique_ptr<kvsymdb, memory::cleanup<kvsymdb>> 
        dbp(new (_db_buf.mem()) kvsymdb(1u));

    assert(dbp->is_init());

    int rc = dbp->reserve(32u);
    assert(!rc);
    rc = dbp->reserve_buffer(1024u);
    assert(!rc);

    std::cout << "1. -- insert all entries --\n";
    for (auto i = 0u; i < rectbl_len; ++i) {
        std::cout << "it @ " << i << " \n";
        kvsymdb::string_view key(rectbl[i].name), val(rectbl[i].data);
        int rc = dbp->insert(key, val, ENT_TYPE);
        if (rc) {
            std::cerr << "dbp->insert failed: " << dbp->errmsg() << "\n";
            dbp->clearerr();
        }
    }
    std::cout << "\n";

    kvsymdb_utils::iterate_and_print_via_raw_ptr(*dbp);

    std::cout << "2. -- get all entries --\n";
    for (auto it = dbp->begin(); it != dbp->end(); ++it) {
        std::cout << "it\n";
        kvsymdb::entry& ent_ref = *it;
        kvsymdb::entry_view ev{};
        int rc = dbp->get_entry_view(&ent_ref, &ev);
        if (rc) {
            std::cerr << "get_entry_view failed: " << dbp->errmsg() << "\n";
            dbp->clearerr();
            continue;
        }
        kvsymdb_utils::print_entry_view(ev);
    }
    std::cout << "\n";

    std::cout << "2.a. -- get all entries with for each loop --\n";
    for (kvsymdb::entry& ent_ref : *dbp) {
        std::cout << "it\n";
        kvsymdb::entry_view ev{};
        int rc = dbp->get_entry_view(&ent_ref, &ev);
        if (rc) {
            std::cerr << "get_entry_view failed: " << dbp->errmsg() << "\n";
            dbp->clearerr();
            continue;
        }
        kvsymdb_utils::print_entry_view(ev);
    }
    std::cout << "\n";

    std::cout << "2.b. -- get all entries offset info --\n";
    for (kvsymdb::entry& ent_ref : *dbp) {
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
    for (auto& ent_ref : *dbp) {
        assert(dbp->is_valid_entry(&ent_ref));

        dbg_log_msg("#2");
        int rc = dbp->mark_dead(std::addressof(ent_ref));
    
        // this is safe because mark_dead does NOT move memory
        // it only toggles state flag
        dbg_log_msg("#3");
        assert(!dbp->is_valid_entry(&ent_ref));

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
        kvsymdb::entry& ent_ref = dbp->deref(it);
        assert(
            static_cast<ptrdiff_t>(st.buf_len) ==
            reinterpret_cast<const uint8_t*>(
                kvsymdb_iterator_end(dbp->_raw())
            ) - st.arena_buf
        );

        dbg_log_msg("#1");
        int rc = dbp->mark_dead(&ent_ref);
    
        // this is safe because mark_dead does NOT move memory
        // it only toggles state flag
        dbg_log_msg("#2");
        assert(!dbp->is_valid_entry(&ent_ref));

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
        kvsymdb::entry& ent_ref = dbp->deref(it);
        // this is safe because mark_dead does NOT move memory
        // it only toggles state flag
        assert(!dbp->is_valid_entry(&ent_ref));
        std::cout << 
            "#DEADBEEF entry!!!\n";
        kvsymdb::entry_view ev{};
        int rc = dbp->get_entry_view(&ent_ref, &ev);
        if (rc) {
            std::cerr << "get_entry_view failed: " << dbp->errmsg() << "\n";
            dbp->clearerr();
            continue;
        }
        kvsymdb_utils::print_entry_view(ev);
    }
    std::cout << "\n";

    std::cout << "1. -- insert all entries (again) --\n";
    for (auto i = 0u; i < rectbl_len; ++i) {
        std::cout << "it @ " << i << " \n";
        kvsymdb::string_view key(rectbl[rectbl_len - i - 1].name), val(rectbl[rectbl_len - i - 1].data);
        int rc = dbp->insert(key, val, ENT_TYPE);
        if (rc) {
            std::cerr << "dbp->insert failed: " << dbp->errmsg() << "\n";
            dbp->clearerr();
        }
    }

    // test move && gc

    kvsymdb db1(std::move(*dbp.get()));
    assert(db1.is_init());
    assert(!dbp->is_init());
    std::cout << "5. -- compaction --\n";
    rc = db1.compact();
    if (rc) {
        std::cerr << "db1.compact failed: " << db1.errmsg() << '\n';
        return -1;
    }
    *dbp = std::move(db1);
    assert(dbp->is_init());
    assert(!db1.is_init());

    // test weird move
    {
        kvsymdb db1(std::move(*dbp.get()));
        *dbp = std::move(db1);
    }

    //dbp.reset(); //it must crash

    std::cout << "6. -- get all entries --\n";
    for (auto it = dbp->begin(); it != dbp->end(); it = dbp->next(it)) {
        std::cout << "it\n";
        kvsymdb::entry& ent_ref = dbp->deref(it);
        // this is safe because mark_dead does NOT move memory
        // it only toggles state flag
        // assert(!dbp->is_valid_entry(std::addressof(ent_ref)));
        // std::cout << "#DEADBEEF entry!!!\n";
        kvsymdb::entry_view ev{};
        int rc = dbp->get_entry_view(&ent_ref, &ev);
        if (rc) {
            std::cerr << "get_entry_view failed: " << dbp->errmsg() << "\n";
            dbp->clearerr();
            continue;
        }
        kvsymdb_utils::print_entry_view(ev);
    }
    std::cout << "\n";

    std::cout << "7. -- testing reader --\n\n";
    {
        std::cout << "SCOPE_BEGIN\n";
        kvsymdb::reader reader(*dbp);
        assert(reader.is_init());

        const kvsymdb::entry *ent_p = nullptr;
        /*
        if (!reader.read()) {
            std::cerr << "reader.read failed: " << dbp->errmsg() << '\n';
        }
        */
        while ((ent_p = reader.read()) != nullptr) {
            std::cout << "it\n";
            kvsymdb::entry_view ev{};
            int rc = dbp->get_entry_view(ent_p, &ev);
            assert(!rc);
            kvsymdb_utils::print_entry_view(ev);
        }

        std::cout << "\n-- reader rewind and now redo --\n";
        reader.rewind();
        while ((ent_p = reader.read()) != nullptr) {
            std::cout << "it\n";
            kvsymdb::entry_view ev{};
            int rc = dbp->get_entry_view(ent_p, &ev);
            assert(!rc);
            kvsymdb_utils::print_entry_view(ev);
        }

        std::cout << "SCOPE_END\n";
    }


    std::cout << "------ test hash table ------" << std::endl;
    {
        kvsymdb::self_state dbst{};
        int rc = dbp->get_self_state(&dbst);
        assert(!rc);
    
        kvsymdb::hash_index hidx{};
        rc = hidx.init(*dbp, dbst.ent_capacity);
        assert(!rc);

        std::cout << "\n------ insert all entries ------" << std::endl;
        for (auto &ent_ref : *dbp) {
            rc = hidx.insert(&ent_ref);
            if (rc) {
                std::cerr << "lookup_result failed: " << hidx.errmsg() << std::endl;
                hidx.clearerr();
                break;
            }
        }

        std::cout << "\n------ get all entries ------" << std::endl;
        for (auto &ent_ref : *dbp) {
            kvsymdb::entry_view ent_view{};
            int rc = dbp->get_entry_view(&ent_ref, &ent_view);
            if (rc) {
                std::cerr << "dbp->get_entry_view failed: " << dbp->errmsg() << std::endl;
                dbp->clearerr();
                break;
            }
            kvsymdb::string_view key(ent_view.name, ent_view.name_len);
            auto *ent_p = hidx.get(key);
            if (!ent_p) {
                std::cerr << "hidx.lookup failed: " << hidx.errmsg() << std::endl;
                hidx.clearerr();
                break;
            }
            rc = dbp->get_entry_view(ent_p, &ent_view);
            if (rc) {
                std::cerr << "dbp->get_entry_view failed: " << dbp->errmsg() << std::endl;
                dbp->clearerr();
                break;
            }
            kvsymdb_utils::print_entry_view(ent_view);
        }

        std::cout << "\n------ delete all entries ------" << std::endl;
        for (auto &ent_ref : *dbp) {
            int rc = hidx.remove(&ent_ref);
            if (rc) {
                std::cerr << "hidx.remove failed: " << hidx.errmsg() << std::endl;
                hidx.clearerr();
                break;
            }
        }

        std::cout << "\n------ get all entries ------" << std::endl;
        for (auto &ent_ref : *dbp) {
            kvsymdb::entry_view ent_view{};
            int rc = dbp->get_entry_view(&ent_ref, &ent_view);
            if (rc) {
                std::cerr << "dbp->get_entry_view failed: " << dbp->errmsg() << std::endl;
                dbp->clearerr();
                break;
            }
            kvsymdb::string_view key(ent_view.name, ent_view.name_len);
            auto *ent_p = hidx.get(key);
            if (!ent_p) {
                std::cerr << "hidx.lookup failed: " << hidx.errmsg() << std::endl;
                hidx.clearerr();
                break;
            }
            rc = dbp->get_entry_view(ent_p, &ent_view);
            if (rc) {
                std::cerr << "dbp->get_entry_view failed: " << dbp->errmsg() << std::endl;
                dbp->clearerr();
                break;
            }
            kvsymdb_utils::print_entry_view(ent_view);
        }

        std::cout << "\n------ insert all entries AGAIN ------" << std::endl;
        for (auto &ent_ref : *dbp) {
            rc = hidx.insert(&ent_ref);
            if (rc) {
                std::cerr << "lookup_result failed: " << hidx.errmsg() << std::endl;
                hidx.clearerr();
                break;
            }
        }

        std::cout << "\n------ get all entries ------" << std::endl;
        for (auto &ent_ref : *dbp) {
            kvsymdb::entry_view ent_view{};
            int rc = dbp->get_entry_view(&ent_ref, &ent_view);
            if (rc) {
                std::cerr << "dbp->get_entry_view failed: " << dbp->errmsg() << std::endl;
                dbp->clearerr();
                break;
            }
            kvsymdb::string_view key(ent_view.name, ent_view.name_len);
            auto *ent_p = hidx.get(key);
            if (!ent_p) {
                std::cerr << "hidx.lookup failed: " << hidx.errmsg() << std::endl;
                hidx.clearerr();
                break;
            }
            rc = dbp->get_entry_view(ent_p, &ent_view);
            if (rc) {
                std::cerr << "dbp->get_entry_view failed: " << dbp->errmsg() << std::endl;
                dbp->clearerr();
                break;
            }
            kvsymdb_utils::print_entry_view(ent_view);
        }

        std::cout << "\n------ delete all entries ------" << std::endl;
        for (auto &ent_ref : *dbp) {
            int rc = hidx.remove(&ent_ref);
            if (rc) {
                std::cerr << "hidx.remove failed: " << hidx.errmsg() << std::endl;
                hidx.clearerr();
                break;
            }
        }
    }

    //dbp->~kvsymdb(); 
    return 0;
}
