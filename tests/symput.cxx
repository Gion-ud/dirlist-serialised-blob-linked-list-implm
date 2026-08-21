// c++11, c++14

#include <kvsymdb.h>
#include <iostream>
#include <assert.h>
#include <array>
#include "string_utils.hpp"

#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <errno.h>

/*
#include <stdio.h>
#include "dbg_print.h"
#include "class_utils.hpp"
#include "array_utils.hpp"
*/

#include <unistd.h>
#define PROG_NAME "symput"


constexpr auto usage_msg = string_utils::make_string_literal(
    "USAGE: " PROG_NAME " <filename> <key> <value>\n"
);

constexpr const size_t DB_FILE_NAME_MAX = 256UL;

int main(int argc, char *argv[]) {
    using namespace cxx_kvsymdb;
    using string_utils::operator<<;

    int opt = 0;
    while ((opt = getopt(argc, argv, "hf:k:v:")) != -1) {
        switch (opt) {
            case 'h':
                std::cout << usage_msg << std::endl;
                return 0;
            case '?':
                std::cerr << usage_msg << std::endl;
                return -1;
        }
    }

    if (argc != 4) {
        std::cerr
            << "[Error] Invalid argc: 4 expected but "
            << argc
            << " was given!\n"
            << usage_msg << std::endl;
        return -1;
    }

    const char *opt_filename    = argv[1];
    const char *opt_key         = argv[2];
    const char *opt_value       = argv[3];

    if (strlen(opt_filename) > DB_FILE_NAME_MAX) {
        std::cerr
            << "[Error] db filename length exceeding "
            << DB_FILE_NAME_MAX << std::endl;
        return -1;
    }

    std::array<char, DB_FILE_NAME_MAX * 2> tmp_filename_buf{};
    memset(tmp_filename_buf.data(), 0, tmp_filename_buf.size());

    auto t = time(nullptr);
    int nwrite = snprintf(
        tmp_filename_buf.data(),
        tmp_filename_buf.size(),
        "%.8x_%s.tmp",
        fnv_1a_hash32(&t, sizeof(t)),
        opt_filename
    );
    assert(nwrite < static_cast<int>(tmp_filename_buf.size()));

    const char *new_filename = tmp_filename_buf.data();

    {
        kvsymdb::file_mapper dbif(opt_filename);
        if (!dbif.is_init()) {
            std::cerr
                << "kvsymdb::file_mapper(): "
                << dbif.errmsg() << std::endl;
            return -1;
        }

        kvsymdb db1(dbif.entc() + 1);
        if (!db1.is_init()) {
            std::cerr << "kvsymdb(): " << db1.errmsg() << std::endl;
            return -1;
        }

        {
            kvsymdb::reader db_reader(dbif);
            if (!db_reader.is_init()) {
                std::cerr << "kvsymdb::reader(): " << dbif.errmsg() << std::endl;
                return -1;
            }

            const kvsymdb::entry *ent_p = nullptr;
            while ((ent_p = db_reader.read()) != nullptr) {
                kvsymdb::entry_view ev{};
                int rc = dbif.get_entry_view(ent_p, &ev);
                if (rc) {
                    std::cerr << "dbif.get_entry_view(): " << dbif.errmsg() << std::endl;
                    return -1;
                }

                if (strcmp(ev.name, opt_key) == 0) continue; // this will be reinserted
                rc = db1.insert(ev);
                if (rc) {
                    std::cerr << "dbif.get_entry_view(): " << dbif.errmsg() << std::endl;
                    return -1;
                }
            }
            db_reader.rewind();
        }

        int rc = db1.insert(
            kvsymdb::string_view(opt_key),
            kvsymdb::string_view(opt_value),
            0xEF0F
        );
        if (rc) {
            std::cerr << "db1.insert(): " << db1.errmsg() << std::endl;
            return -1;
        }

        kvsymdb::file_builder new_dbf(db1);
        rc = new_dbf.dump(new_filename);
        if (rc) {
            std::cerr << "new_dbf.dump(): " << new_dbf.errmsg() << std::endl;
            return -1;
        }
    }

    if (remove(opt_filename) == -1) {
        std::cerr << "remove: " << strerror(errno) << std::endl;
        return errno;
    }

    if (rename(new_filename, opt_filename) == -1) {
        std::cerr << "rename: " << strerror(errno) << std::endl;
        return errno;
    }

    return 0;
}
