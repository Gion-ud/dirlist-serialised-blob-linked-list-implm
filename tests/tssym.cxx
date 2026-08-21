#include <kvsymdb.h>
#include <iostream>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "dbg_print.h"

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

namespace tssym {
using namespace cxx_kvsymdb;
using namespace array_utils;

template<size_t Nkv>
inline bool insert_from_kv_arr(
    kvsymdb         &db_ref,
    const cstr_kv   (&c_kv_arr_ref)[Nkv]
) noexcept {
    const auto kv_arr_view = c_array::make_array_view(c_kv_arr_ref);

    for (const auto &kv_ref : kv_arr_view) {
        int rc = db_ref.insert(
            kv_ref.name,
            kvsymdb::string_view(kv_ref.data),
            ENT_TYPE
        );
        if (rc != KVSYMDB_OK) {
            std::cerr
                << "db_ref.insert failed: "
                << db_ref.errmsg() << std::endl;
            return false;
        }
    }

    return true;
}

inline void iter_and_print_all(kvsymdb &db_ref) noexcept {
    using kvsymdb_print::operator<<;

    for (const auto &ent_ref : db_ref) {
        auto ev = kvsymdb::entry_view(db_ref, &ent_ref);
        assert(ev.is_init());
        if (!db_ref.is_valid_entry(&ent_ref)) continue;
        kvsymdb_print::print_ent_kv(ev);
    }
}

inline void read_and_print_all_chked(const kvsymdb &db_ref) noexcept {
    using kvsymdb_print::operator<<;

    kvsymdb::reader reader(db_ref);
    assert(reader.is_init());

    decltype(reader.read()) ent_p = nullptr;
    while ((ent_p = reader.read()) != nullptr) {
        if (!db_ref.is_valid_entry(ent_p)) continue;
        auto ev = kvsymdb::entry_view(db_ref, ent_p);
        assert(ev.is_init());
        kvsymdb_print::print_ent_kv(ev);
    }
}

inline void iter_and_delete_all(kvsymdb &db_ref) noexcept {
    using kvsymdb_print::operator<<;

    for (auto &ent_ref : db_ref) {
        int rc = db_ref.mark_dead(&ent_ref);
        assert(rc == KVSYMDB_OK);
    }
}

}

int main() {
    //std::ios::sync_with_stdio(false);
    //std::cin.tie(nullptr);
    //std::cout.tie(nullptr);
    //std::cerr.tie(nullptr);
    setvbuf(stdout, nullptr, _IOFBF, STDIO_BUFSIZE);
    setvbuf(stderr, nullptr, _IOFBF, STDIO_BUFSIZE);

    using kvsymdb_print::operator<<;
    using namespace string_utils;
    using namespace array_utils;
    using namespace tssym;

    constexpr auto kv_arr = c_array::make_array_view(_rec_arr);

{
    kvsymdb db(0u);
    assert(db.is_init());

    dbg_log_msg("#1");
    int rc = db.reserve(kv_arr.length());
    if (rc != KVSYMDB_OK) {
        std::cerr << db.errmsg() << std::endl;
        return -1;
    }

    dbg_log_msg("#2");
    bool res_b = insert_from_kv_arr(db, _rec_arr);
    assert(res_b);

    dbg_log_msg("#3");
    iter_and_print_all(db);

    dbg_log_msg("#4");
    read_and_print_all_chked(db);

    dbg_log_msg("#5");
    iter_and_delete_all(db);

    dbg_log_msg("#6");
    db.reserve(kv_arr.length() * 2);

    dbg_log_msg("#7");
    res_b = insert_from_kv_arr(db, _rec_arr);
    assert(res_b);

    dbg_log_msg("#8");
    read_and_print_all_chked(db);

    rc = db.compact();
    assert(rc == KVSYMDB_OK);

    dbg_log_msg("#9");
    read_and_print_all_chked(db);

    dbg_log_msg("#10");
    kvsymdb::file_builder dbof(db);
    rc = dbof.dump("kvdb.bin");
    assert(rc == KVSYMDB_OK);
}

    kvsymdb::file_mapper db_mpr("kvdb.bin");
    assert(db_mpr.is_init());

    kvsymdb::reader db_rdr(db_mpr);
    assert(db_rdr.is_init());

    dbg_log_msg("# reload 1");
    decltype(db_rdr.read()) ent_p = nullptr;
    while ((ent_p = db_rdr.read()) != nullptr) {
        kvsymdb::entry_view ev{};
        int rc = db_mpr.get_entry_view(ent_p, &ev);
        assert(rc == KVSYMDB_OK);
        kvsymdb_print::print_ent_kv(ev);
    }

    db_rdr.rewind();
    dbg_log_msg("# reload 2 after rewind");
    while ((ent_p = db_rdr.read()) != nullptr) {
        kvsymdb::entry_view ev{};
        int rc = db_mpr.get_entry_view(ent_p, &ev);
        assert(rc == KVSYMDB_OK);
        kvsymdb_print::print_ent_kv(ev);
    }

    return 0;
}
