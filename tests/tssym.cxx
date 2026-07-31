#include <kvsymdb.h>
#include <iostream>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "dbg_print.h"

#include "class_utils.hpp"
#include "array_utils.hpp"
#include "string_utils.hpp"
#include "kvsymdb_print.hpp"

constexpr uint32_t ENT_TYPE = 0xE10F;


#include <vector>

struct cstr_kv {
    const char *name;
    const char *data;
};

static constexpr cstr_kv _rec_arr[] = {
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
    using namespace string_utils;
    using namespace array_utils;
    using kvsymdb_print::operator<<;


    constexpr auto rec_arr = c_array::make_array_view(_rec_arr);

    memory::buffer<kvsymdb> _db_buf{};
    memory::placement_unique_ptr<kvsymdb> dbp = _db_buf.make_unique(32u);

    assert(dbp->is_init());

    for (const auto &kv_ref : rec_arr) {
        kvsymdb::string_view key(kv_ref.name), val(kv_ref.data);
        int rc = dbp->insert(key, val, ENT_TYPE);
        if (rc) {
            dbg_log_msg("");
            std::cerr << "dbp->insert: " << dbp->errmsg() << "\n";
            dbp->clearerr();
        }
    }

    for (const auto &ent_ref : *dbp) {
        assert(dbp->is_valid_entry(&ent_ref));
        kvsymdb::entry_view ev(*dbp, &ent_ref);
        kvsymdb_print::print_ent_kv(ev);
    }
    std::cout << std::endl;

    for (auto &ent_ref : *dbp) {
        int rc = dbp->mark_dead(&ent_ref);
        assert(!rc);
    }
    std::cout << std::endl;

    for (const auto &ent_ref : *dbp) {
        if (!dbp->is_valid_entry(&ent_ref)) {
            std::cerr << "[DeadEntry]\n";
            continue;
        }
        kvsymdb::entry_view ev(*dbp, &ent_ref);
        kvsymdb_print::print_ent_kv(ev);
    }
    std::cout << std::endl;


    if (dbp->compact()) {
        std::cerr << "dbp->compact: " << dbp->errmsg() << "\n";
        dbp->clearerr();
    }

    for (const auto &kv_ref : rec_arr) {
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
        kvsymdb::entry_view ev(*dbp, &ent_ref);
        assert(ev.is_init());
        kvsymdb_print::print_ent_kv(ev);
    }
    std::cout << std::endl;

    {
        kvsymdb::hash_index hidx{};
        int rc = hidx.init(*dbp, rec_arr.length());
        assert(!rc);

        std::cout << "-- insert all entries --" << std::endl;
        for (const auto &ent_ref : *dbp) {
            int rc = hidx.insert(&ent_ref);
            assert(!rc);
        }

        std::cout << "-- print all entries --" << std::endl;
        for (const auto &ent_ref : *dbp) {
            kvsymdb::entry_view ev(*dbp, &ent_ref);
            assert(ev.is_init());
            std::cout <<
                "[" << ev.id << "]\t" <<
                "key='" <<
                kvsymdb::buffer_view{ev.name_len, ev.name} <<
                "'\n";
            
            kvsymdb::string_view key(ev.name, ev.name_len);
            auto ent_p = hidx[key];
            assert(ent_p);
            kvsymdb::entry_view ev1(*dbp, ent_p);
            assert(ev1.is_init());
            assert(&ent_ref == ent_p);

            std::cout <<
                "\tval='" <<
                kvsymdb::buffer_view{ev1.data_len, ev1.data} <<
                "'\n" << std::endl;
        }
        std::cout << std::endl;

        std::cout << "-- remove all entries --" << std::endl;
        for (const auto &ent_ref : *dbp) {
            int rc = hidx.remove(&ent_ref);
            assert(!rc);
        }

        std::cout << "-- get all entries (zero) --" << std::endl;
        for (const auto &ent_ref : *dbp) {
            kvsymdb::entry_view ev(*dbp, &ent_ref);
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
            kvsymdb::entry_view ev(*dbp, &ent_ref);
            assert(ev.is_init());

            kvsymdb::string_view key(ev.name, ev.name_len);
            std::cout << "[" << ev.id << "]\t" << "key='" << kvsymdb::string_view(ev.name, ev.name_len) << "'\n";            

            auto ent_p = hidx.get(key);
            if (!ent_p) {
                std::cerr << "hidx.get failed: " << hidx.errmsg() << std::endl;
            }
            kvsymdb::entry_view ev1(*dbp, ent_p);
            assert(ev1.is_init());
            assert(&ent_ref == ent_p);

            std::cout << "\tval='"
                << kvsymdb::buffer_view{ev1.data_len, ev1.data}
                << "'\n" << std::endl;
        }

        std::cout << std::endl;
    
        auto key = make_string_literal("readdir");
        kvsymdb::entry_view ev{};

        ev.from_entry(*dbp, hidx.get(kvsymdb::string_view(key.data(), key.length())));
        kvsymdb_print::print_ent_kv(ev);

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
                kvsymdb_print::print_ent_kv(ev);
                ent_p_vec.push_back(ep);
            }
            reader.rewind();

            std::cout << std::endl;
        } else {
            std::cerr << "kvsymdb::file_reader() failed: " << dbf.errmsg() << std::endl;
            dbf.clearerr();
        }
    }

    
    return 0;
}
