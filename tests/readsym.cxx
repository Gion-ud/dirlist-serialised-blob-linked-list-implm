// c++11, c++14

#include <kvsymdb.h>
#include <iostream>
#include <iomanip>
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

#define PROG_NAME "readsym"

constexpr auto USAGE_MSG =
    string_utils::make_string_literal(
        "Usage: " PROG_NAME
        " <filename> [option]\n\n"
        "Options:\n"
        "\t--key <name>\n"
        "\t--id <id>\n"
        "\t--keys-only\n"
        "\t--kv-pairs\n"
        "\t--help\n"
    );


int main(int argc, char *argv[]) {
    using namespace cxx_kvsymdb;
    using string_utils::operator<<;

    if (argc < 2 || argc > 4) {
        std::cerr <<
            "[ERROR] Invalid argc: " <<
            "2-4 args expected but " <<
            argc << " were given\n" <<
            USAGE_MSG << std::endl;

        return -1;
    }

    std::cout << "-- kvsymdb reader --" << std::endl;

    auto filename = argv[1];

    struct {
        using string_view = string_utils::string_view;

        string_view key;
        uint32_t    key_hash;
        uint32_t    id;
        bool        is_key_used;
        bool        is_id_used;
        bool        is_keys_only;
        bool        is_kv_pair;
        bool        is_info;
    } cli_opt = {
        .key{},
        .key_hash       = 0u,
        .id             = 0u,
        .is_key_used    = false,
        .is_id_used     = false,
        .is_keys_only   = false,
        .is_kv_pair     = false,
        .is_info        = false,
    };

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

    do {
        using namespace string_utils;

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
            } else if (res == UINT32_MAX) {
                std::cerr << "[ERROR] cstr_to_u32_id: u32 overflow" << std::endl;
                return false;
            }

            *out_val_p = static_cast<uint32_t>(res);

            return true;
        };
        
        if (argc == 3) {
            auto opt = string_view(argv[2]);
            if (compare_string_view(opt, make_string_literal("--keys-only")) == 0) {
                cli_opt.is_keys_only = true;
                break;
            
            } else if (compare_string_view(opt, make_string_literal("--kv-pairs")) == 0) {
                cli_opt.is_kv_pair = true;
                break;
            } else if (compare_string_view(opt, make_string_literal("--help")) == 0) {
                std::cout << USAGE_MSG << std::endl;
                return 0;
            } else if (compare_string_view(opt, make_string_literal("--info")) == 0) {
                cli_opt.is_info = true;
                break;
            } else {
                std::cerr << "[ERROR] bad option\n" << USAGE_MSG << std::endl;
                return -1;
            }
        }

        if (argc == 4) {
            auto opt        = string_view(argv[2]);
            auto opt_val    = string_view(argv[3]);
            if (compare_string_view(opt, string_view("--id")) == 0) {
                if (!cstr_to_u32(opt_val.data(), &cli_opt.id)) {
                    std::cerr <<
                        "[ERROR] bad id option\n" << USAGE_MSG << std::endl;
                    return -1;
                }
                cli_opt.is_id_used = true;
                break;
            } else if (compare_string_view(opt, string_view("--key")) == 0) {
                cli_opt.key.set(opt_val.data(), opt_val.length());
                cli_opt.key_hash = cli_opt.key.hash32();
                cli_opt.is_key_used = true;
                break;
            } else {
                std::cerr << "[ERROR] bad option\n" << USAGE_MSG << std::endl;
                return -1;
            }
        }
    } while (0);

    kvsymdb::file_reader dbif(filename);
    if (!dbif.is_init()) {
        std::cerr << "[ERROR] kvsymdb::file_reader failed: " << dbif.errmsg() << "\n" << std::endl;
        return -1;
    }

    if (cli_opt.is_info) {
        auto *fhdr_p = dbif.get_file_header();
        std::printf(
            "file_header:\n"
            "\tfh_magic:\t0x%.8x\n"   
            "\tfh_version:\t0x%.4x\n"
            "\tfh_align:\t%u\n"
            "\tfh_entcnt:\t%u\n"
            "\tfh_buflen:\t%u\n"
            "\tfh_crc32:\t0x%.8x\n\n",
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

    if (cli_opt.is_id_used && cli_opt.id >= dbif.entc()) {
        std::cerr << "[ERROR] id out of range" << USAGE_MSG << "\n" << std::endl;
        return -1;
    }

    kvsymdb::reader reader(dbif);
    if (!reader.is_init()) {
        std::cerr << "[ERROR] kvsymdb::reader failed: " << reader.errmsg() << "\n" << std::endl;
        return -1;
    }

    using db_entry_ptr_t = decltype(reader.read());

    std::vector<db_entry_ptr_t> db_entp_v{};
    db_entp_v.reserve(dbif.entc());

    {
        db_entry_ptr_t ent_p = nullptr;
        while ((ent_p = reader.read()) != nullptr) {
            if (
                (cli_opt.is_id_used && ent_p->id == cli_opt.id) ||
                (
                    cli_opt.is_key_used &&
                    cli_opt.key_hash == ent_p->hash &&
                    compare_string_view(
                        string_utils::string_view(
                            reinterpret_cast<const char*>(ent_p->payload),
                            ent_p->name_len
                        ),
                        cli_opt.key
                    ) == 0
                )
            ) {
                kvsymdb::entry_view ev{};
                int rc = dbif.get_entry_view(ent_p, &ev);
                if (rc) {
                    std::cerr << "[ERROR] dbif.get_entry_view failed: " << dbif.errmsg() << std::endl;
                    return -1;
                }
                kvsymdb_print::print_entry_view(ev);
                return 0;
            }

            db_entp_v.push_back(ent_p);
        }
        reader.rewind();
    }

    assert(db_entp_v.size() == dbif.entc());

    if (cli_opt.is_key_used) {
        std::cerr << "[ERROR] key not found\n" << std::endl;
        return -1;
    }

    for (auto it = db_entp_v.begin(); it != db_entp_v.end(); ++it) {
        assert(it - db_entp_v.begin() == (*it)->id);
        kvsymdb::entry_view ev{};
        int rc = dbif.get_entry_view(*it, &ev);
        if (rc) {
            std::cerr << "[ERROR] dbif.get_entry_view failed: " << dbif.errmsg() << std::endl;
            return -1;
        }
        if (cli_opt.is_keys_only) {
            using kvsymdb_print::operator<<;
            std::cout << kvsymdb::string_view(ev.name, ev.name_len) << "\n";
            continue;
        } else if (cli_opt.is_kv_pair) {
            kvsymdb_print::print_ent_kv(ev);
            continue;
        }
        kvsymdb_print::print_entry_view(ev);
    }

    return 0;
}