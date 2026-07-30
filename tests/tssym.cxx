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
#include <array>

constexpr uint32_t ENT_TYPE = 0xE10F;

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
            void *data() noexcept {
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
    using namespace cxx_kvsymdb;
    inline void print_entry_view(kvsymdb::entry_view &entview_r) {
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
    inline void print_ent_kv(kvsymdb::entry_view &entview_r) {
        printf(
            "[%u] (0x%.8X, '%.*s', '%.*s')\n",
            entview_r.id,
            entview_r.hash,
            static_cast<int>(entview_r.name_len),
            static_cast<const char*>(entview_r.name),
            static_cast<int>(entview_r.data_len),
            static_cast<const char*>(entview_r.data)
        );
    }
    inline std::ostream &operator<<(
        std::ostream               &of_ref,
        const kvsymdb::buffer_view &str_view_ref
    ) {
        return of_ref.write(
            static_cast<const char*>(str_view_ref.data),
            str_view_ref.size
        );
    }
}


namespace array_utils {
    namespace c_array {
        template <typename ElemType, size_t N>
        constexpr size_t length(const ElemType (&)[N]){
            return N;
        }

        template <typename ElemType, size_t N>
        std::array<ElemType, N> make_std_array(
            const ElemType (&c_arr_ref)[N]
        ){
            std::array<ElemType, N> cxx_arr{};
            for (size_t i = 0ul; i < N; ++i)
                cxx_arr[i] = c_arr_ref[i];

            return cxx_arr;
        }
    }
}

namespace string_utils {
    template<size_t N>
    struct string_literal {
    private:
        const char *m_data;
    public:
        constexpr string_literal(const char (&cstr_ref)[N]) noexcept 
            : m_data{cstr_ref}
        {  
        }
        constexpr size_t length() const noexcept {
            return N - 1;
        }
        constexpr const char *data()  const noexcept {
            return this->m_data;
        }
        ~string_literal() noexcept = default;
    };

    template<size_t N>
    constexpr string_literal<N> make_string_literal(const char (&cstr_ref)[N]) {
        return string_literal<N>(cstr_ref);
    }
}

struct cstr_kv {
    const char *name;
    const char *data;
};

cstr_kv _rectbl_c_arr[] = {
    {"open", "libc fcntl.h: open"},
    {"close", "libc unistd.h: close"},
    {"read", "libc unistd.h: read"},
    {"write", "libc unistd.h: write"},
    {"lseek", "libc unistd.h: lseek"},
    {"mmap", "libc sys/mman.h: mmap"},
    {"munmap", "libc sys/mman.h: munmap"},
    {"opendir", "libc dirent.h: opendir"},
    {"closedir", "libc dirent.h: closedir"},
    {"readdir", "libc dirent.h: readdir"},
    {"seekdir", "libc dirent.h: seekdir"},
    {"telldir", "libc dirent.h: telldir"},
    {"malloc", "libc stdlib.h: malloc"},
    {"calloc", "libc stdlib.h: calloc"},
    {"realloc", "libc stdlib.h: realloc"},
    {"free", "libc stdlib.h: free"},
    {"getdents64", "(x86-64_linux, GNU libc) sys/syscall.h: getdents64"},
    {"db_open", "(BSD) libdb db.h: db_open"},
    {"vsnprintf", "libc stdio.h: vsnprintf"},
    {"new", "libstdc++ new: operator new"},
    {"delete", "libstdc++ new: operator delete"},
};

const auto rectbl = array_utils::c_array::make_std_array(_rectbl_c_arr);

constexpr uint32_t STDIO_BUFSIZE = 8192u;

#include <vector>

int main() {
    //std::ios::sync_with_stdio(false);
    //std::cin.tie(nullptr);
    //std::cout.tie(nullptr);
    //std::cerr.tie(nullptr);
    setvbuf(stdout, nullptr, _IOFBF, STDIO_BUFSIZE);
    setvbuf(stderr, nullptr, _IOFBF, STDIO_BUFSIZE);

    using namespace cxx_kvsymdb;
    using namespace cxx_class_utils;
    using namespace string_utils;
    using kvsymdb_utils::operator<<;

    memory::buffer<kvsymdb> _db_buf{};
    std::unique_ptr<kvsymdb, memory::cleanup<kvsymdb>>
        dbp(new (_db_buf.data()) kvsymdb(rectbl.size()));

    assert(dbp->is_init());

    for (const auto &kv_ref : rectbl) {
        kvsymdb::string_view key(kv_ref.name), val(kv_ref.data);
        int rc = dbp->insert(key, val, ENT_TYPE);
        if (rc) {
            dbg_log_msg("");
            std::cerr << "dbp->insert: " << dbp->errmsg() << "\n";
            dbp->clearerr();
        }
    }

    for (const auto &ent_ref : *dbp) {
        if (!dbp->is_valid_entry(&ent_ref)) {
            std::cerr << "[DeadEntry]\n";
            continue;
        }
        kvsymdb::entry_view ev(*dbp, ent_ref);
        assert(ev.is_init());
        kvsymdb_utils::print_ent_kv(ev);
    }
    std::cout << std::endl;

    for (auto &ent_ref : *dbp) {
        int rc = dbp->mark_dead(&ent_ref);
        if (rc) {
            std::cerr << "mark_dead failed: " << dbp->errmsg() << "\n";
            dbp->clearerr();
        }
    }
    std::cout << std::endl;

    for (const auto &ent_ref : *dbp) {
        if (!dbp->is_valid_entry(&ent_ref)) {
            std::cerr << "[DeadEntry]\n";
            continue;
        }
        kvsymdb::entry_view ev(*dbp, ent_ref);
        assert(ev.is_init());
        kvsymdb_utils::print_ent_kv(ev);
    }
    std::cout << std::endl;


    if (dbp->compact()) {
        std::cerr << "dbp->compact: " << dbp->errmsg() << "\n";
        dbp->clearerr();
    }

    for (const auto &kv_ref : rectbl) {
        kvsymdb::string_view key(kv_ref.name), val(kv_ref.data);
        int rc = dbp->insert(key, val, ENT_TYPE);
        if (rc) {
            dbg_log_msg("");
            std::cerr << "dbp->insert: " << dbp->errmsg() << "\n";
            dbp->clearerr();
        }
    }

    for (const auto &ent_ref : *dbp) {
        if (!dbp->is_valid_entry(&ent_ref)) {
            std::cerr << "[DeadEntry]\n";
            continue;
        }
        kvsymdb::entry_view ev(*dbp, ent_ref);
        assert(ev.is_init());
        kvsymdb_utils::print_ent_kv(ev);
    }
    std::cout << std::endl;

    {
        kvsymdb::hash_index hidx{};
        int rc = hidx.init(*dbp, rectbl.size());
        assert(!rc);

        std::cout << "-- insert all entries --" << std::endl;
        for (const auto &ent_ref : *dbp) {
            int rc = hidx.insert(&ent_ref);
            assert(!rc);
        }

        std::cout << "-- print all entries --" << std::endl;
        for (const auto &ent_ref : *dbp) {
            kvsymdb::entry_view ev(*dbp, ent_ref);
            assert(ev.is_init());
            std::cout << "[" << ev.id << "] \n\t" << "key='" << ev.name << "'\n";
            
            kvsymdb::string_view key(ev.name, ev.name_len);
            auto ent_p = hidx[key];
            assert(ent_p);
            kvsymdb::entry_view ev1(*dbp, *ent_p);
            assert(ev1.is_init());
            assert(&ent_ref == ent_p);

            printf(
                "\tval='%.*s'\n",
                static_cast<int>(ev1.data_len),
                static_cast<const char*>(ev1.data)
            );
        }
        std::cout << std::endl;

        std::cout << "-- remove all entries --" << std::endl;
        for (const auto &ent_ref : *dbp) {
            int rc = hidx.remove(&ent_ref);
            assert(!rc);
        }

        std::cout << "-- get all entries (zero) --" << std::endl;
        for (const auto &ent_ref : *dbp) {
            kvsymdb::entry_view ev(*dbp, ent_ref);
            assert(ev.is_init());
            auto ent_p = hidx.get(kvsymdb::string_view(ev.name, ev.name_len));
            assert(!ent_p);
        }

        std::cout << "-- insert all entries again --" << std::endl;
        for (const auto &ent_ref : *dbp) {
            int rc = hidx.insert(&ent_ref);
            assert(!rc);
        }

        std::cout << "-- get all entries --" << std::endl;
        for (const auto &ent_ref : *dbp) {
            kvsymdb::entry_view ev(*dbp, ent_ref);
            assert(ev.is_init());
            std::cout << "[" << ev.id << "] \n\t" << "key='" << ev.name << "'\n";            

            kvsymdb::string_view key(ev.name, ev.name_len);
            auto ent_p = hidx.get(key);
            if (!ent_p) {
                std::cerr << "hidx.get failed: " << hidx.errmsg() << std::endl;
            }
            kvsymdb::entry_view ev1(*dbp, *ent_p);
            assert(ev1.is_init());
            assert(&ent_ref == ent_p);

            std::cout << "\tval='";
            std::cout.write(static_cast<const char*>(ev1.data), ev1.data_len);
            std::cout << "'\n";
        }

        std::cout << std::endl;
    
        auto key = make_string_literal("readdir");
        kvsymdb::entry_view ev{};

        ev.from_entry(*dbp, *hidx.get(kvsymdb::string_view(key.data(), key.length())));
        kvsymdb_utils::print_ent_kv(ev);

        std::cout << std::endl;
    }
    std::cout << std::endl;

    {
        kvsymdb::file_builder dbf(*dbp);

        int rc = dbf.dump("data.bin");
        if (rc) {
            std::cerr << "db.dump failed: " << dbf.errmsg() << std::endl;
            dbf.clearerr();
        }
    }

    std::cout << "reload testing:\n";
    {
        kvsymdb::file_reader dbf("data.bin");
        if (dbf.is_init()) {
            kvsymdb::reader reader(dbf);
            assert(reader.is_init());
            std::cout << "after reader init" << std::endl;

            auto *fhdr_p = dbf.get_file_header();

            uint32_t entc = fhdr_p->fh_entcnt;
            std::vector<const kvsymdb::entry*> ent_p_vec{};
            ent_p_vec.reserve(entc);
    
            const kvsymdb::entry *ep = nullptr;
            while ((ep = reader.read()) != nullptr) {
                kvsymdb::entry_view ev{};
                int rc = dbf.get_entry_view(ep, &ev);
                if (rc) {
                    std::cerr << "dbf.get_entry_view: " << dbf.errmsg() << "\n";
                    dbf.clearerr();
                    break;
                }
                kvsymdb_utils::print_ent_kv(ev);
                ent_p_vec.push_back(ep);
            }
            reader.rewind();

            std::cout << std::endl;
/*
            {
                kvsymdb::hash_index hidx{};
                if (hidx.init(dbf.base(), dbf.entc()) == KVSYMDB_SUCCESS) {
                    for (auto &ent_p_ref : ent_p_vec) {
                        if (hidx.insert(ent_p_ref)) {
                            std::cerr << "hidx.insert: " << hidx.errmsg() << std::endl;
                            hidx.clearerr();
                        }
                    }

                    for (auto &ent_p_ref : ent_p_vec) {
                        kvsymdb::entry_view ev(dbf.base(), *ent_p_ref);
                        assert(ev.is_init());

                        auto key = kvsymdb::string_view(ev.name, ev.name_len);

                        auto ent_p = hidx[key];
                        assert(ent_p);
                        
                        kvsymdb::entry_view ev1(dbf.base(), *ent_p);

                        auto val = kvsymdb::string_view(
                            static_cast<const char*>(ev1.data), ev1.data_len
                        );

                        std::cout << "[" << ev1.id << "]";
                        std::cout << "\tkey: \"" << key << "\"\n";
                        std::cout << "\tval: \"" << val << "\"\n" << std::endl;
                    }
                } else {
                    std::cerr << "hidx.init failed: " << hidx.errmsg() << std::endl;
                }
            }
*/

        } else {
            std::cerr << "kvsymdb::file_reader() failed: " << dbf.errmsg() << std::endl;
            dbf.clearerr();
        }
    }

    
    return 0;
}
