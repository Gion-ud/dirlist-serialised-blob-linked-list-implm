// c++11, c++14

#include <kvsymdb.h>
#include <iostream>
#include <assert.h>
#include <stdint.h>
#include <vector>
#include "string_utils.hpp"

#include <stdlib.h>
#include <limits.h>

/*
#include <stdio.h>
#include "dbg_print.h"
#include "class_utils.hpp"
#include "array_utils.hpp"
*/

#include "kvsymdb_print.hpp"
#include <getopt.h>
#include <unistd.h>


#define PROG_NAME "readsym"

int main(int argc, char *argv[]) {
    struct jw_cxx_opt_arg {
        const char *opt_arg     = nullptr;
        bool        opt_used    = false;
        jw_cxx_opt_arg() noexcept {
        }
        jw_cxx_opt_arg(const char *arg_cstr) noexcept :
            opt_arg(arg_cstr),
            opt_used(true)
        {
        }
        void setopt(const char *arg) noexcept {
            this->opt_arg   = arg;
            this->opt_used  = true;
        }
    }
        opt_file{},
        opt_id{},
        opt_key{},
        opt_type{};

    constexpr static struct option long_opt_tbl[] = {
        {"file",        required_argument,  0,  'f'},
        {"id",          required_argument,  0,  'e'},
        {"key",         required_argument,  0,  'k'},
        {"type",        required_argument,  0,  't'},
        {"help",        no_argument,        0,  'h'},
        {"info",        no_argument,        0,  'I'},
        {"keys-only",   no_argument,        0,  'K'},
        {"kv-pairs",    no_argument,        0,  'P'},
        {0, 0, 0, 0}
    };

    constexpr auto usage_msg =
        string_utils::make_string_literal(
            "Usage: " PROG_NAME
            " -f <filename> [option]\n\n"
            "Options:\n"
            "\t--file       <filename>\n"
            "\t--id         <id>\n"
            "\t--key        <name>\n"
            "\t--type       <type>\n"
            "\t--help\n"
            "\t--info\n"
            "\t--key-only\n"
            "\t--kv-pair\n"
        );

    bool is_opt_info        = false;
    bool is_opt_key_only    = false;
    bool is_opt_kv_pair     = false;

    int opt = 0;
    while (
        (opt = getopt_long(
            argc,
            argv,
            "f:e:k:t:hIKP",
            long_opt_tbl,
            nullptr
        )) != -1
    ) {
        switch (opt) {
            case 'f':
                opt_file.setopt(optarg);
                break;
            case 'e':
                opt_id.setopt(optarg);
                break;
            case 'k':
                opt_key.setopt(optarg);
                break;
            case 't':
                opt_type.setopt(optarg);
                break;
            case 'h': {
                using string_utils::operator<<;
                std::cout << usage_msg << std::endl;
                return 0;
            }
            case 'I':
                is_opt_info = true;
                break;
            case 'K':
                is_opt_key_only = true;
                break;
            case 'P':
                is_opt_kv_pair = true;
                break;
            case '?': {
                using string_utils::operator<<;
                std::cout << usage_msg << std::endl;
                return 0;
            }
        }
    }

    if (!opt_file.opt_arg) {
        std::cerr
            << "[ERROR] you must provide a file"
            << std::endl;
        return -1;
    }

    using namespace cxx_kvsymdb;
    kvsymdb::file_reader dbf(opt_file.opt_arg);

    if (!dbf.is_init()) {
        std::cerr
            << "kvsymdb::file_reader() failed:"
            << dbf.errmsg() << std::endl;

        return -1;
    }

    if (is_opt_info) {
        auto *fhdr_p = dbf.get_file_header();
        std::printf(
            "file_header:\n"
            "\tmagic\t: 0x%.8x\n"
            "\tversion\t: 0x%.4x\n"
            "\talign\t: 0x%.4x\n"
            "\tentcnt\t: %u\n"
            "\tbuflen\t: %u\n"
            "\tcrc32\t: 0x%.8x\n\n",
            fhdr_p->fh_magic,
            fhdr_p->fh_version,
            fhdr_p->fh_align,
            fhdr_p->fh_entcnt,
            fhdr_p->fh_buflen,
            fhdr_p->fh_crc32
        );
        std::fflush(stdout);
        return 0;
    }

    if (is_opt_key_only && is_opt_kv_pair) {
        std::cerr <<
            "[WARN] Cannot have both --key-only and --kv-pair; --kv-pair ignored"
            << std::endl;
        is_opt_kv_pair = false;
    }

    if (opt_id.opt_used && opt_key.opt_used) {
        std::cerr <<
            "[WARN] Cannot have both --id <id> and --key <key>; --key ignored"
            << std::endl;
        opt_key.opt_arg     = nullptr;
        opt_key.opt_used    = false;
    }

    kvsymdb::reader reader(dbf);
    if (!reader.is_init()) {
        std::cerr
            << "kvsymdb::reader() failed: "
            << reader.errmsg() << std::endl;
        return -1;
    }

    using ent_ptr_t = decltype(reader.read());
    std::vector<ent_ptr_t> ent_p_vec{};
    ent_p_vec.reserve(dbf.entc());

    auto compare_string_view = [](
        const string_utils::string_view  &view1_ref,
        const string_utils::string_view  &view2_ref
    ) -> int {
        if (view1_ref.length() > view2_ref.length())
            return 1;
        else if (view1_ref.length() < view2_ref.length())
            return -1;

        return memcmp(view1_ref.data(), view2_ref.data(), view1_ref.length());
    };

    auto cstr_to_u32 = [](
        const char *cstr,
        uint32_t   *out_val_p
    ) -> bool {
        if (!cstr || !out_val_p) return false;

        char *end_p = nullptr;
        auto res = std::strtoul(cstr, &end_p, 10);

        if (cstr == end_p) {
            std::cerr << "[ERROR] cstr_to_u32_id: no digits found" << std::endl;
            return false;
        } else if (res >= UINT32_MAX) {
            std::cerr << "[ERROR] cstr_to_u32_id: u32 overflow" << std::endl;
            return false;
        }

        *out_val_p = static_cast<uint32_t>(res);

        return true;
    };

    uint32_t ent_id = 0u;
    if (opt_id.opt_used) {
        if (!cstr_to_u32(opt_id.opt_arg, &ent_id) || ent_id >= dbf.entc()) {
            std::cerr << "[ERROR] bad option id" << std::endl;
            return -1;
        }
    }

    uint16_t ent_type = 0u;
    {
        if (opt_type.opt_used) {
            uint32_t opt_type_u32 = 0u;
            if (!cstr_to_u32(opt_type.opt_arg, &opt_type_u32)) {
                std::cerr
                    << "[ERROR] Failed to convert option --type into u32"
                    << std::endl;
                return -1;
            }
            if (opt_type_u32 > UINT16_MAX) {
                std::cerr
                    << "[ERROR] invalid option --type: u16 overflow"
                    << std::endl;
                return -1;
            }
            ent_type = static_cast<uint16_t>(opt_type_u32);
        }
    }

    auto print_syment = [
        is_opt_key_only,
        is_opt_kv_pair,
        &opt_type,
        ent_type
    ](const kvsymdb::entry_view &ev_ref) -> void {
        if (is_opt_key_only) {
            if (opt_type.opt_used && ent_type != ev_ref.type) return;
            std::cout <<
                string_utils::string_view(ev_ref.name, ev_ref.name_len)
                << "\n";
            return;
        }
        if (is_opt_kv_pair) {
            if (opt_type.opt_used && ent_type != ev_ref.type) return;
            std::cout
                << "\""
                << string_utils::string_view(ev_ref.name, ev_ref.name_len)
                << "\" -> \""
                << string_utils::string_view(
                    static_cast<const char*>(ev_ref.data),
                    ev_ref.data_len
                ) << "\"\n";
            return;
        }
        kvsymdb_print::print_entry_view(ev_ref);
    };

    ent_ptr_t ent_p = nullptr;
    while ((ent_p = reader.read()) != nullptr) {
        kvsymdb::entry_view entview{};
        int rc = dbf.get_entry_view(ent_p, &entview);
        if (rc) {
            std::cerr
                << "dbf.get_entry_view(): "
                << dbf.errmsg() << std::endl;
            return -1;
        }

        if (opt_id.opt_used && entview.id == ent_id) {
            print_syment(entview);
            return 0;
        }

        if (
            opt_key.opt_used &&
            compare_string_view(
                string_utils::string_view(entview.name, entview.name_len),
                string_utils::string_view(opt_key.opt_arg)
            ) == 0
        ) {
            print_syment(entview);
            return 0;
        }

        ent_p_vec.push_back(ent_p);
    }
    reader.rewind();

    if (opt_key.opt_used || opt_id.opt_used || opt_type.opt_used) {
        std::cerr
            << "[ERROR] Entry not found"
            << std::endl;
        return -1;
    }

    for (auto ent_p : ent_p_vec) {
        kvsymdb::entry_view entview{};
        int rc = dbf.get_entry_view(ent_p, &entview);
        if (rc) {
            std::cerr
                << "dbf.get_entry_view(): "
                << dbf.errmsg() << std::endl;
            return -1;
        }

        print_syment(entview);
    }

    return 0;
}