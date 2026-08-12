// c++11, c++14

#include <kvsymdb.h>
#include <iostream>
#include <assert.h>
#include "string_utils.hpp"

#include <stdlib.h>
#include <limits.h>
#include <time.h>


#include <unistd.h>
#define PROG_NAME "mksym"

struct CXX_FILE {
private:
    FILE   *m_fp = nullptr;
public:
    CXX_FILE(FILE *fp) noexcept : m_fp(fp) {}
    bool is_open() const noexcept {
        return (!!m_fp);
    }
    FILE *get() noexcept {
        return m_fp;
    }
    ~CXX_FILE() noexcept {
        if (m_fp) {
            fclose(m_fp);
            m_fp = nullptr;
        }
    }
    CXX_FILE(const CXX_FILE &) = delete;
    CXX_FILE &operator=(const CXX_FILE &) = delete;
};

constexpr auto usage_msg = string_utils::make_string_literal(
    "USAGE: " PROG_NAME " -f <filename>\n"
);

constexpr const size_t DB_FILE_NAME_MAX = 256UL;

int main(int argc, char *argv[]) {
    using namespace cxx_kvsymdb;
    using string_utils::operator<<;

    const char *opt_f_filename  = nullptr;

    int opt = 0;
    while ((opt = getopt(argc, argv, "hf:")) != -1) {
        switch (opt) {
            case 'h':
                std::cout << usage_msg << std::endl;
                return 0;
            case 'f':
                opt_f_filename = optarg;
                break;
            case '?':
                std::cerr << usage_msg << std::endl;
                return -1;
        }
    }

    if (!opt_f_filename) {
        std::cerr << usage_msg << std::endl;
        return -1;
    }

    if (strlen(opt_f_filename) > DB_FILE_NAME_MAX) {
        std::cerr
            << "[Error] db filename length exceeding "
            << DB_FILE_NAME_MAX << std::endl;
        return -1;
    }


    {
        kvsymdb db1(0u);
        if (!db1.is_init()) {
            std::cerr << "kvsymdb(): " << db1.errmsg() << std::endl;
            return -1;
        }

        kvsymdb::file_builder new_dbf(db1);
        int rc = new_dbf.dump(opt_f_filename);
        if (rc) {
            std::cerr << "new_dbf.dump(): " << new_dbf.errmsg() << std::endl;
            return -1;
        }
    }


    return 0;
}
