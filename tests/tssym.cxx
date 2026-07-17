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

struct cstr_kv {
    const char *name;
    const char *data;
};

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
    inline void print_ent_kv(cxx_kvsymdb::kvsymdb::entry_view &entview_r) {
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
}


namespace array_utils {
    namespace c_array {
        template <typename ElemType, size_t N>
        constexpr size_t length(const ElemType (&)[N]){
            return N;
        }

        template <typename ElemType, size_t N>
        constexpr std::array<ElemType, N> make_std_array(
            const ElemType (&c_arr_ref)[N]
        ){
            std::array<ElemType, N> cxx_arr{};
            for (size_t i = 0ul; i < N; ++i)
                cxx_arr[i] = c_arr_ref[i];

            return cxx_arr;
        }
    }
}

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

auto rectbl = array_utils::c_array::make_std_array(_rectbl_c_arr);

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
        kvsymdb::entry_view ev(*dbp, ent_ref);
        assert(ev.is_init());
        kvsymdb_utils::print_ent_kv(ev);
    }
    std::cout << std::endl;

    {
        kvsymdb::hash_table hidx{};
        int rc = hidx.init(*dbp, rectbl.size());
        assert(!rc);

        for (const auto &ent_ref : *dbp) {
            int rc = hidx.insert(&ent_ref);
            assert(!rc);
        }

        for (const auto &ent_ref : *dbp) {
            kvsymdb::entry_view ev(*dbp, ent_ref);
            assert(ev.is_init());
            std::cout << "[" << ev.id << "] \n\t" << "key='" << ev.name << "'\n";
            
            kvsymdb::string_view key(ev.name, ev.name_len);
            auto ent_p = hidx.lookup(key);
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

        for (const auto &ent_ref : *dbp) {
            int rc = hidx.remove(&ent_ref);
            assert(!rc);
        }

        for (const auto &ent_ref : *dbp) {
            kvsymdb::entry_view ev(*dbp, ent_ref);
            assert(ev.is_init());
    
            kvsymdb::string_view key(ev.name, ev.name_len);
            auto ent_p = hidx.lookup(key);
            assert(!ent_p);
        }

        for (const auto &ent_ref : *dbp) {
            int rc = hidx.insert(&ent_ref);
            assert(!rc);
        }

        for (const auto &ent_ref : *dbp) {
            kvsymdb::entry_view ev(*dbp, ent_ref);
            assert(ev.is_init());
            std::cout << "[" << ev.id << "] \n\t" << "key='" << ev.name << "'\n";
            
            kvsymdb::string_view key(ev.name, ev.name_len);
            auto ent_p = hidx.lookup(key);
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
    }
    std::cout << std::endl;

    
    
    return 0;
}
